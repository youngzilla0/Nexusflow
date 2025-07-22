
#include "MockOutputModule.hpp"
#include "../src/utils/logging.hpp"
#include "MyMessage.hpp"

MockOutputModule::MockOutputModule(const std::string& name) : Module(name) {
    LOG_TRACE("MockOutputModule constructor, name={}", name);
}

MockOutputModule::~MockOutputModule() { LOG_TRACE("MockOutputModule destructor, name={}", GetModuleName()); }

nexusflow::ProcessStatus MockOutputModule::Process(nexusflow::ProcessingContext& ctx) {
    if (const std::shared_ptr<SeqMessage>* seqMsgPtr = ctx.BorrowPayload<std::shared_ptr<SeqMessage>>()) {
        auto& seqMsg = *seqMsgPtr;
        LOG_INFO(GetModuleName() + " received message: {}", seqMsg->toString());
    }
    return nexusflow::ProcessStatus::OK;
}

// REGISTER_MODULE(MockOutputModule);