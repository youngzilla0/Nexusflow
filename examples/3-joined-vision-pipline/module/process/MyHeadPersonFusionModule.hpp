#pragma once

#include "../MyMessage.hpp"
#include "nexusflow/Error.hpp"

#include <nexusflow/Message.hpp>
#include <nexusflow/Module.hpp>

namespace ns = nexusflow;

class MyHeadPersonFusionModule : public ns::Module {
    

public:
    MyHeadPersonFusionModule(const std::string& name);
    ~MyHeadPersonFusionModule() override;

    ns::Error Configure(const ns::Config& config) override;

    ns::Error Init() override;

protected:
    void Process(const ns::PortInputsView& inputs, ns::PortOutputs& outputs) override;

private:
    InferenceMessage DoFusion(const InferenceMessage& headMessage, const InferenceMessage& personMessage) const;

private:
    std::string m_modelPath;
};
