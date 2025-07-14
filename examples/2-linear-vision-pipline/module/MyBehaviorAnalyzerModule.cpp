
#include "MyBehaviorAnalyzerModule.hpp"
#include "../src/utils/logging.hpp" // TODO: remove
#include "MyMessage.hpp"
#include "nexusflow/ErrorCode.hpp"
#include "nexusflow/Message.hpp"

MyBehaviorAnalyzerModule::MyBehaviorAnalyzerModule(const std::string& name) : Module(name) {
    LOG_TRACE("MyBehaviorAnalyzerModule constructor, name={}", name);
}

MyBehaviorAnalyzerModule::~MyBehaviorAnalyzerModule() { LOG_TRACE("MyBehaviorAnalyzerModule destructor, name={}", GetModuleName()); }

void MyBehaviorAnalyzerModule::Configure(const nexusflow::ConfigMap& cfgMap) {
    m_modelPath = GetConfigOrDefault(cfgMap, "modelPath", std::string(""));
    LOG_INFO("MyBehaviorAnalyzerModule::Configure, name={}, modelPath={}", GetModuleName(), m_modelPath);
}

nexusflow::ErrorCode MyBehaviorAnalyzerModule::Init() {
    LOG_INFO("Tring to load model from {}", m_modelPath);

    LOG_INFO("MyBehaviorAnalyzerModule::Init, name={}, modelPath={}", GetModuleName(), m_modelPath);
    return nexusflow::ErrorCode::SUCCESS;
}

void MyBehaviorAnalyzerModule::Process(nexusflow::Message& inputMessage) {
    if (auto* msg = inputMessage.GetData<InferenceMessage>()) {
        for (auto& box : msg->boxes) {
            box.clsLabel = 999;
            box.clsScore = 1.0f;
            box.clsLabelName = "Class-" + std::to_string(box.label);
        }
        LOG_INFO("'{}' Send message to next module, data={}", GetModuleName(), msg->toString());
        Broadcast(inputMessage);
    }
}
