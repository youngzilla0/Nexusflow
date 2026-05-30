# NexusFlow架构设计文档

> High-Performance Modern C++ Dataflow Pipeline Framework

---

##1.总体架构

NexusFlow 是一个基于 **DAG (Directed Acyclic Graph)** 的高性能数据流框架。核心思想是:

> **业务逻辑 (Module) 与框架机制 (Worker/Dispatcher/Queue) 完全解耦。**

```
┌─────────────────────────────────────────────────────────────────────┐
│ Public API Layer │
│ │
│ Pipeline::CreateFromYaml() PipelineBuilder::Build() │
│ nexusflow::Module nexusflow::PipelineConfig │
│ nexusflow::Message NEXUSFLOW_REGISTER_MODULE() │
└──────────────────────────────┬──────────────────────────────────────┘
 │
 ▼
┌─────────────────────────────────────────────────────────────────────┐
│ Pipeline Orchestration │
│ │
│ Pipeline::Impl │
│ ├── Graph (DAG topology) │
│ ├── PipelineConfig (runtime: batch/queue) │
│ └── ActorNode map │
└──────────────────────────────┬──────────────────────────────────────┘
 │
 ▼
┌─────────────────────────────────────────────────────────────────────┐
│ Module Runtime Actor │
│ │
│ ModuleActor │
│ ├── Module ← 用户实现的业务逻辑 │
│ ├── Dispatcher ←消息分发 (Broadcast/SendTo) │
│ └── Worker ←线程驱动 +批处理 │
└──────────────────────────────┬──────────────────────────────────────┘
 │
 ▼
┌─────────────────────────────────────────────────────────────────────┐
│ Internal Infrastructure │
│ │
│ MessageQueue (Bounded BlockingQueue) │
│ ViewPtr (Non-owning pointer) │
│ Any (Type-erased value) │
└─────────────────────────────────────────────────────────────────────┘
```

---

##2.目录结构

```
NexusFlow/
├── include/nexusflow/ #公共 API头文件
│ ├── Pipeline.hpp # Pipeline 类 + PipelineConfig
│ ├── PipelineBuilder.hpp #链式构造器
│ ├── Module.hpp # 用户实现的基类
│ ├── Message.hpp # 类型擦除 + COW 数据容器
│ ├── Config.hpp #通用配置容器 (ModuleConfig 别名)
│ ├── ModuleFactory.hpp # 模块反射注册
│ ├── ErrorCode.hpp #统一错误码
│ ├── Any.hpp # 类型擦除容器
│ └── TypeTraits.hpp
│
├── src/ #内部实现 (不对外暴露)
│ ├── pipeline/
│ │ ├── Pipeline.cpp # Pipeline公共 API 实现
│ │ └── impl/
│ │ └── PipelineImpl.* # Pipeline 的 pImpl 实现 (核心编排)
│ ├── module/
│ │ ├── Module.cpp
│ │ ├── ModuleActor.* # 模块运行 Actor (Module+Worker+Dispatcher)
│ │ └── ModuleFactory.cpp
│ ├── core/
│ │ └── Worker.* #线程驱动 +批处理循环
│ ├── dispatcher/
│ │ └── Dispatcher.* #消息广播/单播
│ ├── base/
│ │ ├── Define.hpp #公共类型别名 (MessageQueue 等)
│ │ └── Graph.* # DAG 数据结构
│ ├── builder/
│ │ └── PipelineBuilder.cpp # Builder 实现
│ ├── common/
│ │ ├── ConcurrentQueue.hpp # 有界阻塞队列
│ │ ├── ViewPtr.hpp # 非拥有指针
│ │ └── Optional.hpp
│ └── utils/
│ └── logging.hpp
│
├── examples/ # 示例
├── benchmarks/ #性能基准
└── tests/ #单元测试
```

---

##3.核心组件

###3.1 Pipeline (流水线编排)

**职责**:拥有 Graph、创建 Actor、调度生命周期。

```
┌─────────────────────────────┐
│ Pipeline │
├─────────────────────────────┤
│ - m_pImpl: Impl │
│ - CreateFromYaml() │
│ - Init/Start/Stop/DeInit │
└─────────────────────────────┘
 │
 ▼
┌─────────────────────────────┐
│ Pipeline::Impl │
├─────────────────────────────┤
│ - graph: unique_ptr<Graph> │
│ - queues: vector<Queue> │ ←拥有所有消息队列
│ - config: PipelineConfig │
│ - actorModuleMap │
│ - actorOrderedNodes │
└─────────────────────────────┘
```

