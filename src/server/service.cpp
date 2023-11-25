#include "server/service.h"

#include <cstdlib>
#include <cstring>

#include "coke/coke.h"
#include "common/utils.h"
#include "common/fcopy_log.h"

static
coke::Task<> write_file(int fd, std::string_view data, uint64_t offset, int &error) {
    coke::FileResult res;
    void *pdata = (void *)data.data();
    std::size_t psize = data.size();

    if (psize % FCOPY_CHUNK_BASE != 0) {
        // last unaligned chunk
        psize += FCOPY_CHUNK_BASE;
        psize = psize / FCOPY_CHUNK_BASE * FCOPY_CHUNK_BASE;
        pdata = std::aligned_alloc(FCOPY_CHUNK_BASE, psize);

        if (pdata == nullptr) {
            error = errno;
            co_return;
        }

        memcpy(pdata, data.data(), data.size());
        memset((char *)pdata + data.size(), 0, psize - data.size());
    }

    res = co_await coke::pwrite(fd, pdata, psize, offset);
    if (res.state != coke::STATE_SUCCESS)
        error = res.error;
    else
        error = 0;

    if (pdata != data.data())
        std::free(pdata);
}

static
coke::Task<int> send_one(FcopyClient &cli, const RemoteTarget &target, SendFileReq req) {
    SendFileResp resp;
    std::string token;
    int error;

    token = req.file_token;
    error = co_await cli.request(target, std::move(req), resp);
    if (error == 0)
        error = resp.get_error();

    if (error == 0) {
        FLOG_DEBUG("ChainSendSuccess host:%s port:%u token:%s",
            target.host.c_str(), (unsigned)target.port, token.c_str()
        );
    }
    else {
        FLOG_ERROR("ChainSendFailed host:%s port:%u token:%s error:%d",
            target.host.c_str(), (unsigned)target.port, token.c_str(), error
        );
    }

    co_return error;
}

static
coke::Task<> send_chain(FcopyClient &cli, SendFileReq &origin,
                        const std::vector<ChainTarget> &targets,
                        std::vector<int> &errors) {
    std::size_t size = targets.size();
    std::string_view data = origin.get_content_view();
    std::vector<coke::Task<int>> tasks;
    tasks.reserve(size);

    for (std::size_t i = 0; i < size; i++) {
        const ChainTarget &to = targets[i];
        SendFileReq req;
        RemoteTarget target;

        req.max_chain_len = origin.max_chain_len - 1;
        req.compress_type = origin.compress_type;
        req.origin_size = origin.origin_size;
        req.crc32 = origin.crc32;
        req.offset = origin.offset;
        req.file_token = to.file_token;
        req.set_content_view(data);

        target.host = to.host;
        target.port = to.port;

        tasks.push_back(send_one(cli, target, std::move(req)));
    }

    errors = co_await coke::async_wait(std::move(tasks));
    co_return;
}

int FcopyService::start() {
    int ret;
    WFServerParams srv_params = SERVER_PARAMS_DEFAULT;
    srv_params.max_connections = params.srv_params.max_connections;
    srv_params.peer_response_timeout = params.srv_params.peer_response_timeout;
    srv_params.receive_timeout = params.srv_params.receive_timeout;
    srv_params.keep_alive_timeout = params.srv_params.keep_alive_timeout;
    srv_params.request_size_limit = params.srv_params.request_size_limit;

    if (params.default_partition.empty()) {
        FLOG_ERROR("ServerStartFailed no default_partition");
        return -1;
    }

    FcopyProcessor processor = [this](FcopyServerContext ctx) -> coke::Task<> {
        co_await this->process(std::move(ctx));
    };

    servers.emplace_back(std::make_unique<FcopyServer>(srv_params, processor));

    ret = servers.back()->start(params.port);
    if (ret != 0) {
        FLOG_ERROR("ServerStartFailed error:%d", (int)errno);
        return ret;
    }

    cli = std::make_unique<FcopyClient>(params.cli_params);
    mng = std::make_unique<FileManager>();

    FLOG_INFO("ServerStart port:%d", params.port);
    running = true;

    return 0;
}

void FcopyService::wait() {
    if (running)
        running.wait(true);
}

void FcopyService::notify() {
    running = false;
    running.notify_all();
}

