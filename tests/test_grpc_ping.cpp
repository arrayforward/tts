#include <iostream>
#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>
#include <grpcpp/support/channel_arguments.h>
#include "pb2/tts.grpc.pb.h"

int main(int argc, char** argv) {
    std::string target = "localhost:50061";
    if (argc > 1) target = argv[1];
    std::cout << "connecting to " << target << "\n";

    auto channel = grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
    auto stub = ::tts::TTS::NewStub(channel);

    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(3));
    ::google::protobuf::Empty req;
    ::tts::MetricsSnapshot resp;
    auto status = stub->GetMetrics(&ctx, req, &resp);
    std::cout << "status=" << status.error_code() << " msg=" << status.error_message()
              << " inflight=" << resp.inflight_requests() << "\n";
    return status.ok() ? 0 : 1;
}