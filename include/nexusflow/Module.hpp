#ifndef NEXUSFLOW_MODULE_HPP
#define NEXUSFLOW_MODULE_HPP

#include <nexusflow/Define.hpp>
#include <nexusflow/ErrorCode.hpp>
#include <nexusflow/Message.hpp>
#include <nexusflow/TypeTraits.hpp>

#include <memory>
#include <unordered_map>

// --- Forward Declarations ---
// Forward-declare internal and framework classes to keep this header clean.
namespace nexusflow { namespace dispatcher {
class Dispatcher;
}} // namespace nexusflow::dispatcher

namespace nexusflow {
class Pipeline;
}

namespace nexusflow {

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
    virtual void Configure(const ConfigMap& cfgMap);

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
    virtual void Process(Message& inputMessage) = 0;

    /**
     * @brief The core processing logic for a batch of messages.
     * The framework calls this method by default. The base implementation
     * simply iterates through the batch and calls `process()` for each message.
     * Override this for more efficient batch-oriented processing.
     * @param inputBatchMessages A batch of messages to be processed.
     */
    virtual void ProcessBatch(std::vector<Message>& inputBatchMessages);

    // --- Getter and Setter ---

    /**
     * @brief Gets the unique name of the module.
     * @return A const reference to the module's name.
     */
    const std::string& GetModuleName() const;

protected:
    // --- Protected API for Derived Classes ---

    /**
     * @brief Broadcasts a message to all connected downstream outputs.
     * @param msg The message to be sent.
     */
    void Broadcast(const Message& msg);

    /**
     * @brief Sends a message to a specific downstream output.
     * @param outputName The name of the output port to send the message to.
     * @param msg The message to be sent.
     */
    void SendTo(const std::string& outputName, const Message& msg);

    /**
     * @brief Gets a configuration parameter with a default value if not found.
     * @tparam T The type of the configuration parameter, must be one of bool, int, uint32_t, float, double, or std::string.
     * @param params The parameter map to search.
     * @param key The key of the parameter to retrieve.
     * @param defaultValue The default value to return if the parameter is not found.
     * @return The value of the parameter if found, otherwise the default value.
     * @note This method is a template and can be used with various types.
     */
    template <typename T, std::enable_if_t<is_any_of<T, bool, int, uint32_t, float, double, std::string>::value, int> = 0>
    T GetConfigOrDefault(const nexusflow::ConfigMap& params, const std::string& key, const T& defaultValue) {
        auto it = params.find(key);
        if (it != params.end()) {
            if (auto* ptr = it->second.get<T>()) {
                return *ptr;
            }
        }
        return defaultValue;
    }

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