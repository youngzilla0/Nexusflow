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

std::unordered_map<std::shared_ptr<GraphNode>, std::vector<std::shared_ptr<GraphNode>>>
BuildIncomingMap(const std::vector<Edge>& edges) {
    std::unordered_map<std::shared_ptr<GraphNode>, std::vector<std::shared_ptr<GraphNode>>> incomingMap;
    for (const auto& edge : edges) {
        auto srcNode = edge.srcNodePtr.lock();
        auto dstNode = edge.dstNodePtr.lock();
        if (!srcNode || !dstNode) {
            continue;
        }
        incomingMap[dstNode].push_back(srcNode);
    }
    return incomingMap;
}

std::unordered_map<std::shared_ptr<GraphNode>, std::vector<std::shared_ptr<GraphNode>>>
BuildOutgoingMap(const std::vector<Edge>& edges) {
    std::unordered_map<std::shared_ptr<GraphNode>, std::vector<std::shared_ptr<GraphNode>>> outgoingMap;
    for (const auto& edge : edges) {
        auto srcNode = edge.srcNodePtr.lock();
        auto dstNode = edge.dstNodePtr.lock();
        if (!srcNode || !dstNode) {
            continue;
        }
        outgoingMap[srcNode].push_back(dstNode);
    }
    return outgoingMap;
}

std::unordered_set<std::shared_ptr<GraphNode>> CollectAncestors(
    const std::unordered_map<std::shared_ptr<GraphNode>, std::vector<std::shared_ptr<GraphNode>>>& incomingMap,
    const std::shared_ptr<GraphNode>& startNode) {
    std::unordered_set<std::shared_ptr<GraphNode>> ancestors;
    if (!startNode) {
        return ancestors;
    }

    std::queue<std::shared_ptr<GraphNode>> nodeQueue;
    nodeQueue.push(startNode);
    ancestors.insert(startNode);

    while (!nodeQueue.empty()) {
        auto node = nodeQueue.front();
        nodeQueue.pop();

        auto it = incomingMap.find(node);
        if (it == incomingMap.end()) {
            continue;
        }

        for (const auto& parent : it->second) {
            if (ancestors.insert(parent).second) {
                nodeQueue.push(parent);
            }
        }
    }

    return ancestors;
}

std::size_t ShortestDistanceToJoin(
    const std::unordered_map<std::shared_ptr<GraphNode>, std::vector<std::shared_ptr<GraphNode>>>& outgoingMap,
    const std::shared_ptr<GraphNode>& startNode,
    const std::shared_ptr<GraphNode>& joinNode) {
    if (!startNode || !joinNode) {
        return static_cast<std::size_t>(-1);
    }
    if (startNode == joinNode) {
        return 0;
    }

    std::queue<std::pair<std::shared_ptr<GraphNode>, std::size_t>> nodeQueue;
    std::unordered_set<std::shared_ptr<GraphNode>> visited;
    nodeQueue.push({startNode, 0});
    visited.insert(startNode);

    while (!nodeQueue.empty()) {
        auto entry = nodeQueue.front();
        nodeQueue.pop();
        auto node = entry.first;
        std::size_t distance = entry.second;

        auto it = outgoingMap.find(node);
        if (it == outgoingMap.end()) {
            continue;
        }

        for (const auto& nextNode : it->second) {
            if (!nextNode || !visited.insert(nextNode).second) {
                continue;
            }
            if (nextNode == joinNode) {
                return distance + 1;
            }
            nodeQueue.push({nextNode, distance + 1});
        }
    }

    return static_cast<std::size_t>(-1);
}

std::shared_ptr<GraphNode> FindLowestCommonFork(
    const std::unordered_map<std::shared_ptr<GraphNode>, std::vector<std::shared_ptr<GraphNode>>>& incomingMap,
    const std::unordered_map<std::shared_ptr<GraphNode>, std::vector<std::shared_ptr<GraphNode>>>& outgoingMap,
    const std::shared_ptr<GraphNode>& joinNode) {
    if (!joinNode) {
        return nullptr;
    }

    auto joinIt = incomingMap.find(joinNode);
    if (joinIt == incomingMap.end() || joinIt->second.size() < 2) {
        return nullptr;
    }

    std::vector<std::unordered_set<std::shared_ptr<GraphNode>>> ancestorSets;
    ancestorSets.reserve(joinIt->second.size());
    for (const auto& predecessor : joinIt->second) {
        ancestorSets.push_back(CollectAncestors(incomingMap, predecessor));
    }

    std::shared_ptr<GraphNode> bestFork;
    std::size_t bestDistance = static_cast<std::size_t>(-1);

    for (const auto& candidate : ancestorSets.front()) {
        if (!candidate || candidate == joinNode) {
            continue;
        }

        bool reachableFromAll = true;
        for (std::size_t index = 1; index < ancestorSets.size(); ++index) {
            if (ancestorSets[index].find(candidate) == ancestorSets[index].end()) {
                reachableFromAll = false;
                break;
            }
        }
        if (!reachableFromAll) {
            continue;
        }

        auto outIt = outgoingMap.find(candidate);
        if (outIt == outgoingMap.end() || outIt->second.size() < 2) {
            continue;
        }

        const auto distance = ShortestDistanceToJoin(outgoingMap, candidate, joinNode);
        if (distance == static_cast<std::size_t>(-1)) {
            continue;
        }
        if (!bestFork || distance < bestDistance) {
            bestFork = candidate;
            bestDistance = distance;
        }
    }

    return bestFork;
}

