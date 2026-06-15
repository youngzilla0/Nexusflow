# 可观测性示例

这个示例演示两件事：

1. 如何开启并读取 Pipeline 的运行时统计快照。
2. 如何使用事件型 `CallbackPipelineObserver` 监听消息丢弃事件。

## 示例拓扑

```text
Source -> SlowPass -> Sink
```

- `Source` 是手动输入模块，会快速非阻塞发送 8 条消息。
- 每条消息默认携带一个 `1 MiB` 的 `char` 数组 payload。
- `SlowPass` 每处理一条消息都会额外 sleep 40ms，用来制造明显的队列堆积。
- `Sink` 负责收集最终成功处理的消息。

因为队列容量被设置为 `2`，并且 `Source` 使用 `DropTail` 非阻塞投递，所以运行后你会看到：

- `Source -> SlowPass` 这条边有明显的 `dropCount`
- `Source` 节点会聚合出 `outgoingDropCount`
- `Sink` 最终只收到一部分消息

## 构建与运行

在仓库根目录执行：

```bash
cmake -S . -B build
cmake --build build --target nexusflow_observability_example -j 8
./build/examples/4-observability-example/nexusflow_observability_example
```

## 重点观察

程序会输出三部分内容：

- `Event`
  - Shows drop / reject notifications when a queue overflows.
- `Overview`
  - Summarizes pipeline-level attempts, drop rate, reject rate, sink count, and end-to-end latency.
- `Node Stats / Port Stats`
  - Uses short multi-line blocks so the terminal output stays readable.

如果输出类似下面这样，就说明可观测性链路工作正常：

```text
[Event] Message Dropped
  Path: Source:out -> SlowPass:in
  Count: 1
  Reason: drop tail overflow
================ Overview ================
Attempts:   11
Enqueued:   6
Dropped:    5
Rejected:   0
Drop Rate:  45%
Reject Rate:0%
Sink Count: 3
Latency Samples: 3
Latency P50: 81 ms
Latency P99: 131 ms
Latency Max: 131 ms
```

这表示：

- 源模块总共尝试发送了 8 条消息
- 其中只有 2 条真正进入队列
- 6 条因为队列已满被直接丢弃

如果你想观察更大的负载，只需要调整 [main.cpp](/Users/yang/Code/Nexusflow/examples/4-observability-example/main.cpp) 里的 `kDefaultPayloadBytes` 即可。
