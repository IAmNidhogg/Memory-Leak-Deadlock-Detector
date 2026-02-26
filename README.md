# Detector

基于 **PLT/GOT Hook** 的 C/C++ 运行时检测库，支持**内存泄漏检测**与**死锁检测**，检测结果可输出到控制台或文件。

---

## 功能概览

| 功能         | 说明 |
|--------------|------|
| **内存泄漏检测** | 通过 Hook `malloc`/`free`、`calloc`/`realloc` 以及 C++ `operator new`/`delete`，记录分配与释放，在检测时输出未释放的分配及调用栈（含符号与源码位置）。 |
| **死锁检测**     | 通过 Hook `pthread_mutex_lock`/`unlock`/`trylock`，维护锁与线程的持有/等待关系，使用 DFS 检测等待环路并输出死锁链与调用栈。 |
| **输出控制**     | 支持仅控制台、仅文件或同时输出；日志文件名可按工作目录与时间戳自动生成。 |

---

## 环境要求

- **编译器**：支持 C++11 的 C++ 编译器（如 GCC、Clang）
- **构建系统**：CMake 3.10 及以上
- **平台**：当前实现面向 **Linux（ELF64/x86_64）**；`plthook_elf64` 依赖 ELF 与 `/proc/self/maps` 等，在其他系统上可能需要替换或条件编译

---

## 项目结构

```
detector/
├── CMakeLists.txt          # CMake 构建配置
├── README.md               # 本说明
├── include/                # 头文件
│   ├── detector.hpp        # 检测器 C 接口与选项枚举
│   ├── memory_detect.hpp   # 内存检测接口
│   ├── lock_detect.hpp    # 死锁检测接口
│   ├── output_control.hpp  # 输出控制与 TRACKER_* 宏
│   └── plthook.hpp        # PLT Hook 接口
└── src/                    # 源文件
    ├── detector.cpp        # 检测器总控实现
    ├── memory_detect.cpp   # 内存泄漏检测实现
    ├── lock_detect.cpp     # 死锁检测实现
    ├── output_control.cpp  # 输出控制实现
    ├── plthook_elf64.cpp   # ELF64 PLT Hook 实现
    ├── leak_detector_main.cpp  # 示例：内存+死锁检测
    └── plthook_main.cpp    # 示例：PLT Hook（如 printf）演示
```

---

## 构建

### 基本构建

```bash
cd detector
cmake -B build
cmake --build build
```

生成物：

- **共享库**：`build/libdetector.so`（Linux）或对应平台命名
- **示例可执行文件**（默认会构建）：
  - `build/leak_detector_main` — 内存泄漏与死锁检测示例
  - `build/plthook_demo` — PLT Hook 示例

### 仅构建库、不构建示例

```bash
cmake -B build -DBUILD_DETECTOR_EXAMPLES=OFF
cmake --build build
```

### 指定编译类型（如 Release）

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

---

## 使用方式

### 1. 作为动态库使用（推荐）

将 `detector` 编译为动态库（如 `libdetector.so`），在目标程序中通过 `dlopen`/`dlsym` 调用下述 C 接口，无需在编译时链接该库。

### 2. C 接口说明

所有对外接口在 `include/detector.hpp` 中声明，均为 `extern "C"`。

#### 选项枚举

- **DetectorOption**（检测模式，可组合）  
  - `DetectorOption_Memory = 1` — 仅内存泄漏检测  
  - `DetectorOption_Lock = 2` — 仅死锁检测  
  - `DetectorOption_MemoryLock = 3` — 同时启用两者  

- **OutputOption**（输出方式）  
  - `OutputOption_Console = 1` — 仅控制台  
  - `OutputOption_File = 2` — 仅文件  
  - `OutputOption_ConsoleFile = 3` — 控制台 + 文件  

#### 接口函数

| 函数 | 说明 |
|------|------|
| `void Detector_Init(const char *work_dir, DetectorOption detect_option, OutputOption output_option)` | 初始化。`work_dir` 用于生成日志路径；须在其它接口之前调用。 |
| `void Detector_Register(const char *lib_name)` | 注册要检测的动态库（库名或路径）。须在 `Detector_Start` 之前调用，可多次调用注册多个库。 |
| `void Detector_RegisterMain(void)` | 注册主程序为检测对象（相当于对主程序做 Hook）。须在 `Detector_Start` 之前调用。 |
| `void Detector_Start(void)` | 启动检测（对所有已注册对象安装 Hook）。须在全部 `Register` 之后调用。 |
| `void Detector_Detect(void)` | 执行一次检测：根据选项输出内存泄漏和/或死锁报告。可在程序关键点多次调用。 |

日志文件默认命名为：`work_dir/detector_<时间戳>.log`。

### 3. 调用顺序示例

```c
#include "detector.hpp"

// 1. 初始化：工作目录 "."，内存+死锁，控制台+文件
Detector_Init(".", DetectorOption_MemoryLock, OutputOption_ConsoleFile);

// 2. 注册主程序（或 Detector_Register("libxxx.so") 注册指定库）
Detector_RegisterMain();

// 3. 启动检测（开始 Hook）
Detector_Start();

// 4. 运行你的业务逻辑
// ...

// 5. 在需要时执行检测
Detector_Detect();
```

---

## 示例程序说明

### leak_detector_main

演示内存泄漏与死锁检测：通过 `dlopen` 加载 `detector.so`，调用上述 C 接口，并故意制造泄漏与死锁场景。

- **注意**：源码中 `detector.so` 的路径为硬编码（如 `/mnt/d/project/camping/detector/detector.so`），运行前需按你的构建目录修改，或改为相对路径（如 `./libdetector.so`），并确保从正确的工作目录执行，以便找到库与日志文件。

运行示例（路径需根据实际修改）：

```bash
cd build
./leak_detector_main
```

### plthook_demo

演示对动态库中 `printf` 的 PLT Hook。会加载 `./libdynamic_example.so` 并调用其中的函数（如 `SimpleAdd`），在 Hook 前后对比 `printf` 行为。

- 运行前需自行准备或编译出 `libdynamic_example.so`，并放在可执行文件同目录或库搜索路径下，否则会因找不到库而失败。

---

## 实现要点

- **内存检测**：对目标模块的 PLT 中 `malloc`、`free`、`calloc`、`realloc` 以及 C++ `operator new`/`delete`（含 `new[]`/`delete[]`）进行替换，在包装函数中记录分配/释放并维护调用栈；检测时对未释放块做汇总，并用 `backtrace`/`dladdr`/`addr2line` 解析符号与源码位置。
- **死锁检测**：对 `pthread_mutex_*` 进行 Hook，维护“线程–锁”的持有与等待关系，构建有向图；在每次加锁尝试时做 DFS，若发现等待环路则判定为潜在死锁并打印锁链与调用栈。
- **输出**：由 `OutputControl` 单例统一根据 `OutputOption` 输出到控制台和/或文件；内部使用 `TRACKER_PRINT`、`TRACKER_ERROR`、`TRACKER_WARNING` 等宏。

---

## 许可证与贡献

许可证以仓库为准。欢迎通过 Issue 或 Pull Request 反馈问题与改进建议。
