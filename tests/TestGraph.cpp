#include "base/Graph.hpp"
#include <cstdlib>
#include <gtest/gtest.h>

auto a = std::make_shared<GraphNode>("a");
auto b = std::make_shared<GraphNode>("b");
auto c = std::make_shared<GraphNode>("c");
auto d = std::make_shared<GraphNode>("d");
auto e = std::make_shared<GraphNode>("e");
auto f = std::make_shared<GraphNode>("f");
static constexpr size_t kNumNode = 6;

TEST(GraphTest, HasCycle_LinearGraphVariants) {
    // a -> b -> c -> d
    {
        Graph graph;

        graph.AddEdge(a, b);
        graph.AddEdge(b, c);
        graph.AddEdge(c, d);

        ASSERT_FALSE(graph.HasCycle());
    }

    // a -> b -> c -> b -> d, 有环.
    {
        Graph graph;

        graph.AddEdge(a, b);
        graph.AddEdge(b, c);
        graph.AddEdge(c, b);
        graph.AddEdge(b, d);

        ASSERT_TRUE(graph.HasCycle());
    }

    // a -> b -> c -> a -> d, 有环.
    {
        Graph graph;

        graph.AddEdge(a, b);
        graph.AddEdge(b, c);
        graph.AddEdge(c, a);
        graph.AddEdge(a, d);

        ASSERT_TRUE(graph.HasCycle());
    }
}

TEST(GraphTest, HasCycle_DagAndCyclicVariants) {
    /**
     *           a
     *          / \
     *         /   \
     *        b     c
     *       / \   /
     *      /   \ /
     *     d     e
     *      \   /
     *       \ /
     *        f
     */
    {
        Graph graph;

        graph.AddEdge(a, b);
        graph.AddEdge(a, c);
        graph.AddEdge(b, d);
        graph.AddEdge(b, e);
        graph.AddEdge(c, e);
        graph.AddEdge(d, f);
        graph.AddEdge(e, f);

        ASSERT_TRUE(!graph.HasCycle());
    }

    /**
     *           a
     *          / \
     *         /   \
     *        b     c
     *       / \   /
     *      /   \ /
     *     d     a
     *      \   /
     *       \ /
     *        f
     */
    {
        Graph graph;

        graph.AddEdge(a, b);
        graph.AddEdge(a, c);
        graph.AddEdge(b, d);
        graph.AddEdge(b, a);
        graph.AddEdge(c, a);
        graph.AddEdge(d, f);
        graph.AddEdge(a, f);

        ASSERT_TRUE(graph.HasCycle());
    }
}

TEST(GraphTest, ToEdgeListBfs_DisconnectedGraphFromDifferentRoots) {
    // Graph: a -> b   c -> d (两个不连通的组件)
    Graph graph;
    graph.AddEdge(a, b);
    graph.AddEdge(c, d);

    // 从 'a' 开始BFS，应该只能找到 'a -> b'
    auto edgeListFromA = graph.ToEdgeListBfs(a); // 假设 toEdgeListBFS 接受起点
    ASSERT_EQ(edgeListFromA.size(), 1);
    ASSERT_EQ(edgeListFromA[0].srcNodePtr.lock(), a);
    ASSERT_EQ(edgeListFromA[0].dstNodePtr.lock(), b);

    // 从 'c' 开始BFS，应该只能找到 'c -> d'
    auto edgeListFromC = graph.ToEdgeListBfs(c); // 假设 toEdgeListBFS 接受起点
    ASSERT_EQ(edgeListFromC.size(), 1);
    ASSERT_EQ(edgeListFromC[0].srcNodePtr.lock(), c);
    ASSERT_EQ(edgeListFromC[0].dstNodePtr.lock(), d);
}

TEST(GraphTest, AddEdge_EdgeCases) {
    // Case 1: 添加自环，应该被检测为有环
    {
        Graph graph;
        graph.AddEdge(a, a);
        ASSERT_TRUE(graph.HasCycle());
    }

    // Case 2: 重复添加同一条边
    {
        Graph graph;
        graph.AddEdge(a, b);
        graph.AddEdge(a, b); // 添加第二次

        // 验证 hasCycle 不受影响
        ASSERT_FALSE(graph.HasCycle());

        // 验证 toEdgeListBFS 的结果。取决于实现，可能会有一条或两条边。
        // 一个好的实现应该只包含一条边。
        auto edgeList = graph.ToEdgeListBfs(a); // 假设 toEdgeListBFS 接受起点
        ASSERT_EQ(edgeList.size(), 1);
        ASSERT_EQ(edgeList[0].srcNodePtr.lock(), a);
        ASSERT_EQ(edgeList[0].dstNodePtr.lock(), b);
    }
}

TEST(GraphTest, ToEdgeListBfs_PreservesParallelEdgesWithDifferentPorts) {
    Graph graph;

    graph.AddEdge(a, b, "out_main", "in_primary");
    graph.AddEdge(a, b, "out_aux", "in_secondary");

    auto edgeList = graph.ToEdgeListBfs(a);
    ASSERT_EQ(edgeList.size(), 2);
    ASSERT_EQ(edgeList[0].srcPort, "out_main");
    ASSERT_EQ(edgeList[0].dstPort, "in_primary");
    ASSERT_EQ(edgeList[1].srcPort, "out_aux");
    ASSERT_EQ(edgeList[1].dstPort, "in_secondary");
}

