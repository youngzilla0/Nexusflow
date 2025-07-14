#pragma once

#include "nexusflow/ErrorCode.hpp"
#include <nexusflow/Define.hpp>
#include <nexusflow/Message.hpp>
#include <nexusflow/Module.hpp>

class MyHeadDetectorModule : public nexusflow::Module {
public:
    MyHeadDetectorModule(const std::string& name);
    ~MyHeadDetectorModule() override;

    void Configure(const nexusflow::ConfigMap& cfgMap) override;

    nexusflow::ErrorCode Init() override;

protected:
    void Process(nexusflow::Message& inputMessage) override;

private:
    std::string m_modelPath;
};
