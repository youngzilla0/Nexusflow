# NexusFlow Performance Benchmark Report

> Status: draft based on the current benchmark suite and one local smoke run.
> This report intentionally describes NexusFlow's current runtime path rather than
> borrowing conclusions from other pipeline frameworks.

## Test Environment

| Item | Value |
|------|-------|
| Project | NexusFlow |
| Build type | Release, C++14 |
| Benchmark framework | Google Benchmark |
| Test command | `build/benchmarks/nexusflow_benchmarks` |
| Smoke date | 2026-06-10 |
| Notes | Local run reported unavailable CPU frequency metadata and failed affinity setup, so absolute numbers should be treated as environment-specific. |

## 1. Executive Summary

NexusFlow's current runtime uses a shared `Executor` with a work-stealing `ThreadPool` to schedule module actors, while the shipping pipeline edge queues still use `LockBaseQueue<Message>`. A separate experimental lock-free queue implementation exists and is benchmarked, but it is not yet the pipeline runtime queue.

| Area | Current finding | Assessment |
|------|-----------------|------------|
| Message copy/broadcast | COW `Message` vector-payload copy measured ~13.8 ns; broadcast handle copy measured ~145 ns. | Message wrapper is unlikely to be the main bottleneck. |
| Pipeline depth scaling | 20K-message linear passthrough with 1 KiB shared payload went from 19.7 ms at depth 1 to 63.1 ms at depth 32. | Depth overhead is visible but still sublinear versus node-count growth. |
| Worker scaling | 8-stage linear benchmark peaked at 4 workers in this smoke run; 1 worker timed out. | Current source scheduling wastes capacity when worker count is too low. |
| Fan-out/join topology | Diamond join benchmark delivered 10K joined messages at 2/4/8/16 branches with no drops. | Join path is functional; branch count increases coordination cost. |
| Non-blocking overload behavior | Queue-capacity overload benchmark shows high drop counts and bounded queue depth. | Backpressure/drop accounting is visible and testable. |
| Queue path | Pipeline uses `LockBaseQueue<Message>`; lock-free queues are currently standalone benchmark targets. | Important reporting boundary. |
| Observability | Per-port and per-actor runtime stats exist for enqueue/dequeue/drop/reject and join state. | Enough for operational reporting, but latency percentiles are not built in yet. |

## 2. Architecture Under Test

NexusFlow is a C++ dataflow framework where modules are connected as a DAG. Each edge owns a message queue, each module is registered as an actor, and the shared executor schedules actor work onto a thread pool.

Core runtime pieces:

| Component | Role |
|-----------|------|
| `Pipeline` | Public lifecycle API: `Init()` -> `Start()` -> `Stop()` -> `DeInit()`. |
| `Graph` | Stores module topology and edge metadata. |
| `Module` | User-defined business logic unit. Modules process `PortInputsView` and emit through `PortOutputs`. |
| `Executor` | Owns actor runtime state, dispatches messages, tracks port/actor counters, and schedules actor tasks. |
| `ThreadPool` | Work-stealing scheduler used by the executor. |
| `Message` | Type-erased COW payload container with cheap shared handle copies. |
| `MessageQueue` | Currently aliased to `LockBaseQueue<Message>` in the pipeline runtime. |

## 3. Current Benchmark Coverage

The current benchmark executable covers:

| Benchmark group | Coverage |
|-----------------|----------|
| `BenchmarkMessage.cpp` | Message construction, COW copy, broadcast handle copy, typed access, mutation. |
| `BenchmarkLockBaseQueue.cpp` | Shipping-style lock-based queue microbenchmarks. |
| `BenchmarkLockFreeQueue.cpp` | Experimental lock-free MPMC and node queue microbenchmarks. |
| `BenchmarkPipeline.cpp` | Single-path, linear, diamond, blocking, non-blocking, warm, and simulated-latency pipeline cases. |

The test suite also validates drop policies, queue statistics, join group limits, observer aggregation, and pipeline executor behavior.

