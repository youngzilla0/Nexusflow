#pragma once

#include "nexusflow/ErrorCode.hpp"
#include <fstream>
#include <nexusflow/Message.hpp>
#include <nexusflow/Module.hpp>

class MyAlarmPusherModule : public nexusflow::Module {
public:
    MyAlarmPusherModule(const std::string& name);
    ~MyAlarmPusherModule() override;

    nexusflow::ErrorCode Configure(const nexusflow::Config& config) override;

    nexusflow::ErrorCode Init() override;

    nexusflow::ErrorCode DeInit() override;

protected:
    void Process(const nexusflow::PortInputsView& inputs, nexusflow::PortOutputs& outputs) override;

private:
    std::string m_savePath;
    std::ofstream m_outFile;
};
