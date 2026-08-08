# CatOS 内核设计文档

> 版本：v0.1（M0 里程碑）
> 对应需求：`docs/requirements.md`（FR-TASK / FR-SCHED / FR-PORT / FR-WIN）
> 源码：`m0_kernel_skeleton/kernel/`

---

## 1. 分层架构

```
┌──────────────────────────────────────────────┐
│ 应用层（apps/：demo、tests）                    │
├──────────────────────────────────────────────┤
│ 公共接口 kernel/include/catos/                 │
│   catos.h catos_task.h catos_sched.h          │
│   catos_port.h（移植接口契约）catos_config.h   │
├──────────────────────────────────────────────┤
│ 内核核心 kernel/src/（平台无关）                │
│   task.c  ready_queue.c  sched.c  kernel.c    │
├──────────────────────────────────────────────┤
│ 移植层 kernel/port/<target>/（唯一平台代码）     │
│   win32/port.c                                │
└──────────────────────────────────────────────┘
```

**核心隔离规则**（FR-PORT-006）：`kernel/src` 与 `kernel/include/catos` 不含任何平台分支、Win32 API 或汇编；平台差异全部封装在 `kernel/port/<target>/`。核心与移植层之间只通过 `catos_port.h` 声明的接口交互。

---

## 2. 任务状态机（FR-TASK-002）

```
        catos_task_create()
                 │
                 ▼
            ┌────────┐      k_sched 选中      ┌──────────┐
            │  READY  │ ───────────────────► │  RUNNING │
            └────────┘      (进入就绪队列)     └──────────┘
              ▲    ▲                            │      │
   resume     │    │ suspend(其他任务)           │      │ suspend(self)
   （解除挂起） │    │                            │      │ yield
              │    │                            │      │ 被抢占(tick)
              │    ▼                            │      │
              │  ┌──────────┐   exit()/函数返回 │      ▼
              └──│ SUSPENDED│ ◄─────────────── │  ┌─────────┐
                 └──────────┘                  │  │ READY   │◄─┐
                                               │  └─────────┘  │（仍在就绪队列）
                                               │               │
                                               ▼               │
                                         ┌────────────┐        │
                                         │ TERMINATED │        │
                                         └────────────┘        │
                                      （不再参与调度）           │
                                               └───────────────┘
```

约定与说明：

- **running-in-ready 约定**：运行中的任务（RUNNING）仍位于就绪队列中。就绪队列最高优先级即应运行的任务，调度因此只需"取就绪队列最高优先级"。
- `BLOCKED` 状态（等待信号量/消息等）在 M2 里程碑引入。
- 状态转换均由内核在持锁（临界区）状态下完成；`RUNNING ↔ READY` 的切换由 `k_sched_*` 负责，`SUSPENDED/TERMINATED` 由对应 API 设置。

---

## 3. 就绪队列（FR-SCHED-004）

数据结构（`catos_internal.h` / `ready_queue.c`）：

```c
typedef struct catos_ready_q {
    uint32_t     bitmap;                       /* 位 i = 1 → 优先级 i 有就绪任务 */
    catos_list_t queues[CATOS_CFG_NUM_PRIO];   /* 每优先级一条 FIFO 链表 */
} catos_ready_q_t;
```

- 优先级约定：**数值越小优先级越高**（0 最高，`CATOS_CFG_NUM_PRIO-1` 最低，空闲任务用最低优先级）。
- 取最高优先级：**最低置位位** = 最高优先级，通过移植层 `catos_port_ctz()`（count trailing zeros）O(1) 求得：

  ```c
  prio = catos_port_ctz(rq->bitmap);   /* 位 1 置位 → ctz=1 → 优先级 1 */
  ```

  > 实现注记：曾误用 clz（最高置位位），导致选中最低优先级任务。优先级约定"数字小=高"，所以必须取**最低置位位**（ctz）。

---

## 4. 调度（FR-SCHED-001 基础，FR-SCHED-003 可插拔）

### 4.1 调度决策

`k_pick_next()`：由**当前调度策略**的 `pick_next()` 返回下一个运行任务；策略无就绪任务时由调度核心兜底为空闲任务 `k_idle`。固定优先级策略（缺省）取就绪集中最高有效优先级任务（同优先级取最先进入的，FIFO）。

### 4.2 两条切换路径

