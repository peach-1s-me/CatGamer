# CatGamer

基于 CatOS 实时操作系统的复古游戏平台软件。目标：在 CatOS 上移植并运行 FC（NES）与 GBA 模拟器，且能在不同硬件平台上运行。

## 文档

- **需求规格说明书**：[`docs/requirements.md`](docs/requirements.md)（已定稿，含验收标准）
- **内核设计文档**：[`docs/design/kernel.md`](docs/design/kernel.md)

## 里程碑目录约定

每个里程碑一个**自包含文件夹**，可独立构建、独立运行；新里程碑从上一个里程碑**复制需要的文件**后扩展。**每个里程碑文件夹内含其独立文档 `README.md`**（交付内容/构建步骤/运行验证/已知限制/下一步，见需求规格 FR-DOC）。

| 目录 | 内容 | 状态 |
| --- | --- | --- |
| `m0_kernel_skeleton/` | 项目结构、任务管理、就绪队列、最小调度、空闲任务、Windows 移植 | ✅ 完成 |
| `m1_sched_core/` | 可插拔调度器 `sched_ops`、优先级继承互斥量 | ✅ 完成 |
| `m2_sync_time/` | 同步原语、时间管理、软定时器 | 待做 |
| `m2_sync_time/` | 同步原语、时间管理、软定时器 | 待做 |
| `m3_periodic/` | 周期调度 | 待做 |
| `m4_hal_dev/` | HAL / 设备驱动框架 | 待做 |
| `m5_emulator/` | FC/GBA 模拟器集成 | 待做 |
| `m6_stm32f4/` | STM32 Cortex-M4 移植 | 待做 |
| `m7_test/` | 测试完善、指标测定 | 待做 |

> 里程碑编号与需求文档第 9 节一致。M1 起沿用"复制上一个里程碑 → 扩展"的方式创建。

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

Windows 移植是**开发/调试仿真环境**：任务代码内不得调用可能阻塞的 Win32 API（如 `Sleep`、阻塞式磁盘 I/O），否则可能与抢占式挂起相互干扰。详见 [`docs/design/kernel.md`](docs/design/kernel.md) 第 6.3 节。
