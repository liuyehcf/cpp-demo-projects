#include <brpc/channel.h>

#include "echo.pb.h"

int main(int argc, char* argv[]) {
    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.protocol = "baidu_std";
    options.connection_type = "single";
    options.timeout_ms = 1000;
    options.max_retry = 3;

    if (channel.Init("127.0.0.1:8000", &options) != 0) {
        LOG(ERROR) << "Fail to initialize channel";
        return -1;
    }

    example::EchoService_Stub stub(&channel);

    example::EchoRequest request;
    example::EchoResponse response;
    brpc::Controller cntl;

    request.set_message("Hello BRPC!");

    stub.Echo(&cntl, &request, &response, nullptr);

    if (cntl.Failed()) {
        LOG(ERROR) << "Fail to send request, " << cntl.ErrorText();
        return -1;
    }

    LOG(INFO) << "Received response: " << response.message();
    return 0;
}