| 路径 | 调用上下文 | 切换原语 | 语义 |
| --- | --- | --- | --- |
| `k_sched_from_task()` | 任务调用内核 API（持锁） | `catos_port_yield_to(next)` | 当前任务让出 CPU（挂起自身） |
| `k_sched_from_tick()` | tick 线程（持锁） | `catos_port_preempt_to(old, next)` | tick 线程挂起 old、唤醒 next（抢占） |

切换前统一：`core->current = next`、`next->state = RUNNING`、旧任务恢复 `READY`（若非 RUNNING 状态则由调用方保留，如 SUSPENDED/TERMINATED）。

### 4.3 调度点（何时触发调度）

`catos_task_create`、`catos_task_suspend`、`catos_task_resume`、`catos_task_exit`、`catos_sched_yield`、tick 中断。这些 API 在临界区内完成状态更新后调用 `k_sched_from_task()`；tick 线程调用 `catos_tick()` → `k_sched_from_tick()`。

### 4.4 yield 语义

`catos_sched_yield()` 把当前任务移到**同优先级队列尾部**，然后调度。因此：
- 存在同级任务时 → 轮转（round-robin，同级之间）；
- 存在更高优先级就绪任务时 → 立即被抢占；
- 否则 → 继续运行。

### 4.5 空闲任务

最低优先级任务，**不进入策略的就绪结构**，由调度核心在"无就绪任务"时兜底选用；入口只做空闲计数（`idle_count++`）。Windows 上由调用 `catos_kernel_start` 的主线程扮演。

### 4.6 调度策略接口（FR-SCHED-003）

```c
typedef struct catos_sched_ops {
    const char *name;
    void (*init)(catos_core_t *core);                 /* 初始化策略私有状态 */
    void (*ready_add)(catos_core_t *core, catos_task_t *t);    /* 任务进入就绪 */
    void (*ready_remove)(catos_core_t *core, catos_task_t *t); /* 任务离开就绪 */
    catos_task_t *(*pick_next)(catos_core_t *core);             /* 选下一个任务 */
    void (*on_yield)(catos_core_t *core, catos_task_t *t);      /* yield 语义 */
    void (*on_tick)(catos_core_t *core);                        /* tick 钩子（周期调度用） */
} catos_sched_ops_t;
```

- 策略私有状态挂在 `core->sched`；固定优先级策略（`sched_fp.c`）持有 `catos_ready_q_t`。
- `catos_sched_select(ops)` 运行时切换策略（须在创建用户任务前）；缺省为固定优先级。
- **扩展性验证**：`m1_sched_core/apps/tests/test_sched_ops.c` 在**应用层**实现了一个轮转策略（单 FIFO，无优先级），仅实现接口即可接入——证明新增策略不改内核核心。
- `k_ready_add/k_ready_remove` 是调度核心提供的路由，task.c/mutex.c 经它们操作当前策略的就绪结构。

### 4.7 互斥量与优先级继承 PIP（FR-SYNC-001、FR-PRIO）

**问题**：高/中/低三个任务，低任务持锁时若被中任务抢占，高任务阻塞在锁上会一直被中任务饿死（无界优先级反转）。

**机制**（`mutex.c`）：

- 等待队列按**有效优先级**升序；释放时所有权**直接转移**给队首等待者。
- 高优先级任务阻塞到某互斥量时，持有者临时继承其优先级（`eff_prio`），并沿阻塞链逐级传递。
- 核心函数 `k_recompute_priority(core, t, depth)`：从"t 所持互斥量的最高优先级等待者"重算 `t->eff_prio`，并把 t 在就绪/等待队列中移到正确位置；t 阻塞在别的互斥量上时沿链递归重算其 owner。`depth` 上限防死锁链环。
- 就绪队列按 `eff_prio` 排序（`ready_queue.c` 用 `eff_prio` 而非 `base_prio`），因此继承自动生效。
- 观测：`catos_task_eff_prio(task)`（FR-PRIO-003）。

**正确性要点**：重算时先用**旧** `eff_prio` 从就绪队列移出、更新后再插入，避免位图残留（`ready_q_remove` 按 `eff_prio` 索引子队列）。

**测试**：经典反转断言"Low 持锁被提升到 1 且 `H` 先于 `m`"；链式继承断言"Base 被 High→Low→Base 提升到 1"。

---

## 5. 移植层接口契约（FR-PORT-001）

