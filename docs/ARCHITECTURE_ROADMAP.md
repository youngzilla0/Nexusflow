# NexusFlow Architecture Roadmap

This document turns the current architecture review into a staged refactoring plan.
It is intentionally pragmatic: each phase should improve one layer boundary without
forcing a full rewrite of the runtime.

## 1. Current Architectural Pressure Points

The current runtime is functional and benchmarkable, but several boundaries are still
too soft:

- pipeline construction, validation, and runtime initialization are still coupled
- `Graph` still mixes topology concerns with module-instantiation concerns
- `Pipeline::Impl` still needs to understand concrete graph node kinds
- actor lifecycle order is not modeled explicitly as a topological sequence
- `Executor` owns too many responsibilities at once
- `OnAllInputs` join semantics are hard-coded to `messageId`
- `PipelineBuilder` is not yet a clearly first-class construction path
- `ModuleFactory` still assumes one narrow module creation model

The roadmap below addresses these issues in the order that gives the best
stability-to-effort ratio.

## 2. Guiding Principles

All phases below follow the same design direction:

- `Graph` should describe topology, not runtime materialization policy
- runtime planning should happen before runtime execution starts
- lifecycle order should be explicit and reproducible
- scheduling policy should be configurable instead of hidden in task plumbing
- synchronization semantics should be extensible beyond the current benchmark cases
- public API and internal runtime API should be separated more clearly over time

## 3. v0.4 Stabilize Boundaries

Status: partially implemented on branch/tag `v0.4-architecture-boundaries`

### 3.1 Goal

Make the current architecture easier to reason about without changing the public
programming model too aggressively.

### 3.2 Main Changes

#### A. Split pipeline creation into explicit phases

Today, `CreateFromYaml()` and `InitWithGraph()` quickly drift from parsing into runtime
materialization. In v0.4, the build path should be conceptually split into:

1. `GraphSpec` construction
2. `GraphValidator` checks
3. `ExecutionPlan` generation
4. `PipelineRuntime` creation

Suggested intermediate shape:

```text
Yaml / Builder
  -> GraphSpec
  -> GraphValidator
  -> ExecutionPlan
  -> Pipeline
```

This does not require renaming every class immediately. The main objective is to stop
blending "topology description" and "runtime activation" into the same step.

#### B. Make actor lifecycle order explicit

Replace address-ordered actor storage with an explicit topological order:

- `std::vector<std::shared_ptr<ModuleNode>> moduleTopoOrder`
- `std::unordered_map<std::string, std::shared_ptr<ModuleNode>> moduleByName`

Expected lifecycle semantics:

- `Init()` in topological order
- `Start()` in topological order
- `Stop()` in reverse topological order
- `DeInit()` in reverse topological order

This makes runtime behavior deterministic and easier to debug.

#### C. Narrow `Graph` responsibility

In this phase, `Graph` does not need a full rewrite. The immediate target is simpler:

- keep `Graph` responsible for nodes, edges, and ports
- move module instantiation intent into a separate `NodeSpec` layer
- reduce direct runtime dependence on `Graph` node subclasses

Possible transitional model:

```cpp
struct NodeSpec {
    std::string nodeName;
    std::string moduleClassName;
    Config config;
    std::shared_ptr<Module> moduleInstance;
};
```

The exact final structure can still evolve, but v0.4 should establish the boundary.

#### D. Introduce join-key abstraction

`OnAllInputs` currently assumes correlation by `messageId`. That is fine for some
fan-out/fan-in cases, but too restrictive for general stream processing.

v0.4 should add the ability to resolve a join key via:

- default: `messageId`
- optional metadata key
- optional custom join-key extractor

This can begin as configuration and later evolve into a richer policy model.

### 3.3 Suggested Deliverables

- pipeline build path split into parse/validate/plan/runtime steps
- explicit actor topological order container
- initial `NodeSpec` abstraction or equivalent transition layer
- configurable join key selection for `OnAllInputs`

### 3.4 Acceptance Criteria

- pipeline initialization fails early and clearly when graph/spec validation fails
- actor lifecycle order is deterministic across runs
- runtime no longer depends directly on raw topology-only `GraphNode`
- `OnAllInputs` can correlate by something other than `messageId`

