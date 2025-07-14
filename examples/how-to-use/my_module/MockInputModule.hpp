#pragma once
#include <nexusflow/Define.hpp>
#include <nexusflow/Message.hpp>
#include <nexusflow/Module.hpp>

class MockInputModule : public nexusflow::Module {
public:
    MockInputModule(const std::string& name);
    ~MockInputModule() override;

    void Configure(const nexusflow::ConfigMap& cfgMap) override;

protected:
    void Process(nexusflow::Message& inputMessage) override;

private:
    int m_sendIntervalMs = 1000 / 5; // 5fps
};
