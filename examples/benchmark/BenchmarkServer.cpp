#include <iostream>
#include <atomic>

#include <brynet/net/EventLoop.hpp>
#include <brynet/net/TcpService.hpp>
#include <brynet/net/ListenThread.hpp>
#include <brynet/base/AppStatus.hpp>

#include <gayrpc/utils/UtilsWrapper.h>

#include "./pb/benchmark_service.gayrpc.h"

using namespace brynet;
using namespace brynet::net;
using namespace dodo::benchmark;
using namespace gayrpc::core;
using namespace gayrpc::utils;

static std::atomic<int64_t> count(0);

class MyService : public EchoServerService
{
public:
    explicit MyService(gayrpc::core::ServiceContext&& context)
        :
        EchoServerService(std::forward<gayrpc::core::ServiceContext>(context))
    {}

    void Echo(const EchoRequest& request, 
        const EchoReply::PTR& replyObj,
        InterceptorContextType&& context) override
    {
        EchoResponse response;
        response.set_message(request.message());

        replyObj->reply(response, std::forward<InterceptorContextType>(context));
    }
};

static auto counter(RpcMeta&& meta,
                    const google::protobuf::Message& message,
                    UnaryHandler&& next,
                    InterceptorContextType&& context)
{
    count++;
    return next(std::forward<RpcMeta>(meta), message, std::forward<InterceptorContextType>(context));
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage: <listen port> <thread num>\n");
        exit(-1);
    }

    auto service = TcpService::Create();
    service->startWorkerThread(std::thread::hardware_concurrency());

    auto serviceBuild = ServiceBuilder<EchoServerService>();
    serviceBuild.buildInboundInterceptor([](BuildInterceptor buildInterceptors) {
            buildInterceptors.addInterceptor(counter);
        })
        .configureConnectionOptions( {
            AddSocketOption::WithMaxRecvBufferSize(1024 * 1024),
            AddSocketOption::AddEnterCallback([](const TcpConnection::Ptr& session) {
                session->setHeartBeat(std::chrono::seconds(10));
            })
        })
        .configureService(service)
        .configureCreator([](gayrpc::core::ServiceContext&& context) {
            return std::make_shared<MyService>(std::move(context));
        })
        .configureListen([argv](wrapper::BuildListenConfig listenConfig) {
            listenConfig.setAddr(false, "0.0.0.0", std::stoi(argv[1]));
        })
        .asyncRun();

    EventLoop mainLoop;
    std::atomic<int64_t> tmp(0);

    while (true)
    {
        mainLoop.loop(1000);
        std::cout << "count is:" << (count-tmp) << std::endl;
        tmp.store(count);
        if (brynet::base::app_kbhit() > 0)
        {
            break;
        }
    }

    return 0;
}
