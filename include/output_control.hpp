#pragma once

#include <cstdarg>
#include <string>

#include "detector.hpp"

namespace tracker {

/**
 * @class OutputControl
 * @brief 输出控制类，用于管理检测器的输出行为
 *
 * 该类实现了单例模式，提供统一的输出接口，支持输出到控制台、文件或两者同时。
 * 通过该类可以集中管理所有检测器组件的日志和调试信息，便于问题排查和结果分析。
 */
class OutputControl {
public:
  /**
   * @brief 获取OutputControl单例实例
   * @return OutputControl& 单例实例的引用
   *
   * 使用静态局部变量确保线程安全的单例初始化
   */
  static OutputControl &Instance() {
    static OutputControl instance;
    return instance;
  }

  /**
   * @brief 配置输出选项和文件名
   * @param option 输出选项(控制台/文件/两者)
   * @param filename 输出文件名(默认为空，会使用时间戳生成文件名)
   *
   * 设置输出的目标位置和文件名。如果已经有打开的文件，会先关闭它。
   */
  void Configure(OutputOption option, const std::string &filename = "");

  /**
   * @brief 通用输出函数，根据配置输出到控制台、文件或两者
   * @param format 格式化字符串，类似printf
   * @param ... 可变参数列表
   *
   * 根据当前配置的输出选项，将信息输出到相应的目标位置
   */
  void Print(const char *format, ...) const;

  /**
   * @brief 仅输出到文件
   * @param format 格式化字符串，类似printf
   * @param ... 可变参数列表
   *
   * 无论当前配置如何，都只输出到文件(如果文件已打开)
   */
  void PrintToFile(const char *format, ...) const;

  /**
   * @brief 仅输出到控制台
   * @param format 格式化字符串，类似printf
   * @param ... 可变参数列表
   *
   * 无论当前配置如何，都只输出到控制台
   */
  void PrintToConsole(const char *format, ...) const;

  /**
   * @brief 获取当前输出文件指针
   * @return FILE* 当前输出文件的指针，如果未打开文件则为nullptr
   *
   * 用于需要直接访问文件指针的场景
   */
  FILE *GetOutputFile() const { return output_file_; }

private:
  /**
   * @brief 默认构造函数
   *
   * 私有构造函数，防止外部创建实例，实现单例模式
   */
  OutputControl() = default;

  /**
   * @brief 析构函数
   *
   * 负责清理资源，关闭打开的文件
   */
  ~OutputControl();

  // 禁用拷贝构造和赋值操作，确保单例性质
  OutputControl(const OutputControl &) = delete;
  OutputControl &operator=(const OutputControl &) = delete;

  /**
   * @brief 打开输出文件
   *
   * 根据配置的文件名打开输出文件，如果文件名为空则使用时间戳生成
   */
  void OpenOutputFile();

  /**
   * @brief 关闭输出文件
   *
   * 安全地关闭当前打开的输出文件
   */
  void CloseOutputFile();

  // 当前输出选项(控制台/文件/两者)
  OutputOption output_option_ = OutputOption::OutputOption_ConsoleFile;

  // 输出文件名
  std::string output_file_name_;

  // 输出文件指针
  FILE *output_file_ = nullptr;
};

} // namespace tracker

/**
 * @def TRACKER_PRINT
 * @brief 通用输出宏，根据配置输出到控制台、文件或两者
 * @param ... 格式化字符串和参数，类似printf
 *
 * 使用OutputControl单例的Print方法输出信息
 */
#define TRACKER_PRINT(...) tracker::OutputControl::Instance().Print(__VA_ARGS__)

/**
 * @def TRACKER_PRINT_FILE
 * @brief 仅输出到文件的宏
 * @param ... 格式化字符串和参数，类似printf
 *
 * 使用OutputControl单例的PrintToFile方法输出信息
 */
#define TRACKER_PRINT_FILE(...)                                                \
  tracker::OutputControl::Instance().PrintToFile(__VA_ARGS__)

/**
 * @def TRACKER_PRINT_CONSOLE
 * @brief 仅输出到控制台的宏
 * @param ... 格式化字符串和参数，类似printf
 *
 * 使用OutputControl单例的PrintToConsole方法输出信息
 */
#define TRACKER_PRINT_CONSOLE(...)                                             \
  tracker::OutputControl::Instance().PrintToConsole(__VA_ARGS__)

/**
 * @def TRACKER_PRINT_IF
 * @brief 条件输出宏，满足条件时才输出
 * @param condition 条件表达式
 * @param ... 格式化字符串和参数，类似printf
 *
 * 当条件为真时，使用TRACKER_PRINT输出信息
 */
#define TRACKER_PRINT_IF(condition, ...)                                       \
  do {                                                                         \
    if (condition) {                                                           \
      TRACKER_PRINT(__VA_ARGS__);                                              \
    }                                                                          \
  } while (0)

/**
 * @def TRACKER_ERROR
 * @brief 错误输出宏，添加"ERROR: "前缀
 * @param ... 格式化字符串和参数，类似printf
 *
 * 用于输出错误信息，自动添加ERROR前缀
 */
#define TRACKER_ERROR(...) TRACKER_PRINT("ERROR: " __VA_ARGS__)

/**
 * @def TRACKER_WARNING
 * @brief 警告输出宏，添加"WARNING: "前缀
 * @param ... 格式化字符串和参数，类似printf
 *
 * 用于输出警告信息，自动添加WARNING前缀
 */
#define TRACKER_WARNING(...) TRACKER_PRINT("WARNING: " __VA_ARGS__)

/**
 * @def TRACKER_DEBUG
 * @brief 调试输出宏，添加"DEBUG: "前缀，仅在DEBUG模式下有效
 * @param ... 格式化字符串和参数，类似printf
 *
 * 用于输出调试信息，自动添加DEBUG前缀，在非DEBUG模式下被忽略
 */
#ifdef DEBUG
#define TRACKER_DEBUG(...) TRACKER_PRINT("DEBUG: " __VA_ARGS__)
#else
#define TRACKER_DEBUG(...) ((void)0)
#endif
