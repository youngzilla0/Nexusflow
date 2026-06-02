#ifndef NEXUSFLOW_EXECUTOR_HPP
#define NEXUSFLOW_EXECUTOR_HPP

#include "ThreadPool.hpp"
#include "base/Define.hpp"
#include "common/ViewPtr.hpp"
#include "nexusflow/Message.hpp"
#include "utils/logging.hpp"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>

namespace nexusflow { namespace executor {

/**
 * @class Executor
 * @brief Internal class responsible for executing messages asynchronously.
 *
 * Executor receives messages from a Module and dispatches them to downstream queues.
 * It uses a ThreadPool for parallel fan-out dispatch. Module does NOT hold a reference
 * to Executor — instead Module calls through ModuleActor via a callback interface.
 *
 * This is an implementation detail of the framework and is not part of the public API.
 */
class Executor {
public:
    Executor();
    ~Executor();

    /**
     * @brief Adds a subscriber queue to receive dispatched messages.
     * @param name The name of the subscriber (typically "ModuleName -> OutputName").
     * @param queue The queue to add.
     */
    void AddSubscriber(const std::string& name, ViewPtr<MessageQueue> queue);

    /**
     * @brief Emits a message to all subscribers (broadcast).
     * @param msg The message to emit.
     * @param blocking If true, blocks until all subscribers receive the message; if false, queues for async dispatch.
     */
    void Emit(const Message& msg, bool blocking);

    /**
     * @brief Routes a message to a specific subscriber.
     * @param outputName The name of the subscriber to route to.
     * @param msg The message to route.
     * @param blocking If true, blocks until the subscriber receives the message; if false, queues for async dispatch.
     */
    void Route(const std::string& outputName, const Message& msg, bool blocking);

    /**
     * @brief Starts the Executor and its thread pool.
     */
    void Start();

    /**
     * @brief Stops the Executor and its thread pool.
     */
    void Stop();

private:
    // Dispatch task: fan-out to all subscribers via ThreadPool
    void DispatchTask(const Message& msg);

    std::unordered_map<std::string, ViewPtr<MessageQueue>> m_subscriberMap;

    // Thread pool for parallel dispatch (default: hardware concurrency)
    std::unique_ptr<ThreadPool> m_threadPool;
};

}} // namespace nexusflow::executor

#endif // NEXUSFLOW_EXECUTOR_HPP