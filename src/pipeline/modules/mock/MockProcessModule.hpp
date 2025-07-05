#pragma once

#include "ErrorCode.hpp"
#include "core/ModuleBase.hpp"
#include "helper/logging.hpp"

class MockProcessModule : public ModuleBase {
public:
    MockProcessModule();
    ~MockProcessModule() override;

protected:
    void Process(const std::shared_ptr<Message>& inputMessage) override;

private:
    int m_count = 0; // for debug
};
