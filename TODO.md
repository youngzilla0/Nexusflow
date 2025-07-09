
# NexusFlow 项目开发路线图与待办事项

本文档记录了 NexusFlow 框架未来的功能增强、API优化和架构改进计划。

---

### 核心功能增强 (Core Features)

-   [ ] **支持模块自定义参数 (`params`)**
    -   **目标**: 允许用户在 YAML 配置文件中的 `params` 字段为模块指定自定义配置。
    -   **任务**:
        -   [ ] **YAML 解析**: 在 `GraphBuilder` 中，增加解析 `params` map 的逻辑。可以考虑将其解析为 `std::unordered_map<std::string, std::any>` (C++17) 或一个自定义的 `Variant` 类型 (C++14)。
        -   [ ] **模型扩展**: 在 `internal::FactoryNode` 结构体中增加一个成员变量来存储这些参数。
        -   [ ] **工厂扩展**: 修改 `ModuleFactory::CreateModule` 的接口，使其能够接收并传递这些参数。
        -   [ ] **模块API**: 在 `nexusflow::Module` 基类中增加一个新的虚方法 `virtual void configure(const ParamMap& params)`，该方法在模块实例被创建后、`Init()` 之前由框架调用。

-   [ ] **实现优雅停机 (Graceful Shutdown / Draining)**
    -   **目标**: 在调用 `Pipeline::Stop()` 时，确保上游模块先停止生产数据，并让管道中正在流动的数据被下游模块完全处理完毕，避免数据丢失。
    -   **任务**:
        -   [ ] **拓扑顺序**: 确保 `Pipeline::Impl` 能够随时从 `Graph` 对象获取模块的拓扑排序列表。
        -   [ ] **停止逻辑**: 修改 `Pipeline::Stop()` 的实现，使其按照**拓扑顺序的逆序**来停止 `Worker`。先停止上游生产者，并等待其输入队列为空后，再逐级停止下游消费者。
        -   [ ] **Worker 协作**: `Worker::Stop()` 方法需要更精细的实现，以支持“排空”模式。

---

### 架构与并发模型 (Architecture & Concurrency Model)

-   [ ] **设计并实现 `Message` 的并发安全策略**
    -   **目标**: 解决当一个消息通过 `broadcast` 被分享给多个并行模块时可能出现的数据竞争（Data Race）问题。
    -   **任务**:
        -   [ ] **研究与决策**: 评估并选择最适合本框架的并发模型。
            -   **方案A: 不可变消息 (Immutable Messages)**: 消息一旦创建即为只读。修改操作必须创建新的消息实例。这是最安全、逻辑最清晰的方案。
            -   **方案B: 写时复制 (Copy-on-Write, COW)**: 消息内部数据由 `shared_ptr` 管理。在只读访问时共享数据（零开销），在首次写入时自动进行深拷贝。性能与安全的良好平衡。
            -   **方案C: 架构约定**: 依赖文档和用户自律，约定被广播的消息应视为只读。实现最简单，但安全性最低。
        -   [ ] **实现所选策略**: 根据决策，修改 `nexusflow::Message` 类的实现。
        -   [ ] **更新文档**: 在文档中明确告知用户 `Message` 的线程安全模型和最佳实践。

---

### 监控与可观测性 (Monitoring & Observability)

-   [ ] **抽象监控与状态报告机制**
    -   **目标**: 提供一套机制来监控每个模块和队列的实时状态，便于调试和性能分析。
    -   **任务**:
        -   [ ] **定义指标 (Metrics)**: 确定需要监控的关键指标，例如：
            -   **模块层面**: 已处理消息数、处理错误数、平均处理延迟。
            -   **队列层面**: 当前队列长度、累计入队数、累计出队数（用于检测丢数据）。
        -   [ ] **设计 `MetricsCollector`**: 创建一个中心的监控收集器（可以是单例或由 `Pipeline` 持有），提供如 `incrementCounter(name)`, `setGauge(name, value)` 等接口。
        -   [ ] **集成到框架**:
            -   在 `Worker` 中，每次调用 `processBatch` 前后记录信息，并上报给 `MetricsCollector`。
            -   在 `Dispatcher` 中，每次 `send/broadcast` 时更新计数器。
            -   让 `ConcurrentQueue` 内部也持有原子计数器。
        -   [ ] **暴露接口 (Optional)**: 提供一种方式（如一个HTTP端点或日志定期打印）来查询和展示这些监控数据。

---

### API 优化与导出 (API Refinements)

-   [ ] **导出 `Message` 模块 API**
    -   **目标**: 让用户可以方便地创建和解包 `Message` 对象。
    -   **任务**:
        -   [ ] **文档完善**: 为 `Message` 类的所有公共方法（如 `getData<T>()`, 构造函数等）编写清晰的 Doxygen 注释。
        -   [ ] **确保路径正确**: 确保 `Message.hpp` 位于 `include/nexusflow/` 目录下，并被正确安装。

-   [ ] **导出日志模块 API**
    -   **目标**: 允许用户在他们自己的模块实现中，使用框架统一的日志系统。
    -   **任务**:
        -   [ ] **创建公共头文件**: 创建一个新的头文件，例如 `include/nexusflow/logging.hpp`。
        -   [ ] **提供简洁的宏**: 在该头文件中，提供一组易于使用的日志宏，如 `NEXUSFLOW_LOG_INFO(...)`, `NEXUSFLOW_LOG_WARN(...)`。这些宏内部会调用您的日志系统实现（如 spdlog）。
        -   [ ] **隐藏实现**: 确保 `spdlog` 等第三方库的头文件不会被包含在公共的 `logging.hpp` 中，避免依赖泄露。