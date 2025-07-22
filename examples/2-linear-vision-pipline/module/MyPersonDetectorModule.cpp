
#include "MyPersonDetectorModule.hpp"
#include "../src/utils/logging.hpp" // TODO: remove
#include "MyMessage.hpp"
#include "nexusflow/ErrorCode.hpp"
#include "nexusflow/Message.hpp"
#include "nexusflow/ProcessingContext.hpp"

namespace {

std::vector<Box> DetectInfer() {
    std::vector<Box> boxes;
    // Generate boxes
    for (int i = 0; i < 10; i++) {
        Box box;
        box.label = 666;
        box.labelName = "Detection-" + std::to_string(box.label);
        box.score = 1.0;
        box.rect.x0 = i * 10;
        box.rect.y0 = i * 10;
        box.rect.x1 = i * 10 + 100;
        box.rect.y1 = i * 10 + 100;
        boxes.push_back(box);
    }
    return boxes;
}

} // namespace

MyPersonDetectorModule::MyPersonDetectorModule(const std::string& name) : Module(name) {
    LOG_TRACE("MyPersonDetectorModule constructor, name={}", name);
}

MyPersonDetectorModule::~MyPersonDetectorModule() { LOG_TRACE("MyPersonDetectorModule destructor, name={}", GetModuleName()); }

nexusflow::ErrorCode MyPersonDetectorModule::Configure(const nexusflow::Config& config) {
    m_modelPath = config.GetValueOrDefault("modelPath", std::string(""));
    LOG_INFO("MyPersonDetectorModule::Configure, name={}, modelPath={}", GetModuleName(), m_modelPath);

    return nexusflow::ErrorCode::SUCCESS;
}

nexusflow::ErrorCode MyPersonDetectorModule::Init() {
    LOG_INFO("Tring to load model from {}", m_modelPath);

    LOG_INFO("MyPersonDetectorModule::Init, name={}, modelPath={}", GetModuleName(), m_modelPath);
    return nexusflow::ErrorCode::SUCCESS;
}

nexusflow::ProcessStatus MyPersonDetectorModule::Process(nexusflow::ProcessingContext& ctx) {
    auto* msg = ctx.MutPayload<InferenceMessage>();

    if (msg == nullptr) {
        LOG_ERROR("MyPersonDetectorModule::Process, name={}, msg is null", GetModuleName());
        return nexusflow::ProcessStatus::FAILED_GET_INPUT;
    }

    msg->boxes = DetectInfer();
    LOG_INFO("'{}' Send message to next module, data={}", GetModuleName(), msg->toString());

    return nexusflow::ProcessStatus::OK;
}
