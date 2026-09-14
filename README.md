# CatGamer

基于 CatOS 实时操作系统的复古游戏平台软件。目标：在 CatOS 上移植并运行 FC（NES）与 GBA 模拟器，且能在不同硬件平台上运行。

## 文档

- **需求规格说明书**：[`docs/requirements.md`](docs/requirements.md)（已定稿，含验收标准）
- **内核设计文档**：[`docs/design/kernel.md`](docs/design/kernel.md)

## 里程碑目录约定

每个里程碑一个**自包含文件夹**，可独立构建、独立运行；新里程碑从上一个里程碑**复制需要的文件**后扩展。**每个里程碑文件夹内含其独立文档 `README.md`**（交付内容/构建步骤/运行验证/已知限制/下一步，见需求规格 FR-DOC）。

| 目录 | 内容 | 状态 |
| --- | --- | --- |
| `m0_kernel_skeleton/` | 项目结构、任务管理、就绪队列、最小调度、空闲任务、Windows 移植 | ✅ 完成（含 FR-LIB 整改） |
| `m1_sched_core/` | 可插拔调度器 `sched_ops`、优先级继承互斥量 | ✅ 完成（含 FR-LIB 整改） |
| `m2_sync_time/` | 同步原语、时间管理、软定时器 | 待做 |
| `m3_periodic/` | 周期调度 | 待做 |
| `m4_hal_dev/` | HAL / 设备驱动框架 | 待做 |
| `m5_emulator/` | FC/GBA 模拟器集成 | 待做 |
| `m6_stm32f4/` | STM32 Cortex-M4 移植 | 待做 |
| `m7_test/` | 测试完善、指标测定 | 待做 |

> 里程碑编号与需求文档第 9 节一致。M1 起沿用"复制上一个里程碑 → 扩展"的方式创建。

## 代码组织与依赖约束（FR-LIB）

每个里程碑内部按四层组织，宿主库（libc/CRT/Win32）的使用**只允许出现在移植层**：

```
lib/         运行库：CatOS 自实现的 C 运行库（与 ISO 头一一对应，catos_ 前缀）
kernel/src   内核核心：平台无关；只依赖运行库与公共头
kernel/port/<target>/   移植层：唯一允许调用宿主库/API 的一层
apps/        应用（demo/测试/后续 CatGamer）：只依赖公共接口，换平台零改动
```

运行库 `lib/` 与内核核心以 `-ffreestanding -fno-builtin` 构建；应用输出经 `catos_printf`/`catos_write`（格式化在运行库，输出后端在移植层）。
详见 `docs/requirements.md` 第 4.8 节（FR-LIB）与 `docs/design/kernel.md` 第 1、6.5 节。

**运行库依赖检查**（每个里程碑交付前执行，FR-LIB-007）：

```bash
tools/check_libc.sh m0_kernel_skeleton build/m0
tools/check_libc.sh m1_sched_core build/m1
```

## 构建与运行 M0（Windows 开发环境）

前提：CMake + 一个 Windows 宿主编译器。两种工具链可选：

### 方式 A：自动探测（默认，auto）

把任一 MinGW 的 `bin` 目录加入 PATH，CMake 自动找到编译器：

```bash
export PATH="/d/Qt/Tools/mingw1310_64/bin:$PATH"   # 示例：Qt 自带的 MinGW-w64
cmake -S m0_kernel_skeleton -B build/m0 -DCMAKE_BUILD_TYPE=Debug
cmake --build build/m0
```

### 方式 B：固定使用 D:\MinGW（经典 32 位 MinGW.org）

路径已固化在 `CMakeLists.txt`（`CATOS_TOOLCHAIN=mingw`），无需指定编译器路径：

```bash
# 构建时需把 D:\MinGW\bin 加入 PATH（其 cc1.exe 需要其中的运行时 DLL）
export PATH="/d/MinGW/bin:$PATH"
cmake -S m0_kernel_skeleton -B build/m0 -DCMAKE_BUILD_TYPE=Debug -DCATOS_TOOLCHAIN=mingw
cmake --build build/m0
```

> 该方式自动使用 msys2 的 make（`/usr/bin/make`）做路径转换，并给 exe 静态链接运行时
> （`-static-libgcc -static-libstdc++`），因此**生成的 exe 独立可运行，运行时无需 PATH**。

### 运行

```bash
# 运行演示（固定优先级 + 抢占）
./build/m0/catos_demo.exe

# 运行调度冒烟测试（退出码 0 = 通过）
./build/m0/catos_test_sched.exe
```

## 已知限制（M0 Windows 移植）

Windows 移植是**开发/调试仿真环境**：任务代码内不得调用可能阻塞的 Win32 API（如 `Sleep`、阻塞式磁盘 I/O），否则可能与抢占式挂起相互干扰。**唯一例外**是日志输出后端（`kernel/port/win32/port_rt.c`，直接写 stdout）。详见 [`docs/design/kernel.md`](docs/design/kernel.md) 第 6.3、6.5 节。

输出**不做换行翻译**：应用写 `\n` 就输出 `\n`（需要 CRLF 请自己写 `"\r\n"`）；系统消息（panic 等）的换行由 `CATOS_CFG_NL`（`lib/include/catos_libcfg.h`）配置。
