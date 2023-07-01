#include <string>
#include <fstream>
#include <filesystem>
#include <getopt.h>
#include <unistd.h>

#include "coke/coke.h"
#include "fcopy_handler.h"
#include "fcopy_log.h"
#include "utils.h"

namespace fs = std::filesystem;

enum {
    NO_WAIT_CLOSE = 1000,
    TARGET_LIST   = 1001,
    DRY_RUN       = 1002,
};

const char *opts = "t:p:hv";

struct option long_opts[] = {
    {"target",          1, nullptr, 't'},
    {"target-list",     1, nullptr, TARGET_LIST},
    {"parallel",        1, nullptr, 'p'},
    {"no-wait-close",   0, nullptr, NO_WAIT_CLOSE},
    {"dry-run",         0, nullptr, DRY_RUN},
    {"verbose",         0, nullptr, 'v'},
    {"help",            0, nullptr, 'h'},
    {nullptr,           0, nullptr, 0},
};

struct GlobalConfig {
    int parallel = 1;
    int verbose = 0;
    bool dry_run = false;
    bool wait_close = true;
    std::vector<RemoteTarget> targets;
    std::vector<FileDesc> files;
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
        "  --target-list file\n"
        "                       read target in `file`, one host:port per line\n"
        "  -p, --parallel n     send in parallel, n in [1, 512], default 1\n"
        "  --no-wait-close      not wait server finish close file, default wait\n"
        "  --dry-run            parse parameters, determine file, but do not perform the upload.\n"
        "  -v, --verbose        show more details\n"
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

bool parse_targets(std::vector<RemoteTarget> &targets, std::string filename) {
    std::ifstream ifs(filename);
    std::string line;
    std::string arg;

    std::size_t pos;
    std::size_t length;

    if (!ifs) {
        FLOG_ERROR("Invalid file %s", filename.c_str());
        return false;
    }

    while (std::getline(ifs, line)) {
        pos = 0;
        length = line.length();

        // skip leading spaces
        while (pos < length && std::isspace(line[pos]))
            ++pos;

        // ignore comment line and empty line
        if (pos == length || line[pos] == '#')
            continue;

        arg = line.substr(pos);
        if (!parse_target(targets, arg)) {
            FLOG_ERROR("Invalid target line %s", arg.c_str());
            return false;
        }
    }

    return true;
}

int parse_args(int argc, char *argv[]) {
    const char *arg;
    int copt;

    while ((copt = getopt_long(argc, argv, opts, long_opts, nullptr)) != -1) {
        arg = optarg ? optarg : "";

        switch (copt) {
        case 'p':
            cfg.parallel = std::atoi(arg);
            break;

        case 't':
            if (!parse_target(cfg.targets, arg)) {
                FLOG_ERROR("Invalid target line %s", arg);
                return 1;
            }
            break;

        case 'v':
            ++cfg.verbose;
            break;

        case TARGET_LIST:
            if (!parse_targets(cfg.targets, arg))
                return 1;
            break;

        case NO_WAIT_CLOSE:
            cfg.wait_close = false;
            break;

        case DRY_RUN:
            cfg.dry_run = true;
            break;

        case 'h':
        default:
            usage(argv[0]);
            exit(0);
        }
    }

    std::vector<std::string> paths;
    for (int i = optind; i < argc; i++) {
        if (argv[i])
            paths.push_back(argv[i]);
    }

    try {
        load_files(paths, cfg.files);
    }
    catch (const fs::filesystem_error &e) {
        FLOG_ERROR("%s path:%s error:%d", e.what(), e.path1().c_str(), e.code().value());
        return 1;
    }

    return 0;
}

int main(int argc, char *argv[]) {
    fcopy_set_log_stream(stdout);

    int ret = parse_args(argc, argv);
    if (ret != 0)
        return ret;

    if (cfg.targets.empty()) {
        usage(argv[0]);
        return 1;
    }

    if (cfg.parallel < 1)
        cfg.parallel = 1;
    else if (cfg.parallel > 512)
        cfg.parallel = 512;

    if (cfg.dry_run)
        return 0;

    FcopyClientParams cli_params;
    cli_params.retry_max = 2;

    FcopyClient cli(cli_params);
    int error;

    for (const FileDesc &file : cfg.files) {
        FcopyParams params;
        params.file_path = file.path;
        params.partition = "";
        params.remote_file_dir = ".";
        params.remote_file_name = file.path;
        params.targets = cfg.targets;
        params.parallel = cfg.parallel;

        error = coke::sync_wait(upload_file(cli, params));
        if (error)
            break;
    }

    return 0;
}
