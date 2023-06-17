#include <string>
#include <cstdlib>
#include <cstdio>
#include <csignal>

#include <getopt.h>
#include <unistd.h>
#include <fcntl.h>

#include "fcopy_log.h"
#include "service.h"

std::unique_ptr<FcopyService> service;
const char *opts = "gp:h";
struct option long_opts[] = {
    {"background", 0, nullptr, 'g'},
    {"port", 1, nullptr, 'p'},
    {"help", 0, nullptr, 'h'},
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
    FcopyServiceParams params;
    int copt;
    int background = 0;

    params.port = 0;
    params.cli_params.retry_max = 3;

    while ((copt = getopt_long(argc, argv, opts, long_opts, nullptr)) != -1) {
        switch (copt) {
        case 'p': params.port = std::atoi(optarg); break;
        case 'g': background = 1; break;

        case 'h':
        default:
            usage(argv[0]);
            return 0;
        }
    }

    if (params.port == 0) {
        usage(argv[0]);
        return 1;
    }

    if (background)
        daemon();
    else {
        fcopy_set_log_stream(stdout);
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    int ret;
    service = std::make_unique<FcopyService>(params);
    ret = service->start();
    if (ret == 0) {
        service->wait();
        service->stop();
    }
    service.reset();

    return ret;
}