Implemented so far:

- pipeline initialization now performs explicit graph validation before runtime materialization
- runtime assembly now flows through a lightweight build-plan step
- actor lifecycle order is stored explicitly in topological order instead of pointer-ordered storage
- lifecycle order is covered by runtime tests

## 4. v0.5 Decouple Runtime Components

Status: implemented on branch/tag `v0.5-runtime-semantics`

### 4.1 Goal

Reduce the size and responsibility concentration of `Executor`, while preserving the
current `Module` processing model.

### 4.2 Main Changes

#### A. Split `Executor` into internal components

Current `Executor` responsibilities should be separated into smaller units:

- `ActorRegistry`
- `Scheduler`
- `PortRouter`
- `JoinStateStore`
- `Statistics`

The public `Executor` type may still remain as a facade, but its internal structure
should stop being a single coordination blob.

#### B. Make scheduling policy explicit

The current thread-pool and actor task logic encode fairness, continuation behavior,
and polling behavior indirectly. v0.5 should define a real scheduling-policy layer.

Useful first policy modes:

- low-latency
- throughput-oriented
- deterministic single-worker

This does not require exposing all of them publicly at once, but the runtime should
be organized so that policy changes do not require rewriting actor execution logic.

#### C. Move statistics further off the hot path

Runtime stats are useful and should stay, but the core scheduler path should not need
to know too much about snapshot aggregation.

Direction:

- scheduler emits runtime events
- stats collector consumes events or state snapshots

The immediate target is better separation, not necessarily a fully asynchronous metrics
pipeline yet.

#### D. Make `PipelineBuilder` a real first-class entry point

The builder should become a canonical way to create a pipeline in code, not just an
API shell. Its outputs should align with the same internal build path as YAML.

Expected long-term behavior:

- YAML and builder both produce the same `GraphSpec`
- both pass through the same validator and plan generator
- both end in the same pipeline runtime creation path

### 4.3 Suggested Deliverables

- smaller executor internals with clearer responsibilities
- formal scheduling policy interface or internal strategy layer
- stats collection separated from core dispatch logic
- fully wired `PipelineBuilder`

### 4.4 Acceptance Criteria

- `Executor` no longer owns all state transitions directly
- scheduling tweaks do not require editing unrelated routing or join code
- YAML and builder pipelines share one internal construction pipeline

Implemented so far:

- `OnAllInputs` join correlation is no longer hard-coded to `messageId`
- runtime config now supports `JoinKeyPolicy`
- timestamp-based join correlation is covered by tests
- `Pipeline::CreateFromYaml()` now loads the optional top-level `runtime:` block
- YAML runtime config coverage now includes executor threads, queue size, and statistics disablement
- `PipelineBuilder` and YAML graph creation now share one internal graph assembly and validation helper
- runtime statistics state/snapshot logic now lives in a dedicated `Statistics`
- pending `OnAllInputs` join state now lives in a dedicated `JoinStateStore`
- actor reschedule rules and per-task step budgets now flow through an internal `SchedulingPolicy`
- output subscriber tables and edge dispatch logic now live in a dedicated `PortRouter`
- node runtime registration, input bindings, and state lookup now live in a dedicated `NodeRegistry`
- `Executor` now primarily acts as a scheduling facade over `NodeRegistry`, `PortRouter`, `JoinStateStore`, `SchedulingPolicy`, and `Statistics`

## 5. v1.0 Expand Runtime Semantics

Status: partially implemented on branch/tag `v1.0-module-build-context`

### 5.1 Goal

Turn NexusFlow from a solid benchmarkable runtime into a more extensible stream/dataflow
framework.

### 5.2 Main Changes

#### A. Upgrade module instantiation model

The current `ModuleFactory` is intentionally simple, but long-term it should support:

- richer creator registration
- injected runtime services
- test-friendly dependency hooks
- potentially plugin-backed module discovery

Example direction:

```cpp
struct ModuleBuildContext {
    std::string moduleName;
    Config config;
    PipelineContext& pipelineContext;
    RuntimeServices& services;
};
```

