
#pragma once
#include "core/ModuleBase.hpp"

class MockOutputModule : public ModuleBase {
public:
    MockOutputModule();
    ~MockOutputModule() override;

protected:
    void Process(const std::shared_ptr<Message>& inputMessage) override;
};
