#pragma once

#include "nexusflow/Config.hpp"
#include <nexusflow/TopologyTypes.hpp>
#include <cassert>
#include <cstddef>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace nexusflow {
class Module; // Forward declaration.
}

enum class ModuleSource {
    Type,     // 模块在物化阶段通过 ModuleFactory 按类型名创建。
    Instance, // 模块由 builder/tests 直接提供现成实例。
};

enum class GraphNodeKind {
    Generic,
    Module, // 表示该图节点最终会物化为一个 Module。
};

// Graph Node is a topology vertex. The base type only carries the node name and
// is sufficient for graph algorithms and unit tests.
struct GraphNode {
    std::string name;

    explicit GraphNode(std::string name) : name(std::move(name)) {}
    virtual ~GraphNode() = default;

    virtual GraphNodeKind GetKind() const { return GraphNodeKind::Generic; }
};

// GraphModuleNode 表示“图层里的模块节点”。
// 它统一承载两种来源：
// - Type: 来自 YAML/注册表，运行时再按类型名创建 Module
// - Instance: 来自 builder/tests，运行时直接复用现成 Module
struct GraphModuleNode : GraphNode {
    using Super = GraphNode;
    ModuleSource source = ModuleSource::Type;
    std::string moduleClassName;
    std::shared_ptr<nexusflow::Module> modulePtr = nullptr;
    nexusflow::Config config;

    GraphModuleNode(std::string name, std::string moduleClassName, nexusflow::Config config)
        : Super(std::move(name)), source(ModuleSource::Type), moduleClassName(std::move(moduleClassName)),
          config(std::move(config)) {}

    GraphModuleNode(std::string name, const std::shared_ptr<nexusflow::Module>& modulePtr)
        : Super(std::move(name)), source(ModuleSource::Instance), modulePtr(modulePtr) {}

    GraphNodeKind GetKind() const override { return GraphNodeKind::Module; }
};

struct Edge {
    std::weak_ptr<GraphNode> srcNodePtr, dstNodePtr;
    std::string srcPort;
    std::string dstPort;
};

// DAG based on adjacency list representation, thread unsafe.
class Graph {
public:
    // Type alias for the adjacency list.
    using AdjacencyList = std::unordered_map<std::shared_ptr<GraphNode>, std::vector<std::size_t>>;

    void AddNode(const std::shared_ptr<GraphNode>& nodePtr);

    // Adds an edge from the source node to the destination node.
    void AddEdge(const std::shared_ptr<GraphNode>& srcNodePtr, const std::shared_ptr<GraphNode>& dstNodePtr,
                 std::string srcPort = "out", std::string dstPort = "in");

    // Checks if the graph has a cycle.
    bool HasCycle() const;
    bool hasCycle() const { return HasCycle(); }

    // Converts the graph to a list of edges.
    std::vector<Edge> ToEdgeListBfs(const std::shared_ptr<GraphNode>& inputNodePtr = nullptr) const;
    std::vector<Edge> toEdgeListBFS(const std::shared_ptr<GraphNode>& inputNodePtr = nullptr) const {
        return ToEdgeListBfs(inputNodePtr);
    }

    // Finds all nodes that have multiple incoming edges (converge points / fusion candidates).
    std::vector<std::shared_ptr<GraphNode>> GetConvergeNodes() const;
    std::vector<std::shared_ptr<GraphNode>> FindConvergeNodes() const { return GetConvergeNodes(); }

    // Checks if a given node has multiple incoming edges.
    bool IsConvergeNode(const std::shared_ptr<GraphNode>& node) const;

    // Summarizes the role of each node in the topology.
    GraphTopologyInfo AnalyzeTopology() const;

public:
    // Checks if the graph is empty.
    inline bool IsEmpty() const { return m_name.empty() || m_nodeMap.empty() || m_edges.empty(); }

    // Converts the graph to a string representation.
    std::string ToString() const;
    std::string toString() const { return ToString(); }

    //////////////////////////////////////////////////
    // Setters and getters for the graph's properties.
    //////////////////////////////////////////////////
    void SetName(std::string name) { m_name = std::move(name); }

    const std::string& GetName() const { return m_name; }

private:
    // Checks if the graph has a cycle and converts the graph to a list of edges using BFS.
    std::pair<bool, std::vector<Edge>> CheckCycleAndConvertToEdgeList(const std::shared_ptr<GraphNode>& inputNodePtr) const;

    // Name of the graph.
    std::string m_name;

    // Map of node names to node pointers.
    std::unordered_map<std::string, std::shared_ptr<GraphNode>> m_nodeMap;

    // Adjacency list representing the graph.
    AdjacencyList m_adjList;
    std::vector<Edge> m_edges;
};
