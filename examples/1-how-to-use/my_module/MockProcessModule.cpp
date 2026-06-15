#include "MockProcessModule.hpp"
#include "../src/utils/logging.hpp"
#include "MyMessage.hpp"
#include "nexusflow/Message.hpp"

namespace ns = nexusflow;

MockProcessModule::MockProcessModule(const std::string& name) : Module(name) {
    LOG_TRACE("MockProcessModule constructor, name={}", name);
}

MockProcessModule::~MockProcessModule() { LOG_TRACE("MockProcessModule destructor, name={}", GetModuleName()); }

void MockProcessModule::Process(const ns::PortInputsView& inputs, ns::PortOutputs& outputs) {
    auto* inputMessage = inputs.OnlyMessage();
    if (inputMessage == nullptr) {
        return;
    }

    auto outputMessage = *inputMessage;
    if (auto& seqMsg = outputMessage.Mut<std::shared_ptr<SeqMessage>>()) {
        LOG_DEBUG("Received message is {}", seqMsg->toString());
        seqMsg->addData(GetModuleName() + "_" + std::to_string(m_count++));
        LOG_INFO(GetModuleName() + ": send message: {}", seqMsg->toString());
        outputs.Emit(ns::MakeMessage(std::move(seqMsg), GetModuleName()), true);
    }
}

// REGISTER_MODULE(MockProcessModule);
