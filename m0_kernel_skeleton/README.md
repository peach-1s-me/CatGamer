# CatOS · M0 内核骨架

> 里程碑 M0（需求文档第 9 节）。本文档为本里程碑的独立文档（FR-DOC-001/002）。
> 目标：交付可编译、可运行、能在 Windows 上观察多任务调度行为的内核骨架。

## 1. 交付内容

| 模块 | 说明 | 覆盖需求 |
| --- | --- | --- |
| 内核核心 `kernel/src/` | 任务管理（静态 TCB 池）、就绪队列（优先级位图 + FIFO）、最小固定优先级调度、空闲任务 | FR-TASK、FR-SCHED-004/005 |
| 运行库 `lib/` | CatOS 自实现的 C 运行库（与 ISO 头一一对应）：字符串/内存、格式化输出、退出、字符分类、断言 | FR-LIB-003/005/009 |
| 公共接口 `kernel/include/catos/` | `catos_*` API、配置、错误码、侵入式链表、原子操作、移植接口契约 | FR-PORT-001/005 |
| Windows 移植 `kernel/port/win32/` | 每任务一线程、递归临界区内核锁、可等待定时器 tick、Suspend/Resume 切换；**唯一允许使用宿主库的一层**：输出后端、panic、退出、原子操作实现 | FR-WIN-002、FR-LIB-006 |
| 演示程序 `apps/demo/` | 固定优先级顺序 + resume 抢占演示（只用 CatOS 公共接口，平台无关） | AS-1、FR-LIB-008 |
| 调度冒烟测试 `apps/tests/` | 优先级顺序、yield 轮转、suspend/resume、动态创建/删除、参数校验 | FR-TEST-001/002（部分） |
| 依赖检查脚本 `tools/check_libc.sh` | 头文件 + 符号扫描，验证"核心/应用零宿主依赖、宿主依赖只在移植层" | FR-LIB-007 |
| 内核设计文档 `docs/design/kernel.md` | 任务状态机、调度流程、移植接口契约、Windows 机制、宿主依赖清单 | NFR-001、FR-PORT-007 |

> **运行库独立性（FR-LIB，2026-09 就地整改）**：内核核心与运行库以 `-ffreestanding -fno-builtin` 构建，
> 不依赖任何标准库；宿主库/API 只出现在 `kernel/port/win32/` 内。整改前内核用 `<string.h>` 的 `memset`、
> 应用用 `printf`/`exit`/`windows.h`，现分别改为 `catos_memset`、`catos_printf`/`catos_exit`/`catos_atomic_inc`。

M0 实现的是**固定优先级抢占式调度的基础**（就绪集中取最高优先级 + 两条切换路径）；可插拔调度器接口（FR-SCHED-003）与优先级反转解决（FR-PRIO）属 M1。

## 2. 目录结构

```
m0_kernel_skeleton/
├── CMakeLists.txt                    # 构建脚本（含工具链选择）
├── lib/                              # ★ 运行库（平台无关，FR-LIB-003/009）
│   ├── include/
│   │   ├── catos_string.h             # ← <string.h>：catos_mem*/catos_str*
│   │   ├── catos_stdio.h              # ← <stdio.h>：catos_printf/catos_write/...
│   │   ├── catos_stdlib.h             # ← <stdlib.h>：catos_exit/catos_abort/...
│   │   ├── catos_ctype.h              # ← <ctype.h>
│   │   ├── catos_assert.h             # ← <assert.h>：CATOS_ASSERT/catos_panic
│   │   ├── catos_libcfg.h             # 运行库配置（缓冲上限、系统消息换行）
│   │   └── catos_backend.h            # 运行库后端契约（由移植层实现）
│   └── src/                          # catos_string.c ... catos_assert.c
├── kernel/
│   ├── include/catos/                # ★ 公共/移植接口
│   │   ├── catos.h                    # 总入口
│   │   ├── catos_config.h             # 配置：优先级数/任务上限/tick 等
│   │   ├── catos_types.h              # 基础类型、错误码
│   │   ├── catos_list.h               # 侵入式双向链表（内联）
│   │   ├── catos_task.h               # 任务 API
│   │   ├── catos_sched.h              # 内核启动/调度/yield/tick
│   │   ├── catos_atomic.h             # 原子操作（移植层实现）
│   │   └── catos_port.h               # 移植层接口契约
│   ├── src/                           # ★ 内核核心（平台无关）
│   │   ├── catos_internal.h
│   │   ├── task.c
│   │   ├── ready_queue.c
│   │   ├── sched.c
│   │   └── kernel.c
│   └── port/win32/                    # Windows 移植（唯一允许用宿主库的一层）
│       ├── port.c                     # 任务执行体/内核锁/切换/tick
│       ├── port_rt.c                  # 输出后端/panic/退出/原子操作
│       └── port_internal.h
└── apps/
    ├── demo/main.c                   # 演示：固定优先级 + 抢占
    └── tests/test_sched.c            # 冒烟测试（退出码 0 = 通过）
```

