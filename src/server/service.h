#ifndef FCOPY_SERVICE_H
#define FCOPY_SERVICE_H

#include <atomic>
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

#include "co_fcopy.h"
#include "file_manager.h"

struct FcopyServerParams {
    size_t max_connections      = 4096;
    int peer_response_timeout   = 10 * 1000;
    int receive_timeout         = -1;
    int keep_alive_timeout      = 300 * 1000;
    size_t request_size_limit   = 128ULL * 1024 * 1024;
};

struct FcopyServiceParams {
    int port;

    FcopyServerParams srv_params;
    FcopyClientParams cli_params;
};

class FcopyService {
public:
    FcopyService(const FcopyServiceParams &params) : params(params) { }
    FcopyService(const FcopyService &) = delete;

    int start();
    void wait();
    void notify();
    void stop();

private:
    coke::Task<> process(FcopyServerContext ctx);

    coke::Task<> handle_create_file(FcopyServerContext &ctx);
    coke::Task<> handle_close_file(FcopyServerContext &ctx);
    coke::Task<> handle_send_file(FcopyServerContext &ctx);
    coke::Task<> handle_set_chain(FcopyServerContext &ctx);

private:
    std::atomic<bool> running{false};
    FcopyServiceParams params;

    std::vector<std::unique_ptr<FcopyServer>> servers;
    std::unique_ptr<FcopyClient> cli;
    std::unique_ptr<FileManager> mng;
};

#endif // FCOPY_SERVICE_H
