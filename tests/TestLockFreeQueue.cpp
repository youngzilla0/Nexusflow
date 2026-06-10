#include "common/LockFreeQueue.hpp"
#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

using namespace nexusflow;

// 1. 基本的单线程测试
TEST(LockFreeMPMCQueueTest, TryPushTryPop_BasicSingleThread) {
    LockFreeMPMCQueue<int> q(8);

    int val = 0;
    EXPECT_TRUE(q.tryPush(42));
    EXPECT_TRUE(q.tryPush(100));

    EXPECT_TRUE(q.tryPop(val));
    EXPECT_EQ(val, 42);

    EXPECT_TRUE(q.tryPop(val));
    EXPECT_EQ(val, 100);

    // 队列已空
    EXPECT_FALSE(q.tryPop(val));
}

// 2. 测试队列容量限制及 Try 接口
TEST(LockFreeMPMCQueueTest, TryPushTryPop_CapacityLimits) {
    // 申请容量 2，内部向上取整，刚好是 2
    LockFreeMPMCQueue<int> q(2);

    EXPECT_TRUE(q.tryPush(1));
    EXPECT_TRUE(q.tryPush(2));
    // 队列已满，第三次 push 应该失败
    EXPECT_FALSE(q.tryPush(3));

    int val;
    EXPECT_TRUE(q.tryPop(val));
    EXPECT_EQ(val, 1);
    EXPECT_TRUE(q.tryPop(val));
    EXPECT_EQ(val, 2);
    // 队列已空，第三次 pop 应该失败
    EXPECT_FALSE(q.tryPop(val));
}

