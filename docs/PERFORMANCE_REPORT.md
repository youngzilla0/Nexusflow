# NexusFlow Performance Benchmark Report

> Status: draft based on the current benchmark suite and one local smoke run after the single-worker scheduling fix.
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
| Pipeline depth scaling | 20K-message linear passthrough with 1 KiB shared payload went from 15.7 ms at depth 1 to 50.8 ms at depth 32 in the current run. | Depth overhead is visible but still sublinear versus node-count growth. |
| Worker scaling | 8-stage linear benchmark completed at 1/2/4/8/16 workers and peaked at 2 workers in this smoke run. | Single-worker timeout has been fixed; tiny passthrough workloads still prefer low worker counts. |
| Fan-out/join topology | Diamond join benchmark delivered 10K joined messages at 2/4/8/16 branches with no drops. | Join path is functional; branch count increases coordination cost. |
| Non-blocking overload behavior | Queue-capacity overload benchmark shows high drop counts and bounded queue depth. | Backpressure/drop accounting is visible and testable. |
| Queue path | Pipeline uses `LockBaseQueue<Message>`; lock-free queues are currently standalone benchmark targets. | Important reporting boundary. |
| Observability | Per-port and per-node statistics cover enqueue/dequeue/drop/reject and join state; benchmark sinks also report latency percentiles. | Enough for report-grade benchmark analysis, but built-in runtime histogram telemetry is still missing. |

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
| `BenchmarkPipelineBaseline.cpp` | Baseline single-path, linear, diamond blocking, and diamond non-blocking throughput. |
| `BenchmarkPipelineDepth.cpp` | Linear depth scaling with timestamp and 1 KiB shared payloads. |
| `BenchmarkPipelineLatency.cpp` | End-to-end latency percentiles by linear depth, payload type, and diamond join branch count. |
| `BenchmarkPipelineScaling.cpp` | Worker scaling and diamond join branch scaling. |
| `BenchmarkPipelineBackpressure.cpp` | Queue capacity behavior under non-blocking overload. |
| `BenchmarkPipelinePayload.cpp` | Payload-size sensitivity with shared payload handles. |

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
| LockFreeQueue | 2 | 12.94 M/s |
| LockFreeQueue | 4 | 5.94 M/s |
| LockFreeQueue | 8 | 3.64 M/s |
| LockFreeQueue | 16 | 1.74 M/s |

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

For this section, the report uses the custom `ElapsedUs` counter rather than Google Benchmark's leftmost `Time` column. `ElapsedUs` is the explicit wall-clock interval measured around "send all messages -> wait for delivery -> wait for drain", which matches the report's intended end-to-end elapsed-time definition more closely.

`Avg elapsed / msg` means `ElapsedUs / SinkReceived`. It is different from `Avg latency`: the former is amortized total wall-clock cost per delivered message for the whole burst, while the latter is per-message end-to-end waiting time observed at the sink.

| Linear depth | Payload | Elapsed time | Avg elapsed / msg | Sink received | Throughput | Avg latency | Drops |
|--------------|---------|--------------|-------------------|---------------|------------|-------------|-------|
| 1 | 8 B timestamp | 15.1 ms | 0.76 us | 20 K | 1.323 M/s | 4.31 ms | 0 |
| 2 | 8 B timestamp | 16.2 ms | 0.81 us | 20 K | 1.235 M/s | 5.31 ms | 0 |
| 4 | 8 B timestamp | 19.9 ms | 0.99 us | 20 K | 1.006 M/s | 5.71 ms | 0 |
| 8 | 8 B timestamp | 24.3 ms | 1.22 us | 20 K | 822 K/s | 8.77 ms | 0 |
| 16 | 8 B timestamp | 33.1 ms | 1.66 us | 20 K | 604 K/s | 15.5 ms | 0 |
| 32 | 8 B timestamp | 51.6 ms | 2.58 us | 20 K | 388 K/s | 25.7 ms | 0 |

Benchmark: `BM_ReportPipelineLinearDepthPayload1KiB_Blocking`, 20K messages, blocking output, 4 executor workers, queue size 10K. Payload is a `shared_ptr<vector<char>>` containing 1 KiB, so the payload object is shared through `Message` and not copied at each edge.

