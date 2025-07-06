# Modern C++ Data Processing Pipeline Framework

A flexible, high-performance C++ framework for building and running complex, real-time data processing pipelines. Designed with modern C++ practices, it enables developers to create decoupled, scalable, and observable systems configured dynamically via YAML.

## Core Concepts

The framework is built upon a few key architectural concepts inspired by the "Pipes and Filters" pattern:

*   **`ModuleBase`**: A generic, multi-threaded processing unit. This is the single base class for all custom logic. Conceptually, a module acts as a:
    *   **Source**: If it primarily generates data and has no inputs (e.g., `MockInputModule`).
    *   **Filter/Transformer**: If it receives data, processes it, and forwards results (e.g., `MockProcessModule`).
    *   **Sink**: If it terminates a data stream and has no outputs (e.g., `MockOutputModule`).

*   **`ConcurrentQueue`**: A thread-safe, bounded queue (a "Pipe") that connects modules. It supports efficient, timed, blocking operations and provides natural backpressure to prevent downstream services from being overwhelmed.

*   **`Pipeline`**: A manager for a specific data processing graph. It's responsible for the lifecycle (Init, Start, Stop) of all its modules and the connections between them.

*   **`PipelineManager`**: A singleton factory responsible for parsing YAML configuration files and constructing `Pipeline` instances. This decouples the pipeline's structure from the code.

## Key Features

-   **Dynamic Pipeline Creation**: Define entire pipeline graphs, including nodes, edges, and logical entry/exit points, completely within a YAML file.
-   **High Performance**:
    -   **Multi-threaded by Design**: Each module runs in a dedicated thread for maximum parallelism.
    -   **Efficient Batch Processing**: The core `tryReceiveBatch` mechanism minimizes per-message overhead. `ModuleBase` supports both batch and single-message processing interfaces.
    -   **CPU-Friendly**: Uses condition variables for timed waits, eliminating busy-looping and reducing CPU usage when idle.
-   **Backpressure Support**: Bounded `ConcurrentQueue`s automatically block producers when full, preventing memory exhaustion and creating a stable, self-regulating system.
-   **Decoupled & Extensible**: Modules are completely unaware of each other, communicating only through thread-safe queues. Adding a new custom processing module is as simple as inheriting from `ModuleBase`.

## Getting Started

### 1. Define a Custom Module

To create your own processing logic, inherit from `pipeline_core::ModuleBase` and override the pure virtual functions `ProcessBatch` and `Process`.

```cpp
// MockProcessModule.h
#include "core/ModuleBase.hpp"

class MockProcessModule : public pipeline_core::ModuleBase {
public:
    explicit MockProcessModule(const std::string& name) : ModuleBase(name) {}

protected:
    // For performance, implement your batch-aware logic here directly.
    void ProcessBatch(const std::vector<std::shared_ptr<Message>>& batch) override {
        // For simple cases, you can just loop and call the single-message handler.
        for (const auto& msg : batch) {
            this->Process(msg);
        }
    }

    // Your core processing logic for one message.
    void Process(const std::shared_ptr<Message>& msg) override {
        // e.g., perform some transformation...
        auto processed_msg = transform(msg);
        // dispatch to all connected downstream modules
        broadcast(processed_msg); 
    }
};
```

### 2. Configure the Pipeline (config.yaml)

Create a YAML file to define the graph structure. The framework parses this file to build the pipeline at runtime.

```yaml
# config.yaml
# Defines the overall graph properties
graph:
  name: "MockGraphSingleInputOutput"
  input: "InputNode"   # The logical entry point for external data
  output: "OutputNode"  # The logical exit point for final results

# Defines all processing units (modules)
node:
  - name: "InputNode"
    type: "MockInputModule"

  - name: "ProcessNode1"
    type: "MockProcessModule"

  - name: "ProcessNode2"
    type: "MockProcessModule"

  - name: "OutputNode"
    type: "MockOutputModule"

# Defines the connections (data flow) between nodes
edge:
  - src: "InputNode"
    dst: "ProcessNode1"

  - src: "InputNode"
    dst: "ProcessNode2"

  - src: "ProcessNode1"
    dst: "OutputNode"

  - src: "ProcessNode2"
    dst: "OutputNode"
```

### 3. Run the Pipeline

Your `main.cpp` is responsible for managing the pipeline's lifecycle.

```cpp
#include "helper/logging.hpp"  // src/helper/logging.hpp
#include "run/PipelineManager.hpp"  // src/pipeline/run/PipelineManager.hpp
#include <iostream>

int main() {
    // Initialize the global logger
    helper::logger::LoggerParam loggerParam;
    loggerParam.logLevel = helper::logger::LogLevel::TRACE;
    helper::logger::InitializeGlobalLogger(std::move(loggerParam));

    // Create and run the pipeline.
    try {
        auto& manager = pipeline_run::PipelineManager::GetInstance();
        auto pipeline = manager.CreatePipelineByYamlConfig("path/to/config.yaml");

        if (pipeline && pipeline->Init() == ErrorCode::SUCCESS) {
            pipeline->Start();
            
            std::cout << "Pipeline started. Press Enter to stop.\n";
            std::cin.get();

            pipeline->Stop();
            pipeline->DeInit();
        }
    } catch (const std::exception& e) {
        std::cerr << "An error occurred: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
```

## Roadmap & Future Work (TODO)

-   [ ] **Architectural Refinement: Specialized Module Roles**
    -   Introduce distinct base classes (`SourceModule`, `FilterModule`, `SinkModule`) inheriting from `ModuleBase`. This will create a strongly-typed framework, enabling intelligent, role-based operations.

-   [ ] **Graceful Shutdown**
    -   Implement an End-of-Stream (EOS) message type and logic. This will allow the pipeline to finish processing all in-flight data before shutting down, ensuring zero data loss. This depends on the role specialization above.

-   [ ] **Fault Tolerance**
    -   Create a failure-reporting mechanism (e.g., via callbacks) from `Module` to `Pipeline` to handle runtime exceptions gracefully and allow for pipeline-level recovery strategies.

-   [ ] **Centralized Metrics & Observability**
    -   Expose a `Pipeline::getMetrics()` endpoint to aggregate metrics (queue depth, throughput, latency) from all modules. This is key for monitoring, debugging, and performance tuning.

-   [ ] **Stateful Processing Patterns**
    -   Add helper classes or formal patterns for complex stateful operations like multi-stream joins and time-windowed aggregations, including robust timeout and memory management.

-   [ ] **Data Injection & Egress API**
    -   Implement clean, backpressure-aware `Pipeline` APIs (`FeedData`, `TryGetResult`) that interact with the logical `input` and `output` nodes defined in the YAML.