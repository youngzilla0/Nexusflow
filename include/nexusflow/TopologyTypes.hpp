#ifndef NEXUSFLOW_TOPOLOGY_TYPES_HPP
#define NEXUSFLOW_TOPOLOGY_TYPES_HPP

#include <cstddef>
#include <memory>
#include <vector>

class GraphNode;

struct GraphTopologyInfo {
    struct NodeDegree {
        std::shared_ptr<GraphNode> node;
        std::size_t incomingCount = 0;
        std::size_t outgoingCount = 0;
    };

    struct Path {
        std::vector<std::shared_ptr<GraphNode>> nodes;
    };

    struct ForkJoinGroup {
        std::shared_ptr<GraphNode> forkNode;
        std::shared_ptr<GraphNode> joinNode;
        std::vector<Path> paths;
    };

    std::vector<std::shared_ptr<GraphNode>> sourceNodes;
    std::vector<std::shared_ptr<GraphNode>> sinkNodes;
    std::vector<std::shared_ptr<GraphNode>> branchNodes;
    std::vector<std::shared_ptr<GraphNode>> joinNodes;
    std::vector<ForkJoinGroup> forkJoinGroups;
    std::vector<NodeDegree> nodeDegrees;
};

#endif // NEXUSFLOW_TOPOLOGY_TYPES_HPP
