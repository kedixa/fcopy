#ifndef FCOPY_STRUCTURES_H
#define FCOPY_STRUCTURES_H

#include <string>
#include <map>
#include <cstdint>
#include <vector>

struct ChainTarget {
    std::string host;
    uint16_t port;
    std::string file_token;
};

using ChainTargets = std::vector<ChainTarget>;

struct FsPartition {
    std::string name;
    std::string root_path;
};

struct FcopyConfig {
    bool daemonize  = false;
    bool directio   = true;

    int port        = 5200;
    int loglevel    = 0;

    int srv_max_conn                = 4096;
    int srv_peer_response_timeout   = 10 * 1000;
    int srv_receive_timeout         = -1;
    int srv_keep_alive_timeout      = 300 * 1000;
    std::size_t srv_size_limit      = 128ULL << 20;

    int cli_retry_max           = 2;
    int cli_send_timeout        = -1;
    int cli_receive_timeout     = -1;
    int cli_keep_alive_timeout  = 300 * 1000;

    std::string logfile;
    std::string pidfile;
    std::string basedir;
    std::string conffile;

    std::string default_partition;
    std::map<std::string, FsPartition> partitions;
};

#endif // FCOPY_STRUCTURES_H
