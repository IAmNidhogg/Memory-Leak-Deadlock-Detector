#include "lock_detect.hpp"

#include <algorithm>
#include <cstdio>
#include <dlfcn.h>
#include <execinfo.h>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "output_control.hpp"
#include "plthook.hpp"

namespace tracker {

/**
 * @struct LockInfo
 * @brief 锁的详细信息结构体
 *
 * 存储锁的地址、所有者线程、调用栈和依赖关系等信息，
 * 用于死锁检测和问题诊断
 */
struct LockInfo {
  void *lock_addr = nullptr;              // 锁的内存地址
  pthread_t owner_thread = 0;             // 持有锁的线程ID
  std::vector<void *> callstack;          // 获取锁时的调用栈
  std::unordered_set<void *> waiting_for; // 该锁在等待的其他锁
  bool acquired = false;                  // 是否已获得锁
};

/**
 * @struct ThreadInfo
 * @brief 线程的锁信息结构体
 *
 * 记录线程持有和等待的锁，用于构建锁依赖图和死锁检测
 */
struct ThreadInfo {
  std::vector<void *> held_locks;    // 线程持有的锁
  std::vector<void *> waiting_locks; // 线程正在等待的锁
};

/**
 * @class LockTracker
 * @brief 锁跟踪器类，实现死锁检测的核心功能
 *
 * 该类是一个全局单例，负责记录所有锁的获取和释放操作，
 * 构建锁依赖图，并通过图分析检测潜在的死锁
 */
class LockTracker {
public:
  /**
   * @brief 获取LockTracker单例实例
   * @return LockTracker& 单例实例的引用
   *
   * 使用静态局部变量确保线程安全的单例初始化
   */
  static LockTracker &GetInstance() {
    static LockTracker instance;
    return instance;
  }

  /**
   * @brief 禁用拷贝构造函数
   *
   * 确保单例模式的唯一性
   */
  LockTracker(const LockTracker &) = delete;

  /**
   * @brief 禁用赋值操作符
   *
   * 确保单例模式的唯一性
   */
  LockTracker &operator=(const LockTracker &) = delete;