## 4. Smoke Benchmark Results

These values come from one local smoke/report-matrix run. They are useful for trend discussion and report scaffolding, not yet for final published claims.

### 4.1 Message Operations

| Benchmark | Time | CPU |
|-----------|------|-----|
| `BM_MessageModelTypeErasure_Broadcast_CowHandle` | 145 ns | 138 ns |
| `BM_MessageCopyOnWrite_Copy_VectorInt100` | 13.8 ns | 13.6 ns |

Interpretation:

- COW message copies are cheap because the payload is shared until mutation.
- Broadcast handle copy overhead is small compared with pipeline scheduling and queue contention.
- Payload-size benchmarks should be added before claiming zero-copy bandwidth across large payloads.

### 4.2 Queue Microbenchmarks

Shared-pointer payload, producer/consumer split:

| Queue | Threads | Items/s |
|-------|---------|---------|
| LockBaseQueue | 2 | 9.95 M/s |
| LockBaseQueue | 4 | 4.09 M/s |
| LockBaseQueue | 8 | 4.84 M/s |
| LockBaseQueue | 16 | 2.69 M/s |
| LockFreeMPMCQueue | 2 | 12.94 M/s |
| LockFreeMPMCQueue | 4 | 5.94 M/s |
| LockFreeMPMCQueue | 8 | 3.64 M/s |
| LockFreeMPMCQueue | 16 | 1.74 M/s |

Interpretation:

- The lock-free queue is not universally faster in this smoke run; contention and failed operations matter.
- The current shipping pipeline cannot claim lock-free queue performance until `MessageQueue` is made configurable or switched.
- Queue benchmarks should be reported separately from pipeline benchmarks unless both use the same queue implementation.

### 4.3 Baseline Pipeline Throughput

| Benchmark | Source sent | Sink received | Throughput counter | Avg latency |
|-----------|-------------|---------------|--------------------|-------------|
| Linear blocking | 945 K | 945 K | 934.9 K/s | 11.39 ms |
| Single-path blocking | 851 K | 851 K | 845.7 K/s | 10.23 ms |
| Diamond blocking warm | 807.8 K | 1.616 M | 1.603 M/s | 7.84 ms |
| Diamond non-blocking | 967.1 K | 1.412 M | 1.404 M/s | 16.53 ms |

Additional counters:

| Benchmark | Port enqueued | Port dequeued | Port dropped | Max peak depth |
|-----------|---------------|---------------|--------------|----------------|
| Linear blocking | 2.835 M | 2.835 M | 0 | 10.001 K |
| Single-path blocking | 1.702 M | 1.702 M | 0 | 10.001 K |
| Diamond blocking warm | 3.231 M | 3.231 M | 0 | 10.001 K |
| Diamond non-blocking | 3.311 M | 3.311 M | 521.8 K | 10.001 K |

Interpretation:

- The pipeline runtime can sustain high message rates in both linear and fan-out topologies.
- Diamond sink throughput is higher than source throughput because each source message is broadcast to two downstream branches.
- Non-blocking output exposes overload through explicit drop counters rather than hiding it.
- Current average latency is heavily influenced by one-second burst tests and queue backlog; percentile latency benchmarks are still needed.

### 4.4 Linear Depth Scaling

Benchmark: `BM_ReportPipelineLinearDepth_Blocking`, 20K messages, blocking output, 4 executor workers, queue size 10K. Payload is a `uint64_t` monotonic send timestamp, so this table is best read as a small-message scheduling baseline.

