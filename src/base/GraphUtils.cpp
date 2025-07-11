#include "GraphUtils.hpp"
#include "base/Define.hpp"
#include "base/Graph.hpp"
#include "nexusflow/Variant.hpp"
#include "utils/logging.hpp"
#include "yaml-cpp/node/node.h"
#include "yaml-cpp/yaml.h"
#include <nexusflow/Define.hpp>

#include <memory>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

namespace graphutils {

// TODO: 待优化
nexusflow::Variant convertYamlNodeToVariant(const YAML::Node& node) {
    using nexusflow::Variant;
    switch (node.Type()) {
        case YAML::NodeType::Null: return Variant();

        case YAML::NodeType::Scalar: {
            const std::string value = node.Scalar();
            const std::string& raw = value;

            // ✅ 优先检查是否是 bool（严格判断）
            std::string lower = value;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower == "true" || lower == "yes") return Variant(true);
            if (lower == "false" || lower == "no") return Variant(false);

            // ✅ 使用正则判断是否是 int / uint / float / double
            static const std::regex int_regex(R"(^-?\d+$)");
            static const std::regex uint_regex(R"(^\d+$)");
            static const std::regex float_regex(R"(^-?\d+\.\d+f$)");
            static const std::regex double_regex(R"(^-?\d+\.\d+$)");

            if (std::regex_match(raw, int_regex)) {
                try {
                    return Variant(std::stoi(raw));
                } catch (...) {
                }
            }

            if (std::regex_match(raw, uint_regex)) {
                try {
                    return Variant(static_cast<uint32_t>(std::stoul(raw)));
                } catch (...) {
                }
            }

            if (std::regex_match(raw, float_regex)) {
                try {
                    return Variant(std::stof(raw));
                } catch (...) {
                }
            }

            if (std::regex_match(raw, double_regex)) {
                try {
                    return Variant(std::stod(raw));
                } catch (...) {
                }
            }

            // ❗ fallback — 永远保底是 string
            return Variant(raw);
        }

        case YAML::NodeType::Sequence: {
            std::vector<Variant> vec;
            for (const auto& item : node) {
                vec.push_back(convertYamlNodeToVariant(item));
            }
            return Variant(std::move(vec));
        }

        case YAML::NodeType::Map: {
            std::map<std::string, Variant> map;
            for (const auto& kv : node) {
                std::string key = kv.first.as<std::string>();
                map[key] = convertYamlNodeToVariant(kv.second);
            }
            return Variant(std::move(map));
        }

        case YAML::NodeType::Undefined:
        default: throw std::runtime_error("Unsupported or undefined YAML node type.");
    }
}

