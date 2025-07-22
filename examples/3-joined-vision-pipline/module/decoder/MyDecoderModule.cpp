
#include "MyDecoderModule.hpp"
#include "../MyMessage.hpp"
#include "../src/utils/logging.hpp" // TODO: remove
#include "nexusflow/ErrorCode.hpp"
#include "nexusflow/Message.hpp"
#include "nexusflow/ProcessingContext.hpp"
#include <thread>
#include <type_traits>

MyDecoderModule::MyDecoderModule(const std::string& name) : Module(name) {
    m_frameIdx = 0;
    LOG_TRACE("MyDecoderModule constructor, name={}", name);
}

MyDecoderModule::~MyDecoderModule() { LOG_TRACE("MyDecoderModule destructor, name={}", GetModuleName()); }

nexusflow::ErrorCode MyDecoderModule::Configure(const nexusflow::Config& config) {
    m_skipInterval = config.GetValueOrDefault("skipInterval", 25);
    LOG_INFO("MyDecoderModule::Configure, name={}, skipInterval={}", GetModuleName(), m_skipInterval);
    return nexusflow::ErrorCode::SUCCESS;
}

nexusflow::ProcessStatus MyDecoderModule::Process(nexusflow::ProcessingContext& ctx) {
    auto inputMessage = ctx.TakeInput();
    auto* msg = inputMessage.MutPtr<DecoderMessage>();

    if (msg == nullptr) {
        LOG_ERROR("'{}' Input message is null", GetModuleName());
        return nexusflow::ProcessStatus::FAILED_GET_INPUT;
    }

    auto& videoPackage = msg->videoPackage;
    if (m_frameIdx % m_skipInterval == 0) {
        msg->videoFrame.frameId = m_frameIdx;
        msg->videoFrame.frameData = "frameData-" + std::to_string(m_frameIdx);
        LOG_INFO("'{}' Send message to next module, data={}", GetModuleName(), msg->toString());
        auto outputMessage = ConvertDecoderMessageToInferenceMessage(*msg);
        ctx.AddOutput(nexusflow::MakeMessage(std::move(outputMessage)));
    }
    m_frameIdx++;
    return nexusflow::ProcessStatus::OK;
}