**生命周期**: `Init() → Start() → Stop() → DeInit()`

|阶段 |动作 |
|------|------|
| `Init()` | 调用每个 ModuleActor.Init() → Module.Init() |
| `Start()` |启动每个 ModuleActor 的工作线程 |
| `Stop()` | shutdown 所有队列, join 所有线程 |
| `DeInit()` |逆序调用 Module.DeInit() |

---

###3.2 Graph (DAG拓扑)

**职责**:存储 Module节点和连接关系 (纯数据结构)。

```
Graph
├── m_nodeMap: name → Node
├── m_adjList: adjacency list
└── m_name: pipeline name

Node (abstract)
├── NodeWithModulePtr (Builder 创建, 直接持有 Module 实例)
└── NodeWithModuleClassName (YAML 创建, 通过类名反射构造)

Edge
├── srcNodePtr (weak)
└── dstNodePtr (weak)
```

**关键 API**:
- `addEdge(src, dst)` - 添加边
- `hasCycle()` - 检测环
- `toEdgeListBFS()` - BFS遍历生成边列表

---

### 3.3 ModuleActor (模块运行时 Actor)

**职责**: 把一个 Module包装成可独立运行的实体。**这是核心组合类**。

```
┌─────────────────────────────────────────────┐
│ ModuleActor │
├─────────────────────────────────────────────┤
│ - m_module: Module (业务逻辑) │
│ - m_worker: Worker (线程驱动) │
│ - m_dispatcher: Dispatcher (消息发送) │
│ - m_workThread: thread (工作线程) │
└─────────────────────────────────────────────┘
```

**构造参数**:
```cpp
ModuleActor(module, runtimeConfig)
       │       │
       │       └─ PipelineConfig (batch/queue运行时参数)
       └─ 用户实现的 Module
```

**syncInputs 配置**: 通过 `Module::RequiresSyncInputs()` 虚函数控制,不再走 Config。

---

### 3.4 Worker (线程驱动 +批处理)

**职责**:持有独立线程,循环从输入队列拉消息 →调 Module 处理。

```
┌─────────────────────────────────────────────┐
│ Worker │
├─────────────────────────────────────────────┤
│ - m_modulePtr: Module │
│ - m_runtimeConfig: PipelineConfig │
│ - m_inputQueueMap: name → MessageQueue │
│ - m_stopFlag: atomic<bool> │
└─────────────────────────────────────────────┘
```

**syncInputs 决定执行模式**:

```cpp
// Worker::WorkLoop()
bool isSyncInputs = m_modulePtr->RequiresSyncInputs(); // ← 虚函数,不再是 Config 读
```

|模式 | `RequiresSyncInputs()` |行为 |
|------|----------------------|------|
| **普通流水线** | `false` (默认) |调 `PullBatchMessage()`拉一批,调 `ProcessBatch()` |
| **Fusion同步融合** | `true` | 等所有输入队列凑齐一个 messageId 的消息,合并后处理 |

**批处理流程 (PullBatchMessage)**:
```
Phase1 (Greedy): 非阻塞 tryPop 所有队列 →尽可能多收集
Phase2 (Blocking): waitAndPopFor(1ms)轮询 →凑齐 batch 或超时
```

---

###3.5 Dispatcher (消息分发)

**职责**:持有下游队列视图, 提供 Broadcast/SendTo 接口。

```
┌─────────────────────────────────────────────┐
│ Dispatcher │
├─────────────────────────────────────────────┤
│ - m_subscriberMap: name → MessageQueue │
└─────────────────────────────────────────────┘
```

**两种发送模式**:
|模式 | 函数 |行为 |
|------|------|------|
| **Broadcast** | `Broadcast(msg, blocking=true)` |复制 N-1 次, move 最后一份 |
| **SendTo** | `SendTo(name, msg, blocking=true)` | 发到指定队列 |

`blocking=true` 用 `push()` (阻塞); `blocking=false` 用 `tryPush()` (丢消息保吞吐)。

---

###3.6 Message (类型擦除数据容器)

**职责**: 在队列中流动的通用数据包装。

```
Message
├── m_data: shared_ptr<void> (实际数据)
├── m_meta: MessageMetaData (id/source/timestamp)
└── m_typeInfo: TypeInfo (类型信息 for Borrow/Mut)
```

**Copy-On-Write语义**:
```
原 msg (refcount=1) ──┬──> copy ──→ refcount=2
 │
 └──> Mut() → COW触发:
 ├─ 原 msg refcount=1 (保留旧数据)
 └─ 新 msg refcount=1 (新数据)
```

