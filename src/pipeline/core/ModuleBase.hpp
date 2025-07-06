#pragma once

#include "ErrorCode.hpp"
#include "base/ConcurrentQueue.hpp" // Your thread-safe queue implementation
#include "base/Message.hpp"         // Your base message class
#include "base/Optional.hpp"
#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// Type alias for clarity and convenience.
using MessageQueue = ConcurrentQueue<std::shared_ptr<Message>>;

using ModuleName = std::string;

/**
 * @class Module
 * @brief An abstract base class for a processing unit (a "Filter") in a dataflow pipeline.
 *
 * Each module runs in its own thread and communicates with other modules asynchronously
 * via thread-safe message queues (the "Pipes"). It supports multiple named input
 * and output ports, allowing for complex graph topologies.
 */
class ModuleBase {
public:
    /**
     * @brief Constructs a Module with a given name.
     * @param name A unique identifier for this module instance.
     */
    explicit ModuleBase(std::string name);

    /**
     * @brief Virtual destructor to ensure proper cleanup of derived classes.
     * It also ensures the module's thread is stopped correctly.
     */
    virtual ~ModuleBase();

    // Modules are unique and manage jejich own thread, so they should not be copied or moved.
    ModuleBase(const ModuleBase&) = delete;
    ModuleBase& operator=(const ModuleBase&) = delete;

public:
    /**
     * @brief Initializes the module's resources.
     */
    virtual ErrorCode Init();

    /**
     * @brief Deinitializes the module's resources.
     */
    virtual ErrorCode DeInit();

    /**
     * @brief Starts the module's internal thread and begins processing.
     * The internal thread will repeatedly call the virtual `Process()` method.
     */
    void Start();

    /**
     * @brief Stops the module's thread gracefully.
     * It signals the processing loop to terminate and waits for the thread to finish.
     */
    void Stop();

    // --- Port Management ---

    /**
     * @brief Registers an input port with a specific name.
     * An input port is essentially an incoming message queue.
     * @param portName The name of the input port (e.g., "video_in", "control_in").
     * @param queue A shared pointer to the message queue that will serve as the pipe.
     */
    void addInputPort(const std::string& portName, const std::shared_ptr<MessageQueue>& queue);

    /**
     * @brief Registers an output port with a specific name.
     * An output port is a reference to a downstream module's input queue.
     * @param portName The name of the output port (e.g., "detections_out", "anomalies_out").
     * @param queue A shared pointer to the downstream message queue.
     */
    void addOutputPort(const std::string& portName, const std::shared_ptr<MessageQueue>& queue);

    // --- Accessors ---
    const std::string& getName() const { return m_name; }

    bool isSourceModule() const { return m_inputQueueMap.empty(); }

    bool isSinkModule() const { return m_outputQueueMap.empty(); }

protected:
    // --- Pure virtual function for derived classes ---

    /**
     * @brief The core processing logic of the module.
     * This method is called repeatedly in the module's internal thread. The derived class
     * is responsible for popping data from input ports, processing it, and dispatching
     * results to output ports.
     */
    virtual void Process(const std::shared_ptr<Message>& inputMessage) = 0;

    /**
     * @brief Processes a batch of messages.
     * This method is called when the module receives a batch of messages from its input ports.
     * The derived class is responsible for processing the entire batch.
     * @param inputBatch A vector of shared pointers to the input messages.
     */
    virtual void ProcessBatch(const std::vector<std::shared_ptr<Message>>& inputBatch);

    // --- Helper methods for data I/O ---

    /**
     * @brief Collects a batch of messages from all registered input ports.
     *
     * @details This function implements an efficient, two-phase strategy to gather messages
     * without causing busy-waiting on the CPU.
     *
     * 1.  **Greedy Phase:** It first performs a quick, non-blocking poll (`tryPop`) across all
     *     input queues to immediately collect any readily available messages.
     * 2.  **Blocking Poll Phase:** If the batch is not yet full, it enters a loop that
     *     iterates through the input queues. For each queue, it performs a short-duration
     *     blocking wait (`waitAndPopFor`). This allows the thread to sleep efficiently
     *     if no messages are available, yielding the CPU, yet remaining highly responsive.
     *
     * The function returns when one of the following conditions is met:
     * - The batch reaches the `maxBatchSize`.
     * - The `totalTimeout` is exceeded.
     * - The module's stop flag is signaled.
     *
     * @param maxBatchSize The maximum number of messages to collect in a single batch.
     * @param totalTimeout The maximum time to wait for a batch to be filled.
     * @return A vector of shared pointers to the collected messages.
     */
    std::vector<std::shared_ptr<Message>> tryReceiveBatch(size_t maxBatchSize, std::chrono::milliseconds totalTimeout);

    /**
     * @brief Performs a non-blocking pop from a named input port.
     * @param portName The name of the input port to pop from.
     * @return An std::optional containing the message if one was available, otherwise std::nullopt.
     */
    Optional<std::shared_ptr<Message>> popFrom(const std::string& portName);

    /**
     * @brief Dispatches a message to a specific named output port.
     * @param portName The name of the output port to send to.
     * @param msg The message to dispatch.
     */
    void dispatchTo(const std::string& portName, const std::shared_ptr<Message>& msg);

    /**
     * @brief Broadcasts a message to all registered output ports.
     * @param msg The message to broadcast. A shared_ptr is used to efficiently
     *            share the message data across multiple downstream queues.
     */
    void broadcast(std::shared_ptr<Message> msg);

private:
    /**
     * @brief The main loop for the module's thread.
     */
    void Run();

protected:
    std::string m_name;

private:
    // Named ports for managing input and output queues.
    std::unordered_map<std::string, std::shared_ptr<MessageQueue>> m_inputQueueMap;
    std::unordered_map<std::string, std::shared_ptr<MessageQueue>> m_outputQueueMap;

    std::thread m_thread;
    std::atomic<bool> m_stopFlag{false};
};