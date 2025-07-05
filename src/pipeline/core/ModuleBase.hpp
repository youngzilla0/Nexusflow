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
     * @param isSourceModule A flag indicating whether this module is a source module.
     */
    explicit ModuleBase(std::string name, bool isSourceModule = false);

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

    bool isSourceModule() const { return m_isSourceModule; }

protected:
    // --- Pure virtual function for derived classes ---

    /**
     * @brief The core processing logic of the module.
     * This method is called repeatedly in the module's internal thread. The derived class
     * is responsible for popping data from input ports, processing it, and dispatching
     * results to output ports.
     */
    virtual void Process(const std::shared_ptr<Message>& inputMessage);

    // --- Helper methods for data I/O ---

    /**
     * @brief Selects a message from the input ports.
     * @return An std::shared_ptr to the selected message, or nullptr if no message is available.
     */
    std::shared_ptr<Message> selectMessage();

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
    bool m_isSourceModule = false;
    std::atomic<bool> m_stopFlag{false};
};