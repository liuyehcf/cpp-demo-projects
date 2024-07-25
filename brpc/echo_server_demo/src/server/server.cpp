#include <brpc/server.h>

#include "echo.pb.h"

class EchoServiceImpl : public example::EchoService {
public:
    void Echo(google::protobuf::RpcController* controller, const example::EchoRequest* request,
              example::EchoResponse* response, google::protobuf::Closure* done) override {
        response->set_message("Echo: " + request->message());
        done->Run();
    }
};

int main(int argc, char* argv[]) {
    brpc::Server server;

    EchoServiceImpl echo_service_impl;

    if (server.AddService(&echo_service_impl, brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(ERROR) << "Fail to add service";
        return -1;
    }

    brpc::ServerOptions options;
    if (server.Start(8000, &options) != 0) {
        LOG(ERROR) << "Fail to start server";
        return -1;
    }

    server.RunUntilAskedToQuit();
    return 0;
}
