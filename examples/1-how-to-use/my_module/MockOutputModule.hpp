
#pragma once
#include "nexusflow/Message.hpp"
#include <nexusflow/Module.hpp>

namespace ns = nexusflow;

class MockOutputModule : public ns::Module {
public:
    MockOutputModule(const std::string& name);
    ~MockOutputModule() override;

protected:
    void Process(const ns::PortInputsView& inputs, ns::PortOutputs& outputs) override;
};