| Linear depth | Payload | Elapsed time | Avg elapsed / msg | Sink received | Throughput | Effective payload rate | Drops |
|--------------|---------|--------------|-------------------|---------------|------------|------------------------|-------|
| 1 | 1 KiB shared payload | 15.7 ms | 0.79 us | 20 K | 1.272 M/s | 1.21 GiB/s | 0 |
| 2 | 1 KiB shared payload | 16.9 ms | 0.84 us | 20 K | 1.187 M/s | 1.13 GiB/s | 0 |
| 4 | 1 KiB shared payload | 19.8 ms | 0.99 us | 20 K | 1.011 M/s | 988 MiB/s | 0 |
| 8 | 1 KiB shared payload | 23.8 ms | 1.19 us | 20 K | 841 K/s | 821 MiB/s | 0 |
| 16 | 1 KiB shared payload | 32.8 ms | 1.64 us | 20 K | 610 K/s | 595 MiB/s | 0 |
| 32 | 1 KiB shared payload | 50.8 ms | 2.54 us | 20 K | 394 K/s | 385 MiB/s | 0 |

Interpretation:

- Delivery remained lossless for all tested depths.
- With 1 KiB shared payload, depth 1 -> 32 increased elapsed time by ~3.2x for 32x more passthrough stages, showing useful pipeline overlap.
- Because the 1 KiB payload is passed by `shared_ptr`, this benchmark measures scheduling, queueing, and message-handle movement more than memory-copy bandwidth.
- The timestamp baseline shows a clear inflection after depth 2, while the 1 KiB shared-payload curve stays comparatively smooth.
- Final publication should still use repeated-run medians, because these numbers are sensitive to scheduler noise.

### 4.5 Worker Scaling

Benchmark: `BM_ReportPipelineWorkerScaling_Blocking`, 8-stage linear passthrough, 20K messages, queue size 10K.

| Workers | Elapsed time | Sink received | Throughput | Avg latency | Delivery status |
|---------|--------------|---------------|------------|-------------|-----------------|
| 1 | 27.0 ms | 20 K | 741 K/s | 10.2 ms | Complete |
| 2 | 19.7 ms | 20 K | 1.015 M/s | 7.36 ms | Complete |
| 4 | 22.2 ms | 20 K | 901 K/s | 8.38 ms | Complete |
| 8 | 28.2 ms | 20 K | 710 K/s | 9.83 ms | Complete |
| 16 | 30.9 ms | 20 K | 648 K/s | 11.9 ms | Complete |

Interpretation:

- The best point in this smoke run was 2 workers.
- 1 worker now completes without delivery or drain timeout after adding manual source scheduling and FIFO task submission fairness.
- 8/16 workers did not improve this tiny passthrough workload because coordination overhead dominates useful work.

### 4.6 Diamond Join Branch Scaling

Benchmark: `BM_ReportPipelineDiamondBranches_BlockingJoin`, 10K source messages, blocking output, 8 executor workers, `OnAllInputs` join sink.

| Branches | Elapsed time | Joined messages | Throughput | Avg latency | Drops |
|----------|--------------|-----------------|------------|-------------|-------|
| 2 | 9.83 ms | 10 K | 1.017 M/s | 2.46 ms | 0 |
| 4 | 25.0 ms | 10 K | 401 K/s | 1.91 ms | 0 |
| 8 | 51.8 ms | 10 K | 193 K/s | 1.91 ms | 0 |
| 16 | 101.6 ms | 10 K | 98.4 K/s | 975 us | 0 |

Interpretation:

- Join correctness held across 2, 4, 8, and 16 branches with no drops.
- Branch count increases edge traffic and synchronization work: port dequeues scale from 40K at 2 branches to 320K at 16 branches.
- Average latency is not stable enough to publish as a tail-latency claim yet; percentile collection is required.

### 4.7 Queue Capacity Under Overload

Benchmark: `BM_ReportPipelineQueueCapacity_NonBlocking`, 4-stage linear pipeline, non-blocking `DropTail`, 50K source messages, each pass stage sleeps 10 us.

| Queue capacity | Elapsed time | Sink received | Dropped | Throughput | Avg latency | Peak depth |
|----------------|--------------|---------------|---------|------------|-------------|------------|
| 8 | 5.44 ms | 218 | 49.78 K | 40.1 K/s | 344 us | 9 |
| 16 | 5.51 ms | 197 | 49.80 K | 35.8 K/s | 607 us | 17 |
| 32 | 5.77 ms | 268 | 49.73 K | 46.4 K/s | 844 us | 33 |
| 64 | 6.03 ms | 327 | 49.67 K | 54.2 K/s | 1.23 ms | 65 |
| 128 | 7.31 ms | 390 | 49.61 K | 53.4 K/s | 2.15 ms | 128 |
| 256 | 11.6 ms | 517 | 49.48 K | 44.6 K/s | 4.71 ms | 257 |

