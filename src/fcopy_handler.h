#ifndef FCOPY_HANDLER_H
#define FCOPY_HANDLER_H

#include <string>
#include <vector>
#include <mutex>

#include "co_fcopy.h"

struct FcopyParams {
    uint16_t compress_type  = 0;
    uint32_t chunk_size     = 4UL * 1024 * 1024;
    uint32_t file_perm      = 0;
    std::string file_path;

    std::string partition;
    std::string remote_file_dir;
    std::string remote_file_name;

    int parallel            = 16;
    std::vector<RemoteTarget> targets;
};

struct FcopyInfo : public FcopyParams {
    int fd = -1;
    std::size_t file_size = 0;

    std::vector<std::string> file_tokens;
};

class FcopyHandler {
public:
    FcopyHandler(FcopyClient cli, const FcopyParams &params)
        : error(0), cli(cli)
    {
        finfo.FcopyParams::operator=(params);
        offset = 0;
        file_size = 0;
        send_cost = 0;
    }

    coke::Task<int> create_file();
    coke::Task<int> close_file();
    coke::Task<int> send_file();

    std::string get_speed_str() const;
    int64_t get_cost_ms() const { return send_cost; }

private:
    coke::Task<> parallel_send(RemoteTarget target, std::string token);

private:
    int error;
    FcopyClient cli;
    FcopyInfo finfo;

    std::mutex mtx;
    std::size_t offset;

    std::size_t file_size;
    int64_t send_cost;
};

#endif // FCOPY_HANDLER_H
