#include <string>
#include <cstdlib>
#include <cstdio>
#include <csignal>

#include <getopt.h>
#include <unistd.h>
#include <fcntl.h>
#include <atomic>

#include "coke/coke.h"
#include "co_fcopy.h"
#include "file_manager.h"
#include "fcopy_log.h"

FileManager mng;
FcopyClient cli;
std::atomic<bool> running{true};

void signal_handler(int sig) {
    running = false;
    running.notify_all();
}

coke::Task<> handle_create_file(FcopyRequest &freq, FcopyResponse &fresp) {
    CreateFileReq *req = dynamic_cast<CreateFileReq *>(freq.get_message_pointer());
    CreateFileResp resp;
    std::string file_token;
    int error;

    error = mng.create_file(req->file_name, req->file_size, req->chunk_size, file_token);

    FLOG_INFO("CreateFile file:%s size:%zu error:%d token:%s",
        req->file_name.c_str(), (std::size_t)req->file_size, error, file_token.c_str()
    );

    resp.set_error(error);
    resp.file_token = file_token;
    fresp.set_message(std::move(resp));

    co_return;
}

coke::Task<> handle_close_file(FcopyRequest &freq, FcopyResponse &fresp) {
    CloseFileReq *req = dynamic_cast<CloseFileReq *>(freq.get_message_pointer());
    CloseFileResp resp;
    int error;

    error = mng.close_file(req->file_token);

    FLOG_INFO("CloseFile error:%d token:%s",
        error, req->file_token.c_str()
    );

    resp.set_error(error);
    fresp.set_message(std::move(resp));
    co_return;
}

coke::Task<> handle_send_file(FcopyRequest &freq, FcopyResponse &fresp) {
    std::vector<ChainTarget> targets;
    SendFileReq *req = dynamic_cast<SendFileReq *>(freq.get_message_pointer());
    SendFileResp resp;
    int fd;

    fd = mng.get_fd(req->file_token, targets);
    if (fd < 0) {
        resp.set_error(-ENOENT);
    }
    else {
        std::string_view data = req->get_content_view();
        int error = 0;

        if (req->max_chain_len <= 1 && !targets.empty())
            error = -ECANCELED;

        if (error == 0) {
            // write chain, can be parallelized here
            for (ChainTarget &target : targets) {
                SendFileReq sreq;

                sreq.max_chain_len = req->max_chain_len - 1;
                sreq.compress_type = 0;
                sreq.origin_size = data.size();
                sreq.crc32 = 0;
                sreq.offset = req->offset;
                sreq.file_token = target.file_token;
                sreq.set_content_view(data);

                FcopyRequest fsreq;
                fsreq.set_message(std::move(sreq));

                FcopyAwaiter::ResultType result = co_await cli.request(target.host, target.port, std::move(fsreq));
                if (result.state != coke::STATE_SUCCESS)
                    error = result.error;

                // TODO check error in message

                if (error)
                    break;
            }
        }

        if (error == 0) {
            auto res = co_await coke::pwrite(fd, (void *)data.data(), data.size(), req->offset);

            if (res.state != coke::STATE_SUCCESS)
                error = res.error;
        }

        resp.set_error(error);
    }

    fresp.set_message(std::move(resp));
    co_return;
}

coke::Task<> handle_set_chain(FcopyRequest &freq, FcopyResponse &fresp) {
    SetChainReq *req = dynamic_cast<SetChainReq *>(freq.get_message_pointer());
    SetChainResp resp;
    int error;

    error = mng.set_chain_targets(req->file_token, req->targets);
    resp.set_error(error);

    fresp.set_message(std::move(resp));
    co_return;
}

coke::Task<> process(FcopyServerContext ctx) {
    FcopyRequest &req = ctx.get_req();
    FcopyResponse &resp = ctx.get_resp();
    Command cmd = req.get_command();

    switch (cmd) {
    case Command::CREATE_FILE_REQ:
        co_await handle_create_file(req, resp);
        break;

    case Command::CLOSE_FILE_REQ:
        co_await handle_close_file(req, resp);
        break;

    case Command::SEND_FILE_REQ:
        co_await handle_send_file(req, resp);
        break;

    case Command::SET_CHAIN_REQ:
        co_await handle_set_chain(req, resp);
        break;

    default:
        resp.set_message(MessageBase(Command::UNKNOWN));
        break;
    }

    co_await ctx.reply();
}

void start_server(int port) {
    WFServerParams params = SERVER_PARAMS_DEFAULT;
    params.request_size_limit = 128ULL * 1024 * 1024;

    FcopyServer server(params, process);

    if (server.start(port) == 0) {
        FLOG_INFO("ServerStart port:%d", (int)port);
        FLOG_INFO("Send SIGINT or SIGTERM to exit");

        running.wait(true);
        server.stop();
    }
    else {
        FLOG_ERROR("ServerStartFailed error:%d", (int)errno);
    }
}

void daemon() {
    int fd;

    fd = fork();

    if (fd != 0)
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

const char *opts = "gp:h";

struct option long_opts[] = {
    {"background", 0, nullptr, 'g'},
    {"port", 1, nullptr, 'p'},
    {"help", 0, nullptr, 'h'},
};

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
    int copt;
    int port = 0;
    int background = 0;

    while ((copt = getopt_long(argc, argv, opts, long_opts, nullptr)) != -1) {
        switch (copt) {
        case 'p': port = std::atoi(optarg); break;
        case 'g': background = 1; break;

        case 'h':
        default:
            usage(argv[0]);
            return 0;
        }
    }

    if (port == 0) {
        usage(argv[0]);
        return 1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (background)
        daemon();
    else {
        fcopy_set_log_stream(stdout);
    }

    start_server(port);

    return 0;
}
