
#include "MyPersonDetectorModule.hpp"
#include "../src/utils/logging.hpp" // TODO: remove
#include "MyMessage.hpp"
#include "nexusflow/Error.hpp"
#include "nexusflow/Message.hpp"

namespace ns = nexusflow;

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

ns::Error MyPersonDetectorModule::Configure(const ns::Config& config) {
    m_modelPath = config.GetValueOrDefault("modelPath", std::string(""));
    LOG_INFO("MyPersonDetectorModule::Configure, name={}, modelPath={}", GetModuleName(), m_modelPath);

    return ns::Error::Ok();
}

ns::Error MyPersonDetectorModule::Init() {
    LOG_INFO("Tring to load model from {}", m_modelPath);

    LOG_INFO("MyPersonDetectorModule::Init, name={}, modelPath={}", GetModuleName(), m_modelPath);
    return ns::Error::Ok();
}

void MyPersonDetectorModule::Process(const ns::PortInputsView& inputs, ns::PortOutputs& outputs) {
    auto* inputMessage = inputs.OnlyMessage();
    if (inputMessage == nullptr) {
        return;
    }

    auto outputMessage = *inputMessage;
    if (auto* msg = outputMessage.MutPtr<InferenceMessage>()) {
        msg->boxes = DetectInfer();
        LOG_INFO("'{}' Send message to next module, data={}", GetModuleName(), msg->toString());
        outputs.Emit(std::move(outputMessage), true);
    }
}
