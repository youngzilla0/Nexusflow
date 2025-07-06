
#include "MockInputModule.hpp"
#include "../src/utils/logging.hpp" // TODO: remove
#include "MyMessage.hpp"
#include <thread>

MockInputModule::MockInputModule(const std::string& name) : Module(name) { LOG_TRACE("MockInputModule constructor, name={}", name); }

MockInputModule::~MockInputModule() { LOG_TRACE("MockInputModule destructor, name={}", GetModuleName()); }

void MockInputModule::Process(const std::shared_ptr<nexusflow::Message>& inputMessage) {
    // no input message
    if (inputMessage != nullptr) {
        LOG_WARN("MockInputModule: inputMessage is not nullptr");
        return;
    }

    // mock 5 fps messages.
    std::this_thread::sleep_for(std::chrono::milliseconds(1000 / 5));
    static int counter = 0;
    const auto message = std::make_shared<SeqMessage>();
    message->addData(GetModuleName() + "_" + std::to_string(counter++));
    LOG_INFO(GetModuleName() + ": send message: {}", message->toString());

    Broadcast(message);
}
