
#include "MockInputModule.hpp"
#include "helper/logging.hpp"
#include "modules/ModuleFactory.hpp"
#include <thread>

MockInputModule::MockInputModule() : ModuleBase("MockInputModule") { LOG_TRACE("MockInputModule constructor"); }

MockInputModule::~MockInputModule() { LOG_TRACE("MockInputModule destructor"); }

void MockInputModule::Process(const std::shared_ptr<Message>& inputMessage) {
    // no input message
    if (inputMessage != nullptr) {
        LOG_WARN("MockInputModule: inputMessage is not nullptr");
        return;
    }

    // mock 5 fps messages.
    std::this_thread::sleep_for(std::chrono::milliseconds(1000 / 5));
    static int counter = 0;
    const auto message = std::make_shared<SeqMessage>();
    message->addData("MockInputModule-" + std::to_string(counter++));

    broadcast(message);
}

REGISTER_MODULE(MockInputModule);
