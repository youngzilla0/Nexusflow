/**
 * @file ThreadPool.hpp
 * @brief Work-stealing thread pool for NexusFlow Executor.
 *
 * Based on ai-pipe's WorkStealingThreadPool, adapted for NexusFlow's use case:
 * - Each worker has its own local task queue
 * - Work-stealing when local queue is empty
 * - Simplified interface: Submit(task, detach=true)
 * - C++14 compatible with custom Optional
 */

#ifndef NEXUSFLOW_THREAD_POOL_HPP
#define NEXUSFLOW_THREAD_POOL_HPP

#include "common/Optional.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <utility>
#include <vector>

namespace nexusflow {
namespace executor {

/**
 * @brief 线程安全的 work-stealing 双端队列。
 *
 * 本地 worker 以 FIFO 方式消费自身任务队列；
 * 空闲 worker 可从其他队列尾部窃取任务。
 */
template <typename T>
class WorkStealingDeque {
public:
    WorkStealingDeque() = default;

    /**
     * @brief 向队列头部插入一个任务。
     * @param item 待插入对象。
     */
    void PushFront(T item) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_deque.push_front(std::move(item));
    }

    /**
     * @brief 向队列尾部插入一个任务。
     * @param item 待插入对象。
     */
    void PushBack(T item) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_deque.push_back(std::move(item));
    }

    /**
     * @brief 从队列头部弹出一个任务。
     * @return 若队列非空，则返回弹出的对象。
     */
    Optional<T> PopFront() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_deque.empty()) return nullopt;
        T item = std::move(m_deque.front());
        m_deque.pop_front();
        return item;
    }

    /**
     * @brief 从队列尾部窃取一个任务。
     * @return 若队列非空，则返回窃取到的对象。
     */
    Optional<T> StealBack() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_deque.empty()) return nullopt;
        T item = std::move(m_deque.back());
        m_deque.pop_back();
        return item;
    }

    /**
     * @brief 判断队列是否为空。
     * @return 队列为空时返回 true。
     */
    bool IsEmpty() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_deque.empty();
    }

    /**
     * @brief 返回当前队列大小。
     * @return 队列中的元素数量。
     */
    std::size_t Size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_deque.size();
    }

private:
    mutable std::mutex m_mutex;
    std::deque<T> m_deque;
};

/**
 * @brief Executor 使用的 work-stealing 线程池。
 *
 * 每个 worker 持有一个本地任务队列。
 * 本地消费使用 FIFO 顺序，以避免单 worker 下 actor 续跑饿死其他任务；
 * 当本地队列为空时，worker 会尝试从其他队列窃取任务。
 */
class ThreadPool {
public:
    /**
     * @brief 构造线程池。
     * @param numThreads worker 线程数量，默认取硬件并发数。
     */
    explicit ThreadPool(std::size_t numThreads = std::thread::hardware_concurrency())
        : m_running(false)
        , m_state(State::KRunning)
        , m_queues(numThreads)
        , m_nextWorkerIndex(0)
        , m_pendingTasks(0) {
        // 初始化每个 worker 的本地任务队列。
        for (std::size_t i = 0; i < numThreads; ++i) {
            m_queues[i] = std::make_unique<WorkStealingDeque<std::function<void()>>>();
        }
    }

    /**
     * @brief 启动全部 worker 线程。
     */
    void Start() {
        if (m_running) return;
        m_running = true;
        m_state.store(State::KRunning, std::memory_order_release);
        m_threads.reserve(m_queues.size());
        for (std::size_t i = 0; i < m_queues.size(); ++i) {
            m_threads.emplace_back(&ThreadPool::WorkerLoop, this, i);
        }
    }

    /**
     * @brief 停止线程池并等待全部 worker 退出。
     */
    void Stop() {
        if (!m_running) return;
        m_running = false;
        m_state.store(State::KStopped, std::memory_order_release);
        m_cond.notify_all();
        for (auto& t : m_threads) {
            if (t.joinable()) t.join();
        }
        m_threads.clear();
    }

