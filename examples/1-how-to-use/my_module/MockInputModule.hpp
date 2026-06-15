#pragma once

#include <nexusflow/Message.hpp>
#include <nexusflow/Module.hpp>

namespace ns = nexusflow;

class MockInputModule : public ns::Module {
    

public:
    MockInputModule(const std::string& name);
    ~MockInputModule() override;

    ns::Error Configure(const ns::Config& config) override;

protected:
    void Process(const ns::PortInputsView& inputs, ns::PortOutputs& outputs) override;

private:
    int m_sendIntervalMs = 1000 / 5; // 5fps
};
