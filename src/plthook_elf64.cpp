#include "plthook.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <elf.h>
#include <errno.h>
#include <limits.h>
#include <link.h>
#include <memory>
#include <string>
#include <sys/mman.h>
#include <system_error>
#include <unistd.h>
#include <vector>

// 定义重定位类型常量，这里使用x86_64架构的JUMP_SLOT类型
// 这是PLT表中用于函数调用的重定位类型
constexpr auto R_JUMP_SLOT = R_X86_64_JUMP_SLOT;

// 内存保护信息结构体，用于存储内存区域的保护属性
struct MemoryProtection {
  size_t start;   // 内存区域起始地址
  size_t end;     // 内存区域结束地址
  int protection; // 保护属性(PROT_READ, PROT_WRITE, PROT_EXEC的组合)
};

// PLTHook类的实现细节
struct PLTHook::Impl {
  // 动态符号表指针，包含了程序中所有动态链接符号的信息
  // 每个符号包含名称、大小、类型、绑定信息等
  const Elf64_Sym *dynsym_;

  // 动态字符串表指针，存储了符号名称等字符串数据
  // 符号表中的st_name字段是字符串表的索引
  const char *dynstr_;

  // 动态字符串表的大小，用于边界检查
  size_t dynstr_size_;

  // PLT（过程链接表）的基地址，用于计算函数地址的偏移
  // 这是加载到内存中的共享库的基地址
  void *plt_addr_base_;

  // 重定位表指针，包含了需要重定位的条目信息
  // 对于PLT，这是.rela.plt段，包含了函数调用的重定位信息
  const Elf64_Rela *rela_plt_;

  // 重定位表中条目的数量，用于遍历所有重定位项
  size_t rela_plt_cnt_;

  // 内存页保护属性的vector，记录了各个内存区域的读写执行权限
  // 用于在修改PLT表项时临时更改内存保护属性
  std::vector<MemoryProtection> memory_protections_;

  // 系统内存页大小（静态成员），通常为4KB
  // 用于内存保护操作时对齐地址
  static size_t page_size_;

  // 错误信息存储（静态成员）
  // 用于记录操作过程中的错误信息
  static std::string error_message_;

  // 构造函数，接收动态链接器的映射信息
  // link_map包含了共享库的加载地址和动态段信息
  explicit Impl(struct link_map *lmap);

  // 从link_map初始化PLT hook所需的各种表和地址信息
  // 解析动态段中的符号表、字符串表和重定位表
  void InitializeFromLinkMap(struct link_map *lmap);

  // 加载进程内存映射的保护属性信息
  // 通过读取/proc/self/maps获取内存区域的保护属性
  void LoadMemoryProtections();

  // 获取指定地址的内存保护属性
  // 用于在修改PLT表项前检查内存保护
  int GetMemoryProtection(void *addr) const;

  // 设置错误信息的辅助函数
  // 支持格式化字符串，类似printf
  static void SetError(const char *fmt, ...);

  // 在动态段表中查找指定类型的表项
  // 用于查找符号表、字符串表等关键表的位置
  static const Elf64_Dyn *FindDynamicEntry(const Elf64_Dyn *dyn,
                                           Elf64_Sxword tag);
};

// 初始化静态成员变量
size_t PLTHook::Impl::page_size_ = 0;
std::string PLTHook::Impl::error_message_;

// Impl构造函数实现
PLTHook::Impl::Impl(struct link_map *lmap) {
  // 首次使用时初始化页大小
  if (page_size_ == 0) {
    page_size_ = sysconf(_SC_PAGESIZE); // 获取系统页大小
  }
  // 初始化PLT hook所需的表和地址信息
  InitializeFromLinkMap(lmap);
  // 加载内存保护信息
  LoadMemoryProtections();
}

