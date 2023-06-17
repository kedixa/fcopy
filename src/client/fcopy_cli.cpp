#include <string>
#include <filesystem>
#include <getopt.h>

#include "coke/coke.h"
#include "fcopy_handler.h"
#include "fcopy_log.h"

namespace fs = std::filesystem;

enum {
    NO_WAIT_CLOSE = 1000,
};

const char *opts = "t:p:h";

struct option long_opts[] = {
    {"target",          1, nullptr, 't'},
    {"parallel",        1, nullptr, 'p'},
    {"no-wait-close",   0, nullptr, NO_WAIT_CLOSE},
    {"help",            0, nullptr, 'h'},
    {nullptr,           0, nullptr, 0},
};

struct GlobalConfig {
    int parallel = 1;
    bool wait_close = true;
    std::vector<RemoteTarget> targets;
};

GlobalConfig cfg;

coke::Task<int> upload_file(FcopyClient &cli, FcopyParams params) {
    FcopyHandler h(cli, params);
    int error;

    error = co_await h.create_file();
    if (error) {
        FLOG_ERROR("CreateFileError error:%d", error);
    }
    else {
        FLOG_INFO("CreateFileDone file:%s", params.file_path.c_str());

        error = co_await h.send_file();
        if (error)
            FLOG_ERROR("SendFileError error:%d", error);
        else
            FLOG_INFO("SendFileDone");

        std::string speed_str = h.get_speed_str();
        double cost = h.get_cost_ms();
        cost /= 1000000;
        FLOG_INFO("Send Cost:%.4lf Speed:%s", cost, speed_str.c_str());
    }

    error = co_await h.close_file(cfg.wait_close);
    if (error)
        FLOG_ERROR("CloseFileError error:%d", error);
    else
        FLOG_INFO("CloseFileDone");

    co_return error;
}

void usage(const char *name) {
    fprintf(stdout,
        "%s [OPTION]... [FILE]...\n\n"
        "  -t, --target host:port   add a file server target\n"
        "  -p, --parallel n     send in parallel, n in [1, 512], default 1\n"
        "  --no-wait-close      not wait server finish close file, default wait\n"
        "  -h, --help           show this usage\n"
    , name);
}

bool parse_target(std::vector<RemoteTarget> &targets, std::string arg) {
    std::string host;
    unsigned short port;

    auto pos = arg.find(':');
    if (pos == std::string::npos)
        return false;

    host = arg.substr(0, pos);
    port = std::atoi(arg.data() + pos + 1);

    if (host.empty() || port == 0)
        return false;

    targets.emplace_back(host, port);
    return true;
}

int parse_args(int argc, char *argv[]) {
    int copt;

    while ((copt = getopt_long(argc, argv, opts, long_opts, nullptr)) != -1) {
        switch (copt) {
        case 'p':
            cfg.parallel = std::atoi(optarg);
            break;

        case 't':
            if (!parse_target(cfg.targets, optarg ? optarg : ""))
                return 1;
            break;

        case NO_WAIT_CLOSE:
            cfg.wait_close = false;
            break;

        case 'h':
        default:
            usage(argv[0]);
            exit(0);
        }
    }

    return 0;
}

int main(int argc, char *argv[]) {
    int ret = parse_args(argc, argv);
    if (ret != 0)
        return ret;

    if (cfg.targets.empty()) {
        usage(argv[0]);
        return 1;
    }

    fcopy_set_log_stream(stdout);

    if (cfg.parallel < 1)
        cfg.parallel = 1;
    else if (cfg.parallel > 512)
        cfg.parallel = 512;

    FcopyClientParams cli_params;
    cli_params.retry_max = 2;

    FcopyClient cli(cli_params);
    int error;

    for (int i = optind; i < argc; i++) {
        std::string filename(argv[i] ? argv[i] : "");

        if (!fs::is_regular_file(filename)) {
            FLOG_ERROR("BadFile file:%s", argv[i]);
            continue;
        }

        FcopyParams params;
        params.file_path = filename;
        params.partition = "";
        params.remote_file_dir = ".";
        params.remote_file_name = filename;
        params.targets = cfg.targets;
        params.parallel = cfg.parallel;

        error = coke::sync_wait(upload_file(cli, params));
        if (error)
            break;
    }

    return 0;
}
