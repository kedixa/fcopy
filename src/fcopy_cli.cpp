#include <string>
#include <cstdio>
#include <getopt.h>

#include "coke/coke.h"
#include "fcopy_handler.h"

coke::Task<> upload_file(FcopyClient &cli, FcopyParams params) {
    FcopyHandler h(cli, params);
    int error;

    error = co_await h.create_file();
    if (error) {
        fprintf(stdout, "CreateFileError error:%d\n", error);
    }
    else {
        fprintf(stdout, "CreateFileDone file:%s\n", params.file_path.c_str());

        error = co_await h.send_file();
        if (error)
            fprintf(stdout, "SendFileError error:%d\n", error);
        else
            fprintf(stdout, "SendFileDone\n");

        std::string speed_str = h.get_speed_str();
        double cost = h.get_cost_ms();
        cost /= 1000000;
        fprintf(stdout, "Send Cost:%.4lf Speed:%s\n", cost, speed_str.c_str());
    }

    error = co_await h.close_file();
    if (error)
        fprintf(stdout, "CloseFileError error:%d\n", error);
    else
        fprintf(stdout, "CloseFileDone\n");
}

const char *opts = "f:t:p:h";

struct option long_opts[] = {
    {"file", 1, nullptr, 'f'},
    {"target", 1, nullptr, 't'},
    {"parallel", 1, nullptr, 'p'},
    {"help", 0, nullptr, 'h'},
};

void usage(const char *name) {
    fprintf(stdout, "%s -f file -t host:port [-t host:port]...\n\n"
        "  -f, --file file      send this file to remote server\n"
        "  -t, --target host:port\n"
        "  -p, --parallel n     send in parallel, n in [1, 512], default 16\n"
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

int main(int argc, char *argv[]) {
    std::vector<RemoteTarget> targets;
    std::string filename;
    int parallel = 1;
    int copt;

    while ((copt = getopt_long(argc, argv, opts, long_opts, nullptr)) != -1) {
        switch (copt) {
        case 'f': filename.assign(optarg ? optarg : ""); break;
        case 'p': parallel = std::atoi(optarg); break;
        case 't':
            if (!parse_target(targets, optarg ? optarg : ""))
                return -1;
            break;

        case 'h':
        default:
            usage(argv[0]);
            return 0;
        }
    }

    if (filename.empty() || targets.empty()) {
        usage(argv[0]);
        return -1;
    }

    if (parallel < 1)
        parallel = 1;
    else if (parallel > 512)
        parallel = 512;

    FcopyClientParams cli_params;
    cli_params.retry_max = 2;

    FcopyClient cli(cli_params);

    FcopyParams params;
    params.file_path = filename;
    params.partition = "";
    params.remote_file_dir = ".";
    params.remote_file_name = filename;
    params.targets = targets;
    params.parallel = parallel;

    coke::sync_wait(upload_file(cli, params));

    return 0;
}
