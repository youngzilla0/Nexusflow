#pragma once


#include <nexusflow/Message.hpp>
#include <nexusflow/Module.hpp>

class MyStreamPullerModule : public nexusflow::Module {
    

public:
    MyStreamPullerModule(const std::string& name);
    ~MyStreamPullerModule() override;

protected:
    void Process(const nexusflow::PortInputsView& inputs, nexusflow::PortOutputs& outputs) override;
};
