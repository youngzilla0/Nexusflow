#include "MockProcessModule.hpp"
#include "helper/logging.hpp"
#include "modules/ModuleFactory.hpp"

MockProcessModule::MockProcessModule() : ModuleBase("MockProcessModule") { LOG_TRACE("MockProcessModule constructor"); }

MockProcessModule::~MockProcessModule() { LOG_TRACE("MockProcessModule destructor"); }

void MockProcessModule::Process(const std::shared_ptr<Message>& inputMessage) {
    const auto message = std::static_pointer_cast<SeqMessage>(inputMessage);

    LOG_DEBUG("Received message is {}", message->toString());

    // Add some data to the message
    message->addData("MockProcessModule-" + std::to_string(m_count++));
    broadcast(message);
}

REGISTER_MODULE(MockProcessModule);
