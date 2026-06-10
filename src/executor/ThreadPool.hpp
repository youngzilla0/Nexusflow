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
 * @brief Thread-safe work-stealing deque for fine-grained task distribution.
 *
 * Design:
 * - PushFront/PopFront: local LIFO (fast, single lock)
 * - StealBack: remote FIFO (for work stealing)
 * - IsEmpty: check if deque is empty
 */
template <typename T>
class WorkStealingDeque {
public:
    WorkStealingDeque() = default;

    /** @brief Push to front (local operation) */
    void PushFront(T item) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_deque.push_front(std::move(item));
    }

    /** @brief Pop from front (local operation, LIFO) */
    Optional<T> PopFront() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_deque.empty()) return nullopt;
        T item = std::move(m_deque.front());
        m_deque.pop_front();
        return item;
    }

    /** @brief Steal from back (remote operation, FIFO) */
    Optional<T> StealBack() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_deque.empty()) return nullopt;
        T item = std::move(m_deque.back());
        m_deque.pop_back();
        return item;
    }

    /** @brief Check if empty */
    bool IsEmpty() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_deque.empty();
    }

    /** @brief Get current size */
    std::size_t Size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_deque.size();
    }

private:
    mutable std::mutex m_mutex;
    std::deque<T> m_deque;
};

/**
 * @brief Work-stealing thread pool.
 *
 * Each worker has its own deque. Tasks are pushed to the front (LIFO) and
 * popped from the front (LIFO) for good cache locality. When a worker's
 * queue is empty, it tries to steal from another queue's back (FIFO).
 */
class ThreadPool {
public:
    /**
     * @brief Construct a thread pool.
     * @param numThreads Number of worker threads. Defaults to hardware concurrency.
     */
    explicit ThreadPool(std::size_t numThreads = std::thread::hardware_concurrency())
        : m_running(false)
        , m_state(State::KRunning)
        , m_queues(numThreads)
        , m_nextWorkerIndex(0)
        , m_pendingTasks(0) {
        // Initialize deques
        for (std::size_t i = 0; i < numThreads; ++i) {
            m_queues[i] = std::make_unique<WorkStealingDeque<std::function<void()>>>();
        }
    }

    /** @brief Start the worker threads. */
    void Start() {
        if (m_running) return;
        m_running = true;
        m_state.store(State::KRunning, std::memory_order_release);
        m_threads.reserve(m_queues.size());
        for (std::size_t i = 0; i < m_queues.size(); ++i) {
            m_threads.emplace_back(&ThreadPool::WorkerLoop, this, i);
        }
    }

    /** @brief Stop the thread pool. */
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
     * @brief Submit a task to the pool.
     * @param task Task to execute.
     * @param detach Ignored, always fire-and-forget.
     */
    void Submit(std::function<void()> task, bool detach = true) {
        std::size_t targetQueue = SelectQueue();

        m_queues[targetQueue]->PushFront(std::move(task));
        m_pendingTasks.fetch_add(1, std::memory_order_relaxed);

        m_cond.notify_one();
    }

private:
    void WorkerLoop(std::size_t workerIndex) {
        std::mt19937 rng(std::random_device{}() + static_cast<unsigned>(workerIndex));
        std::uniform_int_distribution<std::size_t> dist(0, m_queues.size() - 1);

        while (true) {
            Optional<std::function<void()>> optTask;

            // Try local queue first (LIFO)
            optTask = m_queues[workerIndex]->PopFront();
            if (!optTask) {
                // Try to steal from other workers
                optTask = TrySteal(workerIndex, rng, dist);
            }

            if (!optTask) {
                // Wait for notification
                std::unique_lock<std::mutex> lock(m_mutex);

                // Double-check after acquiring lock
                optTask = m_queues[workerIndex]->PopFront();
                if (!optTask) {
                    if (m_state.load(std::memory_order_acquire) != State::KRunning) {
                        // Try one more steal before exiting
                        optTask = TrySteal(workerIndex, rng, dist);
                        if (!optTask) return;
                    } else {
                        // Wait for work — predicate: stop requested, local queue has work, or steal target exists
                        m_cond.wait_for(lock, std::chrono::milliseconds(1), [this, workerIndex] {
                            return m_state.load(std::memory_order_acquire) != State::KRunning ||
                                   !m_queues[workerIndex]->IsEmpty() ||
                                   HasStealTarget();
                        });
                        // After wake: re-check local queue (work may have arrived during wake)
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

    bool HasStealTarget() const {
        for (const auto& queue : m_queues) {
            if (!queue->IsEmpty()) return true;
        }
        return false;
    }

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
