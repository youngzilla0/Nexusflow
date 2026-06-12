#include "GraphUtils.hpp"
#include "base/Define.hpp"
#include "base/Graph.hpp"
#include "nexusflow/Any.hpp"
#include "nexusflow/Config.hpp"
#include "nexusflow/PipelineConfig.hpp"
#include "nexusflow/Ports.hpp"
#include "utils/logging.hpp"
#include "yaml-cpp/node/node.h"
#include "yaml-cpp/yaml.h"

#include <memory>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

namespace graphutils {

nexusflow::Any convertYamlNodeToAny(const YAML::Node& node);

namespace detail {

// 判断字符串是否是整数
inline bool isInteger(const std::string& str) {
    if (str.empty()) return false;
    size_t i = (str[0] == '-' || str[0] == '+') ? 1 : 0;
    for (; i < str.size(); ++i) {
        if (!std::isdigit(str[i])) return false;
    }
    return true;
}

// 判断字符串是否是浮点数
inline bool isFloat(const std::string& str) {
    std::istringstream iss(str);
    float f;
    char c;
    return (iss >> f) && !(iss >> c);
}

// 判断是否是 bool
inline bool isBool(const std::string& str, bool& out) {
    if (str == "true" || str == "True" || str == "yes") {
        out = true;
        return true;
    }
    if (str == "false" || str == "False" || str == "no") {
        out = false;
        return true;
    }
    return false;
}

inline nexusflow::QueueFullPolicy parseQueueFullPolicy(const std::string& value) {
    if (value == "DropTail") return nexusflow::QueueFullPolicy::DropTail;
    if (value == "DropHead") return nexusflow::QueueFullPolicy::DropHead;
    throw std::runtime_error("Unsupported QueueFullPolicy: " + value);
}

inline nexusflow::JoinKeyPolicy parseJoinKeyPolicy(const std::string& value) {
    if (value == "MessageId") return nexusflow::JoinKeyPolicy::MessageId;
    if (value == "Timestamp") return nexusflow::JoinKeyPolicy::Timestamp;
    throw std::runtime_error("Unsupported JoinKeyPolicy: " + value);
}

GraphSpec LoadGraphSpecFromYaml(const std::string& configPath) {
    YAML::Node root = YAML::LoadFile(configPath);

    const YAML::Node& graphYaml = root["graph"];
    if (!graphYaml) {
        throw std::runtime_error("YAML configuration must contain a 'graph' root node.");
    }

    GraphSpec spec;
    if (!graphYaml["name"]) {
        throw std::runtime_error("Graph configuration must have a 'name' under the 'graph' section.");
    }
    spec.graphName = graphYaml["name"].as<std::string>();

    const YAML::Node& modulesYaml = graphYaml["modules"];
    if (!modulesYaml || !modulesYaml.IsSequence()) {
        throw std::runtime_error("'modules' section is missing or not a sequence.");
    }

    for (const auto& moduleItem : modulesYaml) {
        GraphNodeSpec nodeSpec;
        nodeSpec.nodeName = moduleItem["name"].as<std::string>();
        nodeSpec.moduleClassName = moduleItem["class"].as<std::string>();

        const YAML::Node& configsNode = moduleItem["config"];
        if (configsNode && configsNode.IsMap()) {
            for (const auto& kv : configsNode) {
                std::string key = kv.first.as<std::string>();
                nodeSpec.config.Add(key, convertYamlNodeToAny(kv.second));
            }
        }

        spec.nodes.push_back(std::move(nodeSpec));
    }

    const YAML::Node& connectionsYaml = graphYaml["connections"];
    if (connectionsYaml && connectionsYaml.IsSequence()) {
        for (const auto& connectionItem : connectionsYaml) {
            GraphConnectionSpec connectionSpec;
            connectionSpec.srcModuleName = connectionItem["from"].as<std::string>();
            connectionSpec.dstModuleName = connectionItem["to"].as<std::string>();
            connectionSpec.srcPort = connectionItem["fromPort"]
                                         ? connectionItem["fromPort"].as<std::string>()
                                         : std::string(nexusflow::kDefaultOutputPort);
            connectionSpec.dstPort = connectionItem["toPort"]
                                         ? connectionItem["toPort"].as<std::string>()
                                         : std::string(nexusflow::kDefaultInputPort);
            spec.connections.push_back(std::move(connectionSpec));
        }
    }

    return spec;
}

} // namespace detail

// TODO: 待优化
nexusflow::Any convertYamlNodeToAny(const YAML::Node& node) {
    using nexusflow::Any;
    switch (node.Type()) {
        case YAML::NodeType::Null: return Any();

        case YAML::NodeType::Scalar: {
            std::string val = node.Scalar();
            bool b;
            if (detail::isBool(val, b)) return Any(b);
            if (detail::isInteger(val)) {
                try {
                    return Any(std::stoi(val));
                } catch (...) {
                }
            }
            if (detail::isFloat(val)) {
                try {
                    return Any(std::stod(val));
                } catch (...) {
                }
            }
            return Any(val); // fallback: string
        }

        case YAML::NodeType::Sequence: {
            std::vector<Any> vec;
            for (const auto& item : node) {
                vec.push_back(convertYamlNodeToAny(item));
            }
            return Any(std::move(vec));
        }

        case YAML::NodeType::Map: {
            std::map<std::string, Any> map;
            for (const auto& kv : node) {
                std::string key = kv.first.as<std::string>();
                map[key] = convertYamlNodeToAny(kv.second);
            }
            return Any(std::move(map));
        }

        case YAML::NodeType::Undefined:
        default: throw std::runtime_error("Unsupported or undefined YAML node type.");
    }
}

std::unique_ptr<Graph> CreateGraphFromSpec(const GraphSpec& spec) {
    if (spec.graphName.empty()) {
        LOG_ERROR("Graph specification must have a non-empty name.");
        return nullptr;
    }

    auto graph = std::make_unique<Graph>();
    graph->SetName(spec.graphName);

    std::unordered_map<std::string, std::shared_ptr<GraphNode>> tempNodeMap;
    tempNodeMap.reserve(spec.nodes.size());

    for (const auto& nodeSpec : spec.nodes) {
        if (nodeSpec.nodeName.empty()) {
            LOG_ERROR("Graph '{}' contains a node with an empty name.", spec.graphName);
            return nullptr;
        }

        std::shared_ptr<GraphNode> node;
        if (nodeSpec.moduleInstance) {
            node = std::make_shared<GraphModuleNode>(nodeSpec.nodeName, nodeSpec.moduleInstance);
        } else if (!nodeSpec.moduleClassName.empty()) {
            node = std::make_shared<GraphModuleNode>(nodeSpec.nodeName, nodeSpec.moduleClassName, nodeSpec.config);
        } else {
            LOG_ERROR("Graph '{}' node '{}' must provide either a module class or a module instance.", spec.graphName,
                      nodeSpec.nodeName);
            return nullptr;
        }

        auto item = tempNodeMap.emplace(nodeSpec.nodeName, node);
        if (!item.second) {
            LOG_ERROR("Duplicate module name found: {} in graph '{}'", nodeSpec.nodeName, spec.graphName);
            return nullptr;
        }
        graph->AddNode(node);
    }

    LOG_INFO("Created {} nodes for graph '{}'.", tempNodeMap.size(), graph->GetName());

    if (spec.connections.empty() && spec.nodes.size() > 1) {
        LOG_ERROR("Graph '{}' contains {} nodes but no connections.", spec.graphName, spec.nodes.size());
        return nullptr;
    }

    for (const auto& connectionSpec : spec.connections) {
        auto srcIt = tempNodeMap.find(connectionSpec.srcModuleName);
        auto dstIt = tempNodeMap.find(connectionSpec.dstModuleName);
        if (srcIt == tempNodeMap.end() || dstIt == tempNodeMap.end()) {
            LOG_ERROR("Connection '{}:{} -> {}:{}' refers to a non-existent module in graph '{}'.",
                      connectionSpec.srcModuleName, connectionSpec.srcPort, connectionSpec.dstModuleName,
                      connectionSpec.dstPort, spec.graphName);
            return nullptr;
        }

        graph->AddEdge(srcIt->second, dstIt->second, connectionSpec.srcPort, connectionSpec.dstPort);
    }

    LOG_INFO("Created {} connections for graph '{}'.", spec.connections.size(), graph->GetName());

    if (graph->HasCycle()) {
        LOG_ERROR("The constructed graph '{}' has a cycle.", graph->GetName());
        return nullptr;
    }
    if (graph->IsEmpty()) {
        LOG_ERROR("The constructed graph '{}' is empty or incomplete.", graph->GetName());
        return nullptr;
    }

    LOG_INFO("Successfully created and validated graph '{}'.", graph->GetName());
    return graph;
}

std::unique_ptr<Graph> CreateGraphFromYaml(const std::string& configPath) {
    try {
        auto spec = detail::LoadGraphSpecFromYaml(configPath);
        LOG_INFO("Start creating graph '{}' from config: {}", spec.graphName, configPath);
        return CreateGraphFromSpec(spec);
    } catch (const YAML::Exception& e) {
        LOG_ERROR("Failed to process YAML file '{}' due to a parsing error: {}", configPath, e.what());
        return nullptr;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to process YAML file '{}' due to an error: {}", configPath, e.what());
        return nullptr;
    }
}

nexusflow::PipelineConfig LoadPipelineConfigFromYaml(const std::string& configPath) {
    nexusflow::PipelineConfig config = nexusflow::PipelineConfig::Default();

    try {
        YAML::Node root = YAML::LoadFile(configPath);
        const YAML::Node& runtimeYaml = root["runtime"];
        if (!runtimeYaml || !runtimeYaml.IsMap()) {
            return config;
        }

        if (runtimeYaml["executorThreadCount"]) {
            config.executorThreadCount = runtimeYaml["executorThreadCount"].as<std::size_t>();
        }
        if (runtimeYaml["queueSize"]) {
            config.queueSize = runtimeYaml["queueSize"].as<std::size_t>();
        }
        if (runtimeYaml["idleWaitUs"]) {
            config.idleWaitUs = runtimeYaml["idleWaitUs"].as<std::size_t>();
        }
        if (runtimeYaml["fusionTimeoutMs"]) {
            config.fusionTimeoutMs = runtimeYaml["fusionTimeoutMs"].as<std::size_t>();
        }
        if (runtimeYaml["maxPendingJoinGroups"]) {
            config.maxPendingJoinGroups = runtimeYaml["maxPendingJoinGroups"].as<std::size_t>();
        }
        if (runtimeYaml["nonBlockingQueueFullPolicy"]) {
            config.nonBlockingQueueFullPolicy =
                detail::parseQueueFullPolicy(runtimeYaml["nonBlockingQueueFullPolicy"].as<std::string>());
        }
        if (runtimeYaml["joinKeyPolicy"]) {
            config.joinKeyPolicy = detail::parseJoinKeyPolicy(runtimeYaml["joinKeyPolicy"].as<std::string>());
        }

        const YAML::Node& statisticsYaml = runtimeYaml["statistics"];
        if (statisticsYaml && statisticsYaml.IsMap()) {
            if (statisticsYaml["enableStatistics"]) {
                config.statistics.enableStatistics = statisticsYaml["enableStatistics"].as<bool>();
            }
            if (statisticsYaml["enableThroughput"]) {
                config.statistics.enableThroughput = statisticsYaml["enableThroughput"].as<bool>();
            }
            if (statisticsYaml["enableLatency"]) {
                config.statistics.enableLatency = statisticsYaml["enableLatency"].as<bool>();
            }
        }
    } catch (const YAML::Exception& e) {
        LOG_ERROR("Failed to parse runtime config from '{}' due to YAML error: {}", configPath, e.what());
    }

    return config;
}

} // namespace graphutils
