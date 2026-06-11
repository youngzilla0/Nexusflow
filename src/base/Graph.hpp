#pragma once

#include "nexusflow/Config.hpp"
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

enum class NodeKind {
    Generic,
    ModuleClass,
    ModuleInstance,
};

// Graph Node is a topology vertex. The base type only carries the node name and
// is sufficient for graph algorithms and unit tests.
struct Node {
    std::string name;

    explicit Node(std::string name) : name(std::move(name)) {}
    virtual ~Node() = default;

    virtual NodeKind GetKind() const { return NodeKind::Generic; }
};

// ModuleClassNode represents a declarative node created from config/YAML. The
// concrete Module instance is materialized later through ModuleFactory.
struct ModuleClassNode : Node {
    using Super = Node;
    std::string moduleClassName;
    nexusflow::Config config;

    ModuleClassNode(std::string name, std::string moduleClassName, nexusflow::Config config)
        : Super(std::move(name)), moduleClassName(std::move(moduleClassName)), config(std::move(config)) {}

    NodeKind GetKind() const override { return NodeKind::ModuleClass; }
};

// ModuleInstanceNode represents a programmatically provided Module instance. It
// is mainly used by builder/tests where the caller already owns the module.
struct ModuleInstanceNode : Node {
    using Super = Node;

    std::shared_ptr<nexusflow::Module> modulePtr = nullptr; // The instance of the module.
    ModuleInstanceNode(std::string name, const std::shared_ptr<nexusflow::Module>& modulePtr)
        : Super(std::move(name)), modulePtr(modulePtr) {}

    NodeKind GetKind() const override { return NodeKind::ModuleInstance; }
};

using NodeWithModuleClassName = ModuleClassNode;
using NodeWithModulePtr = ModuleInstanceNode;

struct Edge {
    std::weak_ptr<Node> srcNodePtr, dstNodePtr;
    std::string srcPort;
    std::string dstPort;
};

// DAG based on adjacency list representation, thread unsafe.
class Graph {
public:
    // Type alias for the adjacency list.
    using AdjacencyList = std::unordered_map<std::shared_ptr<Node>, std::vector<std::size_t>>;

    void AddNode(const std::shared_ptr<Node>& nodePtr);

    // Adds an edge from the source node to the destination node.
    void AddEdge(const std::shared_ptr<Node>& srcNodePtr, const std::shared_ptr<Node>& dstNodePtr,
                 std::string srcPort = "out", std::string dstPort = "in");

    // Checks if the graph has a cycle.
    bool HasCycle() const;
    bool hasCycle() const { return HasCycle(); }

    // Converts the graph to a list of edges.
    std::vector<Edge> ToEdgeListBfs(const std::shared_ptr<Node>& inputNodePtr = nullptr) const;
    std::vector<Edge> toEdgeListBFS(const std::shared_ptr<Node>& inputNodePtr = nullptr) const {
        return ToEdgeListBfs(inputNodePtr);
    }

    // Finds all nodes that have multiple incoming edges (converge points / fusion candidates).
    std::vector<std::shared_ptr<Node>> GetConvergeNodes() const;
    std::vector<std::shared_ptr<Node>> FindConvergeNodes() const { return GetConvergeNodes(); }

    // Checks if a given node has multiple incoming edges.
    bool IsConvergeNode(const std::shared_ptr<Node>& node) const;

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
    std::pair<bool, std::vector<Edge>> CheckCycleAndConvertToEdgeList(const std::shared_ptr<Node>& inputNodePtr) const;

    // Name of the graph.
    std::string m_name;

    // Map of node names to node pointers.
    std::unordered_map<std::string, std::shared_ptr<Node>> m_nodeMap;

    // Adjacency list representing the graph.
    AdjacencyList m_adjList;
    std::vector<Edge> m_edges;
};
