#include "plthook.hpp"
#include <cstdio>
#include <dlfcn.h>
#include <iostream>

// 定义printf函数类型
typedef int (*PrintfFunc)(const char *format, ...);

// 我们的hook函数
int HookedPrintf(const char *format, ...) {
  printf("HookedPrintf was called with format: %s\n", format);
  return 0;
}

int main() {
  try {
    // 加载动态库
    void *lib_handle = dlopen("./libdynamic_example.so", RTLD_LAZY);
    if (!lib_handle) {
      std::cerr << "Failed to load library: " << dlerror() << std::endl;
      return 1;
    }

    // 获取SimpleAdd函数来测试
    typedef int (*SimpleAddFunc)(int, int);
    SimpleAddFunc simple_add = (SimpleAddFunc)dlsym(lib_handle, "SimpleAdd");
    if (!simple_add) {
      std::cerr << "Failed to get SimpleAdd: " << dlerror() << std::endl;
      dlclose(lib_handle);
      return 1;
    }

    std::cout << "Before hook, calling SimpleAdd(1, 2):" << std::endl;
    simple_add(1, 2); // 这会调用原始的printf

    // 创建PLTHook实例
    auto hook = PLTHook::Create("./libdynamic_example.so");

    // 保存原始printf函数指针
    void *original_printf = nullptr;

    // 替换printf函数
    if (hook->ReplaceFunction("printf", (void *)HookedPrintf,
                              &original_printf) != PLTHook::SUCCESS) {
      std::cerr << "Failed to hook printf: " << PLTHook::GetLastError()
                << std::endl;
      dlclose(lib_handle);
      return 1;
    }

    std::cout << "\nSuccessfully hooked printf\n" << std::endl;

    // 测试被hook的函数
    std::cout << "After hook, calling SimpleAdd(1, 2):" << std::endl;
    simple_add(1, 2); // 这会调用被hook的printf

    simple_add(1, 2); // 这会调用被hook的printf
    // 清理
    dlclose(lib_handle);
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