// 从link_map初始化所需的表和地址信息
void PLTHook::Impl::InitializeFromLinkMap(struct link_map *lmap) {
  // 设置基地址，这是共享库加载到内存中的基地址
  plt_addr_base_ = reinterpret_cast<void *>(lmap->l_addr);

  // 获取动态段表中的符号表
  const auto *dyn = FindDynamicEntry(lmap->l_ld, DT_SYMTAB);
  if (!dyn) {
    throw std::runtime_error("Failed to find DT_SYMTAB");
  }
  dynsym_ = reinterpret_cast<const Elf64_Sym *>(dyn->d_un.d_ptr);

  // 获取动态段表中的字符串表
  dyn = FindDynamicEntry(lmap->l_ld, DT_STRTAB);
  if (!dyn) {
    throw std::runtime_error("Failed to find DT_STRTAB");
  }
  dynstr_ = reinterpret_cast<const char *>(dyn->d_un.d_ptr);

  // 获取动态段表中的字符串表大小
  dyn = FindDynamicEntry(lmap->l_ld, DT_STRSZ);
  if (!dyn) {
    throw std::runtime_error("Failed to find DT_STRSZ");
  }
  dynstr_size_ = dyn->d_un.d_val;

  // 获取动态段表中的重定位表（PLT专用）
  dyn = FindDynamicEntry(lmap->l_ld, DT_JMPREL);
  if (!dyn) {
    throw std::runtime_error("Failed to find DT_JMPREL");
  }
  rela_plt_ = reinterpret_cast<const Elf64_Rela *>(dyn->d_un.d_ptr);

  // 获取动态段表中的重定位表大小
  dyn = FindDynamicEntry(lmap->l_ld, DT_PLTRELSZ);
  if (!dyn) {
    throw std::runtime_error("Failed to find DT_PLTRELSZ");
  }
  // 计算重定位表中的条目数量
  rela_plt_cnt_ = dyn->d_un.d_val / sizeof(Elf64_Rela);
}

// 加载内存保护信息
void PLTHook::Impl::LoadMemoryProtections() {
  // 打开/proc/self/maps文件，获取进程内存映射信息
  FILE *fp = fopen("/proc/self/maps", "r");
  if (!fp) {
    throw std::system_error(errno, std::system_category(),
                            "Failed to open /proc/self/maps");
  }

  char buf[PATH_MAX];
  // 逐行读取内存映射信息
  while (fgets(buf, PATH_MAX, fp)) {
    unsigned long start, end;
    char perms[5];
    // 解析内存区域的起始地址、结束地址和权限
    if (sscanf(buf, "%lx-%lx %4s", &start, &end, perms) != 3) {
      continue;
    }

    // 解析权限字符串为保护标志
    int prot = 0;
    if (perms[0] == 'r') {
      prot |= PROT_READ;
    }
    if (perms[1] == 'w') {
      prot |= PROT_WRITE;
    }
    if (perms[2] == 'x') {
      prot |= PROT_EXEC;
    }

    // 保存内存区域的保护信息
    memory_protections_.push_back({start, end, prot});
  }
  fclose(fp);
}

// 获取指定地址的内存保护属性
int PLTHook::Impl::GetMemoryProtection(void *addr) const {
  auto addr_val = reinterpret_cast<size_t>(addr);
  // 遍历内存保护信息，查找包含指定地址的区域
  for (const auto &prot : memory_protections_) {
    if (prot.start <= addr_val && addr_val < prot.end) {
      return prot.protection;
    }
  }
  // 未找到对应的内存区域
  return 0;
}

// 设置错误信息
void PLTHook::Impl::SetError(const char *fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
  va_end(ap);
  error_message_ = buf;
}

// 在动态段表中查找指定类型的表项
const Elf64_Dyn *PLTHook::Impl::FindDynamicEntry(const Elf64_Dyn *dyn,
                                                 Elf64_Sxword tag) {
  // 遍历动态段表，直到找到指定类型的表项或到达表尾
  while (dyn->d_tag != DT_NULL) {
    if (dyn->d_tag == tag) {
      return dyn;
    }
    dyn++;
  }
  // 未找到指定类型的表项
  return nullptr;
}

// PLTHook实现 - 创建PLTHook实例
std::unique_ptr<PLTHook> PLTHook::Create(const char *filename) {
  // 如果filename为NULL，则获取主程序的link_map
  if (!filename) {
    printf("Creating PLTHook for main executable\n");

    // 获取主程序的link_map
    struct link_map *lmap = nullptr;
    void *handle = dlopen(nullptr, RTLD_LAZY);
    if (!handle) {
      printf("dlopen error: %s\n", dlerror());
      Impl::SetError("dlopen error: %s", dlerror());
      throw std::runtime_error(Impl::error_message_);
    }

    // 使用dlinfo获取link_map信息
    if (dlinfo(handle, RTLD_DI_LINKMAP, &lmap) != 0) {
      dlclose(handle);
      Impl::SetError("dlinfo error");
      throw std::runtime_error(Impl::error_message_);
    }
    dlclose(handle);

    // 找到link_map链表的第一个元素（主程序）
    while (lmap->l_prev != nullptr) {
      lmap = lmap->l_prev;
    }

    // 创建PLTHook实例
    return std::unique_ptr<PLTHook>(new PLTHook(lmap));
  }

  // 处理指定的动态库
  printf("Creating PLTHook for %s\n", filename);
  // 打开动态库，但不加载新的副本（RTLD_NOLOAD）
  void *handle = dlopen(filename, RTLD_LAZY | RTLD_NOLOAD);
  if (!handle) {
    printf("dlopen error: %s\n", dlerror());
    Impl::SetError("dlopen error: %s", dlerror());
    throw std::runtime_error(Impl::error_message_);
  }

  // 获取动态链接库的link_map信息
  struct link_map *lmap = nullptr;
  if (dlinfo(handle, RTLD_DI_LINKMAP, &lmap) != 0) {
    dlclose(handle);
    Impl::SetError("dlinfo error");
    throw std::runtime_error(Impl::error_message_);
  }
  dlclose(handle);

  // 创建PLTHook实例，并返回智能指针
  return std::unique_ptr<PLTHook>(new PLTHook(lmap));
}

