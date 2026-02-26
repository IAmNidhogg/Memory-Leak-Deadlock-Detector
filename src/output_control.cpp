#include "output_control.hpp"
#include <ctime>

namespace tracker {

/**
 * @brief 配置输出选项和文件名
 * @param option 输出选项(控制台/文件/两者)
 * @param filename 输出文件名(默认为空，会使用时间戳生成文件名)
 *
 * 该函数设置输出控制器的行为模式和输出文件。
 * 如果已经有打开的文件，会先关闭它，然后根据新的配置决定是否打开新文件。
 */
void OutputControl::Configure(OutputOption option, const std::string& filename) {
  // 关闭之前可能打开的输出文件
  CloseOutputFile();

  // 更新输出选项和文件名
  output_option_ = option;
  output_file_name_ = filename;

  // 如果输出选项不是仅控制台，则打开输出文件
  if (option != OutputOption::OutputOption_Console) {
    OpenOutputFile();
  }
}

/**
 * @brief 通用输出函数，根据配置输出到控制台、文件或两者
 * @param format 格式化字符串，类似printf
 * @param ... 可变参数列表
 *
 * 根据当前配置的输出选项，将信息输出到相应的目标位置。
 * 使用va_list处理可变参数，确保参数能正确传递给不同的输出目标。
 */
void OutputControl::Print(const char* format, ...) const {
  // 初始化可变参数列表
  va_list args1, args2;
  va_start(args1, format);

  // 如果配置为输出到控制台或同时输出到控制台和文件，则输出到控制台
  if (output_option_ == OutputOption::OutputOption_ConsoleFile ||
      output_option_ == OutputOption::OutputOption_Console) {
    // 复制参数列表，因为vprintf会消耗参数
    va_copy(args2, args1);
    vprintf(format, args2);
    va_end(args2);
  }

  // 如果配置为输出到文件或同时输出到控制台和文件，且文件已打开，则输出到文件
  if ((output_option_ == OutputOption::OutputOption_ConsoleFile || output_option_ == OutputOption::OutputOption_File) &&
      output_file_) {
    vfprintf(output_file_, format, args1);
    // 立即刷新文件缓冲区，确保数据写入磁盘
    fflush(output_file_);
  }

  // 清理可变参数列表
  va_end(args1);
}

/**
 * @brief 仅输出到文件
 * @param format 格式化字符串，类似printf
 * @param ... 可变参数列表
 *
 * 无论当前配置如何，都只输出到文件(如果文件已打开)。
 * 如果文件未打开或配置为仅控制台输出，则不执行任何操作。
 */
void OutputControl::PrintToFile(const char* format, ...) const {
  // 如果文件未打开或配置为仅控制台输出，则直接返回
  if (!output_file_ || output_option_ == OutputOption::OutputOption_Console) {
    return;
  }

  // 处理可变参数并输出到文件
  va_list args;
  va_start(args, format);
  vfprintf(output_file_, format, args);
  // 立即刷新文件缓冲区，确保数据写入磁盘
  fflush(output_file_);
  va_end(args);
}

/**
 * @brief 仅输出到控制台
 * @param format 格式化字符串，类似printf
 * @param ... 可变参数列表
 *
 * 无论当前配置如何，都只输出到控制台。
 * 如果配置为仅文件输出，则不执行任何操作。
 */
void OutputControl::PrintToConsole(const char* format, ...) const {
  // 如果配置为仅文件输出，则直接返回
  if (output_option_ == OutputOption::OutputOption_File) {
    return;
  }

  // 处理可变参数并输出到控制台
  va_list args;
  va_start(args, format);
  vprintf(format, args);
  va_end(args);
}

/**
 * @brief 打开输出文件
 *
 * 根据配置的文件名打开输出文件。如果文件名为空，则使用当前时间戳生成默认文件名。
 * 文件以写模式("w")打开，如果已存在则会被覆盖。
 */
void OutputControl::OpenOutputFile() {
  // 使用配置的文件名或生成默认文件名
  std::string filename = output_file_name_;
  if (filename.empty()) {
    // 生成带时间戳的默认文件名
    time_t now = time(nullptr);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S_", localtime(&now));
    filename = timestamp + std::string("detector.log");
  }

  // 以写模式打开文件
  output_file_ = fopen(filename.c_str(), "w");
  if (!output_file_) {
    // 如果打开失败，输出错误信息到控制台
    printf("Failed to open output file: %s\n", filename.c_str());
  }
}

/**
 * @brief 关闭输出文件
 *
 * 安全地关闭当前打开的输出文件，并将文件指针重置为nullptr。
 * 如果文件未打开，则不执行任何操作。
 */
void OutputControl::CloseOutputFile() {
  if (output_file_) {
    fclose(output_file_);
    output_file_ = nullptr;
  }
}

/**
 * @brief 析构函数
 *
 * 负责清理资源，关闭打开的文件。
 * 确保在对象销毁时不会泄漏文件句柄。
 */
OutputControl::~OutputControl() {
  CloseOutputFile();
}

}  // namespace tracker
