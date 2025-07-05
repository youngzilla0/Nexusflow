
#include "MockOutputModule.hpp"
#include "helper/logging.hpp"
#include "modules/ModuleFactory.hpp"

MockOutputModule::MockOutputModule() : ModuleBase("MockOutputModule") { LOG_TRACE("MockOutputModule constructor"); }

MockOutputModule::~MockOutputModule() { LOG_TRACE("MockOutputModule destructor"); }

void MockOutputModule::Process(const std::shared_ptr<Message>& inputMessage) {
    const auto msg = std::static_pointer_cast<SeqMessage>(inputMessage);
    LOG_INFO("MockOutputModule received message: {}", msg->toString());
}

REGISTER_MODULE(MockOutputModule);