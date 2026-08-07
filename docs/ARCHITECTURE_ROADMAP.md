# NexusFlow Roadmap

This document defines the development path for NexusFlow under semantic versioning.

Version format: `major.minor.patch`

- `patch`: bug fixes, refactors, documentation, tests
- `minor`: backward-compatible features and internal architecture upgrades
- `major`: breaking API or model changes

The current codebase is treated as the `1.0.0` baseline. From this point on, we will
plan work by roadmap and ship versions in order.

## 1. Current Focus

The codebase is already usable, but several areas still need tightening:

- runtime semantics are still broader than the public API language
- scheduling policy is present, but not yet fully polished as a first-class concept
- streaming behavior needs better fairness, timeout handling, and backpressure clarity
- join/branch topology support is still not complete enough for richer DAGs
- observability is good, but pipeline-level reporting can still be cleaner

## 2. Versioning Rules

### 2.1 Patch Releases

Patch releases are for non-breaking work only:

- bug fixes
- timeout and fairness fixes
- naming cleanup
- comment and documentation cleanup
- example updates
- benchmark report updates

### 2.2 Minor Releases

Minor releases can add new capabilities as long as the public API remains compatible:

- new runtime modes
- new scheduling or synchronization policies
- new statistics or observability surfaces
- new graph/node capabilities
- new builder options

### 2.3 Major Releases

Major releases are reserved for breaking changes:

- public API renames
- graph/model restructuring
- module runtime model redesign
- incompatible configuration format changes

## 3. 1.0.x Stabilization

Status: current baseline

### Goal

Stabilize the public API and remove remaining ambiguity in names, ownership, and
lifecycle behavior.

### Main Work

- keep `Error` as the unified error type
- keep lifecycle methods returning `Error`
- keep `WithContext` as the preferred builder entry for runtime context
- finish naming cleanup for `Node`, `GraphNode`, `ModuleNode`, and statistics types
- simplify examples and comments
- keep include headers in English, implementation comments in Chinese where helpful

### Exit Criteria

- public API is consistent and readable
- examples compile and match the current naming scheme
- benchmark and report docs align with the implementation

## 4. 1.1.0 Runtime Refinement

### Goal

Improve execution fairness and make stream behavior more predictable.

### Main Work

- fix single-worker timeout behavior
- refine thread-pool scheduling fairness
- keep FIFO behavior where it prevents starvation
- make stream mode scheduling easier to reason about
- reduce light-load overhead in streaming pipelines

### Deliverables

- stable single-worker execution
- clearer scheduler policy abstraction
- better stream-mode latency under light workloads

### Exit Criteria

- no starvation in known light-load cases
- timeout behavior is deterministic
- stream mode remains stable under repeated start/stop cycles

## 5. 1.2.0 Topology Expansion

### Goal

Extend the graph model beyond a simple linear pipeline.

### Main Work

- add join structures
- add branch-oriented graph support
- clarify how `GraphNode` maps to runtime `ModuleNode`
- keep module creation separated from pure topology description
- improve sync semantics for fork-join paths

### Deliverables

- join nodes supported in the builder and runtime
- branch pipelines supported without awkward naming
- graph layer no longer leaks module-layer concerns

### Exit Criteria

- fork-join pipelines are first-class
- graph construction stays topology-only
- runtime materialization remains a separate step

### Migration Alignment

This stage maps to migration plan `P0-3` and `P1-2`:

- make `DiamondJoin` a first-class benchmark topic rather than a correctness-only case
- keep `OnAllInputs` semantics stable while clarifying join-state transitions
- make fork-join overhead measurable and explainable

## 6. 1.3.0 Observability

### Goal

Make performance and behavior measurable at pipeline, node, and port level.

### Main Work

- add pipeline-level latency, P99, and drop-rate summaries
- keep node-level and port-level statistics consistent
- improve event-style observability callbacks
- generate clearer benchmark and report output

### Deliverables

- pipeline summary statistics
- better example output formatting
- report text that can be reused directly in docs

### Exit Criteria

- pipeline-level metrics can be consumed directly by examples and reports
- observability output stays readable under long names and wide graphs

### Migration Alignment

This stage maps to migration plan `P0-1`, `P0-2`, and `P1-3`:

- standardize benchmark/report structure
- lock pipeline summary metrics around latency, drop, reject, and sink receive counts
- make report text directly reusable in release notes and docs

## 7. 1.4.0 Configuration and Construction

### Goal

Make pipeline construction easier to extend and easier to reproduce.

### Main Work

- keep `PipelineBuilder` and YAML/config construction aligned
- improve runtime context injection
- simplify module construction paths
- make graph-spec import/export clearer

### Deliverables

- builder and config-driven construction share the same internal path
- runtime context is a first-class input
- module instantiation rules are easier to understand

### Exit Criteria

- builder and config are consistent
- construction logic is not duplicated across entry points

## 8. 1.5.0 Scheduler and Join Efficiency

### Goal

Reduce topology-related execution overhead without weakening runtime semantics.

### Main Work

- evolve scheduling from thread-count-only tuning toward topology-aware decisions
- improve join hot-path efficiency while preserving `OnAllInputs` semantics
- keep `Linear` and `DiamondJoin` performance comparisons visible in the benchmark suite
- make queue-path versus pipeline-path performance boundaries explicit in docs

### Deliverables

- clearer topology-aware scheduling inputs
- measurable `DiamondJoin` versus `Linear` comparison tables
- lower and better-explained join-path overhead

### Exit Criteria

- join benchmarks show stable, reproducible trends
- no correctness regression in existing join tests
- scheduler behavior is easier to explain from topology and workload shape

### Migration Alignment

This stage maps to migration plan `P1-1`, `P2-1`, `P2-2`, and `P2-3`.

## 9. 2.0.0 Breaking Release

### Goal

Only introduce this when the public model really needs a reset.

### Candidate Changes

- public API renames that are not worth compatibility shims
- module/graph/runtime separation rewrite
- configuration format migration
- deeper scheduler or execution-model redesign

## 10. Release Rule

We do not tag every experiment. A tag should only be created when:

- the target roadmap item is complete
- tests are green
- examples are updated
- the release is meant to be consumed externally