见 `kernel/include/catos/catos_port.h`。核心需要的全部平台能力：

| 函数 | 职责 |
| --- | --- |
| `catos_port_init` | 初始化（内核锁等） |
| `catos_port_bind_idle` | 把空闲任务绑定到"当前线程" |
| `catos_port_task_start` | 为任务创建挂起态执行体 |
| `catos_port_task_destroy` | 销毁任务执行体 |
| `catos_port_critical_enter/exit` | 进入/退出临界区（可嵌套） |
| `catos_port_yield_to` | 自愿切换（当前任务让出 CPU） |
| `catos_port_preempt_to` | 抢占切换（tick 上下文） |
| `catos_port_start_scheduler` | 启动 tick 源 |
| `catos_port_ctz` | 取最低置位位（就绪队列选最高优先级） |

移植到新平台只需实现上述接口 + 提供一个调用 `catos_tick()` 的 tick 源。

---

## 6. Windows 移植机制（FR-WIN）

### 6.1 线程模型

- **每任务一个 Win32 线程**，创建时为 `CREATE_SUSPENDED`，首次被调度时唤醒；
- **内核锁 = 递归 `CRITICAL_SECTION`**，串行化所有内核入口（可嵌套）；
- **tick = 高优先级线程 + `CreateWaitableTimer`（周期 `CATOS_CFG_TICK_MS` ms）**，每次到期调用 `catos_tick()`；
- **切换 = `ResumeThread` / `SuspendThread`**；
- **空闲任务 = 主线程**（`catos_kernel_start` 的调用线程）。

### 6.2 正确性要点

- 内核核心在切换前已把 `core->current` 置为 `next`；
- **自愿切换** `yield_to`：`释放锁 → ResumeThread(next) → SuspendThread(self) → 重新上锁后返回`；
- **抢占切换** `preempt_to`：由 tick 线程持锁执行，`ResumeThread(next)` + `SuspendThread(old)`，不挂起调用者。

由此保证两条关键不变式：

1. **内核锁绝不会被挂起/暂停的任务线程持有**：
   - 自愿切换在挂起自己前先释放锁；
   - 抢占切换由 tick 线程执行，tick 线程不会被挂起；
   - tick 线程获取锁前必须等当前任务的内核操作完成，因此被挂起的 old 必然处于任务代码（未持锁）。
2. **不会双重挂起**：tick 只挂起 `core->current`；而处于"释放锁→自挂起"窗口的任务已不再是 current（切换前已置为 next），tick 不会碰它。

### 6.3 已知限制（仿真环境）

任务代码内**不得调用可能阻塞的 Win32 API**（如 `Sleep`、阻塞式磁盘 I/O），否则可能与抢占式挂起相互干扰。这是教学/调试用途的合理约束；嵌入式目标无此问题。

### 6.4 为什么用线程而不是纤程

纤程（Fiber）方案只能由所属线程主动 `SwitchToFiber`，无法被其它线程在任意指令处强制切换——即无法实现真正的抢占。线程 + `SuspendThread` 能实现真实抢占与真实时间节奏（tick 周期驱动），与嵌入式行为等价。

---

## 7. 多核扩展预留（需求 A-06）

- 就绪队列、当前任务均放在 `catos_core_t`（per-core），全局 `g_cores[CATOS_CFG_NUM_CORES]`；
- 调度器以 core 为单位操作；未来 SMP = 实例化多个 core + 移植层增加核间唤醒（IPI）钩子，不改写核心逻辑。

---

## 8. M0 已实现 vs 后续里程碑

| 里程碑 | 内容 | 状态 |
| --- | --- | --- |
| M0 | 任务管理、就绪队列、固定优先级最小调度、空闲任务、Windows 移植 | ✅ |
| M1 | 可插拔调度器 `sched_ops`、优先级继承互斥量（FR-PRIO）、BLOCKED 状态 | ✅ 本里程碑 |
| M2 | 信号量/消息队列/事件、tick 延时、软定时器 | 待做 |
| M3 | 周期调度（FR-SCHED-002） | 待做 |
| M4 | HAL / 设备驱动框架 | 待做 |
| M5 | FC/GBA 模拟器集成 | 待做 |
| M6 | STM32 Cortex-M4 移植 | 待做 |
| M7 | 测试完善、性能指标测定 | 待做 |
