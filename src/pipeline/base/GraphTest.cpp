#include "Graph.hpp"
#include <cstdlib>
#include <gtest/gtest.h>

auto a = std::make_shared<Node>("a", "a");
auto b = std::make_shared<Node>("b", "b");
auto c = std::make_shared<Node>("c", "c");
auto d = std::make_shared<Node>("d", "d");
auto e = std::make_shared<Node>("e", "e");
auto f = std::make_shared<Node>("f", "f");
static constexpr size_t kNumNode = 6;

TEST(TestGraph, TestHasCycleUsingLinearGraph) {
    // a -> b -> c -> d
    {
        Graph graph;

        graph.addEdge(a, b);
        graph.addEdge(b, c);
        graph.addEdge(c, d);
        graph.setInputNodePtr(a);
        graph.setOutputNodePtr(d);

        ASSERT_FALSE(graph.hasCycle());
    }

    // a -> b -> c -> b -> d, 有环.
    {
        Graph graph;

        graph.addEdge(a, b);
        graph.addEdge(b, c);
        graph.addEdge(c, b);
        graph.addEdge(b, d);
        graph.setInputNodePtr(a);
        graph.setOutputNodePtr(d);

        ASSERT_TRUE(graph.hasCycle());
    }

    // a -> b -> c -> a -> d, 有环.
    {
        Graph graph;

        graph.addEdge(a, b);
        graph.addEdge(b, c);
        graph.addEdge(c, a);
        graph.addEdge(a, d);
        graph.setInputNodePtr(a);
        graph.setOutputNodePtr(d);

        ASSERT_TRUE(graph.hasCycle());
    }
}

TEST(TestGraph, TestHasCycleUsingDAG) {
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

        graph.addEdge(a, b);
        graph.addEdge(a, c);
        graph.addEdge(b, d);
        graph.addEdge(b, e);
        graph.addEdge(c, e);
        graph.addEdge(d, f);
        graph.addEdge(e, f);
        graph.setInputNodePtr(a);
        graph.setOutputNodePtr(f);

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

        graph.addEdge(a, b);
        graph.addEdge(a, c);
        graph.addEdge(b, d);
        graph.addEdge(b, a);
        graph.addEdge(c, a);
        graph.addEdge(d, f);
        graph.addEdge(a, f);

        graph.setInputNodePtr(a);
        graph.setOutputNodePtr(f);

        ASSERT_TRUE(graph.hasCycle());
    }
}

TEST(TestGraph, TestIsEmpty) {
    Graph graph;
    ASSERT_TRUE(graph.isEmpty());

    graph.addEdge(a, b);
    ASSERT_TRUE(graph.isEmpty());

    graph.setInputNodePtr(a);
    ASSERT_TRUE(graph.isEmpty());

    graph.setOutputNodePtr(b);
    ASSERT_TRUE(graph.isEmpty());

    graph.setName("xxx");
    ASSERT_FALSE(graph.isEmpty());
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

TEST(TestGraph, TestDisplayByBFS) {
    Graph graph;
    graph.setName("TestDAG");

    graph.addEdge(a, b);
    graph.addEdge(a, c);
    graph.addEdge(b, d);
    graph.addEdge(b, e);
    graph.addEdge(c, e);
    graph.addEdge(d, f);
    graph.addEdge(e, f);

    // set input and output.
    graph.setInputNodePtr(a);
    graph.setOutputNodePtr(f);

    std::cout << graph.toString() << std::endl;

    // Display graph by edge list.
    auto edgeList = graph.toEdgeListBFS();
    std::cout << "BFS Result, [Graph]: " << graph.getName() << std::endl;

    std::vector<Edge> bfsEdgeList{{a, b}, {a, c}, {b, d}, {b, e}, {c, e}, {d, f}, {e, f}};
    ASSERT_EQ(edgeList.size(), bfsEdgeList.size());

    for (size_t i = 0; i < edgeList.size(); ++i) {
        auto&& edge = edgeList[i];
        auto&& bfsEdge = bfsEdgeList[i];

        ASSERT_EQ(edge.srcNodePtr.lock()->name, edge.srcNodePtr.lock()->name);
        ASSERT_EQ(edge.dstNodePtr.lock()->name, edge.dstNodePtr.lock()->name);
    }

    for (auto&& edge : edgeList) {
        auto srcNodePtr = edge.srcNodePtr.lock();
        auto dstNodePtr = edge.dstNodePtr.lock();
        std::cout << "  " << srcNodePtr->name << " -> " << dstNodePtr->name << std::endl;
    }
    std::cout << "================== TestDisplayByBFS =====================" << std::endl;
}
