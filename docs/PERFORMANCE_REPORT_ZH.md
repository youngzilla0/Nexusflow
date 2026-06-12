# NexusFlow 性能基准测试报告

> 状态：草稿版，基于当前基准测试集合与一次本地报告矩阵运行结果；数据已包含单 worker 调度修复后的结果。
> 本报告仅描述 NexusFlow 当前的真实运行时行为，不借用其他 pipeline 框架的结论。

## 测试环境

| 项目 | 值 |
|------|----|
| 项目 | NexusFlow |
| 构建类型 | Release, C++14 |
| 基准测试框架 | Google Benchmark |
| 测试命令 | `build/benchmarks/nexusflow_benchmarks` |
| 采样日期 | 2026-06-10 |
| 备注 | 本地运行时 CPU 频率元数据不可用，线程亲和性设置失败；因此绝对数值应视为环境相关，趋势更有参考意义。 |

## 1. 执行摘要

NexusFlow 当前运行时通过共享 `Executor` 与 work-stealing `ThreadPool` 调度各个 module actor；pipeline 边上的运行时队列仍使用 `LockBaseQueue<Message>`。代码中已经有独立的实验性 lock-free queue 基准测试，但它还没有接入 pipeline 的实际运行路径。

| 维度 | 当前结果 | 评估 |
|------|----------|------|
| Message 拷贝/广播 | COW `Message` 的 vector payload 拷贝约 13.8 ns；广播时的句柄拷贝约 145 ns。 | Message 包装层大概率不是主要瓶颈。 |
| Pipeline 深度扩展 | 20K 条消息、1 KiB 共享 payload 的线性 passthrough 从 depth 1 的 15.7 ms 增至 depth 32 的 50.8 ms。 | 深度开销可见，但相对节点数增长仍是亚线性的。 |
| Worker 扩展 | 8 级线性 benchmark 在 1/2/4/8/16 workers 下均完成，本次测试峰值出现在 2 workers。 | 单 worker timeout 已修复；极轻量 passthrough 仍偏好较低 worker 数。 |
| Fan-out / Join 拓扑 | Diamond join 在 2/4/8/16 分支下均成功交付 10K 条合并结果，零丢弃。 | Join 路径功能正确；分支数增加会提高协调成本。 |
| 非阻塞过载行为 | Queue capacity overload benchmark 展示了较高的 drop count 与受限的队列深度。 | 背压/drop 统计清晰可观测，也便于测试。 |
| 队列路径 | Pipeline 使用 `LockBaseQueue<Message>`；lock-free queue 当前只在独立 microbenchmark 中测试。 | 这是对外报告时必须说明的边界。 |
| 可观测性 | 端口级与节点级统计已支持 enqueue/dequeue/drop/reject 与 join 状态；benchmark sink 也已支持 latency percentile 统计。 | 足够支撑报告级基准分析，但运行时内置 histogram 仍未实现。 |

## 2. 被测架构

NexusFlow 是一个 C++ dataflow pipeline 框架，module 之间以 DAG 方式连接。每条边拥有一个消息队列，每个 module 会注册为 actor，共享 executor 将这些 actor task 调度到线程池执行。

核心运行时组件：

| 组件 | 职责 |
|------|------|
| `Pipeline` | 对外生命周期 API：`Init()` -> `Start()` -> `Stop()` -> `DeInit()`。 |
| `Graph` | 存储 module 拓扑与边元数据。 |
| `Module` | 用户自定义业务逻辑单元，通过 `PortInputsView` 处理输入，通过 `PortOutputs` 发射输出。 |
| `Executor` | 持有 actor 运行时状态，负责分发消息、统计端口与 actor 计数，并调度 actor task。 |
| `ThreadPool` | Executor 使用的 work-stealing 调度器。 |
| `Message` | Type-erased 的 COW payload 容器，共享句柄拷贝成本较低。 |
| `MessageQueue` | 当前在 pipeline runtime 中映射为 `LockBaseQueue<Message>`。 |

## 3. 当前 Benchmark 覆盖

当前 benchmark 可执行文件覆盖如下：

