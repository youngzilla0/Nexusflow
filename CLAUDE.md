# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Create build directory and configure
mkdir build && cd build
cmake ..

# Compile
make -j$(nproc)

# Run tests
./tests/nexusflow_tests

# Build without tests/benchmarks
cmake -DWITH_TESTING=OFF -DWITH_BENCHMARK=OFF ..
```

## Architecture Overview

NexusFlow is a high-performance C++ dataflow pipeline framework where Modules are connected in a DAG via message queues.

### Key Components

1. **Pipeline** (public API in `include/nexusflow/Pipeline.hpp`)
   - Owns `Pipeline::Impl` which holds the Graph and all ModuleActors
   - Lifecycle: `Init()` → `Start()` → `Stop()` → `DeInit()`

2. **Graph** (`src/base/Graph.hpp`)
   - Stores DAG topology: nodes (modules) and edges (connections)
   - Provides BFS-based edge listing for traversal

3. **ModuleActor** (`src/module/ModuleActor.hpp`)
   - Wraps a Module with its Worker and Dispatcher
   - Each ModuleActor runs on its own thread via Worker

4. **Worker** (`src/core/Worker.hpp`)
   - Runs a dedicated thread per module
   - Pulls batches from input queues, invokes `Module::ProcessBatch()`
   - Manages stop signals and graceful shutdown

5. **Dispatcher** (`src/dispatcher/Dispatcher.hpp`)
   - Holds subscriber queues for each output port
   - `Broadcast()` copies message to N-1 queues, moves to the last

6. **Message** (`include/nexusflow/Message.hpp`)
   - Type-erased container using Copy-On-Write (COW) semantics
   - `Borrow<T>()` / `Mut<T>()` for read-only / read-write access
   - Broadcasting a Message is cheap (shared_ptr copy)

7. **ConcurrentQueue** (`src/common/ConcurrentQueue.hpp`)
   - Thread-safe blocking queue (bounded or unbounded)
   - Uses condition variables to avoid busy-waiting

### Creating Custom Modules

1. Inherit from `nexusflow::Module` and implement `Process(Message&)`
2. Use `Broadcast(msg)` to send to all downstream, or `SendTo("outputName", msg)` for specific output
3. Access data via `msg.BorrowPtr<T>()` (read) or `msg.MutPtr<T>()` (write, triggers COW)
4. Register with `NEXUSFLOW_REGISTER_MODULE(YourModule)` at app startup

### Test Organization

Tests are registered via `GLOBAL_TEST_SOURCES` property in `src/CMakeLists.txt`. Add new test files there to include them in the test executable.