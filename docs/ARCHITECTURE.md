# NexusFlow Architecture

For a Chinese walkthrough focused on quickly re-familiarizing yourself with the codebase, see [ARCHITECTURE_ZH.md](ARCHITECTURE_ZH.md).

## 1. Runtime Shape

The current runtime is pipeline-scoped:

```text
Pipeline
  ├─ PipelineContext
  ├─ Graph
  ├─ Executor
  │   └─ ThreadPool
  └─ ModuleActor[N]
```

Key decisions:

- one `Executor` per `Pipeline`
- one `ThreadPool` per `Executor`
- modules are logical actors, not dedicated OS threads
- user modules process port views, not queue batches

## 2. Public API Model

### Module processing

```cpp
virtual void Process(const PortInputsView& inputs,
                     PortOutputs& outputs) = 0;
```

`PortInputsView` keeps business code simple:

- `OnlyAs<T>()`: single-input fast path
- `Get<T>("port")`: named-port access
- `OnlyMessage()` / `GetMessage("port")`: raw `Message` access when needed

`PortOutputs` keeps emission explicit:

- `Emit(msg, blocking)`: broadcast to every downstream subscriber
- `Set("port", msg, blocking)`: route to a named output port

### TriggerPolicy

```cpp
enum class TriggerPolicy {
    Auto,
    OnAnyInput,
    OnAllInputs
};
```

Semantics:

- `Auto`: currently treated as `OnAnyInput`
- `OnAnyInput`: any arrived message triggers one `Process()` call
- `OnAllInputs`: the executor waits until all input ports have a message with the same `messageId`

This makes synchronization a scheduling concern rather than something hidden inside `Process()`.

### PipelineContext

`PipelineContext` is injected implicitly into each module and exposes:

- pipeline name
- `PipelineConfig`
- resolved executor thread count

Modules read it through `GetPipelineContext()`.

## 3. Graph and Ports

`Graph` stores:

- nodes
- directed edges
- optional `fromPort` / `toPort` metadata per edge

Default ports:

- output: `out`
- input: `in`

Port metadata is used only for routing and fusion. The `Message` payload remains independent from the graph topology.

## 4. Executor Responsibilities

The executor owns all runtime scheduling logic.

### 4.1 Actor registration

Each `ModuleActor` registers:

- module instance
- runtime config snapshot
- input queues
- output subscribers

### 4.2 Start / Stop

On `Start()`:

1. resolve thread count
2. create `ThreadPool`
3. prime runnable actors
4. submit short actor tasks on demand

On `Stop()`:

1. flip stop flag
2. stop thread pool
3. let queues finish shutdown

### 4.3 OnAnyInput scheduling

For normal modules the executor:

1. schedules the actor when an upstream enqueue succeeds
2. scans input queues round-robin for available work
3. wraps the arrived item as one `PortMessage`
4. builds a `PortInputsView`
5. calls `Process()`
6. dispatches `PortOutputs`

### 4.4 OnAllInputs scheduling

For fusion modules the executor:

1. caches messages by `messageId`
2. groups them by input port
3. calls `Process()` once every required port is present
4. drops incomplete groups after `fusionTimeoutMs`
5. caps pending groups with `maxPendingJoinGroups`

This is intentionally simple and readable. It is not yet the final performance shape.

## 5. Message Model

`Message` is still the payload carrier.

Properties:

- type-erased
- copy-on-write
- cheap to broadcast
- metadata includes `messageId`, `timestamp`, and `sourceName`

The port layer does not replace `Message`; it replaces the old "single raw message argument" API.

## 6. PipelineConfig

Current fields:

```cpp
enum class QueueFullPolicy {
    DropTail,
    DropHead,
};

struct PipelineConfig {
    size_t executorThreadCount = 0;
    size_t queueSize = 100;
    size_t idleWaitUs = 50;
    size_t fusionTimeoutMs = 60000;
    size_t maxPendingJoinGroups = 1024;
    QueueFullPolicy nonBlockingQueueFullPolicy = QueueFullPolicy::DropTail;
};
```