Interpretation:

- This is an intentional overload test: the producer pushes far faster than downstream stages can consume.
- Larger queues preserve more messages but also increase average latency because messages wait longer before processing.
- Peak depth is one above the configured capacity in the current stats model, which should be investigated before using peak depth as a strict capacity invariant.

### 4.8 Payload Size Impact

Benchmark: `BM_ReportPipelinePayloadSize_Blocking`, 4-stage linear pipeline, 20K messages, shared pointer payload, blocking output.

| Payload size | Elapsed time | Sink received | Throughput | Effective payload rate | Drops |
|--------------|--------------|---------------|------------|------------------------|-------|
| 64 B | 20.5 ms | 20 K | 973 K/s | 59.4 MiB/s | 0 |
| 256 B | 22.0 ms | 20 K | 909 K/s | 222 MiB/s | 0 |
| 1 KiB | 21.9 ms | 20 K | 914 K/s | 892 MiB/s | 0 |
| 4 KiB | 18.4 ms | 20 K | 1.090 M/s | 4.16 GiB/s | 0 |
| 16 KiB | 21.9 ms | 20 K | 914 K/s | 14.0 GiB/s | 0 |
| 64 KiB | 22.5 ms | 20 K | 887 K/s | 54.2 GiB/s | 0 |

Interpretation:

- Payload size had little effect on elapsed time because the benchmark passes `shared_ptr<vector<char>>` through `Message`, so payload bytes are not copied on each edge.
- The effective payload rate is a derived logical throughput number, not a physical memory bandwidth measurement.
- A separate mutating-payload benchmark is needed to measure COW clone cost under downstream writes.

### 4.9 End-to-End Latency Distribution

Benchmark: `BM_ReportPipelineLinearDepthLatency_Blocking`, 2K samples, blocking output, queue size 16, 4 executor workers. The benchmark sends one message and waits for its delivery before sending the next, keeping queue depth near 1 so the table reflects low-load per-message latency rather than burst backlog.

| Linear depth | Payload | P50 | P90 | P99 | Max | Drops |
|--------------|---------|-----|-----|-----|-----|-------|
| 1 | 8 B timestamp | 5.00 us | 7.71 us | 23.33 us | 47.04 us | 0 |
| 2 | 8 B timestamp | 5.92 us | 9.54 us | 25.25 us | 68.88 us | 0 |
| 4 | 8 B timestamp | 8.33 us | 13.21 us | 37.83 us | 98.50 us | 0 |
| 8 | 8 B timestamp | 9.83 us | 17.79 us | 43.58 us | 656.29 us | 0 |
| 16 | 8 B timestamp | 14.71 us | 28.75 us | 64.75 us | 766.71 us | 0 |
| 32 | 8 B timestamp | 20.67 us | 45.63 us | 81.88 us | 156.46 us | 0 |

Benchmark: `BM_ReportPipelineLinearDepthPayload1KiBLatency_Blocking`, same setup, but payload is a `shared_ptr<vector<char>>` containing 1 KiB.

| Linear depth | Payload | P50 | P90 | P99 | Max | Drops |
|--------------|---------|-----|-----|-----|-----|-------|
| 1 | 1 KiB shared payload | 5.08 us | 7.92 us | 23.58 us | 75.79 us | 0 |
| 2 | 1 KiB shared payload | 5.92 us | 9.71 us | 28.17 us | 64.63 us | 0 |
| 4 | 1 KiB shared payload | 8.21 us | 13.46 us | 34.08 us | 69.13 us | 0 |
| 8 | 1 KiB shared payload | 9.92 us | 15.75 us | 37.17 us | 58.33 us | 0 |
| 16 | 1 KiB shared payload | 10.92 us | 18.04 us | 46.38 us | 97.21 us | 0 |
| 32 | 1 KiB shared payload | 20.13 us | 45.13 us | 79.13 us | 144.29 us | 0 |

Benchmark: `BM_ReportPipelineDiamondJoinLatency_Blocking`, 2K samples, blocking output, queue size 16, 8 executor workers. Topology is `Source -> N passthrough branches -> OnAllInputs Join`. The join latency is measured from source send timestamp to successful join completion.

