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

coke::Task<int> FileSender::create_file() {
    int iflag = O_RDONLY;
    if (params.direct_io)
        iflag |= O_DIRECT;

    if (fd < 0)
        fd = open_file(params.file_path, file_size, iflag);

    if (fd < 0) {
        error = errno;
        co_return error;
    }

    error = co_await remote_open();
    if (error)
        co_return error;

    if (params.send_method == SEND_METHOD_TREE) {
        error = co_await set_send_tree();
    }
    else {
        error = co_await set_send_chain();
    }

    co_return error;
}

coke::Task<int> FileSender::close_file() {
    error = co_await remote_close();

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
    int local_error = 0;

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
            local_error = result.error;
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

        local_error = co_await cli.request(target, std::move(req), resp);
        if (local_error == 0)
            local_error = resp.get_error();
        if (local_error != 0)
            break;
    }

    if (local_error)
        error = local_error;

    std::free(buf);
}

coke::Task<int> FileSender::remote_open() {
    std::size_t ntarget = params.targets.size();
    int local_error = 0;

    if (ntarget == 0)
        co_return EINVAL;

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
        local_error = co_await cli.request(rtarget, std::move(req), resp);
        if (local_error == 0)
            local_error = resp.get_error();

        if (local_error != 0)
            break;

        file_tokens.push_back(resp.file_token);
    }

    co_return local_error;
}

coke::Task<int> FileSender::remote_close() {
    std::size_t ntarget = file_tokens.size();
    int first_error = 0;
    int local_error = 0;
    uint8_t wait = params.wait_close ? 1 : 0;

    for (std::size_t i = 0; i < ntarget; i++) {
        if (file_tokens[i].empty())
            continue;

        RemoteTarget &rtarget = params.targets[i];
        CloseFileReq req;
        CloseFileResp resp;

        req.wait_close = wait;
        req.file_token = file_tokens[i];
        local_error = co_await cli.request(rtarget, std::move(req), resp);
        if (local_error == 0)
            local_error = resp.get_error();

        if (local_error && first_error == 0)
            first_error = local_error;

        if (local_error == 0)
            file_tokens[i].clear();
    }

    if (first_error == 0)
        file_tokens.clear();

    co_return first_error;
}

coke::Task<int> FileSender::set_send_chain() {
    std::size_t ntarget = file_tokens.size();
    int local_error = 0;

    for (std::size_t i = 0; i + 1 < ntarget; i++) {
        SetChainReq req;
        SetChainResp resp;
        ChainTarget chain_target;

        chain_target.file_token = file_tokens[i+1];
        chain_target.host = params.targets[i+1].host;
        chain_target.port = params.targets[i+1].port;

        req.file_token = file_tokens[i];
        req.targets.push_back(chain_target);

        RemoteTarget &rtarget = params.targets[i];
        local_error = co_await cli.request(rtarget, std::move(req), resp);
        if (local_error == 0)
            local_error = resp.get_error();

        if (local_error != 0)
            break;
    }

    co_return local_error;
}

coke::Task<int> FileSender::set_send_tree() {
    std::size_t ntarget = file_tokens.size();
    int local_error = 0;

    for (std::size_t i = 0; i*2+1 < ntarget; i++) {
        SetChainReq req;
        SetChainResp resp;
        {
            ChainTarget chain_target;
            chain_target.file_token = file_tokens[i*2+1];
            chain_target.host = params.targets[i*2+1].host;
            chain_target.port = params.targets[i*2+1].port;
            req.targets.push_back(chain_target);
        }

        if (i*2+2 < ntarget) {
            ChainTarget chain_target;
            chain_target.file_token = file_tokens[i*2+2];
            chain_target.host = params.targets[i*2+2].host;
            chain_target.port = params.targets[i*2+2].port;
            req.targets.push_back(chain_target);
        }

        req.file_token = file_tokens[i];

        RemoteTarget &rtarget = params.targets[i];
        local_error = co_await cli.request(rtarget, std::move(req), resp);

        if (local_error == 0)
            local_error = resp.get_error();
        if (local_error != 0)
            break;
    }

    co_return local_error;
}
