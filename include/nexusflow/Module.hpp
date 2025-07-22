#ifndef NEXUSFLOW_MODULE_HPP
#define NEXUSFLOW_MODULE_HPP

#include <nexusflow/Config.hpp>
#include <nexusflow/ErrorCode.hpp>
#include <nexusflow/Message.hpp>
#include <nexusflow/ProcessingContext.hpp>
#include <nexusflow/TypeTraits.hpp>

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
    virtual ProcessStatus Process(ProcessingContext& context) = 0;

    virtual std::vector<ProcessStatus> ProcessBatch(std::vector<ProcessingContext>& inputBatchContexts);

    // --- Getter and Setter ---

    /**
     * @brief Gets the unique name of the module.
     * @return A const reference to the module's name.
     */
    const std::string& GetModuleName() const;

private:
    friend class ModuleActor;

    std::string m_moduleName;
};

} // namespace nexusflow

#endif // NEXUSFLOW_MODULE_HPP