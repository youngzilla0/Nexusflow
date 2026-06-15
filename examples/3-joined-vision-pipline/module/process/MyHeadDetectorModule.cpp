
#include "MyHeadDetectorModule.hpp"
#include "../MyMessage.hpp"
#include "../src/utils/logging.hpp" // TODO: remove
#include "nexusflow/Error.hpp"
#include "nexusflow/Message.hpp"

namespace ns = nexusflow;

namespace {

std::vector<Box> DetectInfer(uint64_t id) {
    std::vector<Box> boxes;
    // Generate boxes
    for (int i = 0; i < 10; i++) {
        Box box;
        box.label = 333;
        box.labelName = "HEAD-" + std::to_string(id);
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

MyHeadDetectorModule::MyHeadDetectorModule(const std::string& name) : Module(name) {
    LOG_TRACE("MyHeadDetectorModule constructor, name={}", name);
}

MyHeadDetectorModule::~MyHeadDetectorModule() { LOG_TRACE("MyHeadDetectorModule destructor, name={}", GetModuleName()); }

ns::Error MyHeadDetectorModule::Configure(const ns::Config& config) {
    m_modelPath = config.GetValueOrDefault("modelPath", std::string(""));
    LOG_INFO("MyHeadDetectorModule::Configure, name={}, modelPath={}", GetModuleName(), m_modelPath);
    return ns::Error::Ok();
}

ns::Error MyHeadDetectorModule::Init() {
    LOG_INFO("Tring to load model from {}", m_modelPath);
    LOG_INFO("MyHeadDetectorModule::Init, name={}, modelPath={}", GetModuleName(), m_modelPath);
    return ns::Error::Ok();
}

void MyHeadDetectorModule::Process(const ns::PortInputsView& inputs, ns::PortOutputs& outputs) {
    auto* inputMessage = inputs.OnlyMessage();
    if (inputMessage == nullptr) {
        return;
    }

    auto outputMessage = *inputMessage;
    if (auto* msg = outputMessage.MutPtr<InferenceMessage>()) {
        msg->boxes = DetectInfer(msg->videoFrame.frameId);
        LOG_INFO("'{}' Send message to next module, data={}", GetModuleName(), msg->toString());

        outputMessage.MetaData().sourceName = GetModuleName();
        outputs.Emit(std::move(outputMessage), true);
    }
}
