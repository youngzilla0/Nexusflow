
#include "GraphUtils.hpp"
#include "helper/logging.hpp"
#include "yaml-cpp/yaml.h"

namespace graphutils {

std::unique_ptr<Graph> loadGrapFromYaml(const std::string& filepath) {
    try {
        YAML::Node root = YAML::LoadFile(filepath);

        auto graph = std::make_unique<Graph>();

        std::unordered_map<std::string, std::shared_ptr<Node>> tempNodeMap;

        const YAML::Node& nodes_yaml = root["node"];
        if (!nodes_yaml || !nodes_yaml.IsSequence()) {
            LOG_ERROR("'node' section is missing or not a sequence in '{}'", filepath);
            return nullptr;
        }

        for (const auto& node_item : nodes_yaml) {
            std::string nodeName = node_item["name"].as<std::string>();
            std::string moduleType = node_item["type"].as<std::string>();

            if (tempNodeMap.count(nodeName)) {
                LOG_ERROR("Duplicate node name found: {}", nodeName);
                return nullptr;
            }
            tempNodeMap[nodeName] = std::make_shared<Node>(nodeName, moduleType);
        }

        const YAML::Node& edges_yaml = root["edge"];
        if (edges_yaml && edges_yaml.IsSequence()) {
            for (const auto& edge_item : edges_yaml) {
                std::string srcName = edge_item["src"].as<std::string>();
                std::string dstName = edge_item["dst"].as<std::string>();

                auto srcIt = tempNodeMap.find(srcName);
                if (srcIt == tempNodeMap.end()) {
                    LOG_ERROR("Source node '{}' not found for an edge.", srcName);
                    return nullptr;
                }

                auto dstIt = tempNodeMap.find(dstName);
                if (dstIt == tempNodeMap.end()) {
                    LOG_ERROR("Destination node '{}' not found for an edge.", dstName);
                    return nullptr;
                }

                // Add edge to graph
                graph->addEdge(srcIt->second, dstIt->second);
            }
        }

        const YAML::Node& graph_info = root["graph"];
        if (!graph_info) {
            LOG_ERROR("'graph' section not found in YAML file.");
            return nullptr;
        }

        graph->setName(graph_info["name"].as<std::string>());

        std::string inputNodeName = graph_info["input"].as<std::string>();
        auto inputIt = tempNodeMap.find(inputNodeName);
        if (inputIt == tempNodeMap.end()) {
            LOG_ERROR("Specified input node '{}' does not exist.", inputNodeName);
            return nullptr;
        }
        graph->setInputNodePtr(inputIt->second);

        std::string outputNodeName = graph_info["output"].as<std::string>();
        auto outputIt = tempNodeMap.find(outputNodeName);
        if (outputIt == tempNodeMap.end()) {
            LOG_ERROR("Specified output node '{}' does not exist.", outputNodeName);
            return nullptr;
        }
        graph->setOutputNodePtr(outputIt->second);

        return graph;

    } catch (const YAML::Exception& e) {
        LOG_ERROR("Failed to parse YAML file '{}', reason: {}", filepath, e.what());
        return nullptr;
    }
}
} // namespace graphutils
