#include "base/Graph.hpp"
#include <cstdlib>
#include <gtest/gtest.h>

auto a = std::make_shared<Node>("a");
auto b = std::make_shared<Node>("b");
auto c = std::make_shared<Node>("c");
auto d = std::make_shared<Node>("d");
auto e = std::make_shared<Node>("e");
auto f = std::make_shared<Node>("f");
static constexpr size_t kNumNode = 6;

TEST(GraphTest, HasCycle_LinearGraphVariants) {
    // a -> b -> c -> d
    {
        Graph graph;

        graph.AddEdge(a, b);
        graph.AddEdge(b, c);
        graph.AddEdge(c, d);

        ASSERT_FALSE(graph.hasCycle());
    }

    // a -> b -> c -> b -> d, 有环.
    {
        Graph graph;

        graph.AddEdge(a, b);
        graph.AddEdge(b, c);
        graph.AddEdge(c, b);
        graph.AddEdge(b, d);

        ASSERT_TRUE(graph.hasCycle());
    }

    // a -> b -> c -> a -> d, 有环.
    {
        Graph graph;

        graph.AddEdge(a, b);
        graph.AddEdge(b, c);
        graph.AddEdge(c, a);
        graph.AddEdge(a, d);

        ASSERT_TRUE(graph.hasCycle());
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

        ASSERT_TRUE(!graph.hasCycle());
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

        ASSERT_TRUE(graph.hasCycle());
    }
}

TEST(GraphTest, ToEdgeListBfs_DisconnectedGraphFromDifferentRoots) {
    // Graph: a -> b   c -> d (两个不连通的组件)
    Graph graph;
    graph.AddEdge(a, b);
    graph.AddEdge(c, d);

    // 从 'a' 开始BFS，应该只能找到 'a -> b'
    auto edgeListFromA = graph.toEdgeListBFS(a); // 假设 toEdgeListBFS 接受起点
    ASSERT_EQ(edgeListFromA.size(), 1);
    ASSERT_EQ(edgeListFromA[0].srcNodePtr.lock(), a);
    ASSERT_EQ(edgeListFromA[0].dstNodePtr.lock(), b);

    // 从 'c' 开始BFS，应该只能找到 'c -> d'
    auto edgeListFromC = graph.toEdgeListBFS(c); // 假设 toEdgeListBFS 接受起点
    ASSERT_EQ(edgeListFromC.size(), 1);
    ASSERT_EQ(edgeListFromC[0].srcNodePtr.lock(), c);
    ASSERT_EQ(edgeListFromC[0].dstNodePtr.lock(), d);
}

TEST(GraphTest, AddEdge_EdgeCases) {
    // Case 1: 添加自环，应该被检测为有环
    {
        Graph graph;
        graph.AddEdge(a, a);
        ASSERT_TRUE(graph.hasCycle());
    }

    // Case 2: 重复添加同一条边
    {
        Graph graph;
        graph.AddEdge(a, b);
        graph.AddEdge(a, b); // 添加第二次

        // 验证 hasCycle 不受影响
        ASSERT_FALSE(graph.hasCycle());

        // 验证 toEdgeListBFS 的结果。取决于实现，可能会有一条或两条边。
        // 一个好的实现应该只包含一条边。
        auto edgeList = graph.toEdgeListBFS(a); // 假设 toEdgeListBFS 接受起点
        ASSERT_EQ(edgeList.size(), 1);
        ASSERT_EQ(edgeList[0].srcNodePtr.lock(), a);
        ASSERT_EQ(edgeList[0].dstNodePtr.lock(), b);
    }
}

TEST(GraphTest, IsEmpty_DependsOnGraphNameAndEdges) {
    Graph graph;
    ASSERT_TRUE(graph.IsEmpty());

    graph.AddEdge(a, b);
    ASSERT_TRUE(graph.IsEmpty());

    graph.SetName("xxx");
    ASSERT_FALSE(graph.IsEmpty());
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

    std::cout << graph.toString() << std::endl;

    // Display graph by edge list.
    auto edgeList = graph.toEdgeListBFS();
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

    std::string repr = graph.toString();

    // 检查输出是否包含了图的名字和所有节点的名字
    ASSERT_NE(repr.find("MyAwesomeGraph"), std::string::npos);
    ASSERT_NE(repr.find("a"), std::string::npos);
    ASSERT_NE(repr.find("b"), std::string::npos);
    ASSERT_NE(repr.find("c"), std::string::npos);
}
