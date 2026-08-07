# NexusFlow 迁移计划

这份文档用于说明：在参考 `ai-pipe` 时，哪些思路值得迁移到 NexusFlow，哪些内容只适合借鉴而不应直接照搬。

这里说的“迁移”仅指**迁移设计目标、测试方法和能力边界**，不指直接复制实现代码。

## 1. 迁移原则

### 1.1 只迁移思路，不复制实现

- 借鉴 `ai-pipe` 的 benchmark 组织方式、报告结构和性能目标。
- 借鉴 `fork-join`、`DiamondJoin`、可观测性和调度公平性的设计方向。
- 保持 NexusFlow 自己的命名、语义和运行时边界。

### 1.2 语义正确优先于微优化

- `OnAllInputs` 的拼接语义必须先稳定，再做性能压缩。
- `DiamondJoin` 的目标是“损耗可接受且可解释”，不是为了追求不安全的极限数字。
- 任何优化都不能破坏现有测试中的 join key、timeout、overflow eviction 语义。

### 1.3 报告数据必须来自 NexusFlow 自身

- 报告结构可以参考 `ai-pipe`。
- 结论和表格必须来自 NexusFlow 的 benchmark 与 runtime statistics。
- 不直接挪用 `ai-pipe` 的测试结论或性能数字。

## 2. 迁移边界

### 2.1 应该迁移的内容

#### Benchmark 模板

优先迁移以下测试主题：

- `Linear Depth Scaling`
- `Payload Size Scaling`
- `DiamondJoin vs Linear`
- `Worker Scaling`
- `P50 / P99 / Max Latency`
- `Long-run Stability`
- `Backpressure / Drop Rate / Queue Depth`

迁移目标：

- 让 benchmark 既能验证实现，也能直接产出对外报告素材。
- 同一主题在不同版本之间能稳定横向对比。

#### 可观测性口径

优先迁移以下统计思路：

- pipeline 级摘要
- node 级执行统计
- port 级入队、出队、丢弃、拒绝统计
- sink 端到端延迟分位数
- join pending group、timeout drop、overflow drop

迁移目标：

- 让 benchmark、example、日志和最终报告使用同一组统计口径。
- 报告中的每个数字都能追溯到框架内的统计接口。

#### 调度目标

优先迁移以下调度方向：

- 单 worker 下无饥饿、无超时
- 轻载 stream 场景下的可预测行为
- `fork-join` 路径上的调度公平性
- source actor 与普通 actor 的差异化处理

迁移目标：

- 调度策略从“线程数分档”逐步走向“拓扑感知 + 负载感知”。
- stream mode 的行为更容易解释、更容易调优。

#### Join / DiamondJoin 地位

优先迁移以下建模思路：

- `DiamondJoin` 不只是功能测试对象，而是核心拓扑能力。
- `Linear` 与 `Join` 应该并列做性能评测。
- `Join` 的额外损耗需要被明确拆解和解释。

迁移目标：

- 让 `fork-join` 成为一等公民。
- 让 `Join` 路径的正确性、稳定性和性能都可测量。

### 2.2 不建议直接迁移的内容

- `ai-pipe` 的具体类名和命名体系
- `ai-pipe` 的线程池实现细节
- 可能改变 `OnAllInputs` 语义的 join 优化实现
- 未经 NexusFlow 自己 benchmark 验证的性能结论

## 3. 优先级分层

## P0

这一层优先解决“能不能形成 NexusFlow 自己的稳定性能基线”。

### P0-1 统一性能报告模板

目标：

- 把现有 benchmark 输出整理成固定报告结构。
- 每一节统一采用“测试方法 -> 数据表 -> 分析 -> 结论”的格式。

对应文件：

- [docs/PERFORMANCE_REPORT.md](/Users/yang/Code/Nexusflow/docs/PERFORMANCE_REPORT.md)
- [docs/PERFORMANCE_REPORT_ZH.md](/Users/yang/Code/Nexusflow/docs/PERFORMANCE_REPORT_ZH.md)
- [benchmarks](/Users/yang/Code/Nexusflow/benchmarks)

### P0-2 补齐 Pipeline 级统计口径

目标：

- 固化 pipeline summary 的核心指标。
- 让 `P50 / P99 / Max / dropRate / rejectRate / sinkReceiveCount` 成为统一输出。

对应文件：

- [include/nexusflow/StatisticsTypes.hpp](/Users/yang/Code/Nexusflow/include/nexusflow/StatisticsTypes.hpp)
- [include/nexusflow/PipelineStatistics.hpp](/Users/yang/Code/Nexusflow/include/nexusflow/PipelineStatistics.hpp)
- [src/statistics/Statistics.hpp](/Users/yang/Code/Nexusflow/src/statistics/Statistics.hpp)
- [src/statistics/Statistics.cpp](/Users/yang/Code/Nexusflow/src/statistics/Statistics.cpp)
- [src/observability/PipelineStatistics.cpp](/Users/yang/Code/Nexusflow/src/observability/PipelineStatistics.cpp)