| Benchmark 组 | 覆盖内容 |
|--------------|----------|
| `BenchmarkMessage.cpp` | Message 构造、COW 拷贝、广播句柄拷贝、typed access 与 mutation。 |
| `BenchmarkLockBaseQueue.cpp` | 当前正式运行路径所使用的 lock-based queue microbenchmark。 |
| `BenchmarkLockFreeQueue.cpp` | 实验性 lock-free MPMC 与 node queue microbenchmark。 |
| `BenchmarkPipelineBaseline.cpp` | baseline single-path、linear、diamond blocking 与 diamond non-blocking throughput。 |
| `BenchmarkPipelineDepth.cpp` | timestamp 与 1 KiB 共享 payload 的 linear depth scaling。 |
| `BenchmarkPipelineLatency.cpp` | 按 linear depth、payload type 与 diamond join branch count 统计端到端 latency percentiles。 |
| `BenchmarkPipelineScaling.cpp` | worker scaling 与 diamond join branch scaling。 |
| `BenchmarkPipelineBackpressure.cpp` | 非阻塞过载下的 queue capacity 行为。 |
| `BenchmarkPipelinePayload.cpp` | 共享 payload 句柄下的 payload size 敏感度。 |

测试套件同时覆盖 drop policy、queue statistics、join group limit、observer aggregation 以及 pipeline executor 行为。

## 4. Smoke Benchmark 结果

以下数据来自一次本地报告矩阵运行。它们更适合用于趋势讨论与报告搭框架，不应直接作为最终发布声明。

### 4.1 Message 操作

| Benchmark | Time | CPU |
|-----------|------|-----|
| `BM_MessageModelTypeErasure_Broadcast_CowHandle` | 145 ns | 138 ns |
| `BM_MessageCopyOnWrite_Copy_VectorInt100` | 13.8 ns | 13.6 ns |

分析：

- COW message copy 成本很低，因为 payload 在 mutation 前保持共享。
- 广播时的句柄拷贝，相比 pipeline scheduling 与 queue contention 成本更小。
- 在对外宣称大 payload 的零拷贝带宽之前，还需要更完整的 payload-size benchmark 支撑。

### 4.2 Queue Microbenchmarks

此组测试使用 shared-pointer payload，采用 producer/consumer 对打模型：

| Queue | Threads | Items/s |
|-------|---------|---------|
| LockBaseQueue | 2 | 9.95 M/s |
| LockBaseQueue | 4 | 4.09 M/s |
| LockBaseQueue | 8 | 4.84 M/s |
| LockBaseQueue | 16 | 2.69 M/s |
| LockFreeQueue | 2 | 12.94 M/s |
| LockFreeQueue | 4 | 5.94 M/s |
| LockFreeQueue | 8 | 3.64 M/s |
| LockFreeQueue | 16 | 1.74 M/s |

分析：

- 本次测试中，lock-free queue 并非所有场景都更快；竞争与失败操作都会影响结果。
- 在 `MessageQueue` 可配置或切换到 lock-free 前，当前 pipeline 不能声称拥有 lock-free queue 性能。
- Queue benchmark 与 pipeline benchmark 应分开报告，除非两者确实使用同一套运行时队列实现。

### 4.3 Baseline Pipeline Throughput

| Benchmark | Source sent | Sink received | Throughput counter | Avg latency |
|-----------|-------------|---------------|--------------------|-------------|
| Linear blocking | 945 K | 945 K | 934.9 K/s | 11.39 ms |
| Single-path blocking | 851 K | 851 K | 845.7 K/s | 10.23 ms |
| Diamond blocking warm | 807.8 K | 1.616 M | 1.603 M/s | 7.84 ms |
| Diamond non-blocking | 967.1 K | 1.412 M | 1.404 M/s | 16.53 ms |

补充计数器：

| Benchmark | Port enqueued | Port dequeued | Port dropped | Max peak depth |
|-----------|---------------|---------------|--------------|----------------|
| Linear blocking | 2.835 M | 2.835 M | 0 | 10.001 K |
| Single-path blocking | 1.702 M | 1.702 M | 0 | 10.001 K |
| Diamond blocking warm | 3.231 M | 3.231 M | 0 | 10.001 K |
| Diamond non-blocking | 3.311 M | 3.311 M | 521.8 K | 10.001 K |

