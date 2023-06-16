#include <string>
#include <string_view>
#include <cstdlib>
#include <cstdio>
#include <csignal>

#include <getopt.h>
#include <unistd.h>
#include <fcntl.h>
#include <atomic>

#include "coke/coke.h"
#include "co_fcopy.h"
#include "file_manager.h"
#include "fcopy_log.h"

FcopyClientParams client_params {
    .retry_max           = 2,
    .send_timeout        = -1,
    .receive_timeout     = -1,
    .keep_alive_timeout  = 60 * 1000,
};

FileManager mng;
FcopyClient cli(client_params);
std::atomic<bool> running{true};

void signal_handler(int sig) {
    running = false;
    running.notify_all();
}

coke::Task<> handle_create_file(FcopyServerContext &ctx) {
    CreateFileReq req;
    CreateFileResp resp;
    std::string file_token;
    int error;

    if (!ctx.get_req().move_message(req))
        co_return;

    error = mng.create_file(req.file_name, req.file_size, req.chunk_size, file_token);

    FLOG_INFO("CreateFile file:%s size:%zu error:%d token:%s",
        req.file_name.c_str(), (std::size_t)req.file_size, error, file_token.c_str()
    );

    resp.set_error(error);
    resp.file_token = file_token;
    ctx.get_resp().set_message(std::move(resp));

    co_await ctx.reply();
}

coke::Task<> handle_close_file(FcopyServerContext &ctx) {
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
        error = mng.close_file(req.file_token);
    }
    else {
        if (mng.has_file(req.file_token))
            error = 0;
        else
            error = -ENOENT;
    }

    resp.set_error(error);
    ctx.get_resp().set_message(std::move(resp));
    co_await ctx.reply();

    if (!wait) {
        co_await coke::switch_go_thread("close_file");
        error = mng.close_file(req.file_token);
    }

    FLOG_INFO("CloseFile error:%d token:%s",
        error, req.file_token.c_str()
    );
}

coke::Task<int> send_one(const RemoteTarget &target, SendFileReq req) {
    SendFileResp resp;
    std::string token;
    int error;

    token = req.file_token;
    error = co_await cli.request(target, std::move(req), resp);
    if (error == 0)
        error = resp.get_error();

    if (error == 0) {
        FLOG_INFO("ChainSendSuccess host:%s port:%u token:%s",
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

coke::Task<> send_chain(SendFileReq &req, const std::vector<ChainTarget> &targets,
                        std::vector<int> &errors)
{
    std::size_t size = targets.size();
    std::string_view data = req.get_content_view();
    std::vector<coke::Task<int>> tasks;

    tasks.reserve(size);

    for (std::size_t i = 0; i < size; i++) {
        const ChainTarget &target = targets[i];
        SendFileReq s;
        RemoteTarget t;

        s.max_chain_len = req.max_chain_len - 1;
        s.compress_type = req.compress_type;
        s.origin_size = req.origin_size;
        s.crc32 = req.crc32;
        s.offset = req.offset;
        s.file_token = target.file_token;
        s.set_content_view(data);

        t.host = target.host;
        t.port = target.port;

        tasks.push_back(send_one(t, std::move(s)));
    }

    errors = co_await coke::async_wait(std::move(tasks));
    co_return;
}

coke::Task<> write_file(int fd, std::string_view data, uint64_t offset, int &error) {
    coke::FileResult res;
    res = co_await coke::pwrite(fd, (void *)data.data(), data.size(), offset);
    if (res.state != coke::STATE_SUCCESS)
        error = res.error;
    else
        error = 0;
    co_return;
}

coke::Task<> handle_send_file(FcopyServerContext &ctx) {
    std::vector<ChainTarget> targets;
    SendFileReq req;
    SendFileResp resp;
    int fd;

    if (!ctx.get_req().move_message(req))
        co_return;

    fd = mng.get_fd(req.file_token, targets);
    if (fd < 0)
        resp.set_error(-ENOENT);
    else if (req.max_chain_len <= 1 && !targets.empty())
        resp.set_error(-ECANCELED);
    else {
        std::string_view data = req.get_content_view();
        std::vector<int> chain_errors;
        int write_error;

        co_await coke::async_wait(
            send_chain(req, targets, chain_errors),
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

coke::Task<> handle_set_chain(FcopyServerContext &ctx) {
    SetChainReq req;
    SetChainResp resp;
    int error;

    if (!ctx.get_req().move_message(req))
        co_return;

    error = mng.set_chain_targets(req.file_token, req.targets);
    resp.set_error(error);

    ctx.get_resp().set_message(std::move(resp));
    co_return;
}

coke::Task<> process(FcopyServerContext ctx) {
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

void start_server(int port) {
    WFServerParams params = SERVER_PARAMS_DEFAULT;
    params.request_size_limit = 128ULL * 1024 * 1024;

    FcopyServer server(params, process);

    if (server.start(port) == 0) {
        FLOG_INFO("ServerStart port:%d", (int)port);
        FLOG_INFO("Send SIGINT or SIGTERM to exit");

        running.wait(true);
        server.stop();
    }
    else {
        FLOG_ERROR("ServerStartFailed error:%d", (int)errno);
    }
}

void daemon() {
    int fd;

    fd = fork();

    if (fd != 0)
        exit(0);

    setsid();
    fd = open("/dev/null", O_RDWR, 0);

    if (fd > 0) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);

        close(fd);
    }
}

const char *opts = "gp:h";

struct option long_opts[] = {
    {"background", 0, nullptr, 'g'},
    {"port", 1, nullptr, 'p'},
    {"help", 0, nullptr, 'h'},
};

void usage(const char *name) {
    printf(
        "Usage: %s [OPTION]...\n\n"
        "Options:\n"
        "  -p, --port listen_port     start server on `listen port`\n"
        "  -g, --background           running in the background\n"
        "  -h, --help                 show this usage\n"
    , name);
}

int main(int argc, char *argv[]) {
    int copt;
    int port = 0;
    int background = 0;

    while ((copt = getopt_long(argc, argv, opts, long_opts, nullptr)) != -1) {
        switch (copt) {
        case 'p': port = std::atoi(optarg); break;
        case 'g': background = 1; break;

        case 'h':
        default:
            usage(argv[0]);
            return 0;
        }
    }

    if (port == 0) {
        usage(argv[0]);
        return 1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (background)
        daemon();
    else {
        fcopy_set_log_stream(stdout);
    }

    start_server(port);

    return 0;
}
