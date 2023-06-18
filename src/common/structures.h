#ifndef FCOPY_STRUCTURES_H
#define FCOPY_STRUCTURES_H

#include <string>
#include <cstdint>

struct ChainTarget {
    std::string host;
    uint16_t port;
    std::string file_token;
};

struct FcopyConfig {
    bool daemonize = false;

    int port        = 5200;
    int loglevel    = 0;

    int srv_max_conn                = 4096;
    int srv_peer_response_timeout   = 10 * 1000;
    int srv_receive_timeout         = -1;
    int srv_keep_alive_timeout      = 300 * 1000;
    std::size_t srv_request_size_limit = 128ULL << 20;

    int cli_retry_max           = 2;
    int cli_send_timeout        = -1;
    int cli_receive_timeout     = -1;
    int cli_keep_alive_timeout  = 300 * 1000;

    std::string logfile;
    std::string pidfile;
};

#endif // FCOPY_STRUCTURES_H