#### B. Formalize `ExecutionPlan`

Introduce a runtime plan object that captures:

- actor list
- input/output bindings
- queue layout
- trigger policies
- join policies
- scheduling hints

This becomes the bridge between graph description and runtime execution.

#### C. Support richer stream coordination

The current `OnAllInputs` model should evolve toward reusable synchronization semantics:

- custom join keys
- barrier-style synchronization
- time-window joins
- watermark-aware processing
- keyed multi-stream joins

This does not mean all of these must ship at once, but the architecture should stop
assuming that "join = same `messageId`".

#### D. Sharpen public vs internal API boundaries

By v1.0, a cleaner separation should exist between:

- stable user-facing headers in `include/nexusflow`
- internal runtime and planner implementation under `src`
- optional advanced or experimental APIs

That will make future compatibility decisions much easier.

### 5.3 Suggested Deliverables

- richer module build context
- explicit execution-plan representation
- extensible join/sync semantics
- clearer supported public API surface

### 5.4 Acceptance Criteria

- new runtime features can be added without rewriting `Graph` or `Executor`
- module instantiation can support richer real-world dependencies
- synchronization semantics are no longer tied to one implicit correlation rule

Implemented so far:

- module materialization can now flow through `ModuleBuildContext`
- the legacy `CreateModule(className, moduleName, config)` path remains as a compatibility wrapper
- `ExecutionPlan` now carries stable node ordering plus explicit queue-binding metadata, so runtime materialization no longer re-derives node relationships from graph edges

## 5.5 Current In-Progress Slice

The current branch continues beyond the three tagged milestones in one additional area:

- make `PipelineBuilder` more like a first-class construction path
- add explicit builder naming via `WithName(...)`
- fail fast on duplicate module names
- fail fast on connections that reference missing modules

These changes are intended to reduce hidden builder-time errors before the broader
YAML/builder unification work is finished.

## 6. Recommended Implementation Order

If only a few changes can happen soon, prioritize these first:

1. make actor lifecycle order explicit
2. split pipeline build/validate/plan/runtime phases
3. abstract join key selection away from raw `messageId`

These three steps provide the best leverage for the least disruption.

## 7. Mapping To Current Files

The roadmap mainly touches these areas:

- [src/base/Graph.hpp](/Users/yang/Code/Nexusflow/src/base/Graph.hpp)
- [src/base/Graph.cpp](/Users/yang/Code/Nexusflow/src/base/Graph.cpp)
- [src/base/GraphUtils.cpp](/Users/yang/Code/Nexusflow/src/base/GraphUtils.cpp)
- [src/pipeline/Pipeline.cpp](/Users/yang/Code/Nexusflow/src/pipeline/Pipeline.cpp)
- [src/pipeline/impl/PipelineImpl.hpp](/Users/yang/Code/Nexusflow/src/pipeline/impl/PipelineImpl.hpp)
- [src/pipeline/impl/PipelineImpl.cpp](/Users/yang/Code/Nexusflow/src/pipeline/impl/PipelineImpl.cpp)
- [src/executor/Executor.hpp](/Users/yang/Code/Nexusflow/src/executor/Executor.hpp)
- [src/executor/Executor.cpp](/Users/yang/Code/Nexusflow/src/executor/Executor.cpp)
- [src/executor/ThreadPool.hpp](/Users/yang/Code/Nexusflow/src/executor/ThreadPool.hpp)
- [include/nexusflow/PipelineBuilder.hpp](/Users/yang/Code/Nexusflow/include/nexusflow/PipelineBuilder.hpp)
- [include/nexusflow/ModuleFactory.hpp](/Users/yang/Code/Nexusflow/include/nexusflow/ModuleFactory.hpp)

## 8. What This Roadmap Is Not

This roadmap does not recommend a rewrite. The framework already has useful pieces:

- a clean `Module::Process(inputs, outputs)` model
- explicit trigger policies
- clear queue-level statistics
- benchmark coverage for latency, depth scaling, backpressure, and join behavior

The goal is to preserve those strengths while cleaning up the boundaries that will
otherwise slow future evolution.