| Linear depth | Payload | Elapsed time | Sink received | Throughput | Avg latency | Drops |
|--------------|---------|--------------|---------------|------------|-------------|-------|
| 1 | 8 B timestamp | 17.2 ms | 20 K | 1.164 M/s | 5.87 ms | 0 |
| 2 | 8 B timestamp | 18.7 ms | 20 K | 1.067 M/s | 6.58 ms | 0 |
| 4 | 8 B timestamp | 21.2 ms | 20 K | 942 K/s | 6.09 ms | 0 |
| 8 | 8 B timestamp | 85.6 ms | 20 K | 234 K/s | 48.9 ms | 0 |
| 16 | 8 B timestamp | 37.7 ms | 20 K | 530 K/s | 19.0 ms | 0 |
| 32 | 8 B timestamp | 68.1 ms | 20 K | 294 K/s | 38.3 ms | 0 |

Benchmark: `BM_ReportPipelineLinearDepthPayload1KiB_Blocking`, 20K messages, blocking output, 4 executor workers, queue size 10K. Payload is a `shared_ptr<vector<char>>` containing 1 KiB, so the payload object is shared through `Message` and not copied at each edge.

| Linear depth | Payload | Elapsed time | Sink received | Throughput | Effective payload rate | Drops |
|--------------|---------|--------------|---------------|------------|------------------------|-------|
| 1 | 1 KiB shared payload | 19.7 ms | 20 K | 1.014 M/s | 990 MiB/s | 0 |
| 2 | 1 KiB shared payload | 22.2 ms | 20 K | 902 K/s | 881 MiB/s | 0 |
| 4 | 1 KiB shared payload | 20.9 ms | 20 K | 957 K/s | 935 MiB/s | 0 |
| 8 | 1 KiB shared payload | 26.5 ms | 20 K | 753 K/s | 736 MiB/s | 0 |
| 16 | 1 KiB shared payload | 39.3 ms | 20 K | 509 K/s | 497 MiB/s | 0 |
| 32 | 1 KiB shared payload | 63.1 ms | 20 K | 317 K/s | 309 MiB/s | 0 |

Interpretation:

- Delivery remained lossless for all tested depths.
- With 1 KiB shared payload, depth 1 -> 32 increased elapsed time by ~3.2x for 32x more passthrough stages, showing useful pipeline overlap.
- Because the 1 KiB payload is passed by `shared_ptr`, this benchmark measures scheduling, queueing, and message-handle movement more than memory-copy bandwidth.
- The timestamp-payload depth 8 result is noisier than adjacent points, so final publication should use repeated-run medians.

### 4.5 Worker Scaling

Benchmark: `BM_ReportPipelineWorkerScaling_Blocking`, 8-stage linear passthrough, 20K messages, queue size 10K.

| Workers | Elapsed time | Sink received | Throughput | Avg latency | Delivery status |
|---------|--------------|---------------|------------|-------------|-----------------|
| 1 | 5016 ms | 9.09 K | 1.81 K/s | 13.9 ms | Timed out |
| 2 | 27.2 ms | 20 K | 736 K/s | 14.7 ms | Complete |
| 4 | 22.4 ms | 20 K | 894 K/s | 8.59 ms | Complete |
| 8 | 28.9 ms | 20 K | 692 K/s | 10.1 ms | Complete |
| 16 | 40.1 ms | 20 K | 499 K/s | 18.4 ms | Complete |

Interpretation:

- The best point in this smoke run was 4 workers.
- 1 worker timed out because no-input source actors are still primed and rescheduled by the executor. With one worker, that idle source scheduling can starve downstream actors.
- 8/16 workers did not improve this tiny passthrough workload because coordination overhead dominates useful work.

### 4.6 Diamond Join Branch Scaling

Benchmark: `BM_ReportPipelineDiamondBranches_BlockingJoin`, 10K source messages, blocking output, 8 executor workers, `OnAllInputs` join sink.

| Branches | Elapsed time | Joined messages | Throughput | Avg latency | Drops |
|----------|--------------|-----------------|------------|-------------|-------|
| 2 | 15.4 ms | 10 K | 649 K/s | 6.68 ms | 0 |
| 4 | 29.7 ms | 10 K | 336 K/s | 500 us | 0 |
| 8 | 55.6 ms | 10 K | 180 K/s | 246 us | 0 |
| 16 | 55.1 ms | 10 K | 182 K/s | 21.2 ms | 0 |

Interpretation:

- Join correctness held across 2, 4, 8, and 16 branches with no drops.
- Branch count increases edge traffic and synchronization work: port dequeues scale from 40K at 2 branches to 320K at 16 branches.
- Average latency is not stable enough to publish as a tail-latency claim yet; percentile collection is required.

### 4.7 Queue Capacity Under Overload

Benchmark: `BM_ReportPipelineQueueCapacity_NonBlocking`, 4-stage linear pipeline, non-blocking `DropTail`, 50K source messages, each pass stage sleeps 10 us.

| Queue capacity | Elapsed time | Sink received | Dropped | Throughput | Avg latency | Peak depth |
|----------------|--------------|---------------|---------|------------|-------------|------------|
| 8 | 6.17 ms | 156 | 49.84 K | 25.3 K/s | 489 us | 9 |
| 16 | 6.00 ms | 240 | 49.76 K | 40.0 K/s | 615 us | 17 |
| 32 | 6.10 ms | 280 | 49.72 K | 45.9 K/s | 905 us | 33 |
| 64 | 6.50 ms | 329 | 49.67 K | 50.6 K/s | 1.46 ms | 65 |
| 128 | 8.32 ms | 367 | 49.63 K | 44.1 K/s | 2.78 ms | 129 |
| 256 | 10.0 ms | 518 | 49.48 K | 51.7 K/s | 4.10 ms | 257 |

Interpretation:

- This is an intentional overload test: the producer pushes far faster than downstream stages can consume.
- Larger queues preserve more messages but also increase average latency because messages wait longer before processing.
- Peak depth is one above the configured capacity in the current stats model, which should be investigated before using peak depth as a strict capacity invariant.

### 4.8 Payload Size Impact

Benchmark: `BM_ReportPipelinePayloadSize_Blocking`, 4-stage linear pipeline, 20K messages, shared pointer payload, blocking output.

| Payload size | Elapsed time | Sink received | Throughput | Effective payload rate | Drops |
|--------------|--------------|---------------|------------|------------------------|-------|
| 64 B | 21.1 ms | 20 K | 949 K/s | 57.9 MiB/s | 0 |
| 256 B | 23.8 ms | 20 K | 841 K/s | 205 MiB/s | 0 |
| 1 KiB | 22.7 ms | 20 K | 880 K/s | 859 MiB/s | 0 |
| 4 KiB | 19.3 ms | 20 K | 1.038 M/s | 4.05 GiB/s | 0 |
| 16 KiB | 23.4 ms | 20 K | 855 K/s | 13.1 GiB/s | 0 |
| 64 KiB | 19.4 ms | 20 K | 1.032 M/s | 63.0 GiB/s | 0 |

Interpretation:

- Payload size had little effect on elapsed time because the benchmark passes `shared_ptr<vector<char>>` through `Message`, so payload bytes are not copied on each edge.
- The effective payload rate is a derived logical throughput number, not a physical memory bandwidth measurement.
- A separate mutating-payload benchmark is needed to measure COW clone cost under downstream writes.

## 5. Runtime Strengths

### 5.1 Cheap Message Sharing

`Message` uses type erasure plus copy-on-write semantics. This makes fan-out cheap when downstream modules only read the payload, because message copies share the same payload object.

### 5.2 Centralized Scheduling

The runtime has moved away from a strict one-thread-per-module model. Actors are registered with a shared executor and scheduled onto a work-stealing thread pool, which is a better foundation for mixed topology workloads.

### 5.3 Built-in Backpressure Visibility

Port stats expose push attempts, enqueue count, drop count, reject count, dequeue count, current depth, and peak depth. This is enough to build monitoring around overload, queue saturation, and downstream lag.

### 5.4 Join Support

`OnAllInputs` modules can synchronize messages by `messageId`, and runtime stats expose pending join groups plus timeout/overflow drops. This is useful for fork-join and fusion-style pipelines.

