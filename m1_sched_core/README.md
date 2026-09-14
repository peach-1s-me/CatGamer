# CatOS · M1 调度核心

> 里程碑 M1（需求文档第 9 节）。本文档为本里程碑的独立文档（FR-DOC-001/002）。
> 目标：可插拔调度策略 + 优先级继承互斥量（解决优先级反转）。

## 1. 交付内容

| 模块 | 说明 | 覆盖需求 |
| --- | --- | --- |
| 调度策略接口 `sched_ops` | 统一可插拔接口（init/ready_add/ready_remove/pick_next/on_yield/on_tick）；`catos_sched_select` 运行时切换 | FR-SCHED-003 |
| 调度器重构 `kernel/src/sched.c` | 通用调度核心（两条切换路径、就绪操作路由到策略、空闲任务兜底） | FR-SCHED-003 |
| 固定优先级策略 `kernel/src/sched_fp.c` | 缺省策略：位图 + 每级 FIFO 就绪队列，按**有效优先级**排序 | FR-SCHED-001 |
| 优先级继承互斥量 `kernel/src/mutex.c` | lock/trylock/unlock、嵌套获取、等待队列按优先级、所有权转移、链式继承、`catos_task_eff_prio` 观测 | FR-PRIO-001~004、FR-SYNC-001 |
| `BLOCKED` 状态 | 首次投入使用（互斥量阻塞），状态机与 PIP 联动 | FR-TASK-002 |
| 测试 `apps/tests/` | 调度回归、互斥量/PIP（经典反转+链式）、调度策略扩展（应用层轮转策略）、运行库单元测试 | FR-TEST-001/002/003 |
| 运行库 `lib/` | CatOS 自实现的 C 运行库（`catos_string.h`/`catos_stdio.h`/`catos_stdlib.h`/`catos_ctype.h`/`catos_assert.h`，与 ISO 头一一对应） | FR-LIB-003/005/009 |
| 原子操作 `catos/catos_atomic.h` | `catos_atomic_add/inc/dec/get/set/swap/cas`（移植层实现，替代应用的 `InterlockedIncrement`） | FR-PORT-005、FR-LIB-009② |
| Windows 后端 `kernel/port/win32/port_rt.c` | 日志输出后端（`WriteFile` + 日志锁）、panic、`catos_exit`、原子操作；**唯一使用宿主库的一层** | FR-LIB-006/009 |
| 依赖检查 `tools/check_libc.sh` | 头文件 + 符号扫描（含 exe 链接期符号、跨里程碑一致性） | FR-LIB-007 |

## 2. 目录结构

```
m1_sched_core/
├── CMakeLists.txt
├── lib/                      # ★ 运行库（与 M0 逐字相同，平台无关）
│   ├── include/  catos_string.h catos_stdio.h catos_stdlib.h catos_ctype.h
│   │             catos_assert.h catos_libcfg.h catos_backend.h
│   └── src/      catos_string.c catos_stdio.c catos_stdlib.c catos_ctype.c catos_assert.c
├── kernel/
│   ├── include/catos/
│   │   ├── catos.h  catos_config.h  catos_types.h  catos_list.h
│   │   ├── catos_task.h      # TCB 增 eff_prio / held_mutexes / blocked_on
│   │   ├── catos_sched.h     # sched_ops 接口、catos_sched_select
│   │   ├── catos_mutex.h     # ★ 互斥量 API（新）
│   │   ├── catos_atomic.h    # ★ 原子操作（移植层实现，新）
│   │   └── catos_port.h
│   ├── src/
│   │   ├── catos_internal.h  # sched_ops 结构、core（ops+sched）、内部接口
│   │   ├── task.c            # 经策略路由就绪操作；catos_memset
│   │   ├── ready_queue.c     # 就绪队列按 eff_prio 排序
│   │   ├── sched.c           # 通用调度核心 + 路由 + select
│   │   ├── sched_fp.c        # ★ 固定优先级策略（新）
│   │   ├── mutex.c           # ★ 互斥量 + PIP（新）
│   │   └── kernel.c
│   └── port/win32/
│       ├── port.c            # 任务执行体/内核锁/切换/tick
│       ├── port_rt.c         # ★ 输出后端/panic/退出/原子操作（新）
│       └── port_internal.h
└── apps/
    ├── demo/main.c
    └── tests/
        ├── test_sched.c          # M0 回归（调度/挂起恢复/动态创建删除）
        ├── test_mutex.c          # ★ 互斥量 + 优先级反转 + 链式继承
        └── test_sched_ops.c      # ★ 应用层实现的轮转策略（扩展性证明）
```

