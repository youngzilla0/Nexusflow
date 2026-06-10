# NexusFlow

NexusFlow is a learning-oriented C++14 dataflow framework for building DAG pipelines from reusable modules.
The current runtime model is:

- `Pipeline` owns one `Executor`
- `Executor` owns one `ThreadPool`
- each module is scheduled as a logical actor on that thread pool
- data flows through bounded `MessageQueue`s
- module business code uses ports, not raw queues

## What Changed

The project now uses a port-oriented processing API instead of `Process(Message&)` plus implicit join behavior.

```cpp
virtual void Process(const nexusflow::PortInputsView& inputs,
                     nexusflow::PortOutputs& outputs) = 0;
```

New concepts:

- `PipelineContext`: implicit module context for pipeline name, runtime config, and executor thread count
- `TriggerPolicy`: controls when a module is scheduled
- `PortInputsView`: simple read-only view of input ports
- `PortOutputs`: emit to all downstream edges or route to a named output port, with explicit blocking mode
- `QueueFullPolicy`: controls how non-blocking sends behave on full queues
- `Pipeline::GetPortStats()`: snapshots per-edge enqueue, drop, dequeue, and depth stats
- `Pipeline::GetActorStats()` / `PipelineObserver`: snapshots per-actor processing, drop, and join state

## Build

```bash
mkdir -p build
cd build
cmake ..
make -j4
```

Run tests:

```bash
./tests/nexusflow_tests
```

Run benchmarks:

```bash
./benchmarks/nexusflow_benchmarks
```

## Quick Start

### Programmatic pipeline

```cpp
#include <nexusflow/Nexusflow.hpp>

class DoublerModule : public nexusflow::Module {
public:
    explicit DoublerModule(std::string name) : Module(std::move(name)) {}

    void Process(const nexusflow::PortInputsView& inputs,
                 nexusflow::PortOutputs& outputs) override {
        auto* input = inputs.OnlyAs<int>();
        if (input == nullptr) {
            return;
        }

        outputs.Emit(nexusflow::MakeMessage((*input) * 2, GetModuleName()), true);
    }
};

auto pipeline = nexusflow::PipelineBuilder()
    .AddModule(std::make_shared<DoublerModule>("Doubler"))
    .WithConfig(nexusflow::PipelineConfig{
        0,      // executorThreadCount, 0 = auto
        1024,   // queueSize
        50,     // idleWaitUs
        60000,  // fusionTimeoutMs
        1024,   // maxPendingJoinGroups
        nexusflow::QueueFullPolicy::DropTail
    })
    .Build();
```

### YAML pipeline

```yaml
graph:
  name: VideoAnalyticsPipeline

  modules:
    - name: StreamPuller
      class: MyStreamPullerModule

    - name: Decoder
      class: MyDecoderModule
      config:
        skipInterval: 25

    - name: Fusion
      class: MyFusionModule

  connections:
    - from: StreamPuller
      to: Decoder

    - from: Decoder
      fromPort: out
      to: Fusion
      toPort: image
```

`fromPort` and `toPort` are optional. If omitted, the defaults are `out` and `in`.

## Writing Modules

### Single-input module

```cpp
class PersonDetector : public nexusflow::Module {
public:
    explicit PersonDetector(std::string name) : Module(std::move(name)) {}

    void Process(const nexusflow::PortInputsView& inputs,
                 nexusflow::PortOutputs& outputs) override {
        auto* frame = inputs.OnlyAs<Frame>();
        if (frame == nullptr) {
            return;
        }

        outputs.Emit(nexusflow::MakeMessage(Detect(*frame), GetModuleName()), true);
    }
};
```

### Multi-input fusion module

```cpp
class FusionModule : public nexusflow::Module {
public:
    explicit FusionModule(std::string name) : Module(std::move(name)) {
        SetTriggerPolicy(TriggerPolicy::OnAllInputs);
    }

    void Process(const nexusflow::PortInputsView& inputs,
                 nexusflow::PortOutputs& outputs) override {
        auto* head = inputs.Get<Inference>("head");
        auto* person = inputs.Get<Inference>("person");
        if (head == nullptr || person == nullptr) {
            return;
        }

        outputs.Emit(nexusflow::MakeMessage(Merge(*head, *person), GetModuleName()), true);
    }
};
```

### Source modules

Source modules simply ignore `inputs` and emit from `Process()`.

```cpp
void Process(const nexusflow::PortInputsView& inputs,
             nexusflow::PortOutputs& outputs) override {
    (void)inputs;
    outputs.Emit(nexusflow::MakeMessage(ReadNextFrame(), GetModuleName()), true);
}
```

For external/manual injection use cases such as benchmarks, protected helpers `Broadcast()` and `SendTo()` are still available inside derived modules.

## Runtime Model

1. `PipelineBuilder` or `CreateFromYaml()` constructs a DAG.
2. `Pipeline` creates one `PipelineContext`.
3. `Pipeline` creates one shared `Executor`.
4. `Executor` creates a `ThreadPool` when `Start()` is called.
5. Each module actor runs on the executor and is triggered according to its `TriggerPolicy`.

`TriggerPolicy` values:

- `Auto`: currently resolves to `OnAnyInput`
- `OnAnyInput`: run once for each arrived message
- `OnAllInputs`: wait until every input port has a matching `messageId`

## PipelineContext

Modules access runtime context implicitly:

```cpp
const auto& ctx = GetPipelineContext();
auto threadCount = ctx.GetExecutorThreadCount();
auto queueSize = ctx.GetConfig().queueSize;
```

This keeps `Process()` focused on data handling instead of plumbing.

## Runtime Stats

Pipelines expose per-edge runtime snapshots:

```cpp
for (const auto& stats : pipeline->GetPortStats()) {
    std::cout << stats.srcModuleName << ":" << stats.srcPortName
              << " -> " << stats.dstModuleName << ":" << stats.dstPortName
              << " dropped=" << stats.dropCount
              << " depth=" << stats.currentDepth
              << " peak=" << stats.peakDepth << std::endl;
}
```

Pipelines also expose per-actor runtime snapshots and an observer that aggregates edge and actor views:

```cpp
nexusflow::PipelineObserver observer(*pipeline);
std::cout << observer.Describe() << std::endl;
```

## Test Naming

The repo now follows a simple naming rule for gtests:

- suite name: `XxxTest`
- case name: `Behavior_Scenario`

Examples:

- `GraphTest.ToEdgeListBfs_MatchesExpectedTraversalOrder`
- `PipelineRuntimeTest.NonBlockingDropHead_DropsOldestAndPreservesLatestMessage`
- `MessageTest.CopyOnWrite_ValueMutationDoesNotAffectOriginal`

This keeps `--gtest_filter` readable and makes it easier to scan failures in CI or local runs.

## Current Known Limitations

- YAML currently describes topology and module config, but not `PipelineConfig`.
- `TriggerPolicy::OnAllInputs` requires distinct input port names and matches only by `messageId`.
- `TriggerPolicy::OnAllInputs` uses a simple pending-group cache; it does not yet support watermarks or time windows.
- `QueueFullPolicy` currently applies to non-blocking sends only; blocking sends still wait for capacity.
- The real runtime queue is `LockBaseQueue<Message>`, so lock-free queue benchmarks are informative but not representative of pipeline execution.
- Source actors still use `idleWaitUs` when they have no output, and multi-input actors still scan input queues on each activation.

## Benchmarks

See [benchmarks/README.md](/Users/yang/Code/Nexusflow/benchmarks/README.md) for benchmark commands, recent observations, and current bottlenecks.
Benchmark names follow `BM_<DomainOrComponent>_<Operation>_<Scenario>` so filters stay readable as the suite grows.

## Examples

- [examples/1-how-to-use](/Users/yang/Code/Nexusflow/examples/1-how-to-use)
- [examples/2-linear-vision-pipline](/Users/yang/Code/Nexusflow/examples/2-linear-vision-pipline)
- [examples/3-joined-vision-pipline](/Users/yang/Code/Nexusflow/examples/3-joined-vision-pipline)

## Architecture Notes

Detailed design notes live in [docs/ARCHITECTURE.md](/Users/yang/Code/Nexusflow/docs/ARCHITECTURE.md).
