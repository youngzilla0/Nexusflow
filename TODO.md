
# NexusFlow 项目开发路线图与待办事项

本文档记录了 NexusFlow 框架未来的功能增强、API优化和架构改进计划。

---

### 当前已知问题 (Current Issues)

-   [ ] **DiamondJoin 执行损耗仍然偏高**
    -   当前 `Join` 路径已经保证语义正确，`Linear vs DiamondJoin` 对照 benchmark 也已补齐。
    -   但在轻量 workload 下，`DiamondJoin` 的吞吐与调度开销相比线性链路仍有明显差距。
    -   下一步需要继续拆分 `join state`、ready signal、queue traffic 与 task reschedule 的具体损耗来源。

-   [ ] **轻量 workload 下 worker 扩展性不稳定**
    -   当前极轻量 passthrough benchmark 更偏好较低 worker 数，`2 workers` 往往优于 `4+ workers`。
    -   这说明当前调度策略在高并发但低计算密度场景下，仍会被线程协调成本放大。

-   [ ] **Queue 峰值深度统计仍需复核**
    -   过载 benchmark 中，`peak depth` 偶尔会比配置容量高 `1`。
    -   如果后续要把该指标作为严格容量约束对外说明，需要先确认统计口径或更新逻辑是否一致。

---

### 核心功能增强 (Core Features)

-   [ ] **实现优雅停机 (Graceful Shutdown / Draining)**
    -   **目标**: 在调用 `Pipeline::Stop()` 时，确保上游模块先停止生产数据，并让管道中正在流动的数据被下游模块完全处理完毕，避免数据丢失。
    -   **任务**:
        -   [ ] **拓扑顺序**: 确保 `Pipeline::Impl` 能够随时从 `Graph` 对象获取模块的拓扑排序列表。
        -   [ ] **停止逻辑**: 修改 `Pipeline::Stop()` 的实现，使其按照**拓扑顺序的逆序**来停止 `Worker`。先停止上游生产者，并等待其输入队列为空后，再逐级停止下游消费者。
        -   [ ] **Worker 协作**: `Worker::Stop()` 方法需要更精细的实现，以支持“排空”模式。

-   [ ] **优化 Join / DiamondJoin 的执行损耗**
    -   **目标**: 让 `DiamondJoin` 的额外开销尽量接近线性链路，优先保证语义正确，再逐步压低调度和同步损耗。
    -   **任务**:
        -   [ ] **补充基准**: 增加 `Linear` vs `DiamondJoin` 的对照 benchmark，覆盖不同深度、不同分支数和不同 payload 大小。
        -   [ ] **定位瓶颈**: 拆分 join 等待、队列唤醒、任务重排等开销，找出损耗主要来源。
        -   [ ] **优化策略**: 在不破坏 `OnAllInputs` 语义的前提下，优化 join 状态管理和调度路径。

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

-   [ ] **导出日志模块 API**
    -   **目标**: 允许用户在他们自己的模块实现中，使用框架统一的日志系统。
    -   **任务**:
        -   [ ] **创建公共头文件**: 创建一个新的头文件，例如 `include/nexusflow/logging.hpp`。
        -   [ ] **提供简洁的宏**: 在该头文件中，提供一组易于使用的日志宏，如 `NEXUSFLOW_LOG_INFO(...)`, `NEXUSFLOW_LOG_WARN(...)`。这些宏内部会调用您的日志系统实现（如 spdlog）。
        -   [ ] **隐藏实现**: 确保 `spdlog` 等第三方库的头文件不会被包含在公共的 `logging.hpp` 中，避免依赖泄露。
