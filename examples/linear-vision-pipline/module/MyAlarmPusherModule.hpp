#pragma once

#include "nexusflow/ErrorCode.hpp"
#include <fstream>
#include <nexusflow/Define.hpp>
#include <nexusflow/Message.hpp>
#include <nexusflow/Module.hpp>

class MyAlarmPusherModule : public nexusflow::Module {
public:
    MyAlarmPusherModule(const std::string& name);
    ~MyAlarmPusherModule() override;

    void Configure(const nexusflow::ConfigMap& cfgMap) override;

    nexusflow::ErrorCode Init() override;

    nexusflow::ErrorCode DeInit() override;

protected:
    void Process(nexusflow::SharedMessage& inputMessage) override;

private:
    std::string m_savePath;
    std::ofstream m_outFile;
};
