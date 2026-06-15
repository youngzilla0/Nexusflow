#pragma once

#include "nexusflow/Message.hpp"
#include <nexusflow/Module.hpp>

namespace ns = nexusflow;

class MockProcessModule : public ns::Module {
public:
    MockProcessModule(const std::string& name);

    ~MockProcessModule() override;

protected:
    void Process(const ns::PortInputsView& inputs, ns::PortOutputs& outputs) override;

private:
    int m_count = 0;
};
