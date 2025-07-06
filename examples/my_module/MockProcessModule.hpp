#pragma once

#include "nexusflow/Message.hpp"
#include <nexusflow/Module.hpp>

class MockProcessModule : public nexusflow::Module {
public:
    MockProcessModule(const std::string& name);
    ~MockProcessModule() override;

protected:
    void Process(const std::shared_ptr<nexusflow::Message>& inputMessage) override;

private:
    int m_count = 0;
};