## 6. Known Limitations Before a Final Report

### 6.1 Latency Percentiles Are Missing

The current pipeline benchmarks publish average latency, but a production-grade report should include P50, P90, P99, max, and possibly jitter. This requires collecting per-message latency samples or adding a histogram dependency.

### 6.2 Message Timestamp Precision Is Too Low for Microsecond Claims

`MessageMeta.timestamp` currently stores millisecond-resolution system time. That is fine for join timeout bookkeeping, but not enough for microsecond latency reporting. Add a monotonic nanosecond timestamp or benchmark-local latency probe before publishing microsecond latency claims.

### 6.3 Runtime Queue and Queue Benchmarks Use Different Paths

The pipeline runtime uses `LockBaseQueue<Message>`. Lock-free queue benchmarks are useful exploratory data, but they do not describe the shipping pipeline path yet.

### 6.4 Source Scheduling Can Affect Tail Latency

Source actors that produce no output rely on `idleWaitUs`. This is simple and safe, but event-driven or timer-driven source scheduling would produce cleaner latency behavior and clearer reports.

The worker-scaling benchmark exposed a concrete case: with one executor worker, an idle no-input source actor can consume enough scheduling time that downstream actors time out. Manual/external source modules should not be primed as continuously runnable sources unless they actually produce work from `Process()`.

### 6.5 Join Policy Is Message-ID Based

Current join behavior is suitable for forked copies of the same message. Multi-source stream fusion may need timestamp-window joins, watermarks, late-data handling, and configurable join policies.

## 7. Recommended Benchmark Matrix

To produce a detailed NexusFlow-specific report, add the following benchmark groups.

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

## 8. Recommended Framework Changes

| Priority | Change | Why it matters |
|----------|--------|----------------|
| P0 | Add monotonic nanosecond timing for benchmark latency. | Required for credible microsecond latency reporting. |
| P0 | Add percentile latency collection to pipeline benchmarks. | Average latency hides tail latency and queue backlog. |
| P0 | Make runtime queue backend explicit in `PipelineConfig`. | Prevents mixing lock-based runtime claims with lock-free microbenchmarks. |
| P0 | Fix source actor scheduling for manual sources. | 1-worker report benchmark timed out because an idle no-input source actor can starve downstream work. |
| P1 | Add `KeepLatest` as a first-class pipeline drop policy. | Important for video/streaming workloads where newest data matters most. |
| P1 | Add benchmark matrix for depth, workers, branches, queue capacity, and payload size. | Needed for a detailed NexusFlow performance report. |
| P1 | Add source scheduling modes: manual, polling, timer/event driven. | Reduces `idleWaitUs` noise and clarifies stream latency behavior. |
| P2 | Add join strategy abstraction beyond `messageId` joins. | Enables multi-source stream fusion and more realistic fork-join reports. |
| P2 | Add executor stats such as task submissions, steals, idle wakeups, and max task backlog. | Helps explain scaling behavior rather than only reporting outcomes. |

## 9. Suggested Reporting Rules

- Report lock-free queue numbers as queue microbenchmarks until the pipeline runtime uses that queue.
- Report pipeline throughput in both source messages/s and sink messages/s for fan-out topologies.
- Separate blocking and non-blocking output results; they answer different product questions.
- Treat smoke numbers as directional; publish final numbers only after repeated runs with stable CPU frequency, pinned environment notes, and JSON artifacts.
- Include drop/reject counters next to every throughput table so high throughput is not mistaken for full delivery.

## 10. Next Steps

1. Add percentile latency benchmark support.
2. Add a pipeline benchmark matrix for depth, branch count, worker count, queue capacity, and payload size.
3. Decide whether NexusFlow's production queue path should remain lock-based or become configurable.
4. Re-run benchmarks on the target Linux/WSL2 environment and replace smoke values with final repeated-run medians.
5. Generate charts for queue throughput, pipeline topology scaling, and latency distribution.
