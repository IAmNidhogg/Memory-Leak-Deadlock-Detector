#pragma once

#include <memory>
#include <string>

/**
 * @class PLTHook
 * @brief PLT钩子类，用于替换动态链接库中的函数
 *
 * PLT (Procedure Linkage Table) 是ELF格式可执行文件中用于动态链接的表。
 * 该类提供了一种机制，可以在运行时替换动态链接库中的函数，
 * 实现对函数调用的拦截和监控，常用于性能分析、调试和安全审计等场景。
 */
class PLTHook {
 public:
  // 错误代码
  static constexpr int SUCCESS = 0;              // 操作成功
  static constexpr int FILE_NOT_FOUND = -1;      // 找不到指定的文件
  static constexpr int INVALID_ARGUMENT = -2;    // 参数无效
  static constexpr int FUNCTION_NOT_FOUND = -3;  // 找不到指定的函数
  static constexpr int INTERNAL_ERROR = -4;      // 内部错误
  static constexpr int EOF_REACHED = -5;         // 已到达文件末尾

  /**
   * @brief 创建指定库的PLT钩子
   * @param filename 库的名称（如果为nullptr则表示主程序）
   * @return 指向PLTHook实例的唯一指针
   * @throws std::runtime_error 创建失败时抛出异常
   *
   * 该函数创建一个PLTHook实例，用于操作指定库的PLT表。
   * 如果filename为nullptr，则操作主程序的PLT表。
   */
  static std::unique_ptr<PLTHook> Create(const char* filename);

  /**
   * @brief 枚举PLT表中的符号
   * @param pos 位置指针（输入/输出参数）
   * @param name_out 用于存储函数名的引用
   * @param addr_out 用于存储函数地址的引用
   * @return 成功返回0，没有更多条目时返回EOF
   *
   * 该函数用于遍历PLT表中的所有符号，每次调用返回一个符号的信息。
   * 首次调用时pos应设为0，后续调用使用上次调用后的pos值。
   * 当没有更多符号时，返回EOF_REACHED。
   */
  int EnumerateSymbols(unsigned int& pos, const char*& name_out, void**& addr_out) const;

  /**
   * @brief 替换PLT表中的函数
   * @param funcname 要替换的函数名
   * @param newfunc 新的函数指针
   * @param oldfunc 可选参数，用于存储原始函数指针
   * @return 成功返回0，失败返回错误代码
   *
   * 该函数用于替换PLT表中指定名称的函数。
   * 替换后，对原函数的所有调用都会被重定向到newfunc。
   * 如果提供了oldfunc参数，原始函数的地址会被存储在其中，
   * 这样可以在新函数中调用原始函数，实现函数调用的拦截和监控。
   */
  int ReplaceFunction(const char* funcname, void* newfunc, void** oldfunc = nullptr);

  /**
   * @brief 获取最近的错误信息
   * @return 错误信息字符串
   *
   * 当操作失败时，可以通过该函数获取详细的错误信息。
   * 返回的是静态存储的字符串，不需要调用者释放。
   */
  static const std::string& GetLastError();

  /**
   * @brief 析构函数
   *
   * 释放所有资源，包括内部分配的内存和打开的文件句柄。
   */
  ~PLTHook();

 private:
  // 实现细节隐藏在头文件之外
  struct Impl;
  std::unique_ptr<Impl> pimpl_;

  // 私有构造函数 - 使用Create()代替
  explicit PLTHook(struct link_map* lmap);

  // 禁止拷贝
  PLTHook(const PLTHook&) = delete;
  PLTHook& operator=(const PLTHook&) = delete;
};
