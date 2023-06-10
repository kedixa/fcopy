#ifndef FCOPY_TASK_H
#define FCOPY_TASK_H

#include "fcopy_message.h"
#include "coke/network.h"
#include "coke/detail/task.h"
#include "coke/global.h"

#include "workflow/WFServer.h"

struct FcopyClientParams {
    int retry_max           = 0;
    int send_timeout        = -1;
    int receive_timeout     = -1;
    int keep_alive_timeout  = 60 * 1000;
};

struct RemoteTarget {
    std::string host;
    unsigned short port;
};

class FcopyClient {
public:
    using ReqType = FcopyRequest;
    using RespType = FcopyResponse;
    using AwaiterType = coke::NetworkAwaiter<ReqType, RespType>;

public:
    explicit FcopyClient(const FcopyClientParams &params = FcopyClientParams())
        : params(params)
    { }

    AwaiterType request(const std::string &host, unsigned short port, ReqType &&req) noexcept;

    template<typename RequestMsg, typename ResponseMsg>
    coke::Task<int> request(const RemoteTarget &target, RequestMsg &&req, ResponseMsg &resp) noexcept;

private:
    FcopyClientParams params;
};

using FcopyAwaiter = FcopyClient::AwaiterType;
using FcopyTask = WFNetworkTask<FcopyRequest, FcopyResponse>;
using FcopyServerBase = WFServer<FcopyRequest, FcopyResponse>;
using FcopyServerContext = coke::ServerContext<WFNetworkTask<FcopyRequest, FcopyResponse>>;
using FcopyProcessor = std::function<coke::Task<>(FcopyServerContext)>;

class FcopyServer : public FcopyServerBase {
public:
    FcopyServer(const WFServerParams &params, FcopyProcessor co_proc)
      : FcopyServerBase(&params, std::bind(&FcopyServer::do_proc, this, std::placeholders::_1)),
        co_proc(std::move(co_proc))
    { }

private:
    void do_proc(FcopyTask *task) {
        coke::Task<> t = co_proc(FcopyServerContext(task));
        t.start_on_series(series_of(task));
    }

private:
    FcopyProcessor co_proc;
};

template<typename RequestMsg, typename ResponseMsg>
coke::Task<int>FcopyClient::request(const RemoteTarget &target,
                                    RequestMsg &&req, ResponseMsg &resp) noexcept
{
    FcopyRequest freq;
    freq.set_message(std::move(req));

    auto res = co_await this->request(target.host, target.port, std::move(freq));
    if (res.state != coke::STATE_SUCCESS)
        co_return res.error;

    FcopyResponse &fresp = res.resp;
    if (!fresp.move_message(resp))
        co_return EBADMSG;

    co_return 0;
}

#endif // FCOPY_TASK_H
