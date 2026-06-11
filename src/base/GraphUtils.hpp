
#pragma once

#include "base/Graph.hpp"
#include <nexusflow/PipelineConfig.hpp>

namespace graphutils {

std::unique_ptr<Graph> CreateGraphFromYaml(const std::string& configPath);
nexusflow::PipelineConfig LoadPipelineConfigFromYaml(const std::string& configPath);

}