分析：

- Pipeline runtime 在线性与 fan-out 拓扑下都能维持较高 message rate。
- Diamond sink throughput 高于 source throughput，因为每条 source message 都会广播到两个下游分支。
- Non-blocking output 会通过显式 drop counters 直接暴露过载，而不是把问题藏起来。
- 当前 average latency 受 1 秒 burst test 与 queue backlog 影响较大；如果要看尾延迟，应以 percentile latency benchmark 为准。

### 4.4 Linear Depth Scaling

Benchmark：`BM_ReportPipelineLinearDepth_Blocking`，20K 条消息，blocking output，4 个 executor workers，queue size 为 10K。Payload 是 `uint64_t` 单调发送时间戳，因此这张表更适合作为小消息调度开销的基线。

本节使用自定义 `ElapsedUs` counter，而不是 Google Benchmark 左侧的 `Time` 列。`ElapsedUs` 是围绕“发送全部消息 -> 等待交付 -> 等待 drain”显式测得的 wall-clock interval，更符合本报告对端到端耗时的定义。

`Avg elapsed / msg` 表示 `ElapsedUs / SinkReceived`。它和 `Avg latency` 不同：前者是整个 burst 的摊销总 wall-clock 成本，后者是 sink 观察到的单条消息端到端等待时间。

| Linear depth | Payload | Elapsed time | Avg elapsed / msg | Sink received | Throughput | Avg latency | Drops |
|--------------|---------|--------------|-------------------|---------------|------------|-------------|-------|
| 1 | 8 B timestamp | 15.1 ms | 0.76 us | 20 K | 1.323 M/s | 4.31 ms | 0 |
| 2 | 8 B timestamp | 16.2 ms | 0.81 us | 20 K | 1.235 M/s | 5.31 ms | 0 |
| 4 | 8 B timestamp | 19.9 ms | 0.99 us | 20 K | 1.006 M/s | 5.71 ms | 0 |
| 8 | 8 B timestamp | 24.3 ms | 1.22 us | 20 K | 822 K/s | 8.77 ms | 0 |
| 16 | 8 B timestamp | 33.1 ms | 1.66 us | 20 K | 604 K/s | 15.5 ms | 0 |
| 32 | 8 B timestamp | 51.6 ms | 2.58 us | 20 K | 388 K/s | 25.7 ms | 0 |

Benchmark：`BM_ReportPipelineLinearDepthPayload1KiB_Blocking`，20K 条消息，blocking output，4 个 executor workers，queue size 为 10K。Payload 是包含 1 KiB 数据的 `shared_ptr<vector<char>>`，通过 `Message` 共享传递，每条 edge 都不会复制 payload bytes。

| Linear depth | Payload | Elapsed time | Avg elapsed / msg | Sink received | Throughput | Effective payload rate | Drops |
|--------------|---------|--------------|-------------------|---------------|------------|------------------------|-------|
| 1 | 1 KiB shared payload | 15.7 ms | 0.79 us | 20 K | 1.272 M/s | 1.21 GiB/s | 0 |
| 2 | 1 KiB shared payload | 16.9 ms | 0.84 us | 20 K | 1.187 M/s | 1.13 GiB/s | 0 |
| 4 | 1 KiB shared payload | 19.8 ms | 0.99 us | 20 K | 1.011 M/s | 988 MiB/s | 0 |
| 8 | 1 KiB shared payload | 23.8 ms | 1.19 us | 20 K | 841 K/s | 821 MiB/s | 0 |
| 16 | 1 KiB shared payload | 32.8 ms | 1.64 us | 20 K | 610 K/s | 595 MiB/s | 0 |
| 32 | 1 KiB shared payload | 50.8 ms | 2.54 us | 20 K | 394 K/s | 385 MiB/s | 0 |

分析：

- 所有深度下都保持了无丢失交付。
- 1 KiB shared payload 从 depth 1 增长到 depth 32 时，elapsed time 只增加约 3.2x，而 passthrough stage 数增加了 32x，说明 pipeline overlap 是有效的。
- 由于 1 KiB payload 通过 `shared_ptr` 传递，这个 benchmark 主要测量的是 scheduling、queueing 与 message handle movement，而不是 memory-copy bandwidth。
- Timestamp baseline 在 depth 2 之后出现了明显拐点；1 KiB shared payload 曲线相对更平滑。
- 如果作为最终对外数据，仍应使用 repeated-run median，因为这些数字对 scheduler noise 比较敏感。