void FcopyService::stop() {
    for (auto &server : servers)
        server->shutdown();

    for (auto &server : servers)
        server->wait_finish();
}

coke::Task<> FcopyService::process(FcopyServerContext ctx) {
    Command cmd = ctx.get_req().get_command();
    ctx.get_resp().set_message(MessageBase(Command::UNKNOWN));

    switch (cmd) {
    case Command::CREATE_FILE_REQ:
        co_await handle_create_file(ctx);
        break;

    case Command::CLOSE_FILE_REQ:
        co_await handle_close_file(ctx);
        break;

    case Command::SEND_FILE_REQ:
        co_await handle_send_file(ctx);
        break;

    case Command::SET_CHAIN_REQ:
        co_await handle_set_chain(ctx);
        break;

    default:
        co_await ctx.reply();
        break;
    }
}

coke::Task<> FcopyService::handle_create_file(FcopyServerContext &ctx) {
    CreateFileReq req;
    CreateFileResp resp;
    std::string file_token;
    std::string partition_dir;
    std::string abs_path;
    int error;

    if (!ctx.get_req().move_message(req))
        co_return;

    partition_dir = get_partition_dir(req.partition);
    if (partition_dir.empty())
        error = -1;
    else
        error = get_abs_path(partition_dir, req.relative_path, req.file_name, abs_path);

    if (error == 0)
        error = mng->create_file(abs_path, req.file_size, req.chunk_size, params.directio, file_token);

    FLOG_INFO("CreateFile file:%s size:%zu error:%d token:%s",
        abs_path.c_str(), (std::size_t)req.file_size, error, file_token.c_str()
    );

    resp.set_error(error);
    resp.file_token = file_token;
    ctx.get_resp().set_message(std::move(resp));

    co_await ctx.reply();
}

coke::Task<> FcopyService::handle_close_file(FcopyServerContext &ctx) {
    CloseFileReq req;
    CloseFileResp resp;
    bool wait;
    int error;

    if (!ctx.get_req().move_message(req))
        co_return;

    wait = req.wait_close;

    if (wait) {
        // close file may block, switch to go thread
        co_await coke::switch_go_thread("close_file");
        error = mng->close_file(req.file_token);
    }
    else {
        if (mng->has_file(req.file_token))
            error = 0;
        else
            error = -ENOENT;
    }

    resp.set_error(error);
    ctx.get_resp().set_message(std::move(resp));
    co_await ctx.reply();

    if (!wait) {
        co_await coke::switch_go_thread("close_file");
        error = mng->close_file(req.file_token);
    }

    FLOG_INFO("CloseFile error:%d token:%s",
        error, req.file_token.c_str()
    );
}

coke::Task<> FcopyService::handle_send_file(FcopyServerContext &ctx) {
    std::vector<ChainTarget> targets;
    SendFileReq req;
    SendFileResp resp;
    int fd;

    if (!ctx.get_req().move_message(req))
        co_return;

    fd = mng->get_fd(req.file_token, targets);
    if (fd < 0)
        resp.set_error(-ENOENT);
    else if (req.max_chain_len <= 1 && !targets.empty())
        resp.set_error(-ECANCELED);
    else {
        std::string_view data = req.get_content_view();
        std::vector<int> chain_errors;
        int write_error;

        co_await coke::async_wait(
            send_chain(*cli, req, targets, chain_errors),
            write_file(fd, data, req.offset, write_error)
        );

        // get first error
        int error = 0;

        for (int err : chain_errors) {
            if (err != 0) {
                error = err;
                break;
            }
        }

        if (error == 0)
            error = write_error;

        resp.set_error(error);
    }

    ctx.get_resp().set_message(std::move(resp));
    co_return;
}

coke::Task<> FcopyService::handle_set_chain(FcopyServerContext &ctx) {
    SetChainReq req;
    SetChainResp resp;
    int error;

    if (!ctx.get_req().move_message(req))
        co_return;

    error = mng->set_chain_targets(req.file_token, req.targets);
    resp.set_error(error);

    ctx.get_resp().set_message(std::move(resp));
    co_return;
}

std::string FcopyService::get_partition_dir(const std::string &partition) {
    if (partition.empty())
        return params.default_partition;

    auto it = params.partitions.find(partition);
    if (it == params.partitions.end())
        return std::string();

    return it->second.root_path;
}
