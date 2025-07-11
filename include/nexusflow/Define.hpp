#ifndef NEXUSFLOW_DEFINE_HPP
#define NEXUSFLOW_DEFINE_HPP

#include "nexusflow/Variant.hpp"
#include <string>
#include <unordered_map>

namespace nexusflow {
using ConfigMap = std::unordered_map<std::string, Variant>;
}

#endif
