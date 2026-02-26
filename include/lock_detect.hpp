#pragma once

#include <memory>
#include <string>

/**
 * @file lock_detect.h
 * @brief 死锁检测模块头文件
 *
 * 该文件定义了死锁检测的公共接口，提供了一个单例类用于注册、启动和执行死锁检测。
 * 使用PIMPL模式隐藏实现细节，减少编译依赖。
 */

/**
 * @brief 前向声明LockDetectImpl类
 *
 * 使用PIMPL模式（指向实现的指针）隐藏实现细节，
 * 减少编译依赖，提高编译速度，同时避免暴露内部数据结构
 */
class LockDetectImpl;

/**
 * @class LockDetect
 * @brief 死锁检测器类，用于检测多线程程序中的潜在死锁
 *
 * 该类提供了一个单例接口，用于注册、启动和执行死锁检测。
 * 它使用PIMPL模式隐藏实现细节，通过hook
 * pthread_mutex相关函数来跟踪锁的获取和释放。
 * 通过分析锁的依赖关系，可以检测出潜在的死锁情况。
 */
class LockDetect {
public:
  /**
   * @brief 获取LockDetect单例实例
   * @return LockDetect& 单例实例的引用
   *
   * 使用静态局部变量确保线程安全的单例初始化
   * 这种实现方式符合C++11标准，保证了线程安全的懒汉式单例模式
   */
  static LockDetect &GetInstance() {
    static LockDetect instance;
    return instance;
  }

  /**
   * @brief 注册指定库进行死锁检测
   * @param lib_name 库名称或路径
   *
   * 将指定的动态库添加到死锁检测范围，会hook该库中的pthread_mutex相关函数
   * 这样可以跟踪该库中的所有锁操作，用于构建锁依赖图
   */
  void Register(const std::string &lib_name);

  /**
   * @brief 注册主程序进行死锁检测
   *
   * 将主程序添加到死锁检测范围，会hook主程序中的pthread_mutex相关函数
   * 这样可以跟踪主程序中的所有锁操作，用于构建锁依赖图
   */
  void RegisterMain();

  /**
   * @brief 开始死锁检测
   *
   * 启动所有注册的库的hook，开始跟踪锁的获取和释放
   * 必须在Register或RegisterMain调用后执行
   * 这个函数会实际执行函数替换操作，使得后续的锁操作都会被跟踪
   */
  void Start();

  /**
   * @brief 检测并报告潜在的死锁
   *
   * 分析当前锁的依赖关系，检测是否存在潜在的死锁
   * 如果发现死锁，会输出详细的锁依赖链和调用栈信息
   * 可以在程序的关键点调用此函数，检查是否存在死锁风险
   */
  void Detect();

  /**
   * @brief 析构函数
   *
   * 清理资源，释放实现类实例
   * 由于使用了智能指针，资源会自动释放
   */
  ~LockDetect();

private:
  /**
   * @brief 私有构造函数
   *
   * 创建实现类实例，实现单例模式
   * 私有化构造函数确保外部无法直接创建实例
   */
  LockDetect();

  /**
   * @brief 指向实现类的智能指针
   *
   * 使用PIMPL模式隐藏实现细节，减少编译依赖
   * std::unique_ptr确保资源自动释放，避免内存泄漏
   */
  std::unique_ptr<LockDetectImpl> impl_;
};
