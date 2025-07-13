#pragma once

#include "nexusflow/ErrorCode.hpp"
#include <nexusflow/Define.hpp>
#include <nexusflow/Message.hpp>
#include <nexusflow/Module.hpp>

class MyPersonDetectorModule : public nexusflow::Module {
public:
    MyPersonDetectorModule(const std::string& name);
    ~MyPersonDetectorModule() override;

    void Configure(const nexusflow::ConfigMap& cfgMap) override;

    nexusflow::ErrorCode Init() override;

protected:
    void Process(nexusflow::SharedMessage& inputMessage) override;

private:
    std::string m_modelPath;
};
