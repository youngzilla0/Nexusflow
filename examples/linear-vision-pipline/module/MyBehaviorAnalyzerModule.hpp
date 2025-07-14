#pragma once

#include "nexusflow/ErrorCode.hpp"
#include <nexusflow/Define.hpp>
#include <nexusflow/Message.hpp>
#include <nexusflow/Module.hpp>

class MyBehaviorAnalyzerModule : public nexusflow::Module {
public:
    MyBehaviorAnalyzerModule(const std::string& name);
    ~MyBehaviorAnalyzerModule() override;

    void Configure(const nexusflow::ConfigMap& cfgMap) override;

    nexusflow::ErrorCode Init() override;

protected:
    void Process(nexusflow::Message& inputMessage) override;

private:
    std::string m_modelPath;
};
