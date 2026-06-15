#pragma once

#include "nexusflow/Error.hpp"

#include <nexusflow/Message.hpp>
#include <nexusflow/Module.hpp>

namespace ns = nexusflow;

class MyPersonDetectorModule : public ns::Module {
    

public:
    MyPersonDetectorModule(const std::string& name);
    ~MyPersonDetectorModule() override;

    ns::Error Configure(const ns::Config& config) override;

    ns::Error Init() override;

protected:
    void Process(const ns::PortInputsView& inputs, ns::PortOutputs& outputs) override;

private:
    std::string m_modelPath;
};
