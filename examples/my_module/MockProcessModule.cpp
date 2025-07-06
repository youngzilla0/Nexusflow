#include "MockProcessModule.hpp"
#include "../src/utils/logging.hpp"

MockProcessModule::MockProcessModule(const std::string& name) : Module(name) {
    LOG_TRACE("MockProcessModule constructor, name={}", name);
}

MockProcessModule::~MockProcessModule() { LOG_TRACE("MockProcessModule destructor, name={}", GetModuleName()); }

void MockProcessModule::Process(const std::shared_ptr<nexusflow::Message>& inputMessage) {
    const auto message = std::static_pointer_cast<nexusflow::SeqMessage>(inputMessage);

    LOG_DEBUG("Received message is {}", message->toString());

    // Add some data to the message
    message->addData(GetModuleName() + "_" + std::to_string(m_count++));
    LOG_INFO(GetModuleName() + ": send message: {}", message->toString());

    Broadcast(message);
}

// REGISTER_MODULE(MockProcessModule);
