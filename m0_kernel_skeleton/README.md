# CatOS · M0 内核骨架

> 里程碑 M0（需求文档第 9 节）。本文档为本里程碑的独立文档（FR-DOC-001/002）。
> 目标：交付可编译、可运行、能在 Windows 上观察多任务调度行为的内核骨架。

## 1. 交付内容

| 模块 | 说明 | 覆盖需求 |
| --- | --- | --- |
| 内核核心 `kernel/src/` | 任务管理（静态 TCB 池）、就绪队列（优先级位图 + FIFO）、最小固定优先级调度、空闲任务 | FR-TASK、FR-SCHED-004/005 |
| 公共接口 `kernel/include/catos/` | `catos_*` API、配置、错误码、侵入式链表、移植接口契约 | FR-PORT-001 |
| Windows 移植 `kernel/port/win32/` | 每任务一线程、递归临界区内核锁、可等待定时器 tick、Suspend/Resume 切换 | FR-WIN-002 |
| 演示程序 `apps/demo/` | 固定优先级顺序 + resume 抢占演示 | AS-1 |
| 调度冒烟测试 `apps/tests/` | 优先级顺序、yield 轮转、suspend/resume、动态创建/删除、参数校验 | FR-TEST-001/002（部分） |
| 内核设计文档 `docs/design/kernel.md` | 任务状态机、调度流程、移植接口契约、Windows 机制 | NFR-001 |

M0 实现的是**固定优先级抢占式调度的基础**（就绪集中取最高优先级 + 两条切换路径）；可插拔调度器接口（FR-SCHED-003）与优先级反转解决（FR-PRIO）属 M1。

## 2. 目录结构

```
m0_kernel_skeleton/
├── CMakeLists.txt                    # 构建脚本（含工具链选择）
├── kernel/
│   ├── include/catos/                # ★ 公共/移植接口（7 个头文件）
│   │   ├── catos.h                    # 总入口
│   │   ├── catos_config.h             # 配置：优先级数/任务上限/tick 等
│   │   ├── catos_types.h              # 基础类型、错误码
│   │   ├── catos_list.h               # 侵入式双向链表（内联）
│   │   ├── catos_task.h               # 任务 API
│   │   ├── catos_sched.h              # 内核启动/调度/yield/tick
│   │   └── catos_port.h               # 移植层接口契约
│   ├── src/                           # ★ 内核核心（平台无关）
│   │   ├── catos_internal.h
│   │   ├── task.c
│   │   ├── ready_queue.c
│   │   ├── sched.c
│   │   └── kernel.c
│   └── port/win32/port.c              # Windows 移植（唯一平台代码）
└── apps/
    ├── demo/main.c                   # 演示：固定优先级 + 抢占
    └── tests/test_sched.c            # 冒烟测试（退出码 0 = 通过）
```

核心/移植隔离：`kernel/src` 与 `kernel/include/catos` 不含平台分支 / Win32 / 汇编（FR-PORT-006）。

## 3. 构建步骤

前提：CMake + 一个 Windows 宿主编译器。两种方式任选：

### 方式 A：自动探测（默认）

```bash
# 把任一 MinGW 的 bin 目录加入 PATH（示例：Qt 自带 MinGW-w64）
export PATH="/d/Qt/Tools/mingw1310_64/bin:$PATH"
cmake -S m0_kernel_skeleton -B build/m0 -DCMAKE_BUILD_TYPE=Debug
cmake --build build/m0
```

### 方式 B：固定使用 D:\MinGW（路径已固化在 CMakeLists）

```bash
# 构建时须把 D:\MinGW\bin 加入 PATH（其 cc1.exe 需要其中的运行时 DLL）
export PATH="/d/MinGW/bin:$PATH"
cmake -S m0_kernel_skeleton -B build/m0 -DCMAKE_BUILD_TYPE=Debug -DCATOS_TOOLCHAIN=mingw
cmake --build build/m0
```

> 方式 B 在 CMakeLists 中固定编译器为 `/d/MinGW/bin/gcc.exe`、make 为 `/usr/bin/make`，
> 并给 exe 静态链接运行时，因此生成的 exe 独立可运行（运行时无需 PATH）。
> 两种方式生成的产物均在 `build/m0/` 下。

## 4. 运行与验证

```bash
# 演示：固定优先级 + 抢占
./build/m0/catos_demo.exe

# 冒烟测试（退出码 0 = 全部通过）
./build/m0/catos_test_sched.exe
```

**demo 预期输出**（体现固定优先级顺序与抢占）：

```
[catos] M0 demo starting (fixed-priority + preemption)
HHHHHHHHHH
[high] completed 10 times
MMMMMMMM
[mid] completed 8 times
LLLLL
[low] completed 5 times
[preempt] slow start
[preempt] higher-priority task got CPU, preempting slow
[preempt] slow finished (was preempted)
[checker] all tasks done, exiting
```

**测试预期输出**：`[PASS] log = ABabHMLD`，退出码 0。

**本次实测结果**（D:\MinGW 与 Qt MinGW 两种工具链均验证）：

| 项目 | 结果 |
| --- | --- |
| 构建 | 0 警告 |
| demo | 退出码 0，输出与预期一致 |
| 测试 | `[PASS] log = ABabHMLD`，退出码 0 |
| 测试稳定性 | 连跑 20~30 轮全部通过（调度确定性） |
| 核心纯净性 | grep 确认核心层无平台分支/Win32/汇编 |

## 5. 已知限制

- Windows 移植是**开发/调试仿真环境**：任务代码内不得调用可能阻塞的 Win32 API（如 `Sleep`、阻塞式磁盘 I/O），否则可能与抢占式挂起相互干扰（详见 `docs/design/kernel.md` §6.3）。
- M0 无 `BLOCKED` 状态与时间管理（任务延时/定时器属 M2）；yield 只在**同优先级**任务间轮转。
- 已终止/挂起任务的操作系统线程句柄在进程退出前不回收（教学目的可接受）。

## 6. 下一步

M1：**可插拔调度器接口 `sched_ops` + 优先级继承互斥量**（FR-SCHED-003、FR-PRIO，解决优先级反转）。

创建方式：复制本文件夹为 `m1_sched_core/`，扩展内核并在本文档模板上更新为新里程碑内容（FR-DOC-003）。