  /**
   * @brief 记录线程尝试获取互斥锁的行为
   * @param mutex 互斥锁指针
   *
   * 这个函数在实际获取锁之前调用，用于死锁检测
   * 记录线程等待关系，并检查是否形成等待环路
   */
  void RecordLockAcquire(pthread_mutex_t *mutex) {
    if (!mutex) {
      return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    void *lock_addr = static_cast<void *>(mutex);
    pthread_t thread_id = pthread_self();

    // 先检查锁的状态
    auto it = active_locks_.find(lock_addr);
    if (it != active_locks_.end()) {
      // 如果锁已经存在
      if (it->second.acquired) {
        // 记录等待关系
        auto &waiting_thread_info = thread_info_[thread_id];
        waiting_thread_info.waiting_locks.push_back(lock_addr);

        // 对于当前线程已经持有的所有锁，记录它们正在等待这个新的锁
        for (void *held_lock : waiting_thread_info.held_locks) {
          auto &held_lock_info = active_locks_[held_lock];
          held_lock_info.waiting_for.insert(lock_addr);
        }

        DetectDeadlock(lock_addr, thread_id);
      }
    } else {
      // 如果是新的锁，创建新的信息
      LockInfo &info = active_locks_[lock_addr];
      info.lock_addr = lock_addr;
      info.acquired = false;
      GetCallStack(info.callstack);
    }
  }

  /**
   * @brief 记录线程成功获取互斥锁
   * @param mutex 互斥锁指针
   *
   * 更新锁的所有者和状态，记录线程持有的锁
   */
  void RecordLockAcquired(pthread_mutex_t *mutex) {
    if (!mutex) {
      return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    void *lock_addr = static_cast<void *>(mutex);
    pthread_t thread_id = pthread_self();

    auto it = active_locks_.find(lock_addr);
    if (it != active_locks_.end()) {
      it->second.owner_thread = thread_id;
      it->second.acquired = true;

      // 更新线程信息
      auto &thread_locks = thread_info_[thread_id];
      thread_locks.held_locks.push_back(lock_addr);

      // 获得锁后，从等待列表中移除
      auto &waiting_locks = thread_locks.waiting_locks;
      waiting_locks.erase(
          std::remove(waiting_locks.begin(), waiting_locks.end(), lock_addr),
          waiting_locks.end());
    }
  }

  /**
   * @brief 记录线程释放互斥锁
   * @param mutex 互斥锁指针
   *
   * 更新锁的状态和线程持有的锁列表，清理不再需要的记录
   */
  void RecordLockRelease(pthread_mutex_t *mutex) {
    if (!mutex) {
      return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    void *lock_addr = static_cast<void *>(mutex);
    pthread_t thread_id = pthread_self();

    // 从活跃锁列表中移除
    active_locks_.erase(lock_addr);

    // 更新线程信息
    auto it = thread_info_.find(thread_id);
    if (it != thread_info_.end()) {
      auto &held_locks = it->second.held_locks;
      held_locks.erase(
          std::remove(held_locks.begin(), held_locks.end(), lock_addr),
          held_locks.end());

      // 如果线程不再持有或等待任何锁，从线程信息中移除
      if (held_locks.empty() && it->second.waiting_locks.empty()) {
        thread_info_.erase(it);
      }
    }
  }

  /**
   * @brief 打印当前锁的状态信息
   *
   * 输出所有活跃锁的详细信息，包括持有者、调用栈和等待关系
   */
  void PrintStatus() const;

private:
  /**
   * @brief 私有构造函数
   *
   * 实现单例模式
   */
  LockTracker() = default;

  /**
   * @brief 获取当前调用栈
   * @param callstack 用于存储调用栈的向量
   *
   * 使用backtrace函数获取当前线程的调用栈
   */
  void GetCallStack(std::vector<void *> &callstack) {
    void *stack[16];
    int size = backtrace(stack, 16);
    callstack.assign(stack, stack + size);
  }

  /**
   * @brief 检测是否存在死锁
   * @param lock_addr 当前线程正在尝试获取的锁的地址
   * @param thread_id 当前线程ID
   * @return bool 如果检测到死锁返回true，否则返回false
   *
   * 通过分析锁的依赖关系，寻找等待环路来检测死锁
   */
  bool DetectDeadlock(void *lock_addr, pthread_t thread_id);

  /**
   * @brief 使用深度优先搜索检测死锁
   * @param current_lock 当前检查的锁
   * @param current_thread 当前锁的持有线程
   * @param visited_threads 已访问的线程集合
   * @param lock_chain 锁依赖链
   * @return bool 如果检测到死锁返回true，否则返回false
   *
   * 递归检查锁的依赖关系，寻找形成环路的依赖链
   */
  bool DetectDeadlockDFS(void *current_lock, pthread_t current_thread,
                         std::unordered_set<pthread_t> &visited_threads,
                         std::vector<std::pair<void *, pthread_t>> &lock_chain);

  /**
   * @brief 打印锁的详细信息
   * @param info 锁信息结构体
   *
   * 输出锁的地址、持有者、调用栈和等待关系
   */
  void PrintLockInfo(const LockInfo &info) const;

  /**
   * @brief 打印调用栈
   * @param file 输出文件
   * @param callstack 调用栈地址数组
   *
   * 将调用栈地址转换为符号名称并输出
   */
  void PrintCallStack(FILE *file, const std::vector<void *> &callstack) const;

  mutable std::mutex mutex_; // 保护内部数据结构的互斥锁
  std::unordered_map<void *, LockInfo> active_locks_;     // 活跃锁信息映射表
  std::unordered_map<pthread_t, ThreadInfo> thread_info_; // 线程锁信息映射表
};

/**
 * @brief 简化访问LockTracker单例的辅助函数
 * @return LockTracker& LockTracker单例的引用
 */
static LockTracker &Instance() { return LockTracker::GetInstance(); }

/**
 * @brief 检测是否存在死锁
 * @param lock_addr 当前线程正在尝试获取的锁的地址
 * @param thread_id 当前线程ID
 * @return bool 如果检测到死锁返回true，否则返回false
 *
 * 通过分析锁的依赖关系，寻找等待环路来检测死锁
 */
bool LockTracker::DetectDeadlock(void *lock_addr, pthread_t thread_id) {
  // 记录已访问过的线程，用于检测环路
  std::unordered_set<pthread_t> visited_threads;
  // 记录锁的依赖链，用于打印死锁信息
  std::vector<std::pair<void *, pthread_t>> lock_chain;

  // 使用深度优先搜索检查所有可能的等待路径
  bool found_deadlock =
      DetectDeadlockDFS(lock_addr, thread_id, visited_threads, lock_chain);

  if (found_deadlock) {
    // 打印死锁信息
    TRACKER_PRINT("\n=== Potential Deadlock Detected! ===\n");
    TRACKER_PRINT("Lock chain:\n");
    for (const auto &pair : lock_chain) {
      const auto &info = active_locks_[pair.first];
      PrintLockInfo(info);
      TRACKER_PRINT("\n");
    }
  }

  return found_deadlock;
}

/**
 * @brief 使用深度优先搜索检测死锁
 * @param current_lock 当前检查的锁
 * @param current_thread 当前锁的持有线程
 * @param visited_threads 已访问的线程集合
 * @param lock_chain 锁依赖链
 * @return bool 如果检测到死锁返回true，否则返回false
 *
 * 递归检查锁的依赖关系，寻找形成环路的依赖链
 */
bool LockTracker::DetectDeadlockDFS(
    void *current_lock, pthread_t current_thread,
    std::unordered_set<pthread_t> &visited_threads,
    std::vector<std::pair<void *, pthread_t>> &lock_chain) {
  // 如果当前线程已经被访问过，说明形成了环路，即检测到死锁
  if (visited_threads.count(current_thread)) {
    return true;
  }

  // 记录当前线程已被访问
  visited_threads.insert(current_thread);
  // 将当前锁和持有它的线程添加到锁链中
  lock_chain.emplace_back(current_lock, current_thread);

  // 获取当前锁的信息
  const auto &info = active_locks_[current_lock];

  // 对于当前锁等待的每一个锁
  for (void *waited_lock : info.waiting_for) {
    auto waited_lock_it = active_locks_.find(waited_lock);
    if (waited_lock_it == active_locks_.end()) {
      continue; // 跳过已经不存在的锁
    }

    pthread_t owner_thread = waited_lock_it->second.owner_thread;

    // 递归检查这条路径
    if (DetectDeadlockDFS(waited_lock, owner_thread, visited_threads,
                          lock_chain)) {
      return true; // 在这条路径上发现死锁
    }
  }

  // 回溯：移除当前线程和锁
  visited_threads.erase(current_thread);
  lock_chain.pop_back();

  return false; // 这条路径上没有死锁
}

/**
 * @brief 打印锁的详细信息
 * @param info 锁信息结构体
 *
 * 输出锁的地址、持有者、调用栈和等待关系
 */
void LockTracker::PrintLockInfo(const LockInfo &info) const {
  TRACKER_PRINT("Lock %p (Mutex) held by thread %lu\n", info.lock_addr,
                info.owner_thread);
  TRACKER_PRINT("Acquired at:\n");

  char **symbols =
      backtrace_symbols(info.callstack.data(), info.callstack.size());
  if (symbols) {
    for (size_t i = 0; i < info.callstack.size(); ++i) {
      void *abs_addr = info.callstack[i];
      Dl_info dlinfo;
      if (dladdr(abs_addr, &dlinfo)) {
        void *rel_addr =
            (void *)((char *)abs_addr - (char *)dlinfo.dli_fbase);
        TRACKER_PRINT("  [%zu] Absolute: %p, Relative: %p\n", i, abs_addr,
                      rel_addr);
        TRACKER_PRINT("      Module: %s\n", dlinfo.dli_fname);
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "addr2line -e %s -f -C -p %p",
                 dlinfo.dli_fname, rel_addr);
        FILE *pipe = popen(cmd, "r");
        if (pipe) {
          char line[256];
          if (fgets(line, sizeof(line), pipe)) {
            TRACKER_PRINT("      Source: %s", line);
          }
          pclose(pipe);
        }
      } else {
        TRACKER_PRINT("  [%zu] %s\n", i, symbols[i]);
      }
    }
    free(symbols);
  }

  if (!info.waiting_for.empty()) {
    TRACKER_PRINT("Waiting for locks:");
    for (void *waited_lock : info.waiting_for) {
      auto it = active_locks_.find(waited_lock);
      if (it != active_locks_.end()) {
        TRACKER_PRINT(" %p (held by thread %lu)", waited_lock,
                      it->second.owner_thread);
      } else {
        TRACKER_PRINT(" %p (unknown)", waited_lock);
      }
    }
    TRACKER_PRINT("\n");
  }
}

/**
 * @brief 打印当前锁的状态信息
 *
 * 输出所有活跃锁的详细信息，包括持有者、调用栈和等待关系
 */
void LockTracker::PrintStatus() const {
  std::lock_guard<std::mutex> lock(mutex_);

  TRACKER_PRINT("\n=== Lock Detector Status ===\n");
  TRACKER_PRINT("Active locks: %zu\n", active_locks_.size());
  TRACKER_PRINT("Active threads: %zu\n", thread_info_.size());

  if (!active_locks_.empty()) {
    TRACKER_PRINT("\nDetailed lock information:\n");
    for (const auto &pair : active_locks_) {
      TRACKER_PRINT("\n");
      PrintLockInfo(pair.second);
    }
  }

  if (!thread_info_.empty()) {
    TRACKER_PRINT("\nThread Information:\n");
    for (const auto &pair : thread_info_) {
      TRACKER_PRINT("\nThread %lu:\n", pair.first);
      TRACKER_PRINT("  Held locks:");
      for (void *lock : pair.second.held_locks) {
        TRACKER_PRINT(" %p", lock);
      }
      TRACKER_PRINT("\n  Waiting for locks:");
      for (void *lock : pair.second.waiting_locks) {
        auto it = active_locks_.find(lock);
        if (it != active_locks_.end()) {
          TRACKER_PRINT(" %p (held by thread %lu)", lock,
                        it->second.owner_thread);
        } else {
          TRACKER_PRINT(" %p", lock);
        }
      }
      TRACKER_PRINT("\n");
    }
  }

  TRACKER_PRINT("\n===========================\n");
}

void LockTracker::PrintCallStack(FILE *file,
                                 const std::vector<void *> &callstack) const {
  char **symbols = backtrace_symbols(callstack.data(), callstack.size());
  if (symbols) {
    for (size_t i = 0; i < callstack.size(); ++i) {
      TRACKER_PRINT("  [%zu] %s\n", i, symbols[i]);
    }
    free(symbols);
  }
}

} // namespace tracker

// Hook函数实现
static int HookedPthreadMutexLock(pthread_mutex_t *mutex) {
  tracker::Instance().RecordLockAcquire(mutex);

  int result = pthread_mutex_lock(mutex);
  if (result == 0) {
    tracker::Instance().RecordLockAcquired(mutex);
  }
  return result;
}

static int HookedPthreadMutexUnlock(pthread_mutex_t *mutex) {
  tracker::Instance().RecordLockRelease(mutex);
  return pthread_mutex_unlock(mutex);
}

static int HookedPthreadMutexTrylock(pthread_mutex_t *mutex) {
  int result = pthread_mutex_trylock(mutex);
  if (result == 0) {
    tracker::Instance().RecordLockAcquired(mutex);
  }
  return result;
}

class LockHook {
public:
  explicit LockHook(std::string lib_path) : lib_path_(std::move(lib_path)) {}
  ~LockHook() = default;