**访问 API**:
| 函数 | 返回 |行为 |
|------|------|------|
| `Borrow<T>()` | `const T&` | 只读, 不触发 COW |
| `BorrowPtr<T>()` | `const T*` | 只读指针 (no-throw) |
| `Mut<T>()` | `T&` | 可写,触发 COW |
| `MutPtr<T>()` | `T*` | 可写指针 (no-throw) |

---

###3.7 ConcurrentQueue (有界阻塞队列)

**职责**:线程安全的有界消息队列。

```
ConcurrentQueue<Message>
├── m_queue: deque<Message> (数据)
├── m_capacity: size_t (上限)
├── m_mutex + m_cvNotFull/Empty (条件变量)
└── shutdown() →唤醒所有等待线程
```

**API**:
| 函数 |行为 |
|------|------|
| `push(msg)` |阻塞直到有空间 |
| `tryPush(msg)` | 非阻塞,满则丢弃/返回 false |
| `waitAndPopFor(msg, timeout)` |阻塞 pop, 带超时 |
| `tryPop(msg)` | 非阻塞 pop |
| `shutdown()` |唤醒所有阻塞线程 |

---

##4.依赖关系图

```
┌──────────────────────┐
 │ Pipeline (Public) │
 └──────────┬───────────┘
 │ has-a
 ▼
┌──────────────────────┐
 │ Pipeline::Impl │
 └──────────┬───────────┘
 │ owns
┌────────────┼────────────┐
 ▼ ▼ ▼
┌─────────┐┌─────────┐┌──────────────┐
 │ Graph │ │ Queues │ │ ModuleActors │
 └─────────┘ └─────────┘ └──────┬───────┘
 │ contains
 ▼
┌──────────────────────┐
 │ ModuleActor │
 └──┬─────────┬─────────┘
 │ │
┌──────────────┘ └──────────────┐
 ▼ ▼
┌──────────────┐┌──────────────┐
 │ Worker │◄────────uses──────────│ Module │
 │ (thread) │ │ (user code) │
 └──────┬───────┘ └──────┬───────┘
 │ │
 │ reads batch from │ sends via
 ▼ ▼
┌──────────────┐┌──────────────┐
 │MessageQueue │ │ Dispatcher │
 └──────────────┘ └──────┬───────┘
 │
 │ writes to
 ▼
┌──────────────┐
 │MessageQueue │
 └──────────────┘

═══════════════════════════════════════════════════════════════════════
 Layer Responsibilities
═══════════════════════════════════════════════════════════════════════

Layer0 (Foundation - src/common, src/base):
 ConcurrentQueue, ViewPtr, Any, Graph, Define

Layer1 (Runtime - src/core, src/dispatcher, src/module):
 Worker, Dispatcher, ModuleActor, Module (base)

Layer2 (Orchestration - src/pipeline):
 Pipeline, PipelineImpl, GraphUtils

Layer3 (Construction - src/builder, include/nexusflow):
 PipelineBuilder, ModuleFactory

Layer4 (Public API - include/nexusflow):
 Pipeline, Module, Message, Config
```

---

##5. 配置系统 (Config vs PipelineConfig)

这是最容易被混淆的部分, 因此单独说明。

###5.1 两类配置

| 类型 | 定义 |作用域 |注入方式 | 示例字段 |
|------|------|--------|---------|---------|
| **ModuleConfig** | YAML `modules[name].params` | 单个模块 | 通过 Node → ModuleActor → Worker | `syncInputs`, `threshold`, `modelPath` |
| **PipelineConfig** | `PipelineBuilder::WithConfig()` 或代码 |整个流水线 | Pipeline → Impl → 所有 Actor | `maxBatchSize`, `batchTimeoutMs`, `queueSize` |

###5.2 设计原则

```
❌错误做法: 把 PipelineConfig字段塞进 ModuleConfig
 → ModuleConfig语义被污染, 用户困惑

✅正确做法: 完全分离, PipelineConfig 通过构造函数显式传递
 → ModuleConfig =业务参数 (YAML)
 → PipelineConfig =运行时参数 (代码)
```

###5.3 调用链