// 3. 测试超时逻辑 (手动实现 timeout 等效)
TEST(LockFreeMPMCQueueTest, TryOperations_TimeoutEquivalentBehavior) {
    LockFreeMPMCQueue<int> q(2);
    int val = 0;

    // 队列为空，手动实现 timeout：多次 TryPop 应在 50ms 内返回 false
    auto start = std::chrono::steady_clock::now();
    bool popResult = false;
    for (int i = 0; i < 100; ++i) {
        if (q.tryPop(val)) {
            popResult = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    auto end = std::chrono::steady_clock::now();
    EXPECT_FALSE(popResult); // 队列为空，应该没能弹出
    EXPECT_GE(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count(), 49);

    q.tryPush(1);
    q.tryPush(2);

    // 队列已满，手动实现 timeout：多次 TryPush 应在 50ms 内返回 false
    start = std::chrono::steady_clock::now();
    bool pushResult = false;
    for (int i = 0; i < 100; ++i) {
        if (q.tryPush(3)) {
            pushResult = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    end = std::chrono::steady_clock::now();
    EXPECT_FALSE(pushResult); // 队列已满，应该没能推入
    EXPECT_GE(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count(), 49);
}

// 4. 多线程高并发数据完整性测试 (最严苛的测试)
// 测试场景：4 个生产者，4 个消费者，共发送 40 万条数据，确保 0 丢失、0 重复
TEST(LockFreeMPMCQueueTest, ConcurrentProducersConsumers_DataIntegrity) {
    const int numProducers = 4;
    const int numConsumers = 4;
    const int itemsPerProducer = 100000;
    const int totalItems = numProducers * itemsPerProducer;

    LockFreeMPMCQueue<int> q(1024);

    // 用于记录每个数字被消费的次数（初始全为 0）
    std::vector<std::atomic<int>> counts(totalItems);
    for (int i = 0; i < totalItems; ++i) {
        counts[i].store(0);
    }

    std::atomic<int> totalConsumed{0};
    std::vector<std::thread> threads;

    // 启动消费者
    for (int i = 0; i < numConsumers; ++i) {
        threads.emplace_back([&]() {
            int val;
            while (totalConsumed.load() < totalItems) {
                // 使用 tryPop，避免消费者在最后阶段死锁挂起
                if (q.tryPop(val)) {
                    counts[val]++;
                    totalConsumed++;
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    // 启动生产者
    for (int i = 0; i < numProducers; ++i) {
        threads.emplace_back([&, i]() {
            for (int j = 0; j < itemsPerProducer; ++j) {
                // 每个生产者负责一个独立的数据区间
                int data = i * itemsPerProducer + j;
                // 阻塞写入，直到成功（spin + yield）
                while (!q.tryPush(std::move(data))) {
                    std::this_thread::yield();
                }
            }
        });
    }

    // 等待所有线程结束
    for (auto& t : threads) {
        t.join();
    }

    // 校验：必须所有数据都被准确消费且仅被消费了一次
    EXPECT_EQ(totalConsumed.load(), totalItems);
    for (int i = 0; i < totalItems; ++i) {
        EXPECT_EQ(counts[i].load(), 1) << "Data " << i << " was not consumed exactly once!";
    }
}

// 测试用的数据结构
struct TestFrame {
    FrameId id;
    std::string data;
};
class LockFreeNodeQueueTest : public ::testing::Test {
protected:
    // 辅助函数：快速创建一个配置
    LockFreeNodeQueue<TestFrame>::Config createConfig(size_t cap, LockFreeDropPolicy policy) {
        LockFreeNodeQueue<TestFrame>::Config cfg;
        cfg.capacity = cap;
        cfg.drop_policy = policy;
        cfg.node_name = "test_node";
        cfg.port_name = "test_port";
        return cfg;
    }
};

// 1. 基础功能测试：入队与出队
TEST_F(LockFreeNodeQueueTest, PushPop_BasicSingleThread) {
    LockFreeNodeQueue<TestFrame> nq(4); // 默认 DropHead

    TestFrame f1{101, "frame1"};
    EXPECT_TRUE(nq.push(std::move(f1)));
    EXPECT_EQ(nq.size(), 1);

    auto opt = nq.tryPop();
    ASSERT_TRUE(opt.hasValue());
    EXPECT_EQ(opt.value().id, 101);
    EXPECT_EQ(nq.size(), 0);

    // 测试空队列返回 nullopt
    auto empty_opt = nq.tryPop();
    EXPECT_FALSE(empty_opt.hasValue());
}

// 2. 策略测试：DropTail (满了就拒收)
TEST_F(LockFreeNodeQueueTest, DropTail_RejectsNewestWhenFull) {
    auto cfg = createConfig(2, LockFreeDropPolicy::DropTail);
    LockFreeNodeQueue<TestFrame> nq(cfg);

    EXPECT_TRUE(nq.push({1, "a"}));
    EXPECT_TRUE(nq.push({2, "b"}));

    // 队列已满 (实际容量向上取整为 2)
    EXPECT_FALSE(nq.push({3, "c"})); // 应该返回 false

    // 校验统计
    EXPECT_EQ(nq.statistics().total_rejected.load(), 1);
    EXPECT_EQ(nq.size(), 2);
}

// 3. 策略测试：DropHead (满了就踢掉最老的)
TEST_F(LockFreeNodeQueueTest, DropHead_EvictsOldestWhenFull) {
    auto cfg = createConfig(2, LockFreeDropPolicy::DropHead);
    LockFreeNodeQueue<TestFrame> nq(cfg);

    nq.push({1, "oldest"});
    nq.push({2, "middle"});
    nq.push({3, "newest"}); // 这会把 1 踢掉

    // 校验统计
    EXPECT_EQ(nq.statistics().total_dropped.load(), 1);

    // 顺序应该是 2, 3
    auto p1 = nq.tryPop();
    EXPECT_EQ(p1.value().id, 2);
    auto p2 = nq.tryPop();
    EXPECT_EQ(p2.value().id, 3);
}

// 4. 策略测试：KeepLatest (只保留最新的 N 个)
TEST_F(LockFreeNodeQueueTest, KeepLatest_RetainsNewestItems) {
    auto cfg = createConfig(8, LockFreeDropPolicy::KeepLatest);
    cfg.keep_latest_n = 1; // 极其激进：只要最新的
    LockFreeNodeQueue<TestFrame> nq(cfg);

    nq.push({1, "a"});
    nq.push({2, "b"});
    nq.push({3, "c"});

    EXPECT_EQ(nq.size(), 1);
    EXPECT_EQ(nq.tryPop().value().id, 3); // 只有 3 留下了
}

// 5. 功能测试：统计指标跟踪
TEST_F(LockFreeNodeQueueTest, Statistics_TracksPushPopAndReset) {
    LockFreeNodeQueue<int> nq(10);

    for (int i = 0; i < 5; ++i) nq.push(i);
    for (int i = 0; i < 3; ++i) nq.tryPop();

    auto& stats = nq.statistics();
    EXPECT_EQ(stats.total_pushed.load(), 5);
    EXPECT_EQ(stats.total_popped.load(), 3);
    EXPECT_GE(stats.peak_size.load(), 5);

    nq.resetStatistics();
    EXPECT_EQ(stats.total_pushed.load(), 0);
}

// 6. 核心测试：丢弃回调函数 (Drop Callback)
TEST_F(LockFreeNodeQueueTest, DropCallback_ReportsEvictedFrame) {
    auto cfg = createConfig(2, LockFreeDropPolicy::DropHead);
    LockFreeNodeQueue<TestFrame> nq(cfg);

    // 设置如何从 TestFrame 提取 ID
    nq.setFrameIdAccessor([](const TestFrame& f) { return f.id; });

    bool callback_triggered = false;
    FrameId dropped_id = 0;

    nq.setDropCallback([&](const DropEvent& ev) {
        callback_triggered = true;
        dropped_id = ev.frame_id;
        EXPECT_EQ(ev.node_name, "test_node");
        EXPECT_EQ(ev.reason, "DropHead overflow");
    });

    nq.push({1001, "msg1"});
    nq.push({1002, "msg2"});
    nq.push({1003, "msg3"}); // 触发丢弃 1001

    EXPECT_TRUE(callback_triggered);
    EXPECT_EQ(dropped_id, 1001);
}

// 7. 功能测试：Clear 清空队列
TEST_F(LockFreeNodeQueueTest, Clear_RemovesAllQueuedItems) {
    LockFreeNodeQueue<int> nq(10);
    nq.push(1);
    nq.push(2);
    nq.push(3);

    EXPECT_EQ(nq.size(), 3);
    nq.clear();
    EXPECT_EQ(nq.size(), 0);
    EXPECT_FALSE(nq.tryPop().hasValue());
}

// 8. 并发边界测试：多生产单消费下的策略表现
TEST_F(LockFreeNodeQueueTest, DropHead_ConcurrentProducersRespectsCapacity) {
    LockFreeNodeQueue<int>::Config cfg;
    {
        cfg.capacity = 16;
        cfg.drop_policy = LockFreeDropPolicy::DropHead;
        cfg.node_name = "test_node";
        cfg.port_name = "test_port";
    }
    LockFreeNodeQueue<int> nq(cfg);

    const int producers = 4;
    const int items_per_producer = 1000;
    std::vector<std::thread> threads;

    // 多个生产者疯狂灌数据，远远超过容量 16
    for (int i = 0; i < producers; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < items_per_producer; ++j) {
                nq.push(j);
            }
        });
    }

    // 主线程稍微等一下，让丢弃发生
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    for (auto& t : threads) t.join();

    // 最终队列大小不应超过容量
    EXPECT_LE(nq.size(), nq.capacity());
    // 丢弃计数应该大于 0
    EXPECT_GT(nq.statistics().total_dropped.load(), 0);
}
