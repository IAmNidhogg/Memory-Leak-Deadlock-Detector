cmake_minimum_required(VERSION 3.10)

project(detector LANGUAGES CXX)

# 使用至少 C++17（可以根据需要调整）
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 头文件目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# 依赖的系统库：pthread / dl（跨平台写法）
find_package(Threads REQUIRED)

# CMake 会在不同平台自动给出合适的 dl 库（Linux 有，macOS 通常为空）
set(EXTRA_DL_LIBS ${CMAKE_DL_LIBS})

# 核心共享库：detector
add_library(detector SHARED
    src/detector.cpp
    src/memory_detect.cpp
    src/lock_detect.cpp
    src/output_control.cpp
    src/plthook_elf64.cpp
)

target_link_libraries(detector
    PRIVATE
        Threads::Threads
        ${EXTRA_DL_LIBS}
)

target_include_directories(detector
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 是否构建示例程序
option(BUILD_DETECTOR_EXAMPLES "Build example executables for detector" ON)

if(BUILD_DETECTOR_EXAMPLES)
    # 示例 1：内存 & 死锁检测演示
    add_executable(leak_detector_main
        src/leak_detector_main.cpp
    )

    # 这里链接 detector 库以及 pthread / dl
    # 虽然示例用的是 dlopen/dlsym，但链接进来不会有坏处，方便后续扩展
    target_link_libraries(leak_detector_main
        PRIVATE
            detector
            Threads::Threads
            ${EXTRA_DL_LIBS}
    )

    # 示例 2：PLT hook 演示（printf hook）
    add_executable(plthook_demo
        src/plthook_main.cpp
    )

    target_link_libraries(plthook_demo
        PRIVATE
            detector          # 内含 PLTHook 实现
            Threads::Threads
            ${EXTRA_DL_LIBS}
    )
endif()

