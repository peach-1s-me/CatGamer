# ============================================================
# CatOS M1 调度核心
#
# 构建（从仓库根目录或本目录）：
#   cmake -S m1_sched_core -B build/m1 -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build/m1
#
# 运行：
#   build/m1/apps/demo/catos_demo.exe              演示
#   build/m1/apps/tests/catos_test_sched.exe       调度回归测试（退出码 0 = 通过）
#   build/m1/apps/tests/catos_test_mutex.exe       互斥量/优先级继承测试
#   build/m1/apps/tests/catos_test_sched_ops.exe   调度策略扩展测试
# ============================================================

cmake_minimum_required(VERSION 3.16)

# ---- 工具链选择（须在 project() 之前） ----
# CATOS_TOOLCHAIN：
#   auto  —— 由 PATH/环境自动探测编译器（默认）
#   mingw —— 固定使用 D:\MinGW（经典 32 位 MinGW.org gcc）
set(CATOS_TOOLCHAIN "auto" CACHE STRING "Toolchain: auto | mingw (D:/MinGW)")
if(CATOS_TOOLCHAIN STREQUAL "mingw")
    # 编译器与工具链使用 D:\MinGW（经典 32 位 MinGW.org gcc）。
    # 要点：
    #  - 编译器路径用 msys2 POSIX 形式（/d/...）。msys2 的 make 会自动转换为
    #    Windows 路径再启动；D:/... 形式会被 msys2 的 CMake 截断，勿用。
    #  - 构建时须把 D:/MinGW/bin 加入 PATH（cc1.exe 需要其中的运行时 DLL）。
    #  - 明确指定 msys2 的 make（/usr/bin/make），否则 CMake 可能选到
    #    D:\MinGW 自带的 mingw32-make（它不做 /d/ 路径转换，会启动失败）。
    set(CMAKE_C_COMPILER   "/d/MinGW/bin/gcc.exe"     CACHE FILEPATH "C compiler" FORCE)
    set(CMAKE_AR           "/d/MinGW/bin/ar.exe"      CACHE FILEPATH "ar"         FORCE)
    set(CMAKE_RANLIB       "/d/MinGW/bin/ranlib.exe"  CACHE FILEPATH "ranlib"     FORCE)
    set(CMAKE_MAKE_PROGRAM "/usr/bin/make"            CACHE FILEPATH "make"       FORCE)
endif()

project(CatOS_M1 C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# MinGW 静态链接运行时，生成的 exe 不依赖外部 DLL，可直接运行
if(MINGW)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
endif()

# 目标平台选择：win32（当前唯一实现；stm32f4 等在 M6 里程碑加入）
set(CATOS_TARGET "win32" CACHE STRING "Target platform (win32)")
if(NOT CATOS_TARGET STREQUAL "win32")
    message(FATAL_ERROR "Unsupported CATOS_TARGET '${CATOS_TARGET}'. "
                        "M1 only provides 'win32'.")
endif()

# ---------------- 内核静态库 ----------------
add_library(catos_kernel STATIC
    kernel/src/task.c
    kernel/src/ready_queue.c
    kernel/src/sched.c
    kernel/src/sched_fp.c
    kernel/src/mutex.c
    kernel/src/kernel.c
)
target_include_directories(catos_kernel PUBLIC kernel/include)
target_include_directories(catos_kernel PRIVATE kernel/src)

if(CATOS_TARGET STREQUAL "win32")
    target_sources(catos_kernel PRIVATE kernel/port/win32/port.c)
    # 使用较新的 Win32 API 定义
    target_compile_definitions(catos_kernel PUBLIC _WIN32_WINNT=0x0601)
endif()

# ---------------- 演示程序 ----------------
add_executable(catos_demo apps/demo/main.c)
target_link_libraries(catos_demo PRIVATE catos_kernel)

# ---------------- 调度冒烟测试 ----------------
add_executable(catos_test_sched apps/tests/test_sched.c)
target_link_libraries(catos_test_sched PRIVATE catos_kernel)

# ---------------- 互斥量/优先级继承测试 ----------------
add_executable(catos_test_mutex apps/tests/test_mutex.c)
target_link_libraries(catos_test_mutex PRIVATE catos_kernel)

# ---------------- 调度策略扩展测试（FR-TEST-003） ----------------
add_executable(catos_test_sched_ops apps/tests/test_sched_ops.c)
target_link_libraries(catos_test_sched_ops PRIVATE catos_kernel)