| Branches | Topology | P50 | P90 | P99 | Max | Drops |
|----------|----------|-----|-----|-----|-----|-------|
| 2 | Diamond join | 8.50 us | 14.63 us | 35.13 us | 487.79 us | 0 |
| 4 | Diamond join | 11.29 us | 22.46 us | 38.25 us | 67.46 us | 0 |
| 8 | Diamond join | 28.50 us | 38.96 us | 60.67 us | 96.71 us | 0 |
| 16 | Diamond join | 41.08 us | 57.25 us | 83.63 us | 134.50 us | 0 |

Interpretation:

- Low-load P50 latency stays around 5-10 us through depth 8 for both payload modes.
- At depth 32, P99 remains under 82 us for the timestamp payload and under 80 us for the 1 KiB shared payload in this smoke run.
- The 1 KiB payload does not materially change median latency because the payload is shared by handle rather than copied at each edge.
- Diamond join latency scales predictably with branch count in this run: P50 rises from 8.50 us at 2 branches to 41.08 us at 16 branches.
- Max latency is noisier than percentile latency and should be reported with environment notes.

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

### 6.1 Runtime Latency Histograms Are Not Built In

The benchmark suite now reports P50, P90, P99, and max latency using benchmark-specific sink modules. The runtime itself still exposes counter snapshots rather than latency histograms, so production telemetry would need a runtime observer or histogram extension.

### 6.2 Message Timestamp Precision Is Too Low for Microsecond Claims

`MessageMeta.timestamp` currently stores millisecond-resolution system time. That is fine for join timeout bookkeeping, but not enough for microsecond latency reporting. Add a monotonic nanosecond timestamp or benchmark-local latency probe before publishing microsecond latency claims.

### 6.3 Runtime Queue and Queue Benchmarks Use Different Paths

The pipeline runtime uses `LockBaseQueue<Message>`. Lock-free queue benchmarks are useful exploratory data, but they do not describe the shipping pipeline path yet.

### 6.4 Source Scheduling Can Affect Tail Latency

Source actors that produce no output rely on `idleWaitUs`. This is simple and safe, but event-driven or timer-driven source scheduling would produce cleaner latency behavior and clearer reports.

Manual/external source modules now opt out of polling with `Module::SourcePolicy::Manual`, and the thread pool uses FIFO local submission to avoid single-worker actor continuation starvation. Polling sources are still continuously runnable by design, so timer/event-driven source scheduling remains a useful future extension.

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
| P0 | Add runtime-level latency histograms or observer hooks. | Benchmarks now have percentiles, but production telemetry still only has counter snapshots. |
| P0 | Make runtime queue backend explicit in `PipelineConfig`. | Prevents mixing lock-based runtime claims with lock-free microbenchmarks. |
| Done | Fix source actor scheduling for manual sources and single-worker task fairness. | The 1-worker report benchmark now completes with `DeliveryTimedOut=0` and `DrainTimedOut=0`. |
| P1 | Add `KeepLatest` as a first-class pipeline drop policy. | Important for video/streaming workloads where newest data matters most. |
| P1 | Add benchmark matrix for depth, workers, branches, queue capacity, and payload size. | Needed for a detailed NexusFlow performance report. |
| P1 | Add timer/event-driven source scheduling modes. | Manual and polling modes exist; timer/event-driven scheduling would further reduce `idleWaitUs` noise. |
| P2 | Add join strategy abstraction beyond `messageId` joins. | Enables multi-source stream fusion and more realistic fork-join reports. |
| P2 | Add executor stats such as task submissions, steals, idle wakeups, and max task backlog. | Helps explain scaling behavior rather than only reporting outcomes. |

## 9. Suggested Reporting Rules

- Report lock-free queue numbers as queue microbenchmarks until the pipeline runtime uses that queue.
- Report pipeline throughput in both source messages/s and sink messages/s for fan-out topologies.
- Separate blocking and non-blocking output results; they answer different product questions.
- Treat smoke numbers as directional; publish final numbers only after repeated runs with stable CPU frequency, pinned environment notes, and JSON artifacts.
- Include drop/reject counters next to every throughput table so high throughput is not mistaken for full delivery.

## 10. Next Steps

1. Decide whether NexusFlow's production queue path should remain lock-based or become configurable.
2. Add runtime-level latency observer hooks if production telemetry needs P50/P99 outside benchmarks.
3. Re-run benchmarks on the target Linux/WSL2 environment and replace smoke values with final repeated-run medians.
4. Generate charts for queue throughput, pipeline topology scaling, and latency distribution.
5. Add mutating-payload benchmarks to quantify COW clone cost under downstream writes.