### 4.5 Worker Scaling

Benchmark：`BM_ReportPipelineWorkerScaling_Blocking`，8 级 linear passthrough，20K 条消息，queue size 为 10K。

| Workers | Elapsed time | Sink received | Throughput | Avg latency | Delivery status |
|---------|--------------|---------------|------------|-------------|-----------------|
| 1 | 27.0 ms | 20 K | 741 K/s | 10.2 ms | Complete |
| 2 | 19.7 ms | 20 K | 1.015 M/s | 7.36 ms | Complete |
| 4 | 22.2 ms | 20 K | 901 K/s | 8.38 ms | Complete |
| 8 | 28.2 ms | 20 K | 710 K/s | 9.83 ms | Complete |
| 16 | 30.9 ms | 20 K | 648 K/s | 11.9 ms | Complete |

分析：

- 本次测试中的最佳点是 2 workers。
- 加入 manual source scheduling 与 FIFO task submission fairness 后，1 worker 已经可以完成交付和 drain，不再 timeout。
- 对这种极轻量的 passthrough workload 来说，8/16 workers 没有带来提升，因为协调成本已经开始高于有效工作量。

### 4.6 Diamond Join Branch Scaling

Benchmark：`BM_ReportPipelineDiamondBranches_BlockingJoin`，10K 条 source messages，blocking output，8 个 executor workers，`OnAllInputs` join sink。

| Branches | Elapsed time | Joined messages | Throughput | Avg latency | Drops |
|----------|--------------|-----------------|------------|-------------|-------|
| 2 | 9.83 ms | 10 K | 1.017 M/s | 2.46 ms | 0 |
| 4 | 25.0 ms | 10 K | 401 K/s | 1.91 ms | 0 |
| 8 | 51.8 ms | 10 K | 193 K/s | 1.91 ms | 0 |
| 16 | 101.6 ms | 10 K | 98.4 K/s | 975 us | 0 |

分析：

- 在 2、4、8、16 分支下，join correctness 都成立，且没有出现 drop。
- 分支数增加会提高 edge traffic 与同步工作量：port dequeues 从 2 分支的 40K 增长到 16 分支的 320K。
- Average latency 不适合直接拿来作为尾延迟结论，发布时还是应以 percentile 数据为准。

### 4.7 Queue Capacity Under Overload

Benchmark：`BM_ReportPipelineQueueCapacity_NonBlocking`，4 级 linear pipeline，non-blocking `DropTail`，50K 条 source messages，每个 pass stage sleep 10 us。

| Queue capacity | Elapsed time | Sink received | Dropped | Throughput | Avg latency | Peak depth |
|----------------|--------------|---------------|---------|------------|-------------|------------|
| 8 | 5.44 ms | 218 | 49.78 K | 40.1 K/s | 344 us | 9 |
| 16 | 5.51 ms | 197 | 49.80 K | 35.8 K/s | 607 us | 17 |
| 32 | 5.77 ms | 268 | 49.73 K | 46.4 K/s | 844 us | 33 |
| 64 | 6.03 ms | 327 | 49.67 K | 54.2 K/s | 1.23 ms | 65 |
| 128 | 7.31 ms | 390 | 49.61 K | 53.4 K/s | 2.15 ms | 128 |
| 256 | 11.6 ms | 517 | 49.48 K | 44.6 K/s | 4.71 ms | 257 |

分析：

- 这是一个刻意制造的过载测试：producer push 速度明显高于 downstream stage 的消费速度。
- 更大的队列能保留更多消息，但也会提高平均延迟，因为消息在处理前会等待更久。
- 当前 stats model 中，peak depth 有时会比配置容量高 1；如果要把 peak depth 当成严格的 capacity invariant，还需要进一步核查。

### 4.8 Payload Size Impact

Benchmark：`BM_ReportPipelinePayloadSize_Blocking`，4 级 linear pipeline，20K 条消息，shared pointer payload，blocking output。

