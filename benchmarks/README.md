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
BM_Inheritance_Create          19.8 ns         19.5 ns    113726828
BM_TypeErasure_Create          34.1 ns         33.5 ns     79894767
BM_Inheritance_Broadcast       139  ns         133  ns     21076386
BM_TypeErasure_Broadcast       135  ns         133  ns     21013307
BM_Inheritance_Process        76.1 ns         73.1 ns     38333899
BM_TypeErasure_Process         4.36 ns         4.17 ns    679278606
BM_Message_COW_Copy            13.4 ns         13.2 ns    212266422
BM_Message_COW_Mutate          2.16 ns         2.06 ns   1000000000
BM_Message_Borrow              20.7 ns         20.4 ns    136759484
BM_Message_Mut                 6.71 ns         6.51 ns    432618843
BM_ConcurrentQueue_PushPop     30.8 ns         29.8 ns     95595440
```

**Key Finding**: Type-erasure Message is ~17x faster than inheritance for `Process()` (4.36ns vs 76.1ns) due to no virtual dispatch overhead. COW mutate is extremely cheap (2.16ns).

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

| Benchmark                                | Throughput    | Sent          | Avg Latency   | Notes                          |
|----------------------------------------|--------------|--------------|---------------|--------------------------------|
| BM_Pipeline_Linear_Throughput          | 949k/s       | 2.19M/s      | 9.08M ns     | Linear topology baseline        |
| BM_Pipeline_Throughput_SingleOutput    | 1.27M/s      | 2.92M/s      | 3.13M ns     | Diamond, only Pass1 connected   |
| BM_Pipeline_Latency (diamond)          | —            | —            | 42.2k ns/msg | Average per-message latency      |
| BM_Pipeline_Throughput (diamond)       | 272k/s       | 408k/s       | 116.5k ns    | True diamond fan-out + join      |
| BM_Pipeline_Throughput_Warmup (diamond)| 341k/s       | 450k/s       | 99.9k ns     | Diamond, 100-msg warmup          |
| BM_Pipeline_Throughput_NonBlocking     | 301k/s       | 483k/s       | 405.0k ns    | All non-blocking sends           |
| BM_Pipeline_Throughput_LargeSinkQueue  | 302k/s       | 527k/s       | 144.9k ns    | Large per-module queues          |
| BM_Pipeline_Linear_Throughput_WithLatency | 415k/s   | 7.49k/s      | 1.48G ns     | Linear + 100us simulated delay   |
| BM_Pipeline_Throughput_WithLatency     | 566k/s       | 7.36k/s      | 1.36G ns     | Diamond + 100us simulated delay |

### 2.3 Analysis

**Diamond vs Linear**: Diamond (~300k/s) is ~3x slower than Linear (949k/s) — root causes:

1. **Fan-out serialization**: `Broadcast(blocking=true)` waits for each downstream to consume before returning. Source must wait for Pass1 before pushing to Pass2.

2. **Join bottleneck at Sink**: When Sink drains two queues, if one is empty, Worker spins with `TryPop()` + 5µs sleep before switching queues.

3. **Independent message IDs**: Pass1 and Pass2 receive different message IDs from Source (broadcast copies), causing join mismatches at Sink.

**SingleOutput** (1.27M/s) is close to Linear (949k/s), confirming the fan-out/fan-in join is the main bottleneck, not the Broadcast itself.

**Non-blocking Broadcast** (301k/s) is similar to blocking (272k/s) — non-blocking prevents producer back-pressure but doesn't solve the join bottleneck.

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
| Diamond efficiency       | ~300k/s (32% of Linear)   | ~100% (first-arrival, no sync)   |

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