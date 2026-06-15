
#include "MyBehaviorAnalyzerModule.hpp"
#include "../MyMessage.hpp"
#include "../src/utils/logging.hpp" // TODO: remove
#include "nexusflow/Error.hpp"
#include "nexusflow/Message.hpp"

namespace ns = nexusflow;

MyBehaviorAnalyzerModule::MyBehaviorAnalyzerModule(const std::string& name) : Module(name) {
    LOG_TRACE("MyBehaviorAnalyzerModule constructor, name={}", name);
}

MyBehaviorAnalyzerModule::~MyBehaviorAnalyzerModule() { LOG_TRACE("MyBehaviorAnalyzerModule destructor, name={}", GetModuleName()); }

ns::Error MyBehaviorAnalyzerModule::Configure(const ns::Config& config) {
    m_modelPath = config.GetValueOrDefault("modelPath", std::string(""));
    LOG_INFO("MyBehaviorAnalyzerModule::Configure, name={}, modelPath={}", GetModuleName(), m_modelPath);
    return ns::Error::Ok();
}

ns::Error MyBehaviorAnalyzerModule::Init() {
    LOG_INFO("Tring to load model from {}", m_modelPath);

    LOG_INFO("MyBehaviorAnalyzerModule::Init, name={}, modelPath={}", GetModuleName(), m_modelPath);
    return ns::Error::Ok();
}

void MyBehaviorAnalyzerModule::Process(const ns::PortInputsView& inputs, ns::PortOutputs& outputs) {
    auto* inputMessage = inputs.OnlyMessage();
    if (inputMessage == nullptr) {
        return;
    }

    auto outputMessage = *inputMessage;
    if (auto* msg = outputMessage.MutPtr<InferenceMessage>()) {
        for (auto& box : msg->boxes) {
            box.clsLabel = 999;
            box.clsScore = 1.0f;
            box.clsLabelName = "Class-" + std::to_string(msg->videoFrame.frameId);
        }
        LOG_INFO("'{}' Send message to next module, data={}", GetModuleName(), msg->toString());
        outputs.Emit(std::move(outputMessage), true);
    }
}