| Payload size | Elapsed time | Sink received | Throughput | Effective payload rate | Drops |
|--------------|--------------|---------------|------------|------------------------|-------|
| 64 B | 20.5 ms | 20 K | 973 K/s | 59.4 MiB/s | 0 |
| 256 B | 22.0 ms | 20 K | 909 K/s | 222 MiB/s | 0 |
| 1 KiB | 21.9 ms | 20 K | 914 K/s | 892 MiB/s | 0 |
| 4 KiB | 18.4 ms | 20 K | 1.090 M/s | 4.16 GiB/s | 0 |
| 16 KiB | 21.9 ms | 20 K | 914 K/s | 14.0 GiB/s | 0 |
| 64 KiB | 22.5 ms | 20 K | 887 K/s | 54.2 GiB/s | 0 |

分析：

- Payload size 对 elapsed time 的影响较小，因为 benchmark 通过 `Message` 传递的是 `shared_ptr<vector<char>>`，每条 edge 都不会复制 payload bytes。
- Effective payload rate 是按逻辑吞吐推导出来的数值，不是物理内存带宽的直接测量结果。
- 还需要单独的 mutating-payload benchmark，来量化下游写入触发 COW clone 的成本。

### 4.9 端到端延迟分布

Benchmark：`BM_ReportPipelineLinearDepthLatency_Blocking`，2K samples，blocking output，queue size 为 16，4 个 executor workers。该 benchmark 逐条发送消息，并在确认交付后再发送下一条，使 queue depth 接近 1，因此这张表反映的是低负载下的单消息延迟，而不是 burst backlog。

| Linear depth | Payload | P50 | P90 | P99 | Max | Drops |
|--------------|---------|-----|-----|-----|-----|-------|
| 1 | 8 B timestamp | 5.00 us | 7.71 us | 23.33 us | 47.04 us | 0 |
| 2 | 8 B timestamp | 5.92 us | 9.54 us | 25.25 us | 68.88 us | 0 |
| 4 | 8 B timestamp | 8.33 us | 13.21 us | 37.83 us | 98.50 us | 0 |
| 8 | 8 B timestamp | 9.83 us | 17.79 us | 43.58 us | 656.29 us | 0 |
| 16 | 8 B timestamp | 14.71 us | 28.75 us | 64.75 us | 766.71 us | 0 |
| 32 | 8 B timestamp | 20.67 us | 45.63 us | 81.88 us | 156.46 us | 0 |

Benchmark：`BM_ReportPipelineLinearDepthPayload1KiBLatency_Blocking`，设置相同，但 payload 是包含 1 KiB 数据的 `shared_ptr<vector<char>>`。

| Linear depth | Payload | P50 | P90 | P99 | Max | Drops |
|--------------|---------|-----|-----|-----|-----|-------|
| 1 | 1 KiB shared payload | 5.08 us | 7.92 us | 23.58 us | 75.79 us | 0 |
| 2 | 1 KiB shared payload | 5.92 us | 9.71 us | 28.17 us | 64.63 us | 0 |
| 4 | 1 KiB shared payload | 8.21 us | 13.46 us | 34.08 us | 69.13 us | 0 |
| 8 | 1 KiB shared payload | 9.92 us | 15.75 us | 37.17 us | 58.33 us | 0 |
| 16 | 1 KiB shared payload | 10.92 us | 18.04 us | 46.38 us | 97.21 us | 0 |
| 32 | 1 KiB shared payload | 20.13 us | 45.13 us | 79.13 us | 144.29 us | 0 |

Benchmark：`BM_ReportPipelineDiamondJoinLatency_Blocking`，2K samples，blocking output，queue size 为 16，8 个 executor workers。拓扑为 `Source -> N passthrough branches -> OnAllInputs Join`。Join latency 的测量区间是从 source send timestamp 到 join 成功完成。

| Branches | Topology | P50 | P90 | P99 | Max | Drops |
|----------|----------|-----|-----|-----|-----|-------|
| 2 | Diamond join | 8.50 us | 14.63 us | 35.13 us | 487.79 us | 0 |
| 4 | Diamond join | 11.29 us | 22.46 us | 38.25 us | 67.46 us | 0 |
| 8 | Diamond join | 28.50 us | 38.96 us | 60.67 us | 96.71 us | 0 |
| 16 | Diamond join | 41.08 us | 57.25 us | 83.63 us | 134.50 us | 0 |