std::unique_ptr<Graph> CreateGraphFromYaml(const std::string& configPath) {
    try {
        YAML::Node root = YAML::LoadFile(configPath);

        const YAML::Node& graph_yaml = root["graph"];
        if (!graph_yaml) {
            LOG_ERROR("YAML configuration must contain a 'graph' root node in '{}'", configPath);
            return nullptr;
        }

        auto graph = std::make_unique<Graph>();

        // 1. 设置图的名称
        if (!graph_yaml["name"]) {
            LOG_ERROR("Graph configuration must have a 'name' under 'graph' section in '{}'", configPath);
            return nullptr;
        }
        graph->setName(graph_yaml["name"].as<std::string>());
        LOG_INFO("Start creating graph '{}' from config: {}", graph->getName(), configPath);

        // 2. 创建所有节点
        std::unordered_map<std::string, std::shared_ptr<Node>> tempNodeMap;
        const YAML::Node& modules_yaml = graph_yaml["modules"];
        if (!modules_yaml || !modules_yaml.IsSequence()) {
            LOG_ERROR("'modules' section is missing or not a sequence in '{}'", configPath);
            return nullptr;
        }

        for (const auto& module_item : modules_yaml) {
            std::string nodeName = module_item["name"].as<std::string>();
            std::string moduleClassName = module_item["class"].as<std::string>();

            // parse custom config.
            nexusflow::ConfigMap params;
            const YAML::Node& configsNode = module_item["config"];
            if (configsNode && configsNode.IsMap()) {
                for (const auto& kv : configsNode) {
                    std::string key = kv.first.as<std::string>();
                    params[key] = convertYamlNodeToVariant(kv.second);
                }
            }

            auto node = std::make_shared<NodeWithModuleClassName>(nodeName, moduleClassName, params);

            auto item = tempNodeMap.emplace(nodeName, node);
            if (!item.second) {
                LOG_ERROR("Duplicate module name found: {} in '{}'", nodeName, configPath);
                return nullptr;
            }
        }
        LOG_INFO("Created {} nodes from 'modules' section.", tempNodeMap.size());

        // 3. 创建边，并计算度
        std::unordered_map<std::string, int> inDegree;
        std::unordered_map<std::string, int> outDegree;
        for (const auto& pair : tempNodeMap) {
            inDegree[pair.first] = 0;
            outDegree[pair.first] = 0;
        }

        const YAML::Node& connections_yaml = graph_yaml["connections"];
        if (connections_yaml && connections_yaml.IsSequence()) {
            for (const auto& connection_item : connections_yaml) {
                std::string fromName = connection_item["from"].as<std::string>();
                std::string toName = connection_item["to"].as<std::string>();

                auto srcIt = tempNodeMap.find(fromName);
                auto dstIt = tempNodeMap.find(toName);
                if (srcIt == tempNodeMap.end() || dstIt == tempNodeMap.end()) {
                    LOG_ERROR("Connection '{} -> {}' refers to a non-existent module.", fromName, toName);
                    return nullptr;
                }

                // 假设 addEdge 会将节点添加到 Graph 的内部 m_nodeMap 中
                graph->addEdge(srcIt->second, dstIt->second);
                outDegree[fromName]++;
                inDegree[toName]++;
            }
            LOG_INFO("Created {} connections.", connections_yaml.size());
        }

        // TODO:
        // // 4. 验证图的拓扑结构 (这是关键步骤！)
        // // 尽管 Graph 不再存储输入/输出节点，但我们必须验证它们是否存在且唯一
        // std::vector<std::shared_ptr<Node>> sourceNodes;
        // for (const auto& pair : tempNodeMap) {
        //     if (inDegree.at(pair.first) == 0) {
        //         sourceNodes.push_back(pair.second);
        //     }
        // }

        // if (sourceNodes.size() != 1) {
        //     LOG_ERROR("Graph must have exactly one source node (in-degree 0). Found {}.", sourceNodes.size());
        //     return nullptr;
        // }
        // LOG_INFO("Graph validation passed: Found a single source node '{}'.", sourceNodes[0]->name);

        // // 可选：同样可以验证汇节点（输出）
        // // std::vector<std::shared_ptr<Node>> sinkNodes;
        // // for (const auto& pair : tempNodeMap) {
        // //     if (outDegree.at(pair.first) == 0) {
        // //         sinkNodes.push_back(pair.second);
        // //     }
        // // }
        // // if (sinkNodes.size() != 1) {
        // //     LOG_ERROR("Graph must have exactly one sink node (out-degree 0). Found {}.", sinkNodes.size());
        // //     return nullptr;
        // // }
        // // LOG_INFO("Graph validation passed: Found a single sink node '{}'.", sinkNodes[0]->name);

        // 5. 最终校验
        if (graph->hasCycle()) {
            LOG_ERROR("The constructed graph '{}' has a cycle.", graph->getName());
            return nullptr;
        }
        if (graph->isEmpty()) {
            LOG_ERROR("The constructed graph '{}' is empty or incomplete.", graph->getName());
            return nullptr;
        }

        LOG_INFO("Successfully created and validated graph '{}'.", graph->getName());
        return graph;

    } catch (const YAML::Exception& e) {
        LOG_ERROR("Failed to process YAML file '{}' due to a parsing error: {}", configPath, e.what());
        return nullptr;
    }
}

} // namespace graphutils