#pragma once

#include <nexusflow/Define.hpp>
#include <nexusflow/Message.hpp>
#include <nexusflow/Module.hpp>

class MyStreamPullerModule : public nexusflow::Module {
public:
    MyStreamPullerModule(const std::string& name);
    ~MyStreamPullerModule() override;

    void Configure(const nexusflow::ConfigMap& cfgMap) override;

protected:
    void Process(nexusflow::Message& inputMessage) override;
};
