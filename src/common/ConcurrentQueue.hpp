#ifndef CONCURRENT_QUEUE_HPP_
#define CONCURRENT_QUEUE_HPP_

#include "Optional.hpp"
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>

/**
 * @class ConcurrentQueue
 * @brief A thread-safe, blocking queue for producer-consumer scenarios.
 *
 * This queue can be configured as bounded (with a fixed capacity) or unbounded.
 * It uses condition variables to block producing threads when the queue is full
 * and consuming threads when the queue is empty, preventing busy-waiting.
 *
 * @tparam T The type of elements stored in the queue.
 */
template <typename T>
class ConcurrentQueue {
public:
    /**
     * @brief Constructs a ConcurrentQueue.
     * @param capacity The maximum capacity of the queue. A value of -1 (default)
     *                 indicates an unbounded queue.
     */
    explicit ConcurrentQueue(int capacity = -1) : m_capacity(capacity), m_shutdown(false) {}

    // Disable copy and move semantics to ensure a single owner.
    ConcurrentQueue(const ConcurrentQueue&) = delete;
    ConcurrentQueue& operator=(const ConcurrentQueue&) = delete;

    /**
     * @brief Pushes an item into the queue (blocking).
     * If the queue is bounded and full, this call will block until space becomes available
     * or the queue is shut down.
     * @param item The item to be pushed (rvalue reference for move semantics).
     * @return true if the item was successfully pushed, false if the queue has been shut down.
     */
    bool Push(T&& item) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_condNotFull.wait(lock, [this] { return m_shutdown || !IsFull(); });

        if (m_shutdown) {
            return false;
        }

        m_queue.push(std::move(item));
        m_condNotEmpty.notify_one();
        return true;
    }

    /**
     * @brief Pushes an item into the queue (lvalue reference overload).
     */
    bool Push(const T& item) {
        T temp = item;
        return Push(std::move(temp));
    }

    /**
     * @brief Tries to push an item, waiting up to a specified timeout.
     * @param item The item to push.
     * @param timeout The maximum duration to wait.
     * @return true if pushed, false if timed out or shutdown.
     */
    template <class Rep, class Per>
    bool PushFor(T&& item, const std::chrono::duration<Rep, Per>& timeout) {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (!m_condNotFull.wait_for(lock, timeout, [this] { return m_shutdown || !IsFull(); })) {
            // wait_for returned false, meaning it timed out.
            return false;
        }
        if (m_shutdown) return false;
        m_queue.push(std::move(item));
        m_condNotEmpty.notify_one();
        return true;
    }

    template <class Rep, class Per>
    bool PushFor(const T& item, const std::chrono::duration<Rep, Per>& timeout) {
        return PushFor(T(item), timeout);
    }

    /**
     * @brief Tries to push an item into the queue without blocking.
     * If the queue is bounded and currently full, this method will return immediately
     * with false instead of waiting for space.
     * @param item The item to be pushed (rvalue reference for move semantics).
     * @return true if the item was successfully pushed, false if the queue was full or has been shut down.
     */
    bool TryPush(T&& item) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_shutdown || IsFull()) {
            return false;
        }

        m_queue.push(std::move(item));
        m_condNotEmpty.notify_one();
        return true;
    }

    /**
     * @brief Tries to push an item into the queue without blocking (lvalue reference overload).
     */
    bool TryPush(const T& item) {
        T temp = item;
        return TryPush(std::move(temp));
    }

    /**
     * @brief Tries to pop an item, waiting up to a specified timeout.
     * @param itemRef Reference to store the popped item.
     * @param timeout The maximum duration to wait.
     * @return true if popped, false if timed out or shutdown.
     */
    template <class Rep, class Per>
    bool WaitAndPopFor(T& itemRef, const std::chrono::duration<Rep, Per>& timeout) {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (!m_condNotEmpty.wait_for(lock, timeout, [this] { return m_shutdown || !m_queue.empty(); })) {
            // wait_for returned false, meaning it timed out.
            return false;
        }
        if (m_shutdown && m_queue.empty()) return false;
        itemRef = std::move(m_queue.front());
        m_queue.pop();
        m_condNotFull.notify_one();
        return true;
    }

    /**
     * @brief Waits for and pops an item from the queue (blocking).
     * If the queue is empty, this call will block until an item becomes available
     * or the queue is shut down.
     * @param itemRef A reference to store the popped item.
     * @return true if an item was successfully popped, false if the queue is empty and has been shut down.
     */
    bool WaitAndPop(T& itemRef) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_condNotEmpty.wait(lock, [this] { return m_shutdown || !m_queue.empty(); });

        if (m_shutdown && m_queue.empty()) {
            return false;
        }

        itemRef = std::move(m_queue.front());
        m_queue.pop();
        m_condNotFull.notify_one();
        return true;
    }

    /**
     * @brief Tries to pop an item from the queue without blocking.
     * @return An std::optional containing the item if the queue was not empty,
     *         otherwise std::nullopt.
     */
    nexusflow::Optional<T> TryPop() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_queue.empty()) {
            return nexusflow::nullopt;
        }
        T item = std::move(m_queue.front());
        m_queue.pop();
        m_condNotFull.notify_one();
        return item;
    }

    /**
     * @brief Tries to pop an item from the queue without blocking.
     * @param itemRef Reference to store the popped item (rvalue reference for move semantics).
     * @return true if an item was successfully popped, false if the queue was empty or has been shut down.
     */
    bool TryPop(T& itemRef) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_queue.empty()) {
            return false;
        }
        itemRef = std::move(m_queue.front());
        m_queue.pop();
        m_condNotFull.notify_one();
        return true;
    }

    /**
     * @brief Shuts down the queue.
     * This will wake up all waiting producer and consumer threads.
     */
    void Shutdown() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_shutdown = true;
        m_condNotEmpty.notify_all();
        m_condNotFull.notify_all();
    }

    /**
     * @brief Checks if the queue is currently empty.
     */
    bool IsEmpty() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.empty();
    }

    /**
     * @brief Returns the current number of items in the queue.
     */
    size_t GetSize() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.size();
    }

private:
    /**
     * @brief Checks if the queue is full. Must be called while holding the lock.
     */
    bool IsFull() const {
        if (m_capacity == -1) {
            return false;
        }
        return m_queue.size() >= static_cast<size_t>(m_capacity);
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_condNotEmpty;
    std::condition_variable m_condNotFull;
    std::queue<T> m_queue;
    const int m_capacity;
    bool m_shutdown;
};

#endif // CONCURRENT_QUEUE_HPP