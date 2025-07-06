
#pragma once

#include "base/Graph.hpp"

namespace graphutils {

std::unique_ptr<Graph> loadGrapFromYaml(const std::string& filepath);

}
