
#include "MyHeadPersonFusionModule.hpp"
#include "../MyMessage.hpp"
#include "../src/utils/logging.hpp" // TODO: remove
#include "nexusflow/ErrorCode.hpp"
#include "nexusflow/Message.hpp"
#include "nexusflow/ProcessingContext.hpp"
#include <unordered_map>

MyHeadPersonFusionModule::MyHeadPersonFusionModule(const std::string& name) : Module(name) {
    LOG_TRACE("MyHeadPersonFusionModule constructor, name={}", name);
}

MyHeadPersonFusionModule::~MyHeadPersonFusionModule() { LOG_TRACE("MyHeadPersonFusionModule destructor, name={}", GetModuleName()); }

nexusflow::ErrorCode MyHeadPersonFusionModule::Configure(const nexusflow::Config& config) {
    m_modelPath = config.GetValueOrDefault("modelPath", std::string(""));
    LOG_INFO("MyHeadPersonFusionModule::Configure, name={}, modelPath={}", GetModuleName(), m_modelPath);
    return nexusflow::ErrorCode::SUCCESS;
}

nexusflow::ErrorCode MyHeadPersonFusionModule::Init() {
    LOG_INFO("Tring to load model from {}", m_modelPath);

    LOG_INFO("MyHeadPersonFusionModule::Init, name={}, modelPath={}", GetModuleName(), m_modelPath);
    return nexusflow::ErrorCode::SUCCESS;
}

nexusflow::ProcessStatus MyHeadPersonFusionModule::Process(nexusflow::ProcessingContext& ctx) {
    const std::string HEAD_KEY = "HeadDetector";
    const std::string PERSON_KEY = "PersonDetector";

    auto* headMessagePtr = ctx.BorrowPayload<InferenceMessage>(HEAD_KEY);
    auto* personMessagePtr = ctx.BorrowPayload<InferenceMessage>(PERSON_KEY);

    // Check if the input message contains the required keys
    if (headMessagePtr == nullptr || personMessagePtr == nullptr) {
        LOG_ERROR("Input message does not contain the required keys");
        return nexusflow::ProcessStatus::FAILED_GET_INPUT;
    }

    InferenceMessage fusedMessage = DoFusion(*headMessagePtr, *personMessagePtr);
    LOG_INFO("'{}' Send message to next module, data={}", GetModuleName(), fusedMessage.toString());
    ctx.AddOutput(nexusflow::MakeMessage(std::move(fusedMessage)));

    return nexusflow::ProcessStatus::OK;
}

InferenceMessage MyHeadPersonFusionModule::DoFusion(const InferenceMessage& headMessage, const InferenceMessage& personMessage) const {
    InferenceMessage resultMessage;

    // or
    resultMessage.videoFrame = headMessage.videoFrame; // Assuming the video frame is the same for both messages
    // resultMessage.videoFrame = personMessage.videoFrame;

    // mock do fusion

    auto minSize = std::min(headMessage.boxes.size(), personMessage.boxes.size());
    resultMessage.boxes.resize(minSize);

    for (size_t i = 0; i < minSize; ++i) {
        auto& headBox = headMessage.boxes[i];
        auto& personBox = personMessage.boxes[i];
        auto& resultBox = resultMessage.boxes[i];

        resultBox.labelName = "Fusion(" + headBox.labelName + ", " + personBox.labelName + ")";
        resultBox.score = headBox.score + personBox.score;
        resultBox.label = headBox.label + personBox.label;
        resultBox.rect.x0 = headBox.rect.x0 + personBox.rect.x0;
        resultBox.rect.y0 = headBox.rect.y0 + personBox.rect.y0;
        resultBox.rect.x1 = headBox.rect.x1 + personBox.rect.x1;
        resultBox.rect.y1 = headBox.rect.y1 + personBox.rect.y1;
    }

    return resultMessage;
}