// PLTHook构造函数
PLTHook::PLTHook(struct link_map *lmap)
    : pimpl_(std::make_unique<Impl>(lmap)) {}

// PLTHook析构函数（使用默认实现）
PLTHook::~PLTHook() = default;

// 枚举符号表中的所有符号
int PLTHook::EnumerateSymbols(unsigned int &pos, const char *&name_out,
                              void **&addr_out) const {
  // 遍历重定位表
  while (pos < pimpl_->rela_plt_cnt_) {
    const auto *plt = &pimpl_->rela_plt_[pos];
    // 检查是否为JUMP_SLOT类型的重定位（函数调用）
    if (ELF64_R_TYPE(plt->r_info) == R_JUMP_SLOT) {
      // 获取符号索引
      size_t idx = ELF64_R_SYM(plt->r_info);
      // 获取符号名称
      name_out = pimpl_->dynstr_ + pimpl_->dynsym_[idx].st_name;
      // 获取符号地址（PLT表项地址）
      addr_out = reinterpret_cast<void **>(
          reinterpret_cast<char *>(pimpl_->plt_addr_base_) + plt->r_offset);
      pos++;
      return SUCCESS;
    }
    pos++;
  }
  // 遍历结束
  name_out = nullptr;
  addr_out = nullptr;
  return EOF_REACHED;
}

// 替换函数实现
int PLTHook::ReplaceFunction(const char *funcname, void *newfunc,
                             void **oldfunc) {
  // 先获取原始函数地址，这会强制解析符号
  void *original = dlsym(RTLD_DEFAULT, funcname);
  if (!original) {
    pimpl_->SetError("No such function: %s", funcname);
    return FUNCTION_NOT_FOUND;
  }

  unsigned int pos = 0;
  const char *name;
  void **addr;

  // 遍历所有符号，查找目标函数
  while (EnumerateSymbols(pos, name, addr) == SUCCESS) {
    // 检查函数名是否匹配（考虑版本后缀，如"func@GLIBC_2.2.5"）
    if (strncmp(name, funcname, strlen(funcname)) == 0 &&
        (name[strlen(funcname)] == '\0' || name[strlen(funcname)] == '@')) {
      // 获取内存页的保护属性
      int prot = pimpl_->GetMemoryProtection(addr);
      // 计算包含该地址的页的起始地址
      void *page_addr = reinterpret_cast<void *>(
          reinterpret_cast<size_t>(addr) & ~(pimpl_->page_size_ - 1));

      if (prot == 0) {
        pimpl_->SetError("Could not get memory protection at %p", page_addr);
        return INTERNAL_ERROR;
      }

      // 如果内存页不可写，则临时修改为可写
      if (!(prot & PROT_WRITE)) {
        if (mprotect(page_addr, pimpl_->page_size_, prot | PROT_WRITE) != 0) {
          pimpl_->SetError("Could not change memory protection at %p: %s",
                           page_addr, strerror(errno));
          return INTERNAL_ERROR;
        }
      }

      // 保存原始函数地址并替换为新函数
      if (oldfunc) {
        *oldfunc = original; // 使用已解析的地址
      }
      // 修改GOT表项，指向新函数
      *addr = newfunc;

      // 恢复内存页的保护属性
      if (!(prot & PROT_WRITE)) {
        if (mprotect(page_addr, pimpl_->page_size_, prot) != 0) {
          pimpl_->SetError("Could not restore memory protection at %p: %s",
                           page_addr, strerror(errno));
          return INTERNAL_ERROR;
        }
      }
      return SUCCESS;
    }
  }

  // 未找到目标函数
  pimpl_->SetError("No such function: %s", funcname);
  return FUNCTION_NOT_FOUND;
}

// 获取最后一次错误信息
const std::string &PLTHook::GetLastError() { return Impl::error_message_; }
