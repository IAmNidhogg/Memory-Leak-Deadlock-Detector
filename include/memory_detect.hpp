#pragma once

#include <memory>
#include <string>

/**
 * @brief 前向声明MemoryDetectImpl类
 * 
 * 使用PIMPL模式（指向实现的指针）隐藏实现细节，
 * 减少编译依赖，提高编译速度
 */
class MemoryDetectImpl;

/**
 * @class MemoryDetect
 * @brief 内存检测器类，用于检测内存泄漏
 * 
 * 该类提供了一个单例接口，用于注册、启动和执行内存泄漏检测。
 * 它使用PIMPL模式隐藏实现细节，通过hook内存分配和释放函数来跟踪内存使用。
 */
class MemoryDetect {
 public:
  /**
   * @brief 获取MemoryDetect单例实例
   * @return MemoryDetect& 单例实例的引用
   * 
   * 使用静态局部变量确保线程安全的单例初始化
   */
  static MemoryDetect& GetInstance() {
    static MemoryDetect instance;
    return instance;
  }

  /**
   * @brief 注册指定库进行内存检测
   * @param lib_name 库名称或路径
   * 
   * 将指定的动态库添加到内存检测范围，会hook该库中的内存分配和释放函数
   */
  void Register(const std::string& lib_name);

  /**
   * @brief 注册主程序进行内存检测
   * 
   * 将主程序添加到内存检测范围，会hook主程序中的内存分配和释放函数
   */
  void RegisterMain();

  /**
   * @brief 开始内存检测
   * 
   * 启动所有注册的库的hook，开始跟踪内存分配和释放
   * 必须在Register调用后执行
   */
  void Start();

  /**
   * @brief 检测并报告内存泄漏
   * 
   * 分析当前内存使用情况，输出潜在的内存泄漏信息
   * 包括泄漏地址、大小和调用栈
   */
  void Detect();

  /**
   * @brief 析构函数
   * 
   * 清理资源，释放实现类实例
   */
  ~MemoryDetect();

 private:
  /**
   * @brief 私有构造函数
   * 
   * 创建实现类实例，实现单例模式
   */
  MemoryDetect();

  /**
   * @brief 指向实现类的智能指针
   * 
   * 使用PIMPL模式隐藏实现细节，减少编译依赖
   * std::unique_ptr确保资源自动释放
   */
  std::unique_ptr<MemoryDetectImpl> impl_;
};
