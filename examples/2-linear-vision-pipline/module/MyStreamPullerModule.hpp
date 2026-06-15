#pragma once


#include <nexusflow/Message.hpp>
#include <nexusflow/Module.hpp>

namespace ns = nexusflow;

class MyStreamPullerModule : public ns::Module {
    

public:
    MyStreamPullerModule(const std::string& name);
    ~MyStreamPullerModule() override;

protected:
    void Process(const ns::PortInputsView& inputs, ns::PortOutputs& outputs) override;
};
