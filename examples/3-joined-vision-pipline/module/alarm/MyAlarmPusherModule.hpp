#pragma once

#include <fstream>
#include <nexusflow/Message.hpp>
#include <nexusflow/Module.hpp>

namespace ns = nexusflow;

class MyAlarmPusherModule : public ns::Module {
public:
    MyAlarmPusherModule(const std::string& name);
    ~MyAlarmPusherModule() override;

    ns::Error Configure(const ns::Config& config) override;

    ns::Error Init() override;

    ns::Error DeInit() override;

protected:
    void Process(const ns::PortInputsView& inputs, ns::PortOutputs& outputs) override;

private:
    std::string m_savePath;
    std::ofstream m_outFile;
};
