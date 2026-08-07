# Benchmarks

## Run

```bash
cd build
./benchmarks/nexusflow_benchmarks
```

Useful filters:

```bash
./benchmarks/nexusflow_benchmarks --benchmark_filter=BM_Message
./benchmarks/nexusflow_benchmarks --benchmark_filter=BM_LockBaseQueue
./benchmarks/nexusflow_benchmarks --benchmark_filter=BM_PipelineLinear_Throughput_Blocking
./benchmarks/nexusflow_benchmarks --benchmark_filter='BM_PipelineLinear_Throughput_Blocking|BM_PipelineDiamond_Throughput_BlockingWarm'
./benchmarks/nexusflow_benchmarks --benchmark_filter='BM_ReportPipelineTopologyCompare_.*'
./benchmarks/nexusflow_benchmarks --benchmark_filter='BM_ReportPipelineTopologyCompare_.*Latency.*'
```

Topology compare report workflow:

```bash
./build/benchmarks/nexusflow_benchmarks \
  --benchmark_filter='BM_ReportPipelineTopologyCompare_.*Timestamp.*' \
  --benchmark_out=/tmp/topology_timestamp.json \
  --benchmark_out_format=json

./build/benchmarks/nexusflow_benchmarks \
  --benchmark_filter='BM_ReportPipelineTopologyCompare_.*Payload1KiB.*' \
  --benchmark_out=/tmp/topology_payload.json \
  --benchmark_out_format=json

python3 tools/render_topology_compare_tables.py \
  /tmp/topology_timestamp.json \
  /tmp/topology_payload.json \
  /tmp/topology_compare_tables.md
```

Topology compare latency smoke run:

```bash
./build/benchmarks/nexusflow_benchmarks \
  --benchmark_filter='BM_ReportPipelineTopologyCompare_(Linear|Diamond).*(Timestamp|Payload1KiB)Latency.*' \
  --benchmark_min_time=0.01s
```

## What The Benchmarks Cover

- `BenchmarkMessage.cpp`: `Message` creation, borrow/mutate, and COW overhead
- `BenchmarkLockBaseQueue.cpp`: lock-based queue behavior
- `BenchmarkLockFreeQueue.cpp`: experimental lock-free queue behavior
- `BenchmarkPipelineBaseline.cpp`: baseline end-to-end pipeline throughput
- `BenchmarkPipelineTopology.cpp`: same-config `Linear` vs `DiamondJoin` topology comparison for throughput and latency
- `BenchmarkPipelineDepth.cpp`: linear depth scaling
- `BenchmarkPipelineLatency.cpp`: latency percentile distributions
- `BenchmarkPipelineScaling.cpp`: worker and branch scaling
- `BenchmarkPipelinePayload.cpp`: payload-size sensitivity
- `BenchmarkPipelineBackpressure.cpp`: overload and queue-capacity behavior

The names now follow a compact `BM_<Component>_<Payload>_<Scenario>` pattern for queue tests, while keeping the more descriptive message and pipeline names where it helps readability.

Guidelines:

- `DomainOrComponent`: what is being measured, including implementation or payload detail when needed
- `Operation`: the primary action or metric, such as `Create`, `Throughput`, or `Process`
- `Scenario`: the workload shape or condition, such as `Blocking`, `DropTail`, or `VectorInt100`

Quick examples:

- `BM_MessageCopyOnWrite_Copy_VectorInt100`
- `BM_MessageModelInheritance_Process_DynamicCast`
- `BM_MessageModelTypeErasure_Process_BorrowPtr`
- `BM_LockBaseQueueInt_Throughput_ProducerConsumer`
- `BM_LockFreeQueueSharedPtr_Throughput_ProducerConsumer`
- `BM_LockBaseQueueBlob2K_Throughput_ProducerConsumer`
- `BM_LockFreeNodeQueueBlob2K_Throughput_ProducerConsumer`
- `BM_PipelineDiamond_Throughput_Blocking`
- `BM_PipelineLinear_Throughput_BlockingProcessLatency100us`

## Important Context

The actual pipeline runtime currently uses:

```cpp
using MessageQueue = LockBaseQueue<Message>;
```

That means:

- `BenchmarkLockFreeQueue.cpp` is useful for exploration
- it does not describe the performance of the shipping pipeline path

The queue microbenchmarks now intentionally align the lock-based and lock-free comparisons:

- same bounded capacity
- same non-blocking `push/pop` style
- same producer/consumer split
- same reused payload pool for shared-pointer and blob cases

Each queue benchmark also reports:

- `push_ok`
- `push_fail`
- `pop_ok`
- `pop_fail`

Pipeline benchmarks now wait for queue drain before sampling final counters and also report:

- `DeliveryTimedOut`
- `DrainTimedOut`

Topology comparison benchmarks additionally keep `Linear` and `DiamondJoin` on the same runtime configuration so that:

- `PerMessageElapsedUs` can be compared directly across topologies
- `P50/P90/P99` latency distributions can be compared directly across topologies
- queue traffic growth can be correlated with branch count
- shared-payload and timestamp cases can be contrasted under the same worker/queue setup

Queue benchmark summary chart:

![Queue benchmark summary](/Users/yang/Code/Nexusflow/docs/assets/queue-benchmark-summary.svg)

## Recent Smoke Results

Smoke run on this workspace on 2026-06-10:

```text
BM_PipelineLinear_Throughput_Blocking
  Throughput: ~1.3M recv/s

BM_PipelineDiamond_Throughput_BlockingWarm
  Throughput: ~1.9M recv/s

BM_PipelineDiamond_Throughput_NonBlocking
  PortDropped: visible and non-trivial under overload
```

These numbers are environment-dependent. The more important takeaways are the trends below.

## Current Performance Findings

### 1. `Message` itself is not the main bottleneck

The type-erased COW message container is cheap enough that pipeline throughput is dominated by scheduling and queue behavior more often than payload wrapping.

### 2. Queue contention matters quickly

Once more actors contend on the same runtime queues, throughput drops much faster than raw single-thread queue microbenchmarks suggest.

### 3. Topology matters a lot

Linear pipelines are much easier on the runtime than diamond or fusion-heavy topologies because they avoid:

- repeated fan-out
- extra queue contention
- synchronization on multiple inputs

### 4. Delivery policy is still a real tradeoff

Non-blocking output can improve producer progress, but it can also drop data when queues fill. The runtime now exposes both `DropTail` and `DropHead` for that path, plus per-edge stats to show what actually happened.

## Current Runtime Weak Spots

- Source actors still rely on `idleWaitUs` when they have no output, and `OnAllInputs` still scans every input queue on each activation.
- `OnAllInputs` only supports `messageId`-based synchronization.
- Runtime stats are snapshots; there is no built-in alerting or sampling pipeline yet.
- The shipping runtime still uses `LockBaseQueue<Message>`, so lock contention remains part of the real cost model.

## Suggested Benchmark Workflow

1. Run message and queue microbenchmarks to sanity-check low-level changes.
2. Run `BM_PipelineLinear_Throughput_Blocking` after executor or queue changes.
3. Run one multi-input benchmark after any `TriggerPolicy` or fusion change.
4. Compare trends, not just absolute numbers.