```
PipelineBuilder
 └─ WithConfig(PipelineConfig) ─┐
 │
YAML/YAML ▼
 └─ modules[name].params ──→ ModuleConfig
 │
 ▼
 Pipeline::InitWithGraph(graph, PipelineConfig)
 │
 ▼
 Pipeline::Impl::Init()
 │
┌───────────────────┴────────────────────┐
 ▼ ▼
 GraphUtils.CreateGraphFromYaml() m_pImpl->config = config
 │ │
 ▼ ▼
 creates Graph with Node{...ModuleConfig}┌──────────────────┐
 │ │ GetOrCreateActor │
 └─────────────────┬──────────────────┴────────┬─────────┘
 ▼ ▼
 ModuleActor(module, moduleConfig, runtimeConfig)
 │
┌───────────────┴───────────────┐
 ▼ ▼
 Worker(module, Dispatcher(configView)
 moduleConfig, │
 runtimeConfig) └─ 只持有 ModuleConfig视图
 │
 └─ WorkLoop() 用 m_runtimeConfig决定 batch行为
 用 m_moduleConfig读 syncInputs
```

###5.4字段映射

**ModuleConfig (YAML)**:
```yaml
modules:
 - name: "Decoder"
 class: "DecoderModule"
 params:
 syncInputs: true # ← Worker用来决定 RunFusion模式
 modelPath: "/models/yolo"
 confidence:0.7
```

**PipelineConfig (代码)**:
```cpp
auto pipeline = PipelineBuilder()
 .AddModule(...)
 .Connect(...)
 .WithConfig(PipelineConfig{
 .maxBatchSize =32,
 .batchTimeoutMs =5,
 .queueSize =100
 })
 .Build();
```

---

##6. 数据流示例

以 `InputNode → ProcessNode → OutputNode` 为例:

```
Time ──────────────────────────────────────────────────►

InputNode.Worker:
┌─────────────────────────────────────────┐
 │ Process(empty) │
 │ ↓ make Message{data} │
 │ Broadcast(msg) ──────────────────────┐ │
 └─────────────────────────────────────┘ │
 │
 ▼
ProcessNode.Worker:
┌─────────────────────────────────────────┐
 │ PullBatchMessage(32,5ms) │
 │ ↓ get {msg1, msg2, msg3, ...} │
 │ ProcessBatch({msg1, msg2, ...}) │
 │ ↓ make Message{result} │
 │ Broadcast(msg) ──────────────────────┐ │
 └─────────────────────────────────────────┘ │
 │
 ▼
OutputNode.Worker:
┌─────────────────────────────────────────┐
 │ PullBatchMessage(32,5ms) │
 │ ↓ get {result1, result2, ...} │
 │ ProcessBatch({...}) │
 └─────────────────────────────────────────┘
```

---

##7.线程模型

每个 ModuleActor拥有独立的线程:

```
┌──────────────┐┌──────────────┐┌──────────────┐
│ Thread-1 │ │ Thread-2 │ │ Thread-3 │
│┌──────────┐ │ │┌──────────┐ │ │┌──────────┐ │
│ │ Worker-A │ │ │ │ Worker-B │ │ │ │ Worker-C │ │
│ └────┬─────┘ │ │ └────┬─────┘ │ │ └────┬─────┘ │
│ │ │ │ │ │ │ │ │
│ Module-A │ │ Module-B │ │ Module-C │
└──────┼───────┘ └──────┼───────┘ └──────┼───────┘
 │ │ │
 └───── Queue A→B ───┴───── Queue B→C ───┘
```

- **Source Module** (无输入队列):主动循环调用 `Process(empty)`
- **普通 Module**: `PullBatchMessage`拉批 → `ProcessBatch` 处理
- **Sync Module** (Fusion): 等所有输入凑齐一个 messageId 后处理

---

##8.扩展点

###8.1 添加新 Module

```cpp
//1.继承 Module
class MyModule : public nexusflow::Module {
public:
 explicit MyModule(std::string name) : Module(std::move(name)) {}
 void Process(Message& msg) override { /* ... */ }
};

//2. 注册到 Factory
NEXUSFLOW_REGISTER_MODULE(MyModule);

//3. YAML 中使用
// modules:
// - name: "MyNode"
// class: "MyModule"
```

###8.2替换 Worker行为

继承 `Worker` 并在 `ModuleActor` 中替换 (当前未暴露此扩展点)。

###8.3替换 Dispatcher行为

继承 `dispatcher::Dispatcher` 并在 `ModuleActor`构造时替换。

---

##9.已知限制 / TODO

- `Pipeline::Stop()` 有 `// TODO:优化一下`注释
- `RunFusion()`注释: "如何优化呢, 现在只有单 Batch, 并且需要测试下内存占用"
- Builder 的 source/sink节点推断是启发式的
- Dispatcher持有 Config视图但实际未使用 (可清理)

---

##10. 相关文档

- [README.md](../README.md) - 用户文档
- [BENCHMARK.md](../benchmarks/README.md) -性能基准
- [examples/](../examples/) - 示例代码
