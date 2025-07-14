#pragma once

#include "../MyMessage.hpp"
#include "nexusflow/ErrorCode.hpp"
#include <nexusflow/Define.hpp>
#include <nexusflow/Message.hpp>
#include <nexusflow/Module.hpp>

class MyHeadPersonFusionModule : public nexusflow::Module {
public:
    MyHeadPersonFusionModule(const std::string& name);
    ~MyHeadPersonFusionModule() override;

    void Configure(const nexusflow::ConfigMap& cfgMap) override;

    nexusflow::ErrorCode Init() override;

protected:
    void Process(nexusflow::Message& inputMessage) override;

private:
    InferenceMessage DoFusion(const InferenceMessage& headMessage, const InferenceMessage& personMessage) const;

private:
    std::string m_modelPath;
};