分析：

- 两种 payload 模式在 depth 8 以内，P50 延迟都大致落在 5-10 us。
- 到 depth 32 时，本次测试中 timestamp payload 的 P99 低于 82 us，1 KiB shared payload 的 P99 低于 80 us。
- 1 KiB payload 对 median latency 没有实质影响，因为 payload 共享的是句柄，而不是在每条 edge 上复制。
- Diamond join latency 会随着 branch count 增加而稳定上升：P50 从 2 branches 的 8.50 us 增加到 16 branches 的 41.08 us。
- Max latency 比 percentile latency 更容易受噪声影响，发布时应同时附带环境说明。

## 5. 运行时优势

### 5.1 低成本 Message Sharing

`Message` 使用 type erasure 与 copy-on-write 语义。下游 module 只读 payload 时，fan-out 成本较低，因为多个 message copy 共享同一个 payload object。

### 5.2 集中式调度

运行时已经不再是严格的一模块一线程模型。Actor 注册到共享 executor，并调度到 work-stealing thread pool 上执行，这为混合拓扑 workload 提供了更好的基础。

### 5.3 内置背压可观测性

Port stats 暴露 push attempts、enqueue count、drop count、reject count、dequeue count、current depth 与 peak depth。这已经足以围绕 overload、queue saturation 与 downstream lag 构建监控。

### 5.4 Join 支持

`OnAllInputs` module 可以按 `messageId` 同步消息，runtime stats 也会暴露 pending join groups 以及 timeout/overflow drops。这对 fork-join 与 fusion-style pipeline 很有价值。

## 6. 最终报告前的已知限制

### 6.1 Runtime 尚未内置延迟直方图

Benchmark suite 现在已经可以通过 benchmark-specific sink modules 报告 P50、P90、P99 与 max latency。但 runtime 本身仍只暴露 counter snapshots；如果生产 telemetry 需要延迟分布，还需要 runtime observer 或 histogram extension。

### 6.2 Message Timestamp 精度不足以支撑微秒级结论

`MessageMeta.timestamp` 当前使用毫秒级 system time。它适合 join timeout bookkeeping，但不足以支撑微秒级 latency 报告。发布这类结论前，应加入 monotonic nanosecond timestamp 或 benchmark-local latency probe。

### 6.3 Runtime Queue 与 Queue Benchmark 路径不同

Pipeline runtime 使用 `LockBaseQueue<Message>`。Lock-free queue benchmark 是有价值的探索数据，但尚不代表当前正式运行路径。

### 6.4 Source Scheduling 仍会影响尾延迟

没有输出的 polling source actor 依赖 `idleWaitUs`。这种做法简单且安全，但 event-driven 或 timer-driven source scheduling 会带来更干净的 latency 行为，也更利于统一报告口径。

Manual/external source module 现在可以通过 `Module::SourcePolicy::Manual` 退出 polling，thread pool 也改为使用 FIFO local submission，避免 single-worker actor continuation starvation。Polling source 仍会按设计持续 runnable，因此 timer/event-driven source scheduling 仍是后续值得做的扩展。

### 6.5 Join Policy 当前基于 Message ID

当前 join 行为更适合处理同一条消息的 forked copies。多源 stream fusion 场景下，可能还需要 timestamp-window joins、watermarks、late-data handling 与可配置 join policies。

## 7. 推荐 Benchmark Matrix

如果要形成更完整的 NexusFlow 专属报告，建议继续补充以下 benchmark 组。

### 7.1 Framework Overhead

| Case | Variables | Metrics |
|------|-----------|---------|
| Linear passthrough depth | depth = 1, 2, 4, 8, 16, 32 | wall time, CPU time, throughput, per-node overhead |
| Start/stop lifecycle | iterations = 100, 1K, 10K | average start/stop time, failures |
| Repeated execution stability | iterations = 10, 100, 1K, 10K | amortized runtime, memory growth, queue residual depth |

### 7.2 Concurrency

