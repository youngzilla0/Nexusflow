#pragma once

#include "nexusflow/ErrorCode.hpp"

#include <nexusflow/Message.hpp>
#include <nexusflow/Module.hpp>

class MyPersonDetectorModule : public nexusflow::Module {
    

public:
    MyPersonDetectorModule(const std::string& name);
    ~MyPersonDetectorModule() override;

    nexusflow::ErrorCode Configure(const nexusflow::Config& config) override;

    nexusflow::ErrorCode Init() override;

protected:
    void Process(const nexusflow::PortInputsView& inputs, nexusflow::PortOutputs& outputs) override;

private:
    std::string m_modelPath;
};
