# 可观测性示例

这个示例演示两件事：

1. 如何开启并读取 Pipeline 的运行时统计。
2. 如何使用 `PipelineObserver` 聚合查看节点和边的观测结果。

## 示例拓扑

```text
Source -> SlowPass -> Sink
```

- `Source` 是手动输入模块，会快速非阻塞发送 8 条消息。
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

- `PipelineObserver::Describe()`
  - 这是面向人阅读的聚合摘要。
- `Node Stats`
  - 观察每个节点的 `process`、`incomingDequeue`、`outgoingDrop`。
- `Port Stats`
  - 观察每条边的 `enqueue`、`drop`、`dequeue`、`peak`。

如果输出类似下面这样，就说明可观测性链路工作正常：

```text
Source: outgoingEnqueue=2 outgoingDrop=6
Source:out -> SlowPass:in enqueue=2 drop=6 dequeue=2 peak=2
```

这表示：

- 源模块总共尝试发送了 8 条消息
- 其中只有 2 条真正进入队列
- 6 条因为队列已满被直接丢弃