  void Start();

private:
  std::string lib_path_;
  std::unique_ptr<PLTHook> hook_;
};

void LockHook::Start() {
  hook_ = PLTHook::Create(lib_path_.c_str());

  try {
    if (hook_->ReplaceFunction(
            "pthread_mutex_lock",
            reinterpret_cast<void *>(&HookedPthreadMutexLock),
            nullptr) != PLTHook::SUCCESS) {
      TRACKER_ERROR("Failed to hook pthread_mutex_lock");
    }

    if (hook_->ReplaceFunction(
            "pthread_mutex_unlock",
            reinterpret_cast<void *>(&HookedPthreadMutexUnlock),
            nullptr) != PLTHook::SUCCESS) {
      TRACKER_ERROR("Failed to hook pthread_mutex_unlock");
    }

    if (hook_->ReplaceFunction(
            "pthread_mutex_trylock",
            reinterpret_cast<void *>(&HookedPthreadMutexTrylock),
            nullptr) != PLTHook::SUCCESS) {
      TRACKER_WARNING("Note: pthread_mutex_trylock not found in PLT");
    }

  } catch (const std::exception &e) {
    TRACKER_ERROR("Error starting lock tracking: %s", e.what());
  }
}

class LockDetectImpl {
public:
  LockDetectImpl() = default;
  ~LockDetectImpl() = default;

  void Register(const std::string &lib_name);
  void RegisterMain();
  void Start();
  void Detect();

private:
  std::vector<std::unique_ptr<LockHook>> hooks_;
};

void LockDetectImpl::Register(const std::string &lib_name) {
  hooks_.emplace_back(std::make_unique<LockHook>(lib_name));
}

void LockDetectImpl::RegisterMain() {
  hooks_.emplace_back(std::make_unique<LockHook>(std::string()));
}

void LockDetectImpl::Start() {
  for (auto &hook : hooks_) {
    hook->Start();
  }
}

void LockDetectImpl::Detect() { tracker::Instance().PrintStatus(); }

LockDetect::LockDetect() : impl_(std::make_unique<LockDetectImpl>()) {}

LockDetect::~LockDetect() = default;

void LockDetect::Register(const std::string &lib_name) {
  impl_->Register(lib_name);
}

void LockDetect::RegisterMain() { impl_->RegisterMain(); }

void LockDetect::Start() { impl_->Start(); }

void LockDetect::Detect() { impl_->Detect(); }

