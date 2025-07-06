# NexusFlow: 现代C++高性能数据流管道框架

[![构建状态](https://img.shields.io/badge/build-passing-brightgreen)](https://github.com/your_username/nexusflow)
[![许可证: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++版本](https://img.shields.io/badge/C++-14%2B-blue.svg)]()

**NexusFlow** 是一个为现代C++设计的高性能、高解耦、可动态配置的数据流管道（Pipeline）框架。它旨在帮助开发者轻松构建复杂的多阶段数据处理任务，例如：视频分析流水线、实时数据ETL、工业传感器数据处理等。

框架的核心思想是将复杂的处理流程分解为一系列独立的**模块（Module）**，然后像搭建乐高积木一样，通过简单的配置将它们连接成一个强大的**有向无环图（DAG）**。

## 核心特性

*   **模块化设计，高度解耦**: 每个模块只关注自身的业务逻辑。模块之间通过异步消息队列进行通信，完全不知道彼此的存在。
*   **动态配置，灵活强大**: 使用简单的 YAML 文件即可定义整个数据管道的拓扑结构、模块类型和参数，无需重新编译代码即可调整和重构复杂的业务流程。
*   **自动并发，简化开发**: 框架自动为每个模块分配独立的线程进行驱动。您只需专注于业务逻辑，无需手动管理复杂的线程同步和生命周期。
*   **清晰的API，易于使用**: 提供两种管道构建方式——用于快速开发和测试的**程序化构建（`PipelineBuilder`）**和用于生产环境的**声明式构建（从YAML文件）**。
*   **高性能与现代C++**: 核心组件为性能而设计，充分利用移动语义等现代C++特性，并提供清晰的所有权模型，确保代码既高效又安全。

---

## 快速开始

下面的示例将演示如何构建一个拥有4个模块的管道：一个输入节点将数据分发给两个并行的处理节点，最后由一个输出节点汇总结果。

### 方式一：基于 YAML 的声明式构建 (推荐)

这是 NexusFlow 最强大和推荐的使用方式。它允许您通过配置文件来定义整个系统，极大地提高了灵活性。

#### 1. 编写配置文件 (`graph.yaml`)

```yaml
graph:
  name: "SimpleAnalyticsPipeline"

  modules:
    - name: "InputNode"          # 模块的唯一实例名
      class: "MockInputModule"   # 在模块工厂中注册的类名

    - name: "ProcessNode1"
      class: "MockProcessModule"

    - name: "ProcessNode2"
      class: "MockProcessModule"

    - name: "OutputNode"
      class: "MockOutputModule"

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

#### 2. 编写 C++ 入口程序 (`example.cpp`)

```cpp
#include "nexusflow/Pipeline.hpp"
#include "nexusflow/ModuleFactory.hpp"
#include "my_module/MockInputModule.hpp"    // 包含您的自定义模块头文件
#include "my_module/MockProcessModule.hpp"
#include "my_module/MockOutputModule.hpp"
#include "utils/logging.hpp"               // 您的日志工具

#include <iostream>
#include <chrono>
#include <thread>
#include <stdexcept>
#include <memory>

using namespace nexusflow;

// 在程序启动时，将所有自定义模块注册到工厂
void registerAllModules() {
    auto& factory = ModuleFactory::GetInstance();
    factory.Register<MockInputModule>("MockInputModule");
    factory.Register<MockProcessModule>("MockProcessModule");
    factory.Register<MockOutputModule>("MockOutputModule");
}

// 封装运行和清理逻辑的辅助函数
void executePipeline(Pipeline& pipeline) {
    LOG_INFO("正在初始化管道...");
    if (pipeline.Init() != ErrorCode::SUCCESS) {
        throw std::runtime_error("管道初始化失败。");
    }

    LOG_INFO("管道启动...");
    pipeline.Start();

    LOG_INFO("管道正在运行，持续10秒...");
    std::this_thread::sleep_for(std::chrono::seconds(10));

    LOG_INFO("管道停止中...");
    pipeline.Stop();

    LOG_INFO("管道反初始化...");
    pipeline.DeInit();
}

int main(int argc, char* argv[]) {
    utils::logger::InitializeGlobalLogger(/* ... */); // 初始化日志

    if (argc < 2) {
        LOG_ERROR("使用方式: {} <graph.yaml的路径>", argv[0]);
        return 1;
    }

    try {
        // 1. 注册所有自定义模块
        registerAllModules();

        // 2. 从 YAML 文件创建管道
        std::string configPath = argv[1];
        LOG_INFO("正在从 '{}' 创建管道...", configPath);
        
        // Pipeline 类提供一个静态工厂方法来从配置创建实例
        auto pipeline = Pipeline::CreateFromYaml(configPath);
        if (pipeline == nullptr) {
            throw std::runtime_error("从YAML配置文件创建管道失败。");
        }

        // 3. 执行管道
        executePipeline(*pipeline);

    } catch (const std::exception& e) {
        LOG_CRITICAL("程序发生异常: {}", e.what());
        return 1;
    }

    LOG_INFO("程序执行完毕。");
    return 0;
}
```

### 方式二：使用 `PipelineBuilder` 进行程序化构建

这种方式适用于简单的应用、单元测试或需要用代码动态生成拓扑的场景。

```cpp
#include "nexusflow/Pipeline.hpp"
#include "nexusflow/PipelineBuilder.hpp"
#include "my_module/MockInputModule.hpp"
#include "my_module/MockProcessModule.hpp"
#include "my_module/MockOutputModule.hpp"
// ... 其他必要的头文件 ...

using namespace nexusflow;

void runWithBuildModule() {
    // 1. 手动创建模块实例
    auto inputModule    = std::make_shared<MockInputModule>("InputNode");
    auto process1Module = std::make_shared<MockProcessModule>("ProcessNode1");
    auto process2Module = std::make_shared<MockProcessModule>("ProcessNode2");
    auto outputModule   = std::make_shared<MockOutputModule>("OutputNode");

    // 2. 使用 PipelineBuilder 链式调用来定义拓扑
    auto pipeline = PipelineBuilder()
                        .AddModule(inputModule)
                        .AddModule(process1Module)
                        .AddModule(process2Module)
                        .AddModule(outputModule)
                        .Connect("InputNode", "ProcessNode1")
                        .Connect("InputNode", "ProcessNode2")
                        .Connect("ProcessNode1", "OutputNode")
                        .Connect("ProcessNode2", "OutputNode")
                        .Build();

    if (pipeline == nullptr) {
        throw std::runtime_error("使用Builder构建管道失败。");
    }
    
    // 3. 调用前面定义的 executePipeline(*pipeline) 辅助函数来运行
    executePipeline(*pipeline);
}
```

---

## 如何编写自定义模块

编写一个新模块非常简单，只需两步：

#### 1. 继承 `nexusflow::Module` 并实现 `process`

```cpp
// modules/MultiplierModule.hpp
#include "nexusflow/Module.hpp"
#include "nexusflow/Message.hpp" // 假设 Message 可以包裹一个 int

class MultiplierModule : public nexusflow::Module {
public:
    MultiplierModule(std::string name) : nexusflow::Module(std::move(name)) {}

    // 实现核心处理逻辑
    void Process(nexusflow::Message& msg) override {
        if (auto* data = msg.getData<int>()) {
            // 将收到的数字乘以 2
            int result_value = (*data) * 2;

            // 创建一个新的消息并广播到下游
            Message result_msg(result_value);
            Broadcast(std::move(result_msg));
        }
    }
};
```

#### 2. 在主程序中将其注册到 `ModuleFactory`

```cpp
// main.cpp
void registerAllModules() {
    auto& factory = nexusflow::ModuleFactory::getInstance();
    factory.Register<MultiplierModule>("MultiplierModule");
    // ... 注册其他模块
}
```
完成！现在您就可以在您的 `graph.yaml` 文件中使用 `class: "MultiplierModule"` 了。

## 构建项目

本项目使用 CMake 进行构建。

```bash
# 1. 创建并进入构建目录
mkdir build && cd build

# 2. 运行 CMake 来配置项目
# 假设依赖项（如 spdlog, yaml-cpp）由 CMake 的 FetchContent 管理
cmake ..

# 3. 编译项目 (使用所有可用的CPU核心)
make -j$(nproc)

# 4. 运行示例
./examples/example path/to/your/graph.yaml
```

## 贡献

欢迎任何形式的贡献！如果您有好的想法、建议或发现了Bug，请随时提交 Pull Request 或创建 Issue。

## 许可证

本项目采用 **MIT** 许可证。详情请见 [LICENSE](LICENSE) 文件。