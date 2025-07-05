#pragma once
#include "core/ModuleBase.hpp"

class MockInputModule : public ModuleBase {
public:
    MockInputModule();
    ~MockInputModule() override;

protected:
    void Process(const std::shared_ptr<Message>& inputMessage) override;
};

