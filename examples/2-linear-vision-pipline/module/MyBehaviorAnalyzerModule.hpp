#pragma once

#include "nexusflow/ErrorCode.hpp"

#include <nexusflow/Message.hpp>
#include <nexusflow/Module.hpp>

class MyBehaviorAnalyzerModule : public nexusflow::Module {
    

public:
    MyBehaviorAnalyzerModule(const std::string& name);
    ~MyBehaviorAnalyzerModule() override;

    nexusflow::ErrorCode Configure(const nexusflow::Config& config) override;

    nexusflow::ErrorCode Init() override;

protected:
    nexusflow::ProcessStatus Process(nexusflow::ProcessingContext& ctx) override;

private:
    std::string m_modelPath;
};