| Case | Variables | Metrics |
|------|-----------|---------|
| Worker scaling | workers = 1, 2, 4, 8, 16 | throughput, latency, CPU time |
| Diamond branch scaling | branches = 2, 4, 8, 16, 32 | speedup, branch efficiency, join latency |
| Multi-producer push | producers = 1, 2, 4, 8, 16 | accepted pushes/s, drops/s, queue depth |

### 7.3 Stream Behavior

| Case | Variables | Metrics |
|------|-----------|---------|
| Sustained FPS | target FPS = 30, 60, 120, 500, 1000 | achieved FPS, drops, P50/P99 latency |
| Queue capacity | capacity = 8, 16, 32, 64, 128, 256 | throughput, drops, queue depth |
| Backpressure | producer/consumer rate ratio = 0.5x, 1x, 1.5x, 2x, 3x | drop rate, current/peak depth |
| Long run | duration = 5s, 30s, 5min, 30min | success rate, memory growth, P99 stability |

### 7.4 Payload Behavior

| Case | Variables | Metrics |
|------|-----------|---------|
| Payload size | 64 B, 256 B, 1 KiB, 4 KiB, 16 KiB, 64 KiB, 1 MiB | throughput, latency, COW mutation cost |
| Read-only fan-out | subscribers = 1, 2, 4, 8, 16 | shared copy overhead |
| Mutating fan-out | mutating subscribers = 1, 2, 4, 8 | COW clone cost |

## 8. 推荐框架改进

| 优先级 | 改动 | 价值 |
|--------|------|------|
| P0 | 为 benchmark latency 增加 monotonic nanosecond timing。 | 支撑可信的微秒级 latency 报告。 |
| P0 | 增加 runtime-level latency histograms 或 observer hooks。 | Benchmark 已有 percentiles，但生产 telemetry 仍只有 counter snapshots。 |
| P0 | 在 `PipelineConfig` 中显式配置 runtime queue backend。 | 避免把 lock-based runtime 结果和 lock-free microbenchmark 结果混用。 |
| Done | 修复 manual source scheduling 与 single-worker task fairness。 | 1-worker report benchmark 现在已经达到 `DeliveryTimedOut=0` 且 `DrainTimedOut=0`。 |
| P1 | 将 `KeepLatest` 作为一等 pipeline drop policy。 | 对 video/streaming workload 很重要，因为这类场景通常更关心最新数据。 |
| P1 | 增加 depth、workers、branches、queue capacity、payload size 的 benchmark matrix。 | 形成详细 NexusFlow 性能报告所需。 |
| P1 | 增加 timer/event-driven source scheduling modes。 | Manual 与 polling mode 已存在；timer/event-driven scheduling 可进一步降低 `idleWaitUs` 噪声。 |
| P2 | 增加超越 `messageId` join 的 join strategy abstraction。 | 支持多源 stream fusion 与更真实的 fork-join 报告。 |
| P2 | 增加 executor stats，例如 task submissions、steals、idle wakeups、max task backlog。 | 帮助解释 scaling 行为，而不只是报告结果。 |

## 9. 建议报告规则

- 在 pipeline runtime 真正使用 lock-free queue 之前，只应将 lock-free queue 数字作为 queue microbenchmark 报告。
- Fan-out 拓扑应同时报告 source messages/s 与 sink messages/s。
- Blocking 与 non-blocking output 结果应分开报告，因为它们回答的是不同的问题。
- 当前这些 smoke 数字更适合用作方向性参考；最终发布应基于稳定 CPU 频率、明确环境说明以及 JSON artifacts 的重复运行 median。
- 每个 throughput table 都应同时展示 drop/reject counters，避免把高吞吐误读成完整交付。

## 10. 下一步

1. 决定 NexusFlow 的 production queue path 是继续保持 lock-based，还是改为可配置。
2. 如果生产 telemetry 需要 P50/P99，在 runtime 层增加 latency observer hooks。
3. 在目标 Linux/WSL2 环境重新跑 benchmark，并用 repeated-run medians 替换当前 smoke values。
4. 为 queue throughput、pipeline topology scaling 与 latency distribution 生成图表。
5. 增加 mutating-payload benchmark，量化下游写入触发 COW clone 的成本。
