
#include "MockOutputModule.hpp"
#include "../src/utils/logging.hpp"

MockOutputModule::MockOutputModule(const std::string& name) : Module(name) {
    LOG_TRACE("MockOutputModule constructor, name={}", name);
}

MockOutputModule::~MockOutputModule() { LOG_TRACE("MockOutputModule destructor, name={}", GetModuleName()); }

void MockOutputModule::Process(const std::shared_ptr<nexusflow::Message>& inputMessage) {
    const auto msg = std::static_pointer_cast<nexusflow::SeqMessage>(inputMessage);
    LOG_INFO(GetModuleName() + " received message: {}", msg->toString());
}

// REGISTER_MODULE(MockOutputModule);