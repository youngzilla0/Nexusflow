
#include "MyBehaviorAnalyzerModule.hpp"
#include "../src/utils/logging.hpp" // TODO: remove
#include "MyMessage.hpp"
#include "nexusflow/ErrorCode.hpp"
#include "nexusflow/Message.hpp"
#include "nexusflow/ProcessingContext.hpp"

MyBehaviorAnalyzerModule::MyBehaviorAnalyzerModule(const std::string& name) : Module(name) {
    LOG_TRACE("MyBehaviorAnalyzerModule constructor, name={}", name);
}

MyBehaviorAnalyzerModule::~MyBehaviorAnalyzerModule() { LOG_TRACE("MyBehaviorAnalyzerModule destructor, name={}", GetModuleName()); }

nexusflow::ErrorCode MyBehaviorAnalyzerModule::Configure(const nexusflow::Config& config) {
    m_modelPath = config.GetValueOrDefault("modelPath", std::string(""));
    LOG_INFO("MyBehaviorAnalyzerModule::Configure, name={}, modelPath={}", GetModuleName(), m_modelPath);

    return nexusflow::ErrorCode::SUCCESS;
}

nexusflow::ErrorCode MyBehaviorAnalyzerModule::Init() {
    LOG_INFO("Tring to load model from {}", m_modelPath);

    LOG_INFO("MyBehaviorAnalyzerModule::Init, name={}, modelPath={}", GetModuleName(), m_modelPath);
    return nexusflow::ErrorCode::SUCCESS;
}

nexusflow::ProcessStatus MyBehaviorAnalyzerModule::Process(nexusflow::ProcessingContext& ctx) {
    auto* inferMsgPtr = ctx.MutPayload<InferenceMessage>();

    if (inferMsgPtr == nullptr) {
        LOG_ERROR("'{}' Input message is null", GetModuleName());
        return nexusflow::ProcessStatus::FAILED_GET_INPUT;
    }

    for (auto& box : inferMsgPtr->boxes) {
        box.clsLabel = 999;
        box.clsScore = 1.0f;
        box.clsLabelName = "Class-" + std::to_string(box.label);
    }

    LOG_INFO("'{}' Send message to next module, data={}", GetModuleName(), inferMsgPtr->toString());

    return nexusflow::ProcessStatus::OK;
}
