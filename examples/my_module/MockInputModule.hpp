#pragma once
#include "nexusflow/Message.hpp"
#include <nexusflow/Module.hpp>

class MockInputModule : public nexusflow::Module {
public:
    MockInputModule(const std::string& name);
    ~MockInputModule() override;

protected:
    void Process(nexusflow::SharedMessage& inputMessage) override;
};