核心/移植隔离与 M0 相同（FR-PORT-006）；运行库独立性也相同（FR-LIB）：`lib/` 与 `kernel/port/win32/` 下的文件与 M0 逐字相同（靠 `tools/check_libc.sh` 第 [6/6] 项守护）。

## 3. 构建步骤

与 M0 相同，两种工具链任选：

```bash
# 方式 B：固定 D:\MinGW（构建时须把其 bin 加入 PATH）
export PATH="/d/MinGW/bin:$PATH"
cmake -S m1_sched_core -B build/m1 -DCMAKE_BUILD_TYPE=Debug -DCATOS_TOOLCHAIN=mingw
cmake --build build/m1

# 方式 A：自动探测（任一 MinGW 的 bin 在 PATH 即可）
cmake -S m1_sched_core -B build/m1 -DCMAKE_BUILD_TYPE=Debug
cmake --build build/m1
```

## 4. 运行与验证

```bash
./build/m1/catos_demo.exe            # 演示（固定优先级 + 抢占，同 M0）
./build/m1/catos_test_sched.exe      # 调度回归（退出码 0 = 通过）
./build/m1/catos_test_mutex.exe      # 互斥量/PIP
./build/m1/catos_test_sched_ops.exe  # 调度策略扩展
./build/m1/catos_test_rt.exe         # 运行库单元测试（格式化/字符串/原子/分类）
```

**预期输出**：

| 程序 | 输出 | 含义 |
| --- | --- | --- |
| test_sched | `[PASS] log = ABabHMLD` | 重构后 M0 行为不变 |
| test_mutex | `[PASS] log = LMhHmEBLhlHxb` | 经典反转（`H` 先于 `m`：Low 被提升后抢先于 Medium）+ 链式继承（`BLhlHxb`） |
| test_sched_ops | `[PASS] log = ABCABCABC` | 应用层轮转策略按创建顺序运行，优先级被忽略 |
| test_rt | `[PASS] 运行库单元测试` | 运行库自测：`%.*s` 精度上界、`*` 宽度/精度参数不错位、整数精度、截断与返回值、memmove 重叠、原子 CAS 等 |

**运行库依赖检查**（FR-LIB-007，在仓库根目录执行）：

```bash
tools/check_libc.sh m1_sched_core build/m1
```

**本次实测结果**（D:\MinGW 工具链）：

| 项目 | 结果 |
| --- | --- |
| 构建 | 0 警告（`-Wall -Wextra`） |
| demo / 三个测试 | 全部退出码 0 |
| 稳定性 | FR-LIB 整改后复跑：test_sched 60 轮、test_mutex / test_sched_ops / test_rt 各 10 轮、demo 3 次，全部通过且日志串不变 |
| 核心纯净性 | grep 确认无平台分支/Win32/汇编 |
| 运行库依赖检查 | 6/6 项通过：核心层 0 个宿主符号；应用对象仅 `__main`（编译器启动钩子）；4 个 exe 链接期无宿主符号；移植层宿主符号单列（25 个 Win32 导入）；21 个共享文件与 M0 逐字相同 |

> **与整改前的差异（唯一）**：应用输出不再经 CRT 做 `\n`→CRLF 翻译，重定向后每行少一个 `\r`（内容与顺序完全一致，见设计文档 §6.5）。

## 5. 已知限制

- 互斥量**不支持超时等待**（M2 引入，依赖时间管理）。
- **不支持删除仍持有互斥量的任务**；不支持死锁检测（两任务互持阻塞时双方永久阻塞，PIP 递归有深度保护不崩溃）。
- Windows 移植仿真环境限制沿用 M0（任务内不得调用阻塞 Win32 API）——**唯一例外**是日志输出后端（`port_rt.c` 写 stdout，重定向到管道且读端不消费时会阻塞，见设计文档 §6.5）。

## 6. 下一步

M2：**同步与时间**——信号量、消息队列、事件标志、tick 延时、软定时器（FR-SYNC、FR-TIME），并为互斥量补超时。

创建方式：复制本文件夹为 `m2_sync_time/`，扩展内核并更新本文档（FR-DOC-003）。
