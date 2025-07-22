
#include "MyDecoderModule.hpp"
#include "../src/utils/logging.hpp" // TODO: remove
#include "MyMessage.hpp"
#include "nexusflow/Message.hpp"
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
    auto* msgPtr = inputMessage.MutPtr<DecoderMessage>();

    if (msgPtr == nullptr) {
        LOG_ERROR("MyDecoderModule::Process, name={}, invalid input message type", GetModuleName());
        return nexusflow::ProcessStatus::ERROR;
    }

    LOG_INFO("MyDecoderModule::Process, name={}, inputMessage={}", GetModuleName(), msgPtr->toString());

    auto& videoPackage = msgPtr->videoPackage;
    if (m_frameIdx % m_skipInterval == 0) {
        LOG_INFO("'{}' Send message to next module, data={}", GetModuleName(), msgPtr->toString());

        msgPtr->videoFrame.frameId = m_frameIdx;
        msgPtr->videoFrame.frameData = "frameData-" + std::to_string(m_frameIdx);

        auto inferMessage = ConvertDecoderMessageToInferenceMessage(*msgPtr);
        auto outputMessage = nexusflow::MakeMessage(inferMessage);
        ctx.AddOutput(std::move(outputMessage));
    }
    m_frameIdx++;

    return nexusflow::ProcessStatus::OK;
}
