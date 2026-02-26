/**
 * @file detector.cc
 * @brief 检测器模块实现文件
 *
 * 该文件实现了检测器的核心功能，包括初始化、启动、检测和注册等操作。
 * 检测器支持内存泄漏检测和死锁检测两种模式，可以通过配置选项进行控制。
 */

#include "detector.hpp"

#include <string>
#include <time.h>

#include "lock_detect.h"
#include "memory_detect.h"
#include "output_control.h"

/**
 * @brief 生成输出文件路径
 * @param work_dir 工作目录
 * @return 完整的输出文件路径
 *
 * 根据工作目录和当前时间戳生成唯一的输出文件名
 */
std::string GetFilePath(std::string work_dir) {
  std::string output_file_name =
      work_dir + "/detector_" + std::to_string(time(nullptr)) + ".log";
  return output_file_name;
}

extern "C" {

/**
 * @brief 检测器选项全局变量
 *
 * 默认启用内存和锁检测
 */
static DetectorOption detector_option = DetectorOption_MemoryLock;

/**
 * @brief 初始化检测器
 * @param work_dir 工作目录
 * @param detect_option 检测选项
 * @param output_option 输出选项
 *
 * 配置检测器的工作模式和输出方式，必须在使用其他函数前调用
 */
void Detector_Init(const char *work_dir, DetectorOption detect_option,
                   OutputOption output_option) {
  detector_option = detect_option;
  std::string output_file_name = GetFilePath(work_dir);
  tracker::OutputControl::Instance().Configure(output_option, output_file_name);
}

/**
 * @brief 启动检测器
 *
 * 根据初始化时配置的选项启动相应的检测模块
 * 必须在注册完所有需要检测的库后调用
 */
void Detector_Start(void) {
  if (detector_option & DetectorOption_Memory) {
    MemoryDetect::GetInstance().Start();
  }
  if (detector_option & DetectorOption_Lock) {
    LockDetect::GetInstance().Start();
  }
}

/**
 * @brief 执行检测
 *
 * 根据配置的选项执行内存泄漏检测或死锁检测
 * 可以在程序的关键点调用，检查是否存在内存泄漏或死锁
 */
void Detector_Detect(void) {
  if (detector_option & DetectorOption_Memory) {
    MemoryDetect::GetInstance().Detect();
  }
  if (detector_option & DetectorOption_Lock) {
    LockDetect::GetInstance().Detect();
  }
}

/**
 * @brief 注册指定库进行检测
 * @param lib_name 库名称或路径
 *
 * 将指定的动态库添加到检测范围，会hook该库中的相关函数
 * 必须在Start前调用
 */
void Detector_Register(const char *lib_name) {
  if (lib_name == nullptr) {
    return;
  }
  if (detector_option & DetectorOption_Memory) {
    MemoryDetect::GetInstance().Register(lib_name);
  }
  if (detector_option & DetectorOption_Lock) {
    LockDetect::GetInstance().Register(lib_name);
  }
}

/**
 * @brief 注册主程序进行检测
 *
 * 将主程序添加到检测范围，会hook主程序中的相关函数
 * 必须在Start前调用
 */
void Detector_RegisterMain(void) {
  if (detector_option & DetectorOption_Memory) {
    MemoryDetect::GetInstance().Register("");
  }
  if (detector_option & DetectorOption_Lock) {
    LockDetect::GetInstance().Register("");
  }
}

} // extern "C"
