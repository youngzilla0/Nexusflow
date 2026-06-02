# Benchmark Documentation

## Overview

NexusFlow benchmarks measure framework performance across different pipeline topologies. All benchmarks run on Apple M-series chips (macOS 14.5, 8 cores).

---

## 1. Message Performance

Measures the overhead of the Message type-erasure system vs traditional inheritance.

```
Environment: Apple M-series (8 cores), macOS 14.5
-------------------------------------------------------------------
Benchmark                         Time             CPU   Iterations
-------------------------------------------------------------------
BM_Inheritance_Create          19.5 ns         19.4 ns     37776782
BM_TypeErasure_Create          33.1 ns         33.0 ns     21299514
BM_Inheritance_Broadcast       131  ns         131  ns      5359262
BM_TypeErasure_Broadcast       132  ns         131  ns      5346327
BM_Inheritance_Process        63.0 ns         62.9 ns     11159647
BM_TypeErasure_Process        4.07 ns         4.04 ns    172799400
BM_Message_COW_Copy            13.1 ns         13.1 ns     53564733
BM_Message_COW_Mutate          2.06 ns         2.06 ns    340932890
BM_Message_Borrow              20.3 ns         20.2 ns     34689529
BM_Message_Mut                 6.55 ns         6.53 ns    107889829
BM_ConcurrentQueue_PushPop    29.4 ns         29.4 ns     23657280
```

**Key Finding**: Type-erasure Message is ~15x faster than inheritance for `Process()` (4.07ns vs 63.0ns) due to no virtual dispatch overhead. COW mutate is extremely cheap (2.06ns).

---

## 2. Pipeline Benchmarks

Measures end-to-end throughput for different DAG topologies.

### 2.1 Test Topologies

```
Linear (baseline):                              Diamond (fan-out → fan-in):
    Source → Pass → Sink                            Source → Pass1 ─┐
    (no fan-out, single path)                              ├─→ Sink
                                                            Pass2 ─┘

    ┌─────┐     ┌─────┐     ┌─────┐
    │Source│────▶│ Pass │────▶│ Sink │                  ┌─────┐     ┌─────┐
    └─────┘     └─────┘     └─────┘                  │Source│──┬─▶│ Pass1│──┐
                                                       └─────┘  │  └─────┘  │
                                                                │       ▼
                                                                │    ┌─────┐
                                                                │    │ Sink │
                                                                │    └─────┘
                                                                ▼
                                                             ┌─────┐
                                                             │ Pass2│
                                                             └─────┘
```

### 2.2 Results (Apple M-series, 8 cores, macOS 14.5)

| Benchmark                                | Throughput     | Sent          | Avg Latency    | Notes                          |
|----------------------------------------|---------------|---------------|----------------|--------------------------------|
| BM_Pipeline_Linear_Throughput          | 3.41M/s       | 2.36M/s       | 4.52M ns     | Linear topology baseline        |
| BM_Pipeline_Throughput_SingleOutput    | 3.93M/s       | 2.92M/s       | 2.10M ns     | Diamond with only Pass1 connected |
| BM_Pipeline_Latency (diamond)           | —             | —             | 35.8k ns/msg | Average per-message latency      |
| BM_Pipeline_Throughput (diamond)        | 996k/s        | 419k/s        | 43.0k ns     | True diamond fan-out             |
| BM_Pipeline_Throughput_Warmup (diamond) | 1.10M/s       | 484k/s        | 43.6k ns     | Diamond with 100-msg warmup     |
| BM_Pipeline_Throughput_NonBlocking     | 1.16M/s       | 540k/s        | 58.4k ns     | All non-blocking sends          |
| BM_Pipeline_Throughput_LargeSinkQueue  | 872k/s        | 356k/s        | 35.2k ns     | Large per-module queues         |
| BM_Pipeline_Linear_Throughput_WithLatency | 362k/s     | 6.27k/s       | 1.61G ns      | Linear + 100us simulated delay  |
| BM_Pipeline_Throughput_WithLatency     | 443k/s        | 6.27k/s       | 1.59G ns      | Diamond + 100us simulated delay |

### 2.3 Analysis

**Diamond vs Linear**: Diamond (~1M/s) is ~3x slower than Linear (3.41M/s) — root causes:

