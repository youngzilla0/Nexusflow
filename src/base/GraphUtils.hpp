
#pragma once

#include "base/Graph.hpp"
#include <nexusflow/Config.hpp>
#include <nexusflow/PipelineConfig.hpp>
#include <memory>
#include <string>
#include <vector>

namespace graphutils {

struct GraphNodeSpec {
    std::string nodeName;
    std::string moduleClassName;
    nexusflow::Config config;
    std::shared_ptr<nexusflow::Module> moduleInstance;
};

struct GraphConnectionSpec {
    std::string srcModuleName;
    std::string srcPort;
    std::string dstModuleName;
    std::string dstPort;
};

struct GraphSpec {
    std::string graphName;
    std::vector<GraphNodeSpec> nodes;
    std::vector<GraphConnectionSpec> connections;
};

std::unique_ptr<Graph> CreateGraphFromSpec(const GraphSpec& spec);
std::unique_ptr<Graph> CreateGraphFromYaml(const std::string& configPath);
nexusflow::PipelineConfig LoadPipelineConfigFromYaml(const std::string& configPath);

}
