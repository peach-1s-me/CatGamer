# ============================================================
# CatOS M1 调度核心
#
# 构建（从仓库根目录或本目录）：
#   cmake -S m1_sched_core -B build/m1 -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build/m1
#
# 运行：
#   build/m1/catos_demo.exe              演示
#   build/m1/catos_test_sched.exe        调度回归测试（退出码 0 = 通过）
#   build/m1/catos_test_mutex.exe        互斥量/优先级继承测试
#   build/m1/catos_test_sched_ops.exe    调度策略扩展测试
#   build/m1/catos_test_rt.exe           运行库单元测试
#
# 分层（FR-LIB）：运行库 lib/ → 内核核心 kernel/src → 移植层 kernel/port/<target>
#                 → 应用 apps/
#   运行库与内核核心以独立环境（freestanding）编译，不使用宿主库；
#   只有移植层可以用宿主库/API（FR-LIB-006）。
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

# ---------------- 运行库（平台无关，FR-LIB-003/009） ----------------
# -ffreestanding -fno-builtin 是**必须**的：
#   - freestanding：不假设宿主标准库存在；
#   - fno-builtin ：否则编译器会把运行库自己的字节循环识别成库调用，
#                   生成对函数自身的调用（无限递归）。
set(CATOS_FREESTANDING_FLAGS -ffreestanding -fno-builtin -Wall -Wextra)

add_library(catos_lib STATIC
    lib/src/catos_string.c
    lib/src/catos_stdio.c
    lib/src/catos_stdlib.c
    lib/src/catos_ctype.c
    lib/src/catos_assert.c
)
target_include_directories(catos_lib PUBLIC lib/include)
target_compile_options(catos_lib PRIVATE ${CATOS_FREESTANDING_FLAGS})

# ---------------- 内核核心（平台无关，FR-LIB-001/002） ----------------
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
target_compile_options(catos_kernel PRIVATE ${CATOS_FREESTANDING_FLAGS})
target_link_libraries(catos_kernel PUBLIC catos_lib)

# ---------------- 移植层（唯一允许使用宿主库/API 的一层，FR-LIB-006） ----------------
if(CATOS_TARGET STREQUAL "win32")
    add_library(catos_port_win32 STATIC
        kernel/port/win32/port.c
        kernel/port/win32/port_rt.c
    )
    # 使用较新的 Win32 API 定义（只给移植层，核心层不含宿主头）
    target_compile_definitions(catos_port_win32 PRIVATE _WIN32_WINNT=0x0601)
    # 移植层用宿主运行库，因此**不加** -ffreestanding
    target_compile_options(catos_port_win32 PRIVATE -Wall -Wextra)
    target_link_libraries(catos_port_win32 PUBLIC catos_kernel)

    set(CATOS_PORT_TARGET catos_port_win32)
endif()

# 三个静态库之间存在互相调用（内核调用 catos_port_*，移植层调用 catos_tick 等，
# 运行库调用 catos_port_log_write），静态库默认单遍扫描会留下未定义符号。
# 用 GNU ld 的 --start-group/--end-group 让这三者被重复扫描直到符号全部解析。
# （注：CMake 的 $<LINK_GROUP:RESCAN> 在 MinGW 的 C 链接语言上不受支持。）
set(CATOS_APP_LIBS
    -Wl,--start-group
    ${CATOS_PORT_TARGET} catos_kernel catos_lib
    -Wl,--end-group)

# ---------------- 应用（平台无关：只依赖公共接口，FR-LIB-008） ----------------
# 应用保留自己的 int main(void)：CRT 启动由移植层承载，应用不碰宿主 API。
# 应用源码不随平台变化；新增一个移植只需提供新的 ${CATOS_PORT_TARGET}。

# 演示程序
add_executable(catos_demo apps/demo/main.c)
target_link_libraries(catos_demo PRIVATE ${CATOS_APP_LIBS})
target_compile_options(catos_demo PRIVATE ${CATOS_FREESTANDING_FLAGS})

# 调度冒烟测试
add_executable(catos_test_sched apps/tests/test_sched.c)
target_link_libraries(catos_test_sched PRIVATE ${CATOS_APP_LIBS})
target_compile_options(catos_test_sched PRIVATE ${CATOS_FREESTANDING_FLAGS})

# 互斥量/优先级继承测试
add_executable(catos_test_mutex apps/tests/test_mutex.c)
target_link_libraries(catos_test_mutex PRIVATE ${CATOS_APP_LIBS})
target_compile_options(catos_test_mutex PRIVATE ${CATOS_FREESTANDING_FLAGS})

# 调度策略扩展测试（FR-TEST-003）
add_executable(catos_test_sched_ops apps/tests/test_sched_ops.c)
target_link_libraries(catos_test_sched_ops PRIVATE ${CATOS_APP_LIBS})
target_compile_options(catos_test_sched_ops PRIVATE ${CATOS_FREESTANDING_FLAGS})

# 运行库单元测试（格式化/字符串/原子/分类）
add_executable(catos_test_rt apps/tests/test_rt.c)
target_link_libraries(catos_test_rt PRIVATE ${CATOS_APP_LIBS})
target_compile_options(catos_test_rt PRIVATE ${CATOS_FREESTANDING_FLAGS})