1. **Fan-out serialization**: `Broadcast(blocking=true)` waits for each downstream to consume before returning. Source must wait for Pass1 before pushing to Pass2.

2. **Join bottleneck at Sink**: When Sink drains two queues, if one is empty, Worker spins with `TryPop()` + 5µs sleep before switching queues.

3. **Independent message IDs**: Pass1 and Pass2 receive different message IDs from Source (broadcast copies), causing join mismatches at Sink.

**SingleOutput** (3.93M/s) is close to Linear (3.41M/s), confirming the fan-out/fan-in join is the main bottleneck, not the Broadcast itself.

**Non-blocking Broadcast** (1.16M/s) is slightly better than blocking (996k/s) but still much lower than Linear — non-blocking prevents producer back-pressure but doesn't solve the join bottleneck.

### 2.4 Benchmark Parameters

| Parameter        | Value   | Description                        |
|-----------------|---------|------------------------------------|
| `min_time`      | 2s      | Minimum benchmark duration         |
| `batch_size`    | 64      | Max messages per Worker batch       |
| `batch_timeout` | 0ms     | No batching (low-latency mode)      |
| `queue_size`    | 10000   | Inter-module queue capacity        |

### 2.5 Why Diamond is Not a Bug

The Diamond topology tests fan-out + first-arrival semantics:
- Source broadcasts to both Pass1 and Pass2 concurrently
- Pass1 and Pass2 process independently
- Sink receives messages from both branches

The low throughput is expected behavior for a true join (both branches must complete before Sink processes). NexusFlow's JoinInputs mode requires message IDs to match across branches — if Pass1 receives ID=100 and Pass2 receives ID=101, they won't join until both IDs arrive.

This differs from ai-pipe's AggregatorNode which takes the first-arrived message and drops others.

---

## 3. Benchmark Commands

```bash
# Build
cd build && cmake .. -DWITH_BENCHMARK=ON && make -j4

# Run all benchmarks
./benchmarks/nexusflow_benchmarks

# Run Linear only
./benchmarks/nexusflow_benchmarks --benchmark_filter=BM_Pipeline_Linear

# Run with more iterations
./benchmarks/nexusflow_benchmarks --benchmark_min_time=5s
```

---

## 4. Comparing with ai-pipe

| Aspect                    | NexusFlow                | ai-pipe                          |
|--------------------------|--------------------------|----------------------------------|
| Thread model             | Worker per module        | ExecutionEngine owns ThreadPool  |
| Data passing             | Message (COW, shared_ptr)| shared_ptr (moved via pushToQueue)|
| Join semantics           | True join via JoinInputs | No framework join; user's process() decides |
| Fan-out                  | Broadcast (blocking)     | FanOutNode copies immediately    |
| Diamond efficiency       | ~1M/s (29% of Linear)   | ~100% (first-arrival, no sync)   |

**Note**: ai-pipe's Diamond benchmark tests fan-out + first-arrival, NOT true join. The 100% efficiency is because AggregatorNode takes the first-arrived input and ignores others.

---

## 5. Internal Architecture

```
                    ┌──────────────────────────────────────────────────────┐
                    │                     Pipeline                        │
                    │  ┌────────────┐    ┌────────────┐                   │
                    │  │ModuleActor│    │ModuleActor │                   │
                    │  │  Source   │    │   Pass1    │                   │
                    │  │           │    │           │                   │
                    │  │  Executor │    │  Executor  │                   │
                    │  │  Thread   │    │  Thread    │                   │
                    │  └─────┬─────┘    └─────┬──────┘                   │
                    │        │               │                          │
                    │        ▼               ▼                          │
                    │  ┌────────────┐    ┌────────────┐                 │
                    │  │  Worker    │    │   Worker   │                 │
                    │  │  Thread    │    │   Thread   │                 │
                    │  └────────────┘    └────────────┘                 │
                    └──────────────────────────────────────────────────┘

Executor:
  - Owns ThreadPool (work-stealing, N workers)
  - Receives Emit(msg)/Route(outputName, msg) from Module
  - Posts tasks to ThreadPool: push to subscriber queues asynchronously
  - Module never holds Executor pointer — uses callback interface

ThreadPool:
  - Each worker has WorkStealingDeque
  - PopFront (LIFO) for local, StealBack (FIFO) for steal
  - 1ms wait_for with re-check after wake to avoid missed-wake races
```