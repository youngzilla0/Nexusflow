
#include "MyDecoderModule.hpp"
#include "../MyMessage.hpp"
#include "../src/utils/logging.hpp" // TODO: remove
#include "nexusflow/Error.hpp"
#include "nexusflow/Message.hpp"
#include <thread>
#include <type_traits>

namespace ns = nexusflow;

MyDecoderModule::MyDecoderModule(const std::string& name) : Module(name) {
    m_frameIdx = 0;
    LOG_TRACE("MyDecoderModule constructor, name={}", name);
}

MyDecoderModule::~MyDecoderModule() { LOG_TRACE("MyDecoderModule destructor, name={}", GetModuleName()); }

ns::Error MyDecoderModule::Configure(const ns::Config& config) {
    m_skipInterval = config.GetValueOrDefault("skipInterval", 25);
    LOG_INFO("MyDecoderModule::Configure, name={}, skipInterval={}", GetModuleName(), m_skipInterval);
    return ns::Error::Ok();
}

void MyDecoderModule::Process(const ns::PortInputsView& inputs, ns::PortOutputs& outputs) {
    auto* inputMessage = inputs.OnlyMessage();
    if (inputMessage == nullptr) {
        return;
    }

    auto outputMessage = *inputMessage;
    if (auto* msg = outputMessage.MutPtr<DecoderMessage>()) {
        if (m_frameIdx % m_skipInterval == 0) {
            msg->videoFrame.frameId = m_frameIdx;
            msg->videoFrame.frameData = "frameData-" + std::to_string(m_frameIdx);
            LOG_INFO("'{}' Send message to next module, data={}", GetModuleName(), msg->toString());

            auto inferenceMessage = ConvertDecoderMessageToInferenceMessage(*msg);
            outputs.Emit(ns::MakeMessage(std::move(inferenceMessage), GetModuleName()), true);
        }
        m_frameIdx++;
    }
}