### P0-3 把 DiamondJoin 变成独立性能主题

目标：

- 让 `DiamondJoin vs Linear` 成为独立 benchmark 组。
- 用同一套消息规模、payload 和 worker 配置进行对照。

对应文件：

- [src/executor/Executor.cpp](/Users/yang/Code/Nexusflow/src/executor/Executor.cpp)
- [src/executor/JoinStateStore.hpp](/Users/yang/Code/Nexusflow/src/executor/JoinStateStore.hpp)
- [src/executor/JoinStateStore.cpp](/Users/yang/Code/Nexusflow/src/executor/JoinStateStore.cpp)
- [tests/TestPipelineRuntime.cpp](/Users/yang/Code/Nexusflow/tests/TestPipelineRuntime.cpp)
- [benchmarks](/Users/yang/Code/Nexusflow/benchmarks)

## P1

这一层优先解决“运行时行为是否足够稳定且可解释”。

### P1-1 调度策略升级为拓扑感知

目标：

- 不只按线程数切分策略。
- 显式考虑 source、join、fork-join group、轻载流式场景。

对应文件：

- [src/executor/SchedulingPolicy.hpp](/Users/yang/Code/Nexusflow/src/executor/SchedulingPolicy.hpp)
- [src/executor/SchedulingPolicy.cpp](/Users/yang/Code/Nexusflow/src/executor/SchedulingPolicy.cpp)
- [include/nexusflow/TopologyTypes.hpp](/Users/yang/Code/Nexusflow/include/nexusflow/TopologyTypes.hpp)

### P1-2 梳理 Join 状态机

目标：

- 把 `OnAllInputs` 的执行路径拆成更清晰的几个阶段：
  `收消息`、`拼组`、`淘汰`、`取完整组`、`执行 Process`。
- 为后续 join 优化建立更稳定的演进边界。

对应文件：

- [src/executor/JoinStateStore.hpp](/Users/yang/Code/Nexusflow/src/executor/JoinStateStore.hpp)
- [src/executor/JoinStateStore.cpp](/Users/yang/Code/Nexusflow/src/executor/JoinStateStore.cpp)
- [src/executor/Executor.cpp](/Users/yang/Code/Nexusflow/src/executor/Executor.cpp)

### P1-3 报告文案模板化

目标：

- 固定摘要措辞与指标释义。
- 避免每次写报告都从头组织文本。

对应文件：

- [docs/PERFORMANCE_REPORT.md](/Users/yang/Code/Nexusflow/docs/PERFORMANCE_REPORT.md)
- [docs/PERFORMANCE_REPORT_ZH.md](/Users/yang/Code/Nexusflow/docs/PERFORMANCE_REPORT_ZH.md)

## P2

这一层优先解决“在不破坏语义的前提下，把热点路径压得更低”。

### P2-1 Join 热路径优化

目标：

- 在保持 `OnAllInputs` 语义不变的前提下，降低 join 状态维护和协调损耗。
- 优先考虑状态布局、执行预算和调度触发点，而不是投机性的 ready-port 跳过扫描。

### P2-2 更细的流式负载优化

目标：

- 降低轻载 stream 场景下的线程协调开销。
- 让高 worker 数在轻量任务下的退化更可控。

### P2-3 Pipeline 与 Queue 的性能边界清晰化

目标：

- 明确区分“queue microbenchmark 很快”和“pipeline 实际运行路径很快”。
- 当运行时队列实现切换时，报告应同步更新边界说明。

## 4. 与当前代码的映射关系

### 4.1 已有基础

NexusFlow 当前已经具备以下迁移基础：

- `PipelineSummaryStats` 已能输出 pipeline 级摘要指标。
- `Statistics` 已覆盖 node / port / sink latency 的核心数据。
- `JoinStateStore` 已具备 `timeout` 与 `overflow` 的基本语义。
- `SchedulingPolicy` 已经具备单 worker 与多 worker 的策略分层。
- `GraphTopologyInfo` 已能识别基础 fork-join group。

### 4.2 当前主要差距

还需要补齐的重点有：

- `DiamondJoin` 与 `Linear` 的性能对照还不够系统。
- join 路径的额外损耗尚未被拆解成可解释指标。
- scheduler 还没有真正做到“拓扑感知”。
- benchmark 报告还没有完全固定成可发布模板。

## 5. 成功标准

当出现以下结果时，可以认为这份迁移计划已经真正落地：

- NexusFlow 能独立生成一份完整、可发布的性能报告。
- `Linear`、`DiamondJoin`、`Payload`、`Latency`、`Backpressure` 都有稳定 benchmark。
- pipeline 级、node 级、port 级统计口径一致。
- `OnAllInputs` 路径在正确性测试与性能测试下都稳定通过。
- 后续优化不再依赖“借用别人的结论”，而是基于 NexusFlow 自己的数据闭环推进。
