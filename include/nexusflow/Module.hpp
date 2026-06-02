#ifndef NEXUSFLOW_MODULE_HPP
#define NEXUSFLOW_MODULE_HPP

#include <nexusflow/Config.hpp>
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

    // --- Lifecycle ---
    virtual ErrorCode Configure(const Config& config);

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

    // --- Processing ---

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

    /**
     * @brief Whether this module requires synchronized multi-input fusion.
     * @details When true, the Worker will wait for all input streams to have
     * a message with the same messageId before processing (Fusion mode).
     * Override in subclasses that need graph-style multi-input join.
     * @return True if sync-input fusion is required (default: false).
     */
    /**
     * @brief Whether this module requires multi-input join (fusion).
     * @details When true, the Worker will wait for all input streams to have
     * a message with the same messageId before processing (Join mode).
     * Override in subclasses that need graph-style multi-input join.
     * @return True if multi-input join is required (default: false).
     */
    virtual bool JoinInputs() const {
        // Note: m_joinMode is set by PipelineBuilder.ApplyTopologyJoin for converge nodes.
        // Diamond Sink is a converge node with 2 inputs, so ApplyTopologyJoin sets m_joinMode=true.
        // However, Diamond does NOT need fusion join (Pass1 and Pass2 are independent fan-out).
        // The join is needed only when outputs from MULTIPLE modules need to be synchronized
        // (e.g., HeadDet + PedDet both need to complete before FusionModule).
        // So we only check m_joinHint here, NOT m_joinMode.
        return m_joinHint == JoinHint::AlwaysJoin;
    }

    /**
     * @brief Framework hint for multi-input join mode.
     * @details Auto = framework decides based on topology (default).
     * AlwaysJoin = force join mode even if single input.
     * NeverJoin = disable join mode even if multiple inputs converge.
     */
    enum class JoinHint { Auto, AlwaysJoin, NeverJoin };

    /**
     * @brief Gets the join hint.
     * @return The JoinHint for this module.
     */
    JoinHint GetJoinHint() const { return m_joinHint; }

    /**
     * @brief Sets the join hint (called by Pipeline during topology analysis).
     * @param hint The JoinHint to set.
     */
    void SetJoinHint(JoinHint hint) { m_joinHint = hint; }

    /**
     * @brief Sets the join mode (called by Pipeline during topology analysis).
     * @param mode True to enable join mode, false to disable.
     */
    void SetJoinMode(bool mode) { m_joinMode = mode; }

    /**
     * @brief Gets the current join mode.
     * @return True if join mode is enabled.
     */
    bool GetJoinMode() const { return m_joinMode; }

    /**
     * @brief Gets the unique name of the module.
     * @return A const reference to the module's name.
     */
    const std::string& GetModuleName() const { return m_moduleName; }

protected:
    // --- Protected API for Derived Classes ---

    /**
     * @brief Broadcasts a message to all connected downstream outputs.
     * @param msg The message to be sent.
     * @param blocking If true, blocks until all subscribers receive the message; if false, uses non-blocking tryPush (default: true).
     */
    void Broadcast(const Message& msg, bool blocking = true);

    /**
     * @brief Sends a message to a specific downstream output.
     * @param outputName The name of the output port to send the message to.
     * @param msg The message to be sent.
     * @param blocking If true, blocks until the message is sent; if false, uses non-blocking tryPush (default: true).
     */
    void SendTo(const std::string& outputName, const Message& msg, bool blocking = true);

private:
    friend class ModuleActor;

    // A private setter for the internal handle, callable only by the Pipeline.
    void SetDispatcher(const std::shared_ptr<dispatcher::Dispatcher>& dispatcher);

    std::string m_moduleName;

    // The internal dispatcher handle.
    std::shared_ptr<dispatcher::Dispatcher> m_dispatcherPtr;

    // --- Join mode (set by Pipeline during topology analysis) ---
    bool m_joinMode = false;              // Pipeline-set join mode flag
    JoinHint m_joinHint = JoinHint::Auto; // Join hint (user or pipeline-set)
};

} // namespace nexusflow

#endif // NEXUSFLOW_MODULE_HPP