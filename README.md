# NexusFlow: A High-Performance, Modern C++ Dataflow Pipeline Framework

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)](https://github.com/your_username/nexusflow)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++_Version](https://img.shields.io/badge/C++-14%2B-blue.svg)]()

**NexusFlow** is a high-performance, highly-decoupled, and dynamically configurable dataflow pipeline framework designed for modern C++. It aims to help developers easily build complex, multi-stage data processing tasks, such as video analytics pipelines, real-time ETL, industrial sensor data processing, and more.

The core idea of the framework is to decompose complex processing workflows into a series of independent **Modules**, which can then be connected like LEGO bricks into a powerful **Directed Acyclic Graph (DAG)** using simple configurations.

## Core Features

*   **Modular Design, Highly Decoupled**: Each module focuses exclusively on its own business logic. Modules communicate asynchronously via message queues, remaining completely unaware of each other's existence.
*   **Dynamic Configuration, Powerful & Flexible**: Use simple YAML files to define the entire pipeline topology, module types, and parameters. Reconfigure and refactor complex business flows without recompiling your code.
*   **Automatic Concurrency, Simplified Development**: The framework automatically assigns dedicated threads to drive each module (or uses a defined strategy). You can focus on your business logic without manually managing complex thread synchronization and lifecycles.
*   **Clean API, Easy to Use**: Provides two primary ways to construct a pipeline: a **programmatic approach (`PipelineBuilder`)** for rapid development and testing, and a **declarative approach (from YAML)** for production environments.
*   **High-Performance & Modern C++**: Core components are designed for performance, leveraging modern C++ features like move semantics and a clear ownership model to ensure code is both efficient and safe.

---

## Quick Start

The following example demonstrates how to build a pipeline with four modules: one input node distributes data to two parallel processing nodes, and one output node gathers the results.

### Option 1: Declarative Build via YAML (Recommended)

This is the most powerful and recommended way to use NexusFlow. It allows you to define your entire system through a configuration file, providing maximum flexibility.

#### 1. Create a Configuration File (`graph.yaml`)

```yaml
graph:
  name: "VideoAnalyticsPipeline"

  # 1. Define all module instances
  modules:
    - name: "InputNode"          # A unique instance name for the module
      class: "MockInputModule"   # The class name registered in the ModuleFactory

    - name: "ProcessNode1"
      class: "MockProcessModule"

    - name: "ProcessNode2"
      class: "MockProcessModule"

    - name: "OutputNode"
      class: "MockOutputModule"

  # 2. Define the connections (the dataflow topology)
  connections:
    - from: "InputNode"
      to: "ProcessNode1"
    - from: "InputNode"
      to: "ProcessNode2"
    - from: "ProcessNode1"
      to: "OutputNode"
    - from: "ProcessNode2"
      to: "OutputNode"
```

#### 2. Write the C++ Entry Point (`example.cpp`)

```cpp
#include "nexusflow/Pipeline.hpp"
#include "nexusflow/ModuleFactory.hpp"
#include "my_module/MockInputModule.hpp"    // Include your custom module headers
#include "my_module/MockProcessModule.hpp"
#include "my_module/MockOutputModule.hpp"
#include "utils/logging.hpp"               // Your logging utility

#include <iostream>
#include <chrono>
#include <thread>
#include <stdexcept>
#include <memory>

using namespace nexusflow;

// Register all custom modules with the factory at application startup.
void registerAllModules() {
    auto& factory = ModuleFactory::GetInstance();
    factory.Register<MockInputModule>("MockInputModule");
    factory.Register<MockProcessModule>("MockProcessModule");
    factory.Register<MockOutputModule>("MockOutputModule");
}

// A helper function to encapsulate the pipeline execution and teardown logic.
void executePipeline(Pipeline& pipeline) {
    LOG_INFO("Initializing pipeline...");
    if (pipeline.Init() != ErrorCode::SUCCESS) {
        throw std::runtime_error("Pipeline initialization failed.");
    }

    LOG_INFO("Pipeline starting...");
    pipeline.Start();

    LOG_INFO("Pipeline running for 10 seconds...");
    std::this_thread::sleep_for(std::chrono::seconds(10));

    LOG_INFO("Pipeline stopping...");
    pipeline.Stop();

    LOG_INFO("De-initializing pipeline...");
    pipeline.DeInit();
}

int main(int argc, char* argv[]) {
    utils::logger::InitializeGlobalLogger(/* ... */);

    if (argc < 2) {
        LOG_ERROR("Usage: {} <path_to_graph_yaml>", argv[0]);
        return 1;
    }

    try {
        // 1. Register modules with the factory.
        registerAllModules();

        // 2. Create the pipeline from the YAML file.
        std::string configPath = argv[1];
        LOG_INFO("Creating pipeline from '{}'...", configPath);
        
        // The Pipeline class provides a static factory method as a replacement for a PipelineManager.
        auto pipeline = Pipeline::CreateFromYaml(configPath);
        if (pipeline == nullptr) {
            throw std::runtime_error("Failed to create pipeline from YAML config.");
        }

        // 3. Execute the pipeline.
        executePipeline(*pipeline);

    } catch (const std::exception& e) {
        LOG_CRITICAL("An exception occurred: {}", e.what());
        return 1;
    }

    LOG_INFO("Execution finished successfully.");
    return 0;
}
```

### Option 2: Programmatic Build via `PipelineBuilder`

This approach is suitable for simple applications, unit tests, or scenarios where the topology needs to be generated dynamically in code.

```cpp
#include "nexusflow/Pipeline.hpp"
#include "nexusflow/PipelineBuilder.hpp"
#include "my_module/MockInputModule.hpp"
#include "my_module/MockProcessModule.hpp"
#include "my_module/MockOutputModule.hpp"
// ... other necessary headers ...

using namespace nexusflow;

void runWithBuildModule() {
    auto inputModule    = std::make_shared<MockInputModule>("InputNode");
    auto process1Module = std::make_shared<MockProcessModule>("ProcessNode1");
    auto process2Module = std::make_shared<MockProcessModule>("ProcessNode2");
    auto outputModule   = std::make_shared<MockOutputModule>("OutputNode");

    auto pipeline = PipelineBuilder()
                        .addModule(inputModule)
                        .addModule(process1Module)
                        .addModule(process2Module)
                        .addModule(outputModule)
                        .connect("InputNode", "ProcessNode1")
                        .connect("InputNode", "ProcessNode2")
                        .connect("ProcessNode1", "OutputNode")
                        .connect("ProcessNode2", "OutputNode")
                        .build();

    if (pipeline == nullptr) {
        throw std::runtime_error("Failed to build pipeline.");
    }
    
    // Call the same helper function to run the pipeline.
    executePipeline(*pipeline);
}
```

---

## How to Write a Custom Module

Creating a new module is a simple two-step process:

#### 1. Inherit from `nexusflow::Module` and implement `process`

```cpp
// modules/MultiplierModule.hpp
#include "nexusflow/Module.hpp"
#include "nexusflow/Message.hpp" // Assuming Message can wrap an int

class MultiplierModule : public nexusflow::Module {
public:
    MultiplierModule(std::string name) : nexusflow::Module(std::move(name)) {}

    // Implement the core processing logic.
    void process(nexusflow::Message& msg) override {
        if (auto* data = msg.getData<int>()) {
            // Multiply the received number by 2.
            int result_value = (*data) * 2;

            // Create a new message and broadcast it downstream.
            Message result_msg(result_value);
            broadcast(std::move(result_msg));
        }
    }
};
```

#### 2. Register it with the `ModuleFactory` in your application

```cpp
// main.cpp
void registerAllModules() {
    auto& factory = nexusflow::ModuleFactory::getInstance();
    factory.Register<MultiplierModule>("MultiplierModule");
    // ... register other modules
}
```
That's it! You can now use `class: "MultiplierModule"` in your `graph.yaml` file.

## Building the Project

This project uses CMake for building.

```bash
# 1. Create a build directory
mkdir build
cd build

# 2. Run CMake to configure the project
# Assumes dependencies (like spdlog, yaml-cpp) are managed by CMake's FetchContent
cmake ..

# 3. Compile the project
make -j$(nproc)

# 4. Run the example
./examples/nexusflow_yaml_demo path/to/your/graph.yaml
```

## Contributing

Contributions of any kind are welcome! If you have ideas, suggestions, or have found a bug, please feel free to open a Pull Request or create an Issue.

## License

This project is licensed under the **MIT License**. See the [LICENSE](LICENSE) file for details.