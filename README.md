# NexusFlow: High-Performance Modern C++ Dataflow Pipeline Framework

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)]()
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)]()
[![C++_Version](https://img.shields.io/badge/C++-14%2B-blue.svg)]()

**NexusFlow** is a high-performance, decoupled, dynamically configurable dataflow pipeline framework for modern C++. It helps you build complex multi-stage data processing tasks — video analytics, real-time ETL, sensor processing, AI inference pipelines — by composing independent **Modules** into a **DAG** like LEGO bricks.

---

##Core Features

- **Modular & Decoupled**: Modules communicate only via message queues, never aware of each other.
- **YAML-Driven**: Define topology, modules, parameters in a single YAML — no recompile.
- **Auto-Concurrency**: Framework spawns a dedicated thread per Module.
- **Two Construction Styles**: `PipelineBuilder` (programmatic) or `CreateFromYaml` (declarative).
- **High-Performance COW**: `Message` uses Copy-On-Write — broadcast is cheap, mutation is safe.
- **Clean Config Separation**: `ModuleConfig` (business params) and `PipelineConfig` (runtime params) are kept strictly separate.

---

## Quick Start

### Option1: Declarative (YAML) — Recommended

####1. `graph.yaml`

```yaml
graph:
 name: "VideoAnalyticsPipeline"

 modules:
 - name: "InputNode"
 class: "MockInputModule"

 - name: "ProcessNode1"
 class: "MockProcessModule"

 - name: "OutputNode"
 class: "MockOutputModule"

 connections:
 - from: "InputNode"
 to: "ProcessNode1"
 - from: "ProcessNode1"
 to: "OutputNode"
```

####2. `main.cpp`

```cpp
#include "nexusflow/Pipeline.hpp"
#include "nexusflow/ModuleFactory.hpp"
#include "my_module/MockInputModule.hpp"
#include "my_module/MockProcessModule.hpp"
#include "my_module/MockOutputModule.hpp"

using namespace nexusflow;

void registerAllModules() {
 NEXUSFLOW_REGISTER_MODULE(MockInputModule);
 NEXUSFLOW_REGISTER_MODULE(MockProcessModule);
 NEXUSFLOW_REGISTER_MODULE(MockOutputModule);
}

int main(int argc, char* argv[]) {
 registerAllModules();

 auto pipeline = Pipeline::CreateFromYaml(argv[1]);
 pipeline->Init();
 pipeline->Start();

 std::this_thread::sleep_for(std::chrono::seconds(10));

 pipeline->Stop();
 pipeline->DeInit();
 return0;
}
```

### Option2: Programmatic (`PipelineBuilder`)

```cpp
#include "nexusflow/PipelineBuilder.hpp"

auto pipeline = PipelineBuilder()
 .AddModule(std::make_shared<MockInputModule>("InputNode"))
 .AddModule(std::make_shared<MockProcessModule>("ProcessNode1"))
 .AddModule(std::make_shared<MockOutputModule>("OutputNode"))
 .Connect("InputNode", "ProcessNode1")
 .Connect("ProcessNode1", "OutputNode")
 .WithConfig(PipelineConfig{
 .maxBatchSize =32,
 .batchTimeoutMs =5,
 .queueSize =100
 })
 .Build();

pipeline->Init();
pipeline->Start();
```

---

##Architecture Overview

For full architecture details, see [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

```
┌────────────────────────────────────────────────┐
│ Public API │
│ Pipeline / PipelineBuilder / Module / Message │
└────────────────┬───────────────────────────────┘
 │
┌─────────────┴─────────────┐
 ▼ ▼
┌────────────┐┌─────────────┐
│ Graph (DAG)│ │ PipelineConfig│
└────────────┘ └─────────────┘
 │
 ▼
┌──────────────────────────────┐
│ ModuleActor (per Module) │
│┌────────────────────────┐ │
│ │ Worker (thread) │ ← batch/queue params
│ ├────────────────────────┤ │
│ │ Module (user code) │ ← business params
│ ├────────────────────────┤ │
│ │ Dispatcher │ ← broadcasts to next stage
│ └────────────────────────┘ │
└──────────────────────────────┘
 │
 ▼
┌──────────────────┐
│ MessageQueue │ ← bounded blocking queue
└──────────────────┘
```

**Key design decisions:**

1. **Module is pure business logic** — only knows about `Message` and `Broadcast/SendTo`.
2. **Worker drives the thread** — owns the batch/queue runtime parameters.
3. **Dispatcher routes output** — knows nothing about Module internals.
4. **Two configs, not one** — see [Configuration](#configuration) below.

---

##Core Component: `nexusflow::Message`

The universal thread-safe data wrapper.

### Design

1. **Type-Erasure**: A `Message` can hold any type.
2. **Copy-On-Write (COW)**: Copy is a cheap `shared_ptr` op. Mutation triggers deep-copy only if shared.
3. **Rust-inspired accessors**: `Borrow`/`Mut` make intent clear.

### Usage

```cpp
#include "nexusflow/Message.hpp"

//Create
auto msg1 = nexusflow::MakeMessage(std::string("Hello"));
auto msg2 = nexusflow::MakeMessage(std::vector<int>{1,2,3}, "SensorModule");

//Read-only (never COW)
if (const auto* p = msg1.BorrowPtr<std::string>()) {
 std::cout << *p;
}

//Mutable (triggers COW if shared)
if (auto* p = msg2.MutPtr<std::vector<int>>()) {
 p->push_back(4);
}

//Broadcast is cheap (shared_ptr copy)
Broadcast(msg2);
```

---

##Configuration

NexusFlow has **two distinct configuration systems** with strictly separated concerns.

### `ModuleConfig` — Business Parameters (YAML)

Per-module settings, loaded from `modules[name].params` in YAML:

```yaml
modules:
 - name: "Decoder"
 class: "DecoderModule"
 params:
 modelPath: "/models/yolo.engine" # ← business
 confidence: 0.7 # ← business
```

> **Note**: `syncInputs` is NOT a YAML field. To enable Fusion mode,
> override `Module::RequiresSyncInputs()` in your derived class instead.

### `PipelineConfig` — Runtime Parameters (Code)

Pipeline-wide settings, set via `PipelineBuilder::WithConfig()`:

```cpp
PipelineConfig{
 .maxBatchSize =32, // Max messages per Worker batch
 .batchTimeoutMs =5, // Wait time before flushing partial batch
 .queueSize =100 // Capacity of each inter-module queue
}
```

### Why Separate?

| Concern | ModuleConfig | PipelineConfig |
|---------|--------------|----------------|
| Origin | YAML `params:` | `PipelineBuilder::WithConfig()` |
| Scope | Single module | Entire pipeline |
| Examples | `modelPath`, `threshold`, `syncInputs` | `maxBatchSize`, `queueSize` |
| Mutability | Per-module, can differ | Uniform across all modules |

**Rule of thumb**: If it's about *what the module does*, it's `ModuleConfig`. If it's about *how the framework schedules*, it's `PipelineConfig`.

---

##Writing a Custom Module

```cpp
//1. Inherit from Module
class MultiplierModule : public nexusflow::Module {
public:
 explicit MultiplierModule(std::string name) : Module(std::move(name)) {}

 void Process(nexusflow::Message& msg) override {
 if (auto* data = msg.MutPtr<int>()) {
 *data *=2;
 Broadcast(msg); // Send to all downstream
 }
 }
};

//2. Register
NEXUSFLOW_REGISTER_MODULE(MultiplierModule);

//3. Use in YAML
// modules:
// - name: "Doubler"
// class: "MultiplierModule"
```

### Advanced: Synchronized Multi-Input Fusion

For modules that need to wait for all input streams to arrive at the same `messageId`
before processing (e.g., graph-style multi-stream join), override `RequiresSyncInputs()`:

```cpp
class FusionModule : public nexusflow::Module {
public:
 FusionModule(std::string name) : Module(std::move(name)) {}

 // Wait for all input streams to deliver the same messageId before processing
 bool RequiresSyncInputs() const override { return true; }

 void ProcessBatch(std::vector<Message>& inputBatchMessages) override {
 // Input arrives pre-sorted by messageId across all streams
 // ... join / fuse logic
 Broadcast(resultMsg);
 }
};
```

### Advanced: Blocking vs Non-Blocking Send

```cpp
//Reliable delivery (default) — blocks if downstream queue full
Broadcast(msg, /*blocking=*/true);

//Maximum throughput — drops messages if queue full
Broadcast(msg, /*blocking=*/false);
```

---

##Building

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)

#Run tests
./tests/nexusflow_tests

#Run benchmark
./benchmarks/pipeline/pipeline_benchmark
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `WITH_TESTING` | ON | Build unit tests |
| `WITH_BENCHMARK` | ON | Build benchmarks |
| `WITH_EXAMPLES` | ON | Build examples |

```bash
#Build without tests/benchmarks
cmake -DWITH_TESTING=OFF -DWITH_BENCHMARK=OFF ..
```

---

##Project Layout

```
include/nexusflow/ # Public API headers
├── Pipeline.hpp # Pipeline + PipelineConfig
├── PipelineBuilder.hpp # Fluent builder
├── Module.hpp # User-implemented base class
├── Message.hpp # Type-erased COW data
├── Config.hpp # Config + ModuleConfig alias
└── ModuleFactory.hpp # Reflection registration

src/ # Internal implementation
├── pipeline/ # Pipeline orchestration
├── module/ # ModuleActor + ModuleFactory
├── core/ # Worker (thread driver)
├── dispatcher/ # Message routing
├── base/ # Graph, Define (type aliases)
├── builder/ # PipelineBuilder impl
└── common/ # ConcurrentQueue, ViewPtr, Any
```

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for full architectural details.

---

##Examples

| Example | Description |
|---------|-------------|
| [1-how-to-use](examples/1-how-to-use/) | Minimal pipeline: Input → Process → Output |
| [2-linear-vision-pipline](examples/2-linear-vision-pipline/) | Linear video decoding + inference pipeline |
| [3-joined-vision-pipline](examples/3-joined-vision-pipline/) | Multi-stream Fusion pipeline (syncInputs demo) |

---

##Performance

Latest benchmark (Apple M-series, 8 cores, macOS 14.5):

```
Topology                     TrueThroughput    Sent         Ratio
BM_Pipeline_Linear_Throughput     ~2.4M/s    ~2.4M/s      1.0x  (baseline)
BM_Pipeline_SingleOutput          ~2.3M/s    ~2.3M/s      1.0x
BM_Pipeline_Throughput            ~370k/s    ~370k/s      6.5x  (Diamond: fan-out → fan-in)
BM_Pipeline_Latency               Avg 39.5k ns/msg
```

**Diamond (fan-out → fan-in) is ~6.5x slower than Linear** — root cause is architectural,
not lock contention:
- Source broadcasts to two parallel modules (fan-out), each on its own thread
- Each parallel module independently sends to the shared Sink (fan-in)
- Sink's JoinMode drains two queues in turn: if one queue is empty, Worker spins on
  tryPop + 50µs sleep before switching to the other queue
- This creates a synchronization bottleneck: the faster upstream module is often blocked
  waiting for the slower downstream pair to consume
- Linear/SingleOutput have no such fan-in join overhead

**Optimization opportunities (not yet implemented):**
- Lock-free queues (boost::lockfree::queue) to eliminate mutex contention on hot paths
- Batch dispatch: accumulate messages across multiple source ticks before broadcasting
- Per-queue backpressure signaling so fast producers can throttle early

---

##License

MIT License. See [LICENSE](LICENSE).

---

##Related Docs

- [Architecture](docs/ARCHITECTURE.md) — Full architecture & component design
- [Benchmarks](benchmarks/README.md) — Performance benchmark details
- [Examples](examples/) — Working example pipelines
