#pragma once


#include <nexusflow/Message.hpp>
#include <nexusflow/Module.hpp>

namespace ns = nexusflow;

class MyDecoderModule : public ns::Module {
    

public:
    MyDecoderModule(const std::string& name);
    ~MyDecoderModule() override;

    ns::Error Configure(const ns::Config& config) override;

protected:
    void Process(const ns::PortInputsView& inputs, ns::PortOutputs& outputs) override;

private:
    uint32_t m_skipInterval = 1; // skip every n-th message
    uint32_t m_frameIdx = 0; // frame index
};
