#pragma once

extern "C" {

/**
 * @enum DetectorOption
 * @brief 检测器选项枚举
 *
 * 定义了检测器支持的检测模式，可以通过位运算组合使用
 */
enum DetectorOption {
  DetectorOption_Memory = 1,      // 仅启用内存泄漏检测
  DetectorOption_Lock = 2,        // 仅启用死锁检测
  DetectorOption_MemoryLock = 3,  // 同时启用内存泄漏和死锁检测
};

/**
 * @enum OutputOption
 * @brief 输出选项枚举
 *
 * 定义了检测结果的输出方式，可以通过位运算组合使用
 */
enum OutputOption {
  OutputOption_Console = 1,      // 仅输出到控制台
  OutputOption_File = 2,         // 仅输出到文件
  OutputOption_ConsoleFile = 3,  // 同时输出到控制台和文件
};

/**
 * @brief 初始化检测器
 * @param work_dir 工作目录，用于存放输出文件
 * @param detect_option 检测选项，指定要启用的检测模式
 * @param output_option 输出选项，指定检测结果的输出方式
 *
 * 该函数必须在使用其他检测器函数之前调用，用于配置检测器的工作模式和输出方式。
 * 工作目录用于存放检测结果文件，如果选择了文件输出方式。
 */
void Detector_Init(const char* work_dir, DetectorOption detect_option, OutputOption output_option);

/**
 * @brief 启动检测器
 *
 * 该函数启动所有已注册的检测模块，开始跟踪内存分配和锁操作。
 * 必须在注册完所有需要检测的库后调用，且在调用Detector_Detect之前调用。
 * 启动后，检测器会通过hook机制拦截相关函数调用，收集必要的信息。
 */
void Detector_Start(void);

/**
 * @brief 执行检测
 *
 * 该函数根据初始化时配置的选项，执行内存泄漏检测或死锁检测。
 * 可以在程序的关键点调用，检查是否存在内存泄漏或死锁。
 * 检测结果会根据配置的输出选项输出到控制台或文件。
 */
void Detector_Detect(void);

/**
 * @brief 注册指定库进行检测
 * @param lib_name 库名称或路径
 *
 * 该函数将指定的动态库添加到检测范围，检测器会hook该库中的相关函数。
 * 必须在调用Detector_Start之前调用，可以多次调用以注册多个库。
 * 如果lib_name为NULL，函数会直接返回而不执行任何操作。
 */
void Detector_Register(const char* lib_name);

/**
 * @brief 注册主程序进行检测
 *
 * 该函数将主程序添加到检测范围，检测器会hook主程序中的相关函数。
 * 必须在调用Detector_Start之前调用。
 * 这个函数是Detector_Register的特殊情况，相当于使用空字符串调用Detector_Register。
 */
void Detector_RegisterMain(void);

}  // extern "C"
