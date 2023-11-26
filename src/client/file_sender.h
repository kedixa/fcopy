#ifndef FCOPY_FILE_SENDER_H
#define FCOPY_FILE_SENDER_H

#include <string>
#include <vector>
#include <mutex>
#include <filesystem>
#include <atomic>

#include "common/co_fcopy.h"

enum {
    SEND_METHOD_CHAIN = 0,
    SEND_METHOD_TREE = 1,
};

struct SenderParams {
    using perms = std::filesystem::perms;

    uint16_t compress_type  = 0;
    uint32_t chunk_size     = 4UL * 1024 * 1024;

    // use unknown means keep origin file perms
    perms file_perm = perms::unknown;
    std::string file_path;

    std::string username;
    std::string password;

    std::string partition;
    std::string remote_file_dir;
    std::string remote_file_name;

    bool direct_io          = true;
    bool wait_close         = true;
    int parallel            = 16;
    int send_method         = SEND_METHOD_CHAIN;
    std::vector<RemoteTarget> targets;
};

class FileSender {
public:
    FileSender(FcopyClient &cli, const SenderParams &params)
        : cli(cli), params(params)
    { }

    coke::Task<int> create_file();
    coke::Task<int> close_file();
    coke::Task<int> send_file();

    int get_error() const { return error; }

    // get info after send
    std::size_t get_cost_us() const { return send_cost; }
    std::size_t get_file_size() const { return file_size; }
    std::size_t get_cur_offset() const { return cur_offset; }

private:
    coke::Task<int> remote_open();
    coke::Task<int> remote_close();
    coke::Task<int> set_send_chain();
    coke::Task<int> set_send_tree();
    coke::Task<> parallel_send(RemoteTarget target, std::string token);

private:
    FcopyClient &cli;
    SenderParams params;

    std::mutex mtx;
    std::atomic<int> error{0};
    int fd = -1;

    std::size_t file_size = 0;
    std::size_t cur_offset = 0;
    std::size_t send_cost = 0;

    std::vector<std::string> file_tokens;
};

#endif // FCOPY_FILE_SENDER_H
