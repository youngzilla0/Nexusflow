#include "Graph.hpp"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

void Graph::AddEdge(const std::shared_ptr<Node>& srcNodePtr, const std::shared_ptr<Node>& dstNodePtr, std::string srcPort,
                    std::string dstPort) {
    if (srcNodePtr == nullptr || dstNodePtr == nullptr) return;

    m_nodeMap[srcNodePtr->name] = srcNodePtr;
    m_nodeMap[dstNodePtr->name] = dstNodePtr;

    m_adjList[srcNodePtr].emplace_back(dstNodePtr);
    m_edges.push_back(Edge{srcNodePtr, dstNodePtr, std::move(srcPort), std::move(dstPort)});
}

bool Graph::hasCycle() const { return checkCycleAndConvertToEdgeList(nullptr).first; }

std::vector<Edge> Graph::toEdgeListBFS(const std::shared_ptr<Node>& inputNodePtr) const {
    if (inputNodePtr == nullptr) {
        return m_edges;
    }
    return checkCycleAndConvertToEdgeList(inputNodePtr).second;
}

std::pair<bool, std::vector<Edge>> Graph::checkCycleAndConvertToEdgeList(const std::shared_ptr<Node>& inputNodePtr) const {
    std::vector<Edge> edgeList;
    std::unordered_map<std::shared_ptr<Node>, int> inDegree;
    std::unordered_set<std::shared_ptr<Node>> allNodes;

    for (const auto& entry : m_adjList) {
        allNodes.insert(entry.first);
        if (inDegree.find(entry.first) == inDegree.end()) {
            inDegree[entry.first] = 0;
        }
        for (const auto& neighbor : entry.second) {
            allNodes.insert(neighbor);
            inDegree[neighbor]++;
        }
    }

    // 如果指定inputNodePtr, 则根从这个节点为根节点开始搜索.
    std::queue<std::shared_ptr<Node>> nodeQueue;
    if (inputNodePtr != nullptr) {
        nodeQueue.push(inputNodePtr);
        inDegree[inputNodePtr] = 0;
    } else {
        for (const auto& node : allNodes) {
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
            std::unordered_set<std::shared_ptr<Node>> processedNeighbors;

            for (const auto& neighbor : iter->second) {
                if (--inDegree[neighbor] == 0) {
                    nodeQueue.push(neighbor);
                }

                if (processedNeighbors.find(neighbor) != processedNeighbors.end()) {
                    continue;
                }
                processedNeighbors.insert(neighbor);

                auto edgeIt = std::find_if(m_edges.begin(), m_edges.end(), [&](const Edge& edge) {
                    return edge.srcNodePtr.lock() == node && edge.dstNodePtr.lock() == neighbor;
                });
                if (edgeIt != m_edges.end()) {
                    edgeList.push_back(*edgeIt);
                } else {
                    edgeList.push_back(Edge{node, neighbor, "", ""});
                }
            }
        }
    }

    return {visitedCount != allNodes.size(), std::move(edgeList)};
}

std::vector<std::shared_ptr<Node>> Graph::FindConvergeNodes() const {
    std::vector<std::shared_ptr<Node>> convergeNodes;

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

bool Graph::IsConvergeNode(const std::shared_ptr<Node>& node) const {
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

std::string Graph::toString() const {
    auto edgeList = toEdgeListBFS();

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
