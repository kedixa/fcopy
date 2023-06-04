#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "fcopy_handler.h"
#include "coke/global.h"
#include "coke/fileio.h"
#include "coke/wait.h"

static int open_file(const char *fn, uint64_t &file_size) {
    struct stat file_stat;
    int fd;

    if (stat(fn, &file_stat) != 0)
        return -1;

    fd = open(fn, O_RDONLY);
    file_size = file_stat.st_size;

    return fd;
}

coke::Task<int> FcopyHandler::create_file() {
    if (finfo.fd < 0)
        finfo.fd = open_file(finfo.file_path.c_str(), finfo.file_size);

    if (finfo.fd < 0) {
        error = errno;
        co_return error;
    }

    std::size_t ntarget = finfo.targets.size();

    if (ntarget == 0) {
        error = EINVAL;
        co_return error;
    }

    finfo.file_tokens.clear();
    finfo.file_tokens.reserve(ntarget);

    for (std::size_t i = 0; i < ntarget; i++) {
        CreateFileReq req;
        CreateFileResp resp;
        req.chunk_size = finfo.chunk_size;
        req.file_perm = finfo.file_perm;
        req.file_size = finfo.file_size;
        req.partition = finfo.partition;
        req.relative_path = finfo.remote_file_dir;
        req.file_name = finfo.remote_file_name;

        RemoteTarget &t = finfo.targets[i];
        error = co_await do_request(t, std::move(req), resp);

        if (error != 0)
            co_return error;

        finfo.file_tokens.push_back(resp.file_token);
    }

    for (std::size_t i = 1; i < ntarget; i++) {
        SetChainReq req;
        SetChainResp resp;
        ChainTarget chain_target;

        chain_target.file_token = finfo.file_tokens[i];
        chain_target.host = finfo.targets[i].host;
        chain_target.port = finfo.targets[i].port;

        req.file_token = finfo.file_tokens[i-1];
        req.targets.push_back(chain_target);

        RemoteTarget &t = finfo.targets[i-1];
        error = co_await do_request(t, std::move(req), resp);

        if (error != 0)
            co_return error;
    }

    co_return 0;
}

coke::Task<int> FcopyHandler::close_file() {
    std::size_t ntarget = finfo.file_tokens.size();
    int first_error = 0;

    for (std::size_t i = 0; i < ntarget; i++) {
        if (finfo.file_tokens[i].empty())
            continue;

        RemoteTarget &t = finfo.targets[i];
        CloseFileReq req;
        CloseFileResp resp;

        req.file_token = finfo.file_tokens[i];
        error = co_await do_request(t, std::move(req), resp);

        if (error && first_error == 0)
            first_error = error;

        if (!error)
            finfo.file_tokens[i].clear();
    }

    error = first_error;
    if (error == 0)
        finfo.file_tokens.clear();

    if (finfo.fd > 0) {
        close(finfo.fd);
        finfo.fd = -1;
    }

    co_return error;
}

coke::Task<int> FcopyHandler::send_file() {
    offset = 0;
    error = 0;

    std::vector<coke::Task<>> tasks;
    tasks.reserve(finfo.parallel);

    for (int i = 0; i < finfo.parallel; i++)
        tasks.emplace_back(parallel_send(finfo.targets[0], finfo.file_tokens[0]));

    co_await coke::async_wait(std::move(tasks));
    co_return error;
}

coke::Task<> FcopyHandler::parallel_send(RemoteTarget target, std::string token) {
    std::size_t chunk_size = finfo.chunk_size;
    std::size_t file_size = finfo.file_size;
    int fd = finfo.fd;

    std::vector<char> buf(chunk_size, 0);
    std::size_t cur_off;
    coke::FileResult result;

    while (error == 0) {
        {
            std::lock_guard<std::mutex> lg(mtx);
            if (offset < file_size) {
                cur_off = offset;
                offset += chunk_size;
            }
            else
                break;
        }

        result = co_await coke::pread(fd, buf.data(), chunk_size, cur_off);
        if (result.state != coke::STATE_SUCCESS) {
            error = result.error;
            break;
        }

        SendFileReq req;
        SendFileResp resp;

        req.compress_type = 0;
        req.origin_size = result.nbytes;
        req.crc32 = 0;
        req.offset = cur_off;
        req.file_token = token;
        req.set_content_view(std::string_view(buf.data(), result.nbytes));

        error = co_await do_request(target, std::move(req), resp);
        if (error)
            break;
    }
}

template<typename Req, typename Resp>
coke::Task<int> FcopyHandler::do_request(RemoteTarget target, Req &&req, Resp &resp) {
    FcopyRequest freq;
    freq.set_message(std::move(req));
    FcopyAwaiter::ResultType res = co_await cli.request(target.host, target.port, std::move(freq));

    if (res.state != coke::STATE_SUCCESS)
        co_return res.error;

    FcopyResponse &fresp = res.resp;
    MessageBase *msg = fresp.get_message_pointer();
    Resp *ptr = dynamic_cast<Resp *>(msg);

    if (!ptr)
        co_return EBADMSG;

    resp = std::move(*ptr);
    co_return (int)resp.get_error();
}
