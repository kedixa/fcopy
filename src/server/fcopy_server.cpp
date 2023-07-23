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
#include "utils.h"

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

int init_config(int argc, char *argv[]) {
    std::string conffile;
    std::string basedir;
    std::string err;
    int copt;
    int ret;

    // find config in args
    while ((copt = getopt_long(argc, argv, opts, long_opts, nullptr)) != -1) {
        if (copt == 'c' && optarg) {
            conffile.assign(optarg);
        }
        else if (copt == '?') {
            usage(argv[0]);
            return -1;
        }
    }

    // try to find default config
    basedir = default_basedir();
    if (conffile.empty() && !basedir.empty()) {
        conffile = basedir + "/fcopy.conf";
        if (!is_regular_file(conffile))
            conffile.clear();
    }

    // load config
    if (!conffile.empty()) {
        conf.conffile = conffile;
        ret = load_service_config(conffile, conf, err);
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
            std::exit(0);

        default:
            usage(argv[0]);
            return -1;
        }
    }

    if (conf.basedir.empty())
        conf.basedir = basedir;
    else
        basedir = conf.basedir;

    std::string tmp;

    if (!conf.logfile.empty()) {
        ret = get_abs_path(basedir, conf.logfile, tmp);
        if (ret == 0)
            conf.logfile = tmp;
        else
            return 1;
    }

    if (!conf.pidfile.empty()) {
        ret = get_abs_path(basedir, conf.pidfile, tmp);
        if (ret == 0)
            conf.pidfile = tmp;
        else
            return 1;
    }

    if (conf.default_partition.empty())
        conf.default_partition = current_dir();

    return 0;
}

int main(int argc, char *argv[]) {
    int ret = init_config(argc, argv);
    if (ret != 0)
        return ret;

    if (conf.port < 1 || conf.port > 65535) {
        usage(argv[0]);
        return 1;
    }
    // TODO check all configs

    if (conf.daemonize)
        daemon();

    if (conf.logfile.empty()) {
        if (!conf.daemonize)
            fcopy_set_log_stream(stdout);
    }
    else {
        ret = create_dirs(conf.logfile, true);
        if (ret == 0)
            ret = fcopy_open_log_file(conf.logfile.c_str());
        if (ret != 0) {
            printf("StartFailed logfile:%s error:%d\n", conf.logfile.c_str(), ret);
            return ret;
        }
    }

    // coke global init
    coke::GlobalSettings settings;
    settings.endpoint_params.max_connections = 2048;
    settings.poller_threads = 8;
    settings.handler_threads = 12;
    coke::library_init(settings);

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (!conf.conffile.empty())
        FLOG_INFO("StartWithConfig %s", conf.conffile.data());

    FcopyServiceParams params;
    params.directio = conf.directio;
    params.port = conf.port;
    params.srv_params.max_connections = conf.srv_max_conn;
    params.srv_params.peer_response_timeout = conf.srv_peer_response_timeout;
    params.srv_params.receive_timeout = conf.srv_receive_timeout;
    params.srv_params.keep_alive_timeout = conf.srv_keep_alive_timeout;
    params.srv_params.request_size_limit = conf.srv_size_limit;
    params.cli_params.retry_max = conf.cli_retry_max;
    params.cli_params.send_timeout = conf.cli_send_timeout;
    params.cli_params.receive_timeout = conf.cli_receive_timeout;
    params.cli_params.keep_alive_timeout = conf.cli_keep_alive_timeout;

    params.default_partition = conf.default_partition;
    params.partitions = conf.partitions;

    service = std::make_unique<FcopyService>(params);
    ret = service->start();
    if (ret == 0) {
        service->wait();

        FLOG_INFO("ExitSignal received");
        service->stop();
    }

    service.reset();
    FLOG_INFO("Quit");

    if (!conf.logfile.empty())
        fcopy_close_log_file();

    return ret;
}
