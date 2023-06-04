#include "co_fcopy.h"

#include "workflow/WFTaskFactory.h"

using FcopyTask = WFNetworkTask<FcopyRequest, FcopyResponse>;
using fcopy_callback_t = std::function<void (FcopyTask *)>;

FcopyTask *create_fcopy_task(const std::string &host, unsigned short port,
                             bool use_ssl, int retry_max, fcopy_callback_t callback)
{
    using ComplexType = WFComplexClientTask<FcopyRequest, FcopyResponse>;

    std::string url("fcopy://");
    ParsedURI uri;

    url.append(host).append(":").append(std::to_string(port));
    URIParser::parse(url, uri);

    auto *task = new ComplexType(retry_max, std::move(callback));
    task->init(std::move(uri));
    task->set_transport_type(use_ssl ? TT_TCP_SSL : TT_TCP);

    return task;
}

FcopyClient::AwaiterType
FcopyClient::request(const std::string &host, unsigned short port, ReqType &&req) noexcept {
    FcopyTask *task = create_fcopy_task(host, port, false, params.retry_max, nullptr);

    *(task->get_req()) = std::move(req);

    task->set_send_timeout(params.send_timeout);
    task->set_receive_timeout(params.receive_timeout);
    task->set_keep_alive(params.keep_alive_timeout);

    return AwaiterType(task);
}
