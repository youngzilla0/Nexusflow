
#include "MockOutputModule.hpp"
#include "../src/utils/logging.hpp"
#include "MyMessage.hpp"

MockOutputModule::MockOutputModule(const std::string& name) : Module(name) {
    LOG_TRACE("MockOutputModule constructor, name={}", name);
}

MockOutputModule::~MockOutputModule() { LOG_TRACE("MockOutputModule destructor, name={}", GetModuleName()); }

void MockOutputModule::Process(const nexusflow::PortInputsView& inputs, nexusflow::PortOutputs& outputs) {
    (void)outputs;
    if (auto seqMsg = inputs.OnlyAs<std::shared_ptr<SeqMessage>>()) {
        LOG_INFO(GetModuleName() + " received message: {}", (*seqMsg)->toString());
    }
}

// REGISTER_MODULE(MockOutputModule);