三层隔离（`lib/` → `kernel/src` → `kernel/port/<target>/` → `apps/`）：
- `kernel/src` 与 `kernel/include/catos` 不含平台分支 / Win32 / 汇编（FR-PORT-006）；
- 内核核心、运行库与应用**不依赖任何宿主库**，宿主库/API 只出现在 `kernel/port/win32/`（FR-LIB-006）。

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

**运行库依赖检查**（FR-LIB-007，在仓库根目录执行）：

```bash
tools/check_libc.sh m0_kernel_skeleton build/m0
```

**本次实测结果**（D:\MinGW 工具链）：

| 项目 | 结果 |
| --- | --- |
| 构建 | 0 警告（`-Wall -Wextra`） |
| demo | 退出码 0，输出与预期一致 |
| 测试 | `[PASS] log = ABabHMLD`，退出码 0 |
| 测试稳定性 | FR-LIB 整改后复跑 20 轮全部通过（调度确定性；日志串不变），demo 3 次通过 |
| 核心纯净性 | grep 确认核心层无平台分支/Win32/汇编 |
| 运行库依赖检查 | 6/6 项通过：核心层 0 个宿主符号；应用对象仅 `__main`（编译器启动钩子）；2 个 exe 链接期无宿主符号；移植层宿主符号单列（25 个 Win32 导入） |

## 5. 已知限制

- Windows 移植是**开发/调试仿真环境**：任务代码内不得调用可能阻塞的 Win32 API（如 `Sleep`、阻塞式磁盘 I/O），否则可能与抢占式挂起相互干扰（详见 `docs/design/kernel.md` §6.3）。**唯一例外**是日志输出后端（`kernel/port/win32/port_rt.c`）：它直接写宿主 stdout，若 stdout 重定向到管道且读端不消费可能阻塞；运行时请保证 stdout 被正常消费（终端或 `> 文件`）。详见设计文档 §6.5。
- **输出不再做换行翻译**（FR-LIB）：应用写 `\n` 就输出 `\n`（需要 CRLF 请自己写 `"\r\n"`）；系统消息的换行由 `CATOS_CFG_NL`（`lib/include/catos_libcfg.h`）决定。整改前 CRT 会把 `\n` 翻成 CRLF，因此重定向输出比整改前每行少一个 `\r`。
- M0 无 `BLOCKED` 状态与时间管理（任务延时/定时器属 M2）；yield 只在**同优先级**任务间轮转。
- 已终止/挂起任务的操作系统线程句柄在进程退出前不回收（教学目的可接受）。

## 6. 下一步

M1：**可插拔调度器接口 `sched_ops` + 优先级继承互斥量**（FR-SCHED-003、FR-PRIO，解决优先级反转）。

创建方式：复制本文件夹为 `m1_sched_core/`，扩展内核并在本文档模板上更新为新里程碑内容（FR-DOC-003）。

> M0/M1 两个里程碑均已就地完成 FR-LIB 整改（运行库 `lib/`、宿主依赖收敛到移植层、依赖检查脚本）；
> 后续里程碑新增代码须直接遵守 FR-LIB，并按 FR-LIB-007 记录检查结果。
