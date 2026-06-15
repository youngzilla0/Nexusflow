#ifndef NEXUSFLOW_TOPOLOGY_TYPES_HPP
#define NEXUSFLOW_TOPOLOGY_TYPES_HPP

#include <cstddef>
#include <memory>
#include <vector>

class GraphNode;

struct GraphTopologyInfo {
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
};

#endif // NEXUSFLOW_TOPOLOGY_TYPES_HPP
