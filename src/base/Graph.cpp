#include "Graph.hpp"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace {

bool EdgeMatches(const Edge& edge, const std::shared_ptr<GraphNode>& srcNodePtr, const std::shared_ptr<GraphNode>& dstNodePtr,
                 const std::string& srcPort, const std::string& dstPort) {
    return edge.srcNodePtr.lock() == srcNodePtr && edge.dstNodePtr.lock() == dstNodePtr && edge.srcPort == srcPort &&
           edge.dstPort == dstPort;
}

std::unordered_set<std::shared_ptr<GraphNode>> CollectReachableNodes(
    const Graph::AdjacencyList& adjList, const std::vector<Edge>& edges, const std::shared_ptr<GraphNode>& rootNodePtr) {
    std::unordered_set<std::shared_ptr<GraphNode>> reachableNodes;
    if (!rootNodePtr) {
        return reachableNodes;
    }

    std::queue<std::shared_ptr<GraphNode>> nodeQueue;
    nodeQueue.push(rootNodePtr);
    reachableNodes.insert(rootNodePtr);

    while (!nodeQueue.empty()) {
        auto node = nodeQueue.front();
        nodeQueue.pop();

        auto it = adjList.find(node);
        if (it == adjList.end()) {
            continue;
        }

        for (auto edgeIndex : it->second) {
            const auto& edge = edges.at(edgeIndex);
            auto dstNodePtr = edge.dstNodePtr.lock();
            if (!dstNodePtr) {
                continue;
            }
            if (reachableNodes.insert(dstNodePtr).second) {
                nodeQueue.push(dstNodePtr);
            }
        }
    }

    return reachableNodes;
}

} // namespace

void Graph::AddNode(const std::shared_ptr<GraphNode>& nodePtr) {
    if (nodePtr == nullptr) return;

    auto inserted = m_nodeMap.emplace(nodePtr->name, nodePtr);
    assert((inserted.second || inserted.first->second == nodePtr) &&
           "Graph node names must point to the same shared node instance");

    m_adjList.emplace(nodePtr, std::vector<std::size_t>{});
}

void Graph::AddEdge(const std::shared_ptr<GraphNode>& srcNodePtr, const std::shared_ptr<GraphNode>& dstNodePtr, std::string srcPort,
                    std::string dstPort) {
    if (srcNodePtr == nullptr || dstNodePtr == nullptr) return;

    AddNode(srcNodePtr);
    AddNode(dstNodePtr);

    const auto& outgoingEdges = m_adjList[srcNodePtr];
    auto duplicateIt = std::find_if(outgoingEdges.begin(), outgoingEdges.end(), [&](std::size_t edgeIndex) {
        return EdgeMatches(m_edges.at(edgeIndex), srcNodePtr, dstNodePtr, srcPort, dstPort);
    });
    if (duplicateIt != outgoingEdges.end()) {
        return;
    }

    m_edges.push_back(Edge{srcNodePtr, dstNodePtr, std::move(srcPort), std::move(dstPort)});
    m_adjList[srcNodePtr].push_back(m_edges.size() - 1);
}

bool Graph::HasCycle() const { return CheckCycleAndConvertToEdgeList(nullptr).first; }

std::vector<Edge> Graph::ToEdgeListBfs(const std::shared_ptr<GraphNode>& inputNodePtr) const {
    if (inputNodePtr == nullptr) {
        return m_edges;
    }
    return CheckCycleAndConvertToEdgeList(inputNodePtr).second;
}

std::pair<bool, std::vector<Edge>> Graph::CheckCycleAndConvertToEdgeList(const std::shared_ptr<GraphNode>& inputNodePtr) const {
    std::vector<Edge> edgeList;
    std::unordered_set<std::shared_ptr<GraphNode>> traversalNodes;

    if (inputNodePtr != nullptr) {
        traversalNodes = CollectReachableNodes(m_adjList, m_edges, inputNodePtr);
    } else {
        for (const auto& nodeEntry : m_nodeMap) {
            traversalNodes.insert(nodeEntry.second);
        }
    }

    std::unordered_map<std::shared_ptr<GraphNode>, int> inDegree;
    for (const auto& node : traversalNodes) {
        inDegree[node] = 0;
    }

    for (const auto& node : traversalNodes) {
        auto adjIt = m_adjList.find(node);
        if (adjIt == m_adjList.end()) {
            continue;
        }
        for (auto edgeIndex : adjIt->second) {
            const auto& edge = m_edges.at(edgeIndex);
            auto dstNodePtr = edge.dstNodePtr.lock();
            if (!dstNodePtr || traversalNodes.find(dstNodePtr) == traversalNodes.end()) {
                continue;
            }
            ++inDegree[dstNodePtr];
        }
    }

    std::queue<std::shared_ptr<GraphNode>> nodeQueue;
    if (inputNodePtr != nullptr) {
        nodeQueue.push(inputNodePtr);
        inDegree[inputNodePtr] = 0;
    } else {
        for (const auto& node : traversalNodes) {
            if (inDegree[node] == 0) {
                nodeQueue.push(node);
            }
        }
    }

    int visitedCount = 0;
    while (!nodeQueue.empty()) {
        auto node = nodeQueue.front();
        nodeQueue.pop();
        visitedCount++;

        auto iter = m_adjList.find(node);
        if (iter != m_adjList.end()) {
            for (auto edgeIndex : iter->second) {
                const auto& edge = m_edges.at(edgeIndex);
                auto neighbor = edge.dstNodePtr.lock();
                if (!neighbor || traversalNodes.find(neighbor) == traversalNodes.end()) {
                    continue;
                }

                edgeList.push_back(edge);
                if (--inDegree[neighbor] == 0) {
                    nodeQueue.push(neighbor);
                }
            }
        }
    }

    return {visitedCount != static_cast<int>(traversalNodes.size()), std::move(edgeList)};
}

std::vector<std::shared_ptr<GraphNode>> Graph::GetConvergeNodes() const {
    std::vector<std::shared_ptr<GraphNode>> convergeNodes;

    for (const auto& nodeEntry : m_nodeMap) {
        const auto& node = nodeEntry.second;
        int incomingCount = 0;

        for (const auto& edge : m_edges) {
            auto dstNode = edge.dstNodePtr.lock();
            if (dstNode == node) {
                incomingCount++;
            }
        }

        if (incomingCount >= 2) {
            convergeNodes.push_back(node);
        }
    }

    return convergeNodes;
}

bool Graph::IsConvergeNode(const std::shared_ptr<GraphNode>& node) const {
    if (!node) return false;

    int incomingCount = 0;
    for (const auto& edge : m_edges) {
        auto dstNode = edge.dstNodePtr.lock();
        if (dstNode == node) {
            incomingCount++;
        }
    }
    return incomingCount >= 2;
}

std::string Graph::ToString() const {
    auto edgeList = ToEdgeListBfs();

    std::ostringstream oss;
    oss << "[" + m_name + "]: name=" + m_name + ", graph: \n";

    for (auto&& edge : edgeList) {
        auto srcNodePtr = edge.srcNodePtr.lock();
        auto dstNodePtr = edge.dstNodePtr.lock();
        oss << "  " << srcNodePtr->name << ":" << edge.srcPort << " -> " << dstNodePtr->name << ":" << edge.dstPort
            << "\n";
    }
    return oss.str();
}
