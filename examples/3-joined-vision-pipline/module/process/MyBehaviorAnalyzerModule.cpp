
#include "MyBehaviorAnalyzerModule.hpp"
#include "../MyMessage.hpp"
#include "../src/utils/logging.hpp" // TODO: remove
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
    auto* msg = ctx.MutPayload<InferenceMessage>();
    if (msg == nullptr) {
        LOG_ERROR("Failed to get input message, name={}", GetModuleName());
        return nexusflow::ProcessStatus::FAILED_GET_INPUT;
    }

    for (auto& box : msg->boxes) {
        box.clsLabel = msg->videoFrame.frameId;
        box.clsScore = msg->videoFrame.frameId;
        box.clsLabelName = "Class-" + std::to_string(msg->videoFrame.frameId);
    }
    LOG_INFO("'{}' Send message to next module, data={}", GetModuleName(), msg->toString());

    return nexusflow::ProcessStatus::OK;
}
