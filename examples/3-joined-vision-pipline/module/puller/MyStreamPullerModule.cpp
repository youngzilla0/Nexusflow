
#include "MyStreamPullerModule.hpp"
#include "../MyMessage.hpp"
#include "../src/utils/logging.hpp" // TODO: remove
#include "nexusflow/Message.hpp"
#include <chrono>
#include <thread>

namespace {

DecoderMessage CreateMessage(uint32_t fps) {
    const int sendIntervalMs = 1000 / fps; // 25fps
    std::this_thread::sleep_for(std::chrono::milliseconds(sendIntervalMs));

    static int frameIdx = 0;
    DecoderMessage msg;
    msg.videoPackage = "package-" + std::to_string(frameIdx++);

    auto now = std::chrono::system_clock::now();
    msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    return msg;
}

} // namespace

MyStreamPullerModule::MyStreamPullerModule(const std::string& name) : Module(name) {
    LOG_TRACE("MyStreamPullerModule constructor, name={}", name);
}

MyStreamPullerModule::~MyStreamPullerModule() { LOG_TRACE("MyStreamPullerModule destructor, name={}", GetModuleName()); }

nexusflow::ProcessStatus MyStreamPullerModule::Process(nexusflow::ProcessingContext& ctx) {
    constexpr uint32_t kFPS = 25;
    auto msg = CreateMessage(kFPS);
    ctx.AddOutput(nexusflow::MakeMessage(std::move(msg)));
    return nexusflow::ProcessStatus::OK;
}
