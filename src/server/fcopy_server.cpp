#include <string>
#include <cstdlib>
#include <cstdio>
#include <csignal>

#include <getopt.h>
#include <unistd.h>
#include <fcntl.h>

#include "structures.h"
#include "fcopy_log.h"
#include "service.h"

FcopyConfig conf;
std::unique_ptr<FcopyService> service;

const char *opts = "c:ghp:";
struct option long_opts[] = {
    {"config",      1, nullptr, 'c'},
    {"background",  0, nullptr, 'g'},
    {"port",        1, nullptr, 'p'},
    {"help",        0, nullptr, 'h'},
};

void signal_handler(int sig) {
    if (service)
        service->notify();
}

void daemon() {
    int fd;

    if ((fd = fork()) != 0)
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

int load_service_config(const std::string &filepath, FcopyConfig &p,
                        std::string &err);

void usage(const char *name) {
    printf(
        "Usage: %s [OPTION]...\n\n"
        "Options:\n"
        "  -c, --config conf_file     path of config file, default ~/.fcopy/fcopy.conf\n"
        "  -p, --port listen_port     start server on `listen port`\n"
        "  -g, --background           running in the background\n"
        "  -h, --help                 show this usage\n"
    , name);
}

int main(int argc, char *argv[]) {
    std::string conf_file;
    std::string err;
    int copt;
    int ret;

    while ((copt = getopt_long(argc, argv, opts, long_opts, nullptr)) != -1) {
        if (copt == 'c' && optarg) {
            conf_file.assign(optarg);
        }
        else if (copt == '?') {
            usage(argv[0]);
            return -1;
        }
    }

    if (!conf_file.empty()) {
        ret = load_service_config(conf_file, conf, err);
        if (ret != 0) {
            // We didn't have log file now
            fprintf(stderr, "%s\n", err.c_str());
            return ret;
        }
    }

    optind = 1;
    while ((copt = getopt_long(argc, argv, opts, long_opts, nullptr)) != -1) {
        switch (copt) {
        case 'c': break;
        case 'p': conf.port = std::atoi(optarg); break;
        case 'g': conf.daemonize = true; break;

        case 'h':
            usage(argv[0]);
            return 0;

        default:
            usage(argv[0]);
            return -1;
        }
    }

    if (conf.port < 1 || conf.port > 65535) {
        usage(argv[0]);
        return 1;
    }

    if (conf.daemonize)
        daemon();
    else {
        fcopy_set_log_stream(stdout);
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    FcopyServiceParams params;
    params.port = conf.port;
    params.srv_params.max_connections = conf.srv_max_conn;
    params.srv_params.peer_response_timeout = conf.srv_peer_response_timeout;
    params.srv_params.receive_timeout = conf.srv_receive_timeout;
    params.srv_params.keep_alive_timeout = conf.srv_keep_alive_timeout;
    params.srv_params.request_size_limit = conf.srv_request_size_limit;
    params.cli_params.retry_max = conf.cli_retry_max;
    params.cli_params.send_timeout = conf.cli_send_timeout;
    params.cli_params.receive_timeout = conf.cli_receive_timeout;
    params.cli_params.keep_alive_timeout = conf.cli_keep_alive_timeout;

    service = std::make_unique<FcopyService>(params);
    ret = service->start();
    if (ret == 0) {
        service->wait();
        service->stop();
    }
    service.reset();

    return ret;
}
