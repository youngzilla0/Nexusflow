#pragma once

#include "nexusflow/Config.hpp"
#include <cassert>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace nexusflow {
class Module; // Forward declaration.
}

struct Node {
    std::string name;

    Node(std::string name) : name(std::move(name)) {}
    virtual ~Node() = default;
};

struct NodeWithModuleClassName : Node {
    using Super = Node;
    std::string moduleClassName;
    nexusflow::Config config;

    NodeWithModuleClassName(std::string name, std::string moduleClassName, nexusflow::Config config)
        : Super(std::move(name)), moduleClassName(std::move(moduleClassName)), config(std::move(config)) {}
};

struct NodeWithModulePtr : Node {
    using Super = Node;

    std::shared_ptr<nexusflow::Module> modulePtr = nullptr; // The instance of the module.
    NodeWithModulePtr(std::string name, const std::shared_ptr<nexusflow::Module>& modulePtr)
        : Super(std::move(name)), modulePtr(modulePtr) {}
};

struct Edge {
    std::weak_ptr<Node> srcNodePtr, dstNodePtr;
    std::string srcPort;
    std::string dstPort;
};

// DAG based on adjacency list representation, thread unsafe.
class Graph {
public:
    // Type alias for the adjacency list.
    using AdjacencyList = std::unordered_map<std::shared_ptr<Node>, std::vector<std::shared_ptr<Node>>>;

    // Adds an edge from the source node to the destination node.
    void AddEdge(const std::shared_ptr<Node>& srcNodePtr, const std::shared_ptr<Node>& dstNodePtr,
                 std::string srcPort = "out", std::string dstPort = "in");

    // Checks if the graph has a cycle.
    bool hasCycle() const;

    // Converts the graph to a list of edges.
    std::vector<Edge> toEdgeListBFS(const std::shared_ptr<Node>& inputNodePtr = nullptr) const;

    // Finds all nodes that have multiple incoming edges (converge points / fusion candidates).
    std::vector<std::shared_ptr<Node>> FindConvergeNodes() const;

    // Checks if a given node has multiple incoming edges.
    bool IsConvergeNode(const std::shared_ptr<Node>& node) const;

public:
    // Checks if the graph is empty.
    inline bool IsEmpty() const { return m_name.empty() || m_nodeMap.empty() || m_adjList.empty(); }

    // Converts the graph to a string representation.
    std::string toString() const;

    //////////////////////////////////////////////////
    // Setters and getters for the graph's properties.
    //////////////////////////////////////////////////
    void SetName(std::string name) { m_name = std::move(name); }

    const std::string& GetName() const { return m_name; }

private:
    // Checks if the graph has a cycle and converts the graph to a list of edges using BFS.
    std::pair<bool, std::vector<Edge>> checkCycleAndConvertToEdgeList(const std::shared_ptr<Node>& inputNodePtr) const;

    // Name of the graph.
    std::string m_name;

    // Map of node names to node pointers.
    std::unordered_map<std::string, std::shared_ptr<Node>> m_nodeMap;

    // Adjacency list representing the graph.
    AdjacencyList m_adjList;
    std::vector<Edge> m_edges;
};
