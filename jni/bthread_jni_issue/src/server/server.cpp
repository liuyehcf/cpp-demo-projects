#include <brpc/server.h>
#include <jni_utils.h>

#include "echo.pb.h"

class EchoServiceImpl : public example::EchoService {
public:
    void Echo(google::protobuf::RpcController* controller, const example::EchoRequest* request,
              example::EchoResponse* response, google::protobuf::Closure* done) override {
        response->set_message("Echo: " + request->message() + ", Java message: " + getMessageFromJava());
        done->Run();
    }

private:
    inline static std::string getMessageFromJava() {
        using namespace jni_utils;
        auto* env = get_env();
        AutoLocalJobject jcls = find_class(env, "SynchronizedServer");
        auto mid = get_method(env, jcls, "getMessage", "()Ljava/lang/String;", true);
        AutoLocalJobject jstr = static_cast<jstring>(invoke_static_method(env, jcls, &mid).l);
        const char* chars = env->GetStringUTFChars(jstr, nullptr);
        std::string res = chars;
        env->ReleaseStringUTFChars(jstr, chars);
        return res;
    }
};

namespace brpc {
DECLARE_bool(usercode_in_pthread);
}

int main(int argc, char* argv[]) {
    if (argc > 0) {
        brpc::FLAGS_usercode_in_pthread = (std::stoi(argv[1]) != 0);
    }
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