Intent:

- `executorThreadCount`: explicit sizing or auto
- `queueSize`: per-edge queue capacity
- `idleWaitUs`: backoff for sparse polling
- `fusionTimeoutMs`: stale join cleanup
- `maxPendingJoinGroups`: cap for pending `OnAllInputs` groups
- `nonBlockingQueueFullPolicy`: whether a full queue drops the new item or evicts the oldest one

## 7. Runtime Stats

Each graph edge owns one runtime stats object shared by the producer side and consumer side.

Each actor also tracks:

- process count
- consumed input count
- emitted broadcast and route counts
- pending join group count
- join timeout and join overflow drops

Snapshots expose:

- source module and source port
- destination module and destination port
- blocking vs non-blocking push attempts
- enqueue, drop, reject, and dequeue counts
- current queue depth and peak depth

`Pipeline::GetPortStats()` and `Pipeline::GetActorStats()` collect these snapshots without exposing queue internals to modules.
`PipelineObserver` builds a combined actor + edge view for observability and debugging.

## 8. Data Flow Example

### Single-input path

```text
Source(out) -> Decoder(in) -> Detector(in) -> Sink(in)
```

The module sees only one message at a time:

```cpp
auto* frame = inputs.OnlyAs<Frame>();
```

### Fusion path

```text
DetectorA(out) -> Fusion(head)
DetectorB(out) -> Fusion(person)
```

The fusion module reads named ports:

```cpp
auto* head = inputs.Get<Inference>("head");
auto* person = inputs.Get<Inference>("person");
```

## 9. Why This Direction

Compared with the old `Worker + Dispatcher + ProcessBatch` model, this version is cleaner in three ways:

1. scheduling is centralized in `Executor`
2. thread ownership is explicit and pipeline-scoped
3. module business code is closer to modern graph frameworks that expose ports directly

It also makes future work easier:

- better wake-up strategy
- per-pipeline scheduling policy
- richer port metadata
- stronger delivery semantics

## 10. Current Gaps

These are known limitations of the current implementation:

- `CreateFromYaml()` still uses default `PipelineConfig`
- `OnAllInputs` depends on `messageId` only; there is no watermark or time-window fusion
- runtime stats are snapshot-oriented; there is no push-based monitoring hook yet
- the runtime queue is lock-based, so queue contention is still a meaningful cost
- source actors still rely on `idleWaitUs` when idle, and multi-input actors still scan input queues on each activation

## 11. Code Map

Important files:

- [include/nexusflow/Module.hpp](/Users/yang/Code/Nexusflow/include/nexusflow/Module.hpp)
- [include/nexusflow/PipelineContext.hpp](/Users/yang/Code/Nexusflow/include/nexusflow/PipelineContext.hpp)
- [docs/ARCHITECTURE_ROADMAP.md](/Users/yang/Code/Nexusflow/docs/ARCHITECTURE_ROADMAP.md)
- [include/nexusflow/Ports.hpp](/Users/yang/Code/Nexusflow/include/nexusflow/Ports.hpp)
- [include/nexusflow/PipelineObserver.hpp](/Users/yang/Code/Nexusflow/include/nexusflow/PipelineObserver.hpp)
- [include/nexusflow/RuntimeStats.hpp](/Users/yang/Code/Nexusflow/include/nexusflow/RuntimeStats.hpp)
- [src/executor/Executor.hpp](/Users/yang/Code/Nexusflow/src/executor/Executor.hpp)
- [src/executor/Executor.cpp](/Users/yang/Code/Nexusflow/src/executor/Executor.cpp)
- [src/pipeline/impl/PipelineImpl.cpp](/Users/yang/Code/Nexusflow/src/pipeline/impl/PipelineImpl.cpp)