TEST(GraphTest, IsEmpty_DependsOnGraphNameAndEdges) {
    Graph graph;
    ASSERT_TRUE(graph.IsEmpty());

    graph.AddEdge(a, b);
    ASSERT_TRUE(graph.IsEmpty());

    graph.SetName("xxx");
    ASSERT_FALSE(graph.IsEmpty());
}

TEST(GraphTest, AnalyzeTopology_ReportsSourcesBranchesJoinsAndSinks) {
    Graph graph;
    graph.AddEdge(a, b);
    graph.AddEdge(a, c);
    graph.AddEdge(b, d);
    graph.AddEdge(c, d);

    auto topology = graph.AnalyzeTopology();

    ASSERT_EQ(topology.sourceNodes.size(), 1u);
    ASSERT_EQ(topology.sinkNodes.size(), 1u);
    ASSERT_EQ(topology.branchNodes.size(), 1u);
    ASSERT_EQ(topology.joinNodes.size(), 1u);
    ASSERT_EQ(topology.nodeDegrees.size(), 4u);

    EXPECT_EQ(topology.sourceNodes[0], a);
    EXPECT_EQ(topology.sinkNodes[0], d);
    EXPECT_EQ(topology.branchNodes[0], a);
    EXPECT_EQ(topology.joinNodes[0], d);

    const auto findDegree = [&](const std::shared_ptr<GraphNode>& node) -> const GraphTopologyInfo::NodeDegree* {
        for (const auto& degree : topology.nodeDegrees) {
            if (degree.node == node) {
                return &degree;
            }
        }
        return nullptr;
    };

    const auto* branchDegree = findDegree(a);
    ASSERT_NE(branchDegree, nullptr);
    EXPECT_EQ(branchDegree->incomingCount, 0u);
    EXPECT_EQ(branchDegree->outgoingCount, 2u);

    const auto* joinDegree = findDegree(d);
    ASSERT_NE(joinDegree, nullptr);
    EXPECT_EQ(joinDegree->incomingCount, 2u);
    EXPECT_EQ(joinDegree->outgoingCount, 0u);
}

TEST(GraphTest, AnalyzeTopology_DetectsBasicForkJoinGroup) {
    Graph graph;
    graph.AddEdge(a, b);
    graph.AddEdge(a, c);
    graph.AddEdge(b, d);
    graph.AddEdge(c, d);

    auto topology = graph.AnalyzeTopology();

    ASSERT_EQ(topology.forkJoinGroups.size(), 1u);
    const auto& group = topology.forkJoinGroups[0];
    ASSERT_NE(group.forkNode, nullptr);
    ASSERT_NE(group.joinNode, nullptr);
    ASSERT_EQ(group.paths.size(), 2u);
    ASSERT_EQ(group.paths[0].nodes.size(), 3u);
    EXPECT_EQ(group.forkNode, a);
    EXPECT_EQ(group.joinNode, d);
    EXPECT_EQ(group.paths[0].nodes.front(), a);
    EXPECT_EQ(group.paths[0].nodes.back(), d);
}

/**
 *  DAG (Directed Acyclic Graph) Visualization:
 *           a
 *          / \
 *         /   \
 *        b     c
 *       / \   /
 *      /   \ /
 *     d     e
 *      \   /
 *       \ /
 *        f
 */

TEST(GraphTest, ToEdgeListBfs_MatchesExpectedTraversalOrder) {
    Graph graph;
    graph.SetName("TestDAG");

    graph.AddEdge(a, b);
    graph.AddEdge(a, c);
    graph.AddEdge(b, d);
    graph.AddEdge(b, e);
    graph.AddEdge(c, e);
    graph.AddEdge(d, f);
    graph.AddEdge(e, f);

    std::cout << graph.ToString() << std::endl;

    // Display graph by edge list.
    auto edgeList = graph.ToEdgeListBfs();
    std::cout << "BFS Result, [Graph]: " << graph.GetName() << std::endl;

    std::vector<Edge> expectedEdges{{a, b}, {a, c}, {b, d}, {b, e}, {c, e}, {d, f}, {e, f}};
    ASSERT_EQ(edgeList.size(), expectedEdges.size());

    for (size_t i = 0; i < edgeList.size(); ++i) {
        auto& actual = edgeList[i];
        auto& expected = expectedEdges[i];

        ASSERT_EQ(actual.srcNodePtr.lock(), expected.srcNodePtr.lock());
        ASSERT_EQ(actual.dstNodePtr.lock(), expected.dstNodePtr.lock());
    }

    // for (auto&& edge : edgeList) {
    //     auto srcNodePtr = edge.srcNodePtr.lock();
    //     auto dstNodePtr = edge.dstNodePtr.lock();
    //     std::cout << "  " << srcNodePtr->name << " -> " << dstNodePtr->name << std::endl;
    // }
    // std::cout << "================== TestDisplayByBFS =====================" << std::endl;
}

TEST(GraphTest, ToString_IncludesGraphAndNodeNames) {
    Graph graph;
    graph.SetName("MyAwesomeGraph");
    graph.AddEdge(a, b);
    graph.AddEdge(b, c);

    std::string repr = graph.ToString();

    // 检查输出是否包含了图的名字和所有节点的名字
    ASSERT_NE(repr.find("MyAwesomeGraph"), std::string::npos);
    ASSERT_NE(repr.find("a"), std::string::npos);
    ASSERT_NE(repr.find("b"), std::string::npos);
    ASSERT_NE(repr.find("c"), std::string::npos);
}