void FindAllPathsDFS(
    const std::unordered_map<std::shared_ptr<GraphNode>, std::vector<std::shared_ptr<GraphNode>>>& outgoingMap,
    const std::shared_ptr<GraphNode>& current,
    const std::shared_ptr<GraphNode>& dest,
    std::vector<std::shared_ptr<GraphNode>>& currentPath,
    std::unordered_set<std::shared_ptr<GraphNode>>& visited,
    std::vector<::GraphTopologyInfo::Path>& allPaths) {
    if (!current) {
        return;
    }

    visited.insert(current);
    currentPath.push_back(current);

    if (current == dest) {
        if (currentPath.size() >= 2) {
            allPaths.push_back(::GraphTopologyInfo::Path{currentPath});
        }
    } else {
        auto it = outgoingMap.find(current);
        if (it != outgoingMap.end()) {
            for (const auto& nextNode : it->second) {
                if (!nextNode || visited.count(nextNode) > 0) {
                    continue;
                }
                FindAllPathsDFS(outgoingMap, nextNode, dest, currentPath, visited, allPaths);
            }
        }
    }

    currentPath.pop_back();
    visited.erase(current);
}

std::vector<::GraphTopologyInfo::Path> FindAllPaths(
    const std::unordered_map<std::shared_ptr<GraphNode>, std::vector<std::shared_ptr<GraphNode>>>& outgoingMap,
    const std::shared_ptr<GraphNode>& source,
    const std::shared_ptr<GraphNode>& dest) {
    std::vector<::GraphTopologyInfo::Path> allPaths;
    std::vector<std::shared_ptr<GraphNode>> currentPath;
    std::unordered_set<std::shared_ptr<GraphNode>> visited;
    FindAllPathsDFS(outgoingMap, source, dest, currentPath, visited, allPaths);
    return allPaths;
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

::GraphTopologyInfo Graph::AnalyzeTopology() const {
    ::GraphTopologyInfo info;
    auto incomingMap = BuildIncomingMap(m_edges);
    auto outgoingMap = BuildOutgoingMap(m_edges);

    for (const auto& nodeEntry : m_nodeMap) {
        const auto& node = nodeEntry.second;
        std::size_t incomingCount = 0;
        std::size_t outgoingCount = 0;

        for (const auto& edge : m_edges) {
            auto srcNode = edge.srcNodePtr.lock();
            auto dstNode = edge.dstNodePtr.lock();
            if (srcNode == node) {
                ++outgoingCount;
            }
            if (dstNode == node) {
                ++incomingCount;
            }
        }

        if (incomingCount == 0) {
            info.sourceNodes.push_back(node);
        }
        if (outgoingCount == 0) {
            info.sinkNodes.push_back(node);
        }
        if (outgoingCount >= 2) {
            info.branchNodes.push_back(node);
        }
        if (incomingCount >= 2) {
            info.joinNodes.push_back(node);
        }

        info.nodeDegrees.push_back(::GraphTopologyInfo::NodeDegree{node, incomingCount, outgoingCount});
    }

    for (const auto& joinNode : info.joinNodes) {
        auto forkNode = FindLowestCommonFork(incomingMap, outgoingMap, joinNode);
        if (!forkNode) {
            continue;
        }

        auto paths = FindAllPaths(outgoingMap, forkNode, joinNode);
        if (paths.empty()) {
            continue;
        }

        ::GraphTopologyInfo::ForkJoinGroup group;
        group.forkNode = forkNode;
        group.joinNode = joinNode;
        group.paths = std::move(paths);
        info.forkJoinGroups.push_back(std::move(group));
    }

    return info;
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
