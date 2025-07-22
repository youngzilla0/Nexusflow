#pragma once

#include <nexusflow/Message.hpp>
#include <nexusflow/Module.hpp>

class MockInputModule : public nexusflow::Module {
public:
    MockInputModule(const std::string& name);
    ~MockInputModule() override;

    nexusflow::ErrorCode Configure(const nexusflow::Config& config) override;

protected:
    nexusflow::ProcessStatus Process(nexusflow::ProcessingContext& ctx) override;

private:
    int m_sendIntervalMs = 1000 / 5; // 5fps
};
