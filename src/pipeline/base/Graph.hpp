#pragma once

#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

struct Node {
    std::string name;

    // Define your own properties here.
    std::string moduleName;

    Node(std::string name, std::string moduleName) : name(std::move(name)), moduleName(std::move(moduleName)) {}
};

struct Edge {
    std::weak_ptr<Node> srcNodePtr, dstNodePtr;
};

// DAG based on adjacency list representation, thread unsafe.
class Graph {
public:
    // Type alias for the adjacency list.
    using AdjacencyList = std::unordered_map<std::shared_ptr<Node>, std::vector<std::shared_ptr<Node>>>;

    // Adds an edge from the source node to the destination node.
    void addEdge(const std::shared_ptr<Node>& srcNodePtr, const std::shared_ptr<Node>& dstNodePtr);

    // Checks if the graph has a cycle.
    bool hasCycle() const;

    // Converts the graph to a list of edges.
    std::vector<Edge> toEdgeListBFS() const;

public:
    // Checks if the graph is empty.
    inline bool isEmpty() const {
        return m_name.empty() || m_nodeMap.empty() || m_adjList.empty() || m_inputNodePtr == nullptr || m_outputNodePtr == nullptr;
    }

    // Converts the graph to a string representation.
    std::string toString() const;

    //////////////////////////////////////////////////
    // Setters and getters for the graph's properties.
    //////////////////////////////////////////////////
    void setName(std::string name) { m_name = std::move(name); }

    const std::string& getName() const { return m_name; }

    void setInputNodePtr(const std::shared_ptr<Node>& nodePtr) { m_inputNodePtr = nodePtr; }

    void setOutputNodePtr(const std::shared_ptr<Node>& nodePtr) { m_outputNodePtr = nodePtr; }

    const std::shared_ptr<Node>& getInputNodePtr() const { return m_inputNodePtr; }

    const std::shared_ptr<Node>& getOutputNodePtr() const { return m_outputNodePtr; }

private:
    // Checks if the graph has a cycle and converts the graph to a list of edges using BFS.
    std::pair<bool, std::vector<Edge>> checkCycleAndConvertToEdgeList(const std::shared_ptr<Node>& inputNodePtr) const;

    // Name of the graph.
    std::string m_name;

    // Pointer to the input node.
    std::shared_ptr<Node> m_inputNodePtr = nullptr;

    // Pointer to the output node.
    std::shared_ptr<Node> m_outputNodePtr = nullptr;

    // Map of node names to node pointers.
    std::unordered_map<std::string, std::shared_ptr<Node>> m_nodeMap;

    // Adjacency list representing the graph.
    AdjacencyList m_adjList;
};
