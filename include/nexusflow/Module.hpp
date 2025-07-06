#ifndef NEXUSFLOW_MODULE_HPP
#define NEXUSFLOW_MODULE_HPP

#include <nexusflow/ErrorCode.hpp>
#include <nexusflow/Message.hpp>

#include <memory>

// --- Forward Declarations ---
// Forward-declare internal and framework classes to keep this header clean.
namespace nexusflow { namespace dispatcher {
class Dispatcher;
}} // namespace nexusflow::dispatcher

namespace nexusflow {
class Pipeline;
}

namespace nexusflow {

// TODO: 去掉这个名称， 要考虑使用ModuleName还是ModuleType.
using ModuleName = std::string;

/**
 * @class Module
 * @brief An abstract base class for a processing unit within a data pipeline.
 *
 * A Module focuses exclusively on the business logic of "what to do with data."
 * It passively receives data through its `process` methods and sends results
 * via protected APIs. All threading, data I/O, and lifecycle management are
 * handled прозрачно by the framework.
 */
class Module {
public:
    /**
     * @brief Constructs a Module with a given name.
     * @param name A unique identifier for this module instance.
     */
    explicit Module(std::string name);

    /**
     * @brief Virtual destructor to ensure proper cleanup of derived classes.
     */
    virtual ~Module();

    // Modules are unique components and should not be copied or moved.
    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;

    // --- Public User Interface ---

    /**
     * @brief User-defined initialization logic.
     * Called by the framework once before the pipeline starts.
     * @return An ErrorCode indicating success or failure.
     */
    virtual ErrorCode Init();

    /**
     * @brief User-defined resource cleanup logic.
     * Called by the framework once after the pipeline has stopped.
     * @return An ErrorCode indicating success or failure.
     */
    virtual ErrorCode DeInit();

    /**
     * @brief The core processing logic for a single message.
     * @note Derived classes MUST implement this method.
     * @param inputMessage The message to be processed, passed in by the framework.
     */
    virtual void Process(const std::shared_ptr<Message>& inputMessage) = 0;

    /**
     * @brief The core processing logic for a batch of messages.
     * The framework calls this method by default. The base implementation
     * simply iterates through the batch and calls `process()` for each message.
     * Override this for more efficient batch-oriented processing.
     * @param inputBatchMessages A batch of messages to be processed.
     */
    virtual void ProcessBatch(const std::vector<std::shared_ptr<Message>>& inputBatchMessages);

    // --- Getter and Setter ---

    /**
     * @brief Gets the unique name of the module.
     * @return A const reference to the module's name.
     */
    const std::string& GetModuleName() const;

    /**
     * @brief Gets the internal handle for the module.
     */

    bool IsHandleValid() const;

    /**
     * @brief Is source module
     */
    bool IsSourceModule() const;

protected:
    // --- Protected API for Derived Classes ---

    /**
     * @brief Broadcasts a message to all connected downstream outputs.
     * @param msg The message to be sent.
     */
    void Broadcast(const std::shared_ptr<Message>& msg);

    /**
     * @brief Sends a message to a specific downstream output.
     * @param outputName The name of the output port to send the message to.
     * @param msg The message to be sent.
     */
    void SendTo(const std::string& outputName, const std::shared_ptr<Message>& msg);

    /**
     * @brief Gets the number of connected downstream outputs.
     * @return The count of output ports.
     */
    // size_t GetOutputCount() const;

private:
    // Grant Pipeline access to private members for internal setup.
    friend class Pipeline;

    // A private setter for the internal handle, callable only by the Pipeline.
    void SetDispatcher(const std::shared_ptr<dispatcher::Dispatcher>& dispatcher);

    std::string m_moduleName;

    // The internal dispatcher handle.
    std::shared_ptr<dispatcher::Dispatcher> m_dispatcherPtr;
};

} // namespace nexusflow

#endif // NEXUSFLOW_MODULE_HPP