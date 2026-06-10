
#include "MyHeadPersonFusionModule.hpp"
#include "../MyMessage.hpp"
#include "../src/utils/logging.hpp" // TODO: remove
#include "nexusflow/ErrorCode.hpp"
#include "nexusflow/Message.hpp"

MyHeadPersonFusionModule::MyHeadPersonFusionModule(const std::string& name) : Module(name) {
    SetTriggerPolicy(TriggerPolicy::OnAllInputs);
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

void MyHeadPersonFusionModule::Process(const nexusflow::PortInputsView& inputs, nexusflow::PortOutputs& outputs) {
    auto* headMessage = inputs.Get<InferenceMessage>("head");
    auto* personMessage = inputs.Get<InferenceMessage>("person");
    if (headMessage == nullptr || personMessage == nullptr) {
        LOG_ERROR("Fusion module '{}' requires both 'head' and 'person' inputs", GetModuleName());
        return;
    }

    LOG_DEBUG("'{}' Receive head data={}", GetModuleName(), headMessage->toString());
    LOG_DEBUG("'{}' Receive person data={}", GetModuleName(), personMessage->toString());

    auto fusedMessage = DoFusion(*headMessage, *personMessage);

    LOG_INFO("'{}' Send message to next module, data={}", GetModuleName(), fusedMessage.toString());

    outputs.Emit(nexusflow::MakeMessage(std::move(fusedMessage), GetModuleName()), true);
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
        resultBox.score = 0;
        resultBox.label = 0;
        resultBox.rect.x0 = resultBox.rect.y0 = resultBox.rect.x1 = resultBox.rect.y1 = 0;
    }

    return resultMessage;
}
