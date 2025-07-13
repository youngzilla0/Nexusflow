#pragma once

#include "nexusflow/Message.hpp"
#include <nexusflow/Module.hpp>

class MockProcessModule : public nexusflow::Module {
public:
    MockProcessModule(const std::string& name);
    ~MockProcessModule() override;

protected:
    void Process(nexusflow::SharedMessage& inputMessage) override;

private:
    int m_count = 0;
};
