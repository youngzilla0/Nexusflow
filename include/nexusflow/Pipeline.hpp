#ifndef NEXUSFLOW_PIPELINE_HPP
#define NEXUSFLOW_PIPELINE_HPP

#include <nexusflow/Config.hpp>
#include <nexusflow/ErrorCode.hpp>
#include <nexusflow/Module.hpp>

#include <memory>
#include <string>
#include <unordered_map>

// Forward declaration
class Graph;
namespace nexusflow {
class PipelineBuilder;
}

namespace nexusflow {

/**
 * @brief Pipeline configuration parameters.
 */
struct PipelineConfig {
    size_t maxBatchSize = 32;           // Max messages per batch in Worker
    size_t batchTimeoutMs = 5;           // Batch timeout in milliseconds
    size_t queueSize = 100;              // Queue capacity between modules

    // Factory method to create default config
    static PipelineConfig Default() { return PipelineConfig{}; }
};

/**
 * @brief Pipeline
 *
 */
class Pipeline {
public:
    static std::unique_ptr<Pipeline> CreateFromYaml(const std::string& configPath);

    ErrorCode Init();

    ErrorCode Start();

    ErrorCode Stop();

    ErrorCode DeInit();

    ~Pipeline();

private:
    friend PipelineBuilder;

    Pipeline();

    void InitWithGraph(std::unique_ptr<Graph> graph, const PipelineConfig& config);

    class Impl;
    std::unique_ptr<Impl> m_pImpl;
};

} // namespace nexusflow

#endif