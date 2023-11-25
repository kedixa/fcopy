#include <cstdlib>
#include <memory>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "client/file_sender.h"
#include "common/structures.h"
#include "common/utils.h"

#include "coke/global.h"
#include "coke/fileio.h"
#include "coke/wait.h"

static int open_file(const std::string &path, uint64_t &file_size, int flag) {
    struct stat file_stat;
    int fd;

    if (stat(path.c_str(), &file_stat) != 0)
        return -1;

    fd = open(path.c_str(), flag);
    file_size = file_stat.st_size;

    return fd;
}

coke::Task<int> FileSender::create_file(bool direct_io) {
    int iflag = O_RDONLY;
    if (direct_io)
        iflag |= O_DIRECT;

    if (fd < 0)
        fd = open_file(params.file_path, file_size, iflag);

    if (fd < 0) {
        error = errno;
        co_return error;
    }

    std::size_t ntarget = params.targets.size();
    if (ntarget == 0) {
        error = EINVAL;
        co_return error;
    }

    file_tokens.clear();
    file_tokens.reserve(ntarget);

    for (std::size_t i = 0; i < ntarget; i++) {
        CreateFileReq req;
        CreateFileResp resp;
        req.chunk_size = params.chunk_size;
        // TODO req.file_perm = finfo.file_perm;
        req.file_perm = 0;
        req.file_size = file_size;
        req.partition = params.partition;
        req.relative_path = params.remote_file_dir;
        req.file_name = params.remote_file_name;

        RemoteTarget &rtarget = params.targets[i];
        error = co_await cli.request(rtarget, std::move(req), resp);
        if (error == 0)
            error = resp.get_error();

        if (error != 0)
            co_return error;

        file_tokens.push_back(resp.file_token);
    }

    for (std::size_t i = 1; i < ntarget; i++) {
        SetChainReq req;
        SetChainResp resp;
        ChainTarget chain_target;

        chain_target.file_token = file_tokens[i];
        chain_target.host = params.targets[i].host;
        chain_target.port = params.targets[i].port;

        req.file_token = file_tokens[i-1];
        req.targets.push_back(chain_target);

        RemoteTarget &rtarget = params.targets[i-1];
        error = co_await cli.request(rtarget, std::move(req), resp);
        if (error == 0)
            error = resp.get_error();

        if (error != 0)
            co_return error;
    }

    co_return 0;
}

coke::Task<int> FileSender::close_file(bool wait_close) {
    std::size_t ntarget = file_tokens.size();
    int first_error = 0;
    uint8_t wait = wait_close ? 1 : 0;

    for (std::size_t i = 0; i < ntarget; i++) {
        if (file_tokens[i].empty())
            continue;

        RemoteTarget &rtarget = params.targets[i];
        CloseFileReq req;
        CloseFileResp resp;

        req.wait_close = wait;
        req.file_token = file_tokens[i];
        error = co_await cli.request(rtarget, std::move(req), resp);
        if (error == 0)
            error = resp.get_error();

        if (error && first_error == 0)
            first_error = error;

        if (error == 0)
            file_tokens[i].clear();
    }

    error = first_error;
    if (error == 0)
        file_tokens.clear();

    if (fd > 0) {
        close(fd);
        fd = -1;
    }

    co_return error;
}

coke::Task<int> FileSender::send_file() {
    int64_t start = current_usec();

    cur_offset = 0;
    error = 0;

    std::vector<coke::Task<>> tasks;
    tasks.reserve(params.parallel);

    for (int i = 0; i < params.parallel; i++)
        tasks.emplace_back(parallel_send(params.targets[0], file_tokens[0]));

    co_await coke::async_wait(std::move(tasks));
    send_cost = current_usec() - start;
    co_return error;
}

coke::Task<> FileSender::parallel_send(RemoteTarget target, std::string token) {
    std::size_t chunk_size = params.chunk_size;
    std::size_t local_offset;
    coke::FileResult result;

    void *buf = std::aligned_alloc(FCOPY_CHUNK_BASE, chunk_size);
    if (buf == nullptr) {
        error = errno;
        co_return;
    }

    while (error == 0) {
        {
            std::lock_guard<std::mutex> lg(mtx);
            if (cur_offset < file_size) {
                local_offset = cur_offset;
                cur_offset += chunk_size;
            }
            else
                break;
        }

        result = co_await coke::pread(fd, buf, chunk_size, local_offset);
        if (result.state != coke::STATE_SUCCESS) {
            error = result.error;
            break;
        }

        SendFileReq req;
        SendFileResp resp;

        req.max_chain_len = static_cast<uint16_t>(params.targets.size());
        req.compress_type = 0;
        req.origin_size = result.nbytes;
        req.crc32 = 0;
        req.offset = local_offset;
        req.file_token = token;
        req.set_content_view(static_cast<const char *>(buf), result.nbytes);

        error = co_await cli.request(target, std::move(req), resp);
        if (error == 0)
            error = resp.get_error();
    }

    std::free(buf);
}