    /**
     * @brief 提交一个异步任务。
     * @param task 待执行任务。
     * @param detach 保留参数，当前实现始终按 fire-and-forget 处理。
     */
    void Submit(std::function<void()> task, bool detach = true) {
        std::size_t targetQueue = SelectQueue();

        m_queues[targetQueue]->PushBack(std::move(task));
        m_pendingTasks.fetch_add(1, std::memory_order_relaxed);

        m_cond.notify_one();
    }

private:
    /**
     * @brief 单个 worker 的执行主循环。
     * @param workerIndex 当前 worker 索引。
     */
    void WorkerLoop(std::size_t workerIndex) {
        std::mt19937 rng(std::random_device{}() + static_cast<unsigned>(workerIndex));
        std::uniform_int_distribution<std::size_t> dist(0, m_queues.size() - 1);

        while (true) {
            Optional<std::function<void()>> optTask;

            // 优先消费本地队列，保持 FIFO 语义。
            optTask = m_queues[workerIndex]->PopFront();
            if (!optTask) {
                // 本地为空时尝试从其他 worker 窃取任务。
                optTask = TrySteal(workerIndex, rng, dist);
            }

            if (!optTask) {
                // 无任务时进入等待态。
                std::unique_lock<std::mutex> lock(m_mutex);

                // 获取锁后再次检查本地队列，避免错过刚到达的任务。
                optTask = m_queues[workerIndex]->PopFront();
                if (!optTask) {
                    if (m_state.load(std::memory_order_acquire) != State::KRunning) {
                        // 退出前再执行一次窃取尝试，尽量消费残留任务。
                        optTask = TrySteal(workerIndex, rng, dist);
                        if (!optTask) return;
                    } else {
                        // 等待新任务到达或停止信号。
                        m_cond.wait_for(lock, std::chrono::milliseconds(1), [this, workerIndex] {
                            return m_state.load(std::memory_order_acquire) != State::KRunning ||
                                   !m_queues[workerIndex]->IsEmpty() ||
                                   HasStealTarget();
                        });
                        // 唤醒后重新检查本地队列。
                        optTask = m_queues[workerIndex]->PopFront();
                        if (!optTask) continue;
                    }
                }
            }

            if (optTask) {
                m_pendingTasks.fetch_sub(1, std::memory_order_acq_rel);
                optTask.value()();
            }
        }
    }

    /**
     * @brief 尝试从其他 worker 的本地队列窃取任务。
     * @param workerIndex 当前 worker 索引。
     * @param rng 随机数生成器。
     * @param dist victim 选择分布。
     * @return 若窃取成功，则返回任务对象。
     */
    Optional<std::function<void()>> TrySteal(std::size_t workerIndex, std::mt19937& rng,
                                             std::uniform_int_distribution<std::size_t>& dist) {
        const std::size_t numWorkers = m_queues.size();
        if (numWorkers <= 1) return nullopt;

        std::size_t start = dist(rng);
        for (std::size_t i = 0; i < numWorkers; ++i) {
            std::size_t victim = (start + i) % numWorkers;
            if (victim == workerIndex) continue;
            auto stolen = m_queues[victim]->StealBack();
            if (stolen) return stolen;
        }
        return nullopt;
    }

    /**
     * @brief 判断当前是否存在可供窃取的任务。
     * @return 若任一 worker 的本地队列非空，则返回 true。
     */
    bool HasStealTarget() const {
        for (const auto& queue : m_queues) {
            if (!queue->IsEmpty()) return true;
        }
        return false;
    }

    /**
     * @brief 选择任务应投递到的目标 worker 队列。
     * @return 目标 worker 队列索引。
     */
    std::size_t SelectQueue() { return m_nextWorkerIndex.fetch_add(1, std::memory_order_relaxed) % m_queues.size(); }

    enum class State : std::uint8_t { KRunning, KStopped };

    std::atomic<bool> m_running;
    std::atomic<State> m_state{State::KRunning};
    std::vector<std::thread> m_threads;
    std::vector<std::unique_ptr<WorkStealingDeque<std::function<void()>>>> m_queues;
    std::mutex m_mutex;
    std::condition_variable m_cond;
    std::atomic<std::size_t> m_nextWorkerIndex;
    std::atomic<std::size_t> m_pendingTasks{0};
};

} // namespace executor
} // namespace nexusflow

#endif // NEXUSFLOW_THREAD_POOL_HPP
