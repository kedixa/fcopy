#include <string>
#include <vector>
#include <set>
#include <fstream>
#include <filesystem>
#include <getopt.h>
#include <unistd.h>

#include "coke/coke.h"
#include "client/file_sender.h"
#include "common/fcopy_log.h"
#include "common/utils.h"

namespace fs = std::filesystem;

enum {
    TARGET_LIST     = 0x0101,
    DRY_RUN         = 0x0102,
    SEND_METHOD     = 0x0103,
    SPEED_LIMIT     = 0x0104,

    NO_WAIT_CLOSE   = 0x0200,
    WAIT_CLOSE      = 0x0201,
    NO_DIRECT_IO    = 0x0202,
    DIRECT_IO       = 0x0203,
    NO_CHECK_SELF   = 0x0204,
    CHECK_SELF      = 0x0205,
};

const char *opts = "t:p:hv";

struct option long_opts[] = {
    {"target",          1, nullptr, 't'},
    {"target-list",     1, nullptr, TARGET_LIST},
    {"parallel",        1, nullptr, 'p'},
    {"dry-run",         0, nullptr, DRY_RUN},
    {"send-method",     1, nullptr, SEND_METHOD},
    {"speed-limit",     1, nullptr, SPEED_LIMIT},
    {"wait-close",      0, nullptr, WAIT_CLOSE},
    {"no-wait-close",   0, nullptr, NO_WAIT_CLOSE},
    {"direct-io",       0, nullptr, DIRECT_IO},
    {"no-direct-io",    0, nullptr, NO_DIRECT_IO},
    {"check-self",      0, nullptr, CHECK_SELF},
    {"no-check-self",   0, nullptr, NO_CHECK_SELF},
    {"verbose",         0, nullptr, 'v'},
    {"help",            0, nullptr, 'h'},
    {nullptr,           0, nullptr, 0},
};

struct GlobalConfig {
    int parallel = 1;
    int verbose = 0;
    bool dry_run = false;
    bool wait_close = true;
    bool direct_io = true;
    bool check_self = true;

    int send_method = SEND_METHOD_CHAIN;
    long speed_limit = 0;
    std::vector<RemoteTarget> targets;
    std::vector<FileDesc> files;
};

GlobalConfig cfg;
coke::QpsPool speed_limiter(0);

bool do_check_self() {
    std::vector<std::string> addrs;
    if (!get_local_addr(addrs)) {
        FLOG_ERROR("GetLocalAddr Failed errno:%d", (int)errno);
        return false;
    }

    std::set<std::string> addrs_set(addrs.begin(), addrs.end());

    for (const auto &target : cfg.targets) {
        if (addrs_set.find(target.host) != addrs_set.end()) {
            FLOG_ERROR("Cannot send to self %s, close this feature with --no-check-self",
                target.host.c_str());
            return false;
        }
    }

    std::set<std::string> targets_set;
    for (const auto &target : cfg.targets) {
        std::string str = target.host + ":" + std::to_string(target.port);
        if (targets_set.find(str) != targets_set.end()) {
            FLOG_ERROR("Cannot send to duplicate target %s", str.c_str());
            return false;
        }

        targets_set.insert(str);
    }

    return true;
}

coke::Task<int> upload_file(FcopyClient &cli, SenderParams params) {
    FileSender h(cli, params);
    int error;
    int close_error;

    h.set_speed_limiter(&speed_limiter);
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

        std::string speed_str = format_bps(h.get_file_size(), h.get_cost_us());
        double cost = h.get_cost_us() / 1.0e6;
        FLOG_INFO("Send Cost:%.4lf Speed:%s", cost, speed_str.c_str());
    }

    close_error = co_await h.close_file();
    if (close_error)
        FLOG_ERROR("CloseFileError error:%d", close_error);
    else
        FLOG_INFO("CloseFileDone");

    if (error == 0)
        co_return close_error;
    else
        co_return error;
}

void usage(const char *name) {
    fprintf(stdout,
        "%s [OPTION]... [FILE]...\n\n"
        "  -t, --target host:port\n"
        "                       add a file server target\n"
        "  --target-list file\n"
        "                       read target in `file`, one host:port per line\n\n"
        "  -p, --parallel n     send in parallel, n in [1, 900], default 1\n\n"
        "  --send-method m      send with method, support chain, tree\n\n"
        "  --speed-limit n      set the maximum transfer rate in MB\n\n"
        "  --wait-close, --no-wait-close\n"
        "                       whether wait server finish close file, default wait\n\n"
        "  --direct-io, --no-direct-io\n"
        "                       enable/disable direct io when read file, default enable\n\n"
        "  --check-self, --no-check-self\n"
        "                       enable/disable check, Abort transfer if targets include\n"
        "                       self or duplicate, default enable\n\n"
        "  --dry-run            parse parameters, determine file, but do not perform the\n"
        "                       upload\n\n"
        "  -v, --verbose        show more details\n"
        "  -h, --help           show this page\n"
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
    std::string method;
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

        case TARGET_LIST:
            if (!parse_targets(cfg.targets, arg))
                return 1;
            break;

        case DRY_RUN: cfg.dry_run = true; break;

        case SEND_METHOD:
            method.assign(arg);
            if (method == "chain")
                cfg.send_method = SEND_METHOD_CHAIN;
            else if (method == "tree")
                cfg.send_method = SEND_METHOD_TREE;
            else {
                FLOG_ERROR("Invalid send method %s", arg);
                return 1;
            }
            break;

        case SPEED_LIMIT:
            cfg.speed_limit = std::atol(arg);
            if (cfg.speed_limit <= 0) {
                FLOG_ERROR("Invalid speed limit %ld\n", cfg.speed_limit);
                return 1;
            }
            break;

        case WAIT_CLOSE:    cfg.wait_close = true; break;
        case NO_WAIT_CLOSE: cfg.wait_close = false; break;

        case DIRECT_IO:     cfg.direct_io = true; break;
        case NO_DIRECT_IO:  cfg.direct_io = false; break;

        case CHECK_SELF:    cfg.check_self = true; break;
        case NO_CHECK_SELF: cfg.check_self = false; break;

        case 'v': ++cfg.verbose; break;
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
    else if (cfg.parallel > 900)
        cfg.parallel = 900;

    if (cfg.check_self && !do_check_self())
        return -1;

    if (cfg.dry_run)
        return 0;

    // coke global init
    coke::GlobalSettings settings;
    settings.endpoint_params.max_connections = 4096;
    settings.poller_threads = 8;
    settings.handler_threads = 12;
    coke::library_init(settings);

    speed_limiter.reset_qps(cfg.speed_limit);

    FcopyClientParams cli_params;
    cli_params.retry_max = 2;

    FcopyClient cli(cli_params);
    int error;

    for (const FileDesc &file : cfg.files) {
        SenderParams params;
        params.file_path = file.path;
        params.partition = "";
        params.remote_file_dir = ".";
        params.remote_file_name = file.path;
        params.targets = cfg.targets;
        params.parallel = cfg.parallel;
        params.send_method = cfg.send_method;

        params.direct_io = cfg.direct_io;
        params.wait_close = cfg.wait_close;

        error = coke::sync_wait(upload_file(cli, params));
        if (error)
            break;
    }

    return 0;
}
