
#include "MyAlarmPusherModule.hpp"
#include "../src/utils/logging.hpp" // TODO: remove
#include "MyMessage.hpp"
#include "nexusflow/Message.hpp"
#include <thread>

namespace ns = nexusflow;

MyAlarmPusherModule::MyAlarmPusherModule(const std::string& name) : Module(name) {
    LOG_TRACE("MyAlarmPusherModule constructor, name={}", name);
}

MyAlarmPusherModule::~MyAlarmPusherModule() { LOG_TRACE("MyAlarmPusherModule destructor, name={}", GetModuleName()); }

ns::Error MyAlarmPusherModule::Configure(const ns::Config& config) {
    m_savePath = config.GetValueOrDefault("savePath", std::string("default-result.txt"));
    LOG_INFO("MyAlarmPusherModule::Configure, name={}, savePath={}", GetModuleName(), m_savePath);

    return ns::Error::Ok();
}

ns::Error MyAlarmPusherModule::Init() {
    m_outFile.open(m_savePath, std::ios::out);
    if (!m_outFile.is_open()) {
        LOG_ERROR("MyAlarmPusherModule::Init, name={}, open file failed, path={}", GetModuleName(), m_savePath);
        return ns::Error::Err(ns::Error::Code::FileOpenFailed, "Failed to open output file.");
    }

    LOG_INFO("MyAlarmPusherModule::Init, name={}, savePath={}", GetModuleName(), m_savePath);
    return ns::Error::Ok();
}

ns::Error MyAlarmPusherModule::DeInit() {
    if (m_outFile.is_open()) m_outFile.close();
    LOG_INFO("MyAlarmPusherModule::DeInit, name={}", GetModuleName());
    return ns::Error::Ok();
}

void MyAlarmPusherModule::Process(const ns::PortInputsView& inputs, ns::PortOutputs& outputs) {
    (void)outputs;
    if (auto* msg = inputs.OnlyAs<InferenceMessage>()) {
        m_outFile << msg->toString() << std::endl;
    }
}
