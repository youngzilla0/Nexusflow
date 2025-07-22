#include "MockProcessModule.hpp"
#include "../src/utils/logging.hpp"
#include "MyMessage.hpp"
#include "nexusflow/Message.hpp"
#include "nexusflow/ProcessingContext.hpp"

MockProcessModule::MockProcessModule(const std::string& name) : Module(name) {
    LOG_TRACE("MockProcessModule constructor, name={}", name);
}

MockProcessModule::~MockProcessModule() { LOG_TRACE("MockProcessModule destructor, name={}", GetModuleName()); }

nexusflow::ProcessStatus MockProcessModule::Process(nexusflow::ProcessingContext& ctx) {
    auto seqMsgPtr = ctx.MutPayload<std::shared_ptr<SeqMessage>>();
    if (seqMsgPtr == nullptr) {
        return nexusflow::ProcessStatus::FAILED_GET_INPUT;
    }
    auto& seqMsg = *seqMsgPtr;
    LOG_DEBUG("Received message is {}", seqMsg->toString());
    // Add some data to the message
    seqMsg->addData(GetModuleName() + "_" + std::to_string(m_count++));
    LOG_INFO(GetModuleName() + ": send message: {}", seqMsg->toString());

    return nexusflow::ProcessStatus::OK;
}

// REGISTER_MODULE(MockProcessModule);
