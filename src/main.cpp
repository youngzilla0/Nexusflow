#include "Graph/Graph.h"
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

// --- 自定义断言宏 ---
// 核心宏，用于处理断言失败
#define MY_ASSERT_IMPLEMENT(condition, message)                                              \
    do {                                                                                     \
        if (!(condition)) {                                                                  \
            std::cerr << "Assertion failed at " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::cerr << "  Condition: " << #condition << std::endl;                         \
            std::cerr << "  Message: " << (message) << std::endl;                            \
            std::abort();                                                                    \
        }                                                                                    \
    } while (0)

// 模拟 GTest 的断言宏
#define MY_ASSERT_TRUE(condition) MY_ASSERT_IMPLEMENT(condition, "Expected true, but got false.")

#define MY_ASSERT_FALSE(condition) MY_ASSERT_IMPLEMENT(!(condition), "Expected false, but got true.")

#define MY_ASSERT_EQ(val1, val2)                                                        \
    do {                                                                                \
        auto v1 = (val1);                                                               \
        auto v2 = (val2);                                                               \
        std::stringstream ss;                                                           \
        ss << "Expected " << #val1 << " and " << #val2 << " to be equal." << std::endl; \
        ss << "  " << #val1 << " (" << v1 << ")" << std::endl;                          \
        ss << "  " << #val2 << " (" << v2 << ")";                                       \
        MY_ASSERT_IMPLEMENT(v1 == v2, ss.str());                                        \
    } while (0)

auto a = std::make_shared<Node>("a");
auto b = std::make_shared<Node>("b");
auto c = std::make_shared<Node>("c");
auto d = std::make_shared<Node>("d");
auto e = std::make_shared<Node>("e");
auto f = std::make_shared<Node>("f");
static constexpr size_t kNumNode = 6;

void TestHasCycleUsingLinearGraph() {
    std::cout << "Running Test: TestHasCycleUsingLinearGraph..." << std::endl;
    // a -> b -> c -> d
    {
        Graph graph;
        graph.addEdge(a, b);
        graph.addEdge(b, c);
        graph.addEdge(c, d);
        graph.setInputNodePtr(a);
        graph.setOutputNodePtr(d);
        MY_ASSERT_FALSE(graph.hasCycle());
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
        MY_ASSERT_TRUE(graph.hasCycle());
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
        MY_ASSERT_TRUE(graph.hasCycle());
    }
    std::cout << "TestHasCycleUsingLinearGraph PASSED." << std::endl;
}

void TestHasCycleUsingDAG() {
    std::cout << "Running Test: TestHasCycleUsingDAG..." << std::endl;
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
        MY_ASSERT_FALSE(graph.hasCycle());
    }
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
        MY_ASSERT_TRUE(graph.hasCycle());
    }
    std::cout << "TestHasCycleUsingDAG PASSED." << std::endl;
}

void TestIsEmpty() {
    std::cout << "Running Test: TestIsEmpty..." << std::endl;
    Graph graph;
    MY_ASSERT_TRUE(graph.isEmpty());

    graph.addEdge(a, b);
    MY_ASSERT_TRUE(graph.isEmpty());

    graph.setInputNodePtr(a);
    MY_ASSERT_TRUE(graph.isEmpty());

    graph.setOutputNodePtr(b);
    MY_ASSERT_TRUE(graph.isEmpty());

    graph.setName("xxx");
    MY_ASSERT_FALSE(graph.isEmpty());
    std::cout << "TestIsEmpty PASSED." << std::endl;
}

void TestDisplayByBFS() {
    std::cout << "================== TestDisplayByBFS =====================" << std::endl;

    Graph graph;
    graph.setName("TestDAG");

    graph.addEdge(a, b);
    graph.addEdge(a, c);
    graph.addEdge(b, d);
    graph.addEdge(b, e);
    graph.addEdge(c, e);
    graph.addEdge(d, f);
    graph.addEdge(e, f);

    graph.setInputNodePtr(a);
    graph.setOutputNodePtr(f);

    std::cout << graph.toString() << std::endl;

    auto edgeList = graph.toEdgeListBFS();
    std::cout << "BFS Result, [Graph]: " << graph.getName() << std::endl;

    std::vector<Edge> bfsEdgeList{{a, b}, {a, c}, {b, d}, {b, e}, {c, e}, {d, f}, {e, f}};
    MY_ASSERT_EQ(edgeList.size(), bfsEdgeList.size());

    for (size_t i = 0; i < edgeList.size(); ++i) {
        auto&& edge = edgeList[i];
        auto&& bfsEdge = bfsEdgeList[i];
        MY_ASSERT_EQ(edge.srcNodePtr.lock()->name, bfsEdge.srcNodePtr.lock()->name);
        MY_ASSERT_EQ(edge.dstNodePtr.lock()->name, bfsEdge.dstNodePtr.lock()->name);
    }

    for (auto&& edge : edgeList) {
        auto srcNodePtr = edge.srcNodePtr.lock();
        auto dstNodePtr = edge.dstNodePtr.lock();
        if (srcNodePtr && dstNodePtr) {
            std::cout << "  " << srcNodePtr->name << " -> " << dstNodePtr->name << std::endl;
        }
    }
    std::cout << "================== TestDisplayByBFS =====================" << std::endl;
    std::cout << "TestDisplayByBFS PASSED." << std::endl;
}

// --- main 函数 ---
int main(int argc, char* argv[]) {
    try {
        TestHasCycleUsingLinearGraph();
        std::cout << "\n";
        TestHasCycleUsingDAG();
        std::cout << "\n";
        TestIsEmpty();
        std::cout << "\n";
        TestDisplayByBFS();
        std::cout << "\n";

        std::cout << "All tests passed successfully!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "A test failed with exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "A test failed with an unknown exception." << std::endl;
        return 1;
    }

    return 0;
}