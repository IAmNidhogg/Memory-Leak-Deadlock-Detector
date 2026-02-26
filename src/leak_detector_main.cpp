#include <chrono>
#include <dlfcn.h>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>

void *lib_handle = nullptr;

typedef void (*Detector_Register)(const char *lib_name);
typedef void (*Detector_Start)();
typedef void (*Detector_Detect)(void);
typedef void (*Detector_RegisterMain)(void);
typedef void (*Detector_Init)(const char *, int, int);

Detector_Register DetectorRegister = nullptr;
Detector_Start DetectorStart = nullptr;
Detector_Detect DetectorDetect = nullptr;
Detector_RegisterMain DetectorRegisterMain = nullptr;
Detector_Init DetectorInit = nullptr;

void OpenDetector() {
  lib_handle = dlopen("/mnt/d/project/camping/detector/detector.so", RTLD_LAZY);
  if (!lib_handle) {
    std::cerr << "Failed to load library: " << dlerror() << std::endl;
    return;
  }
  DetectorRegister = (Detector_Register)dlsym(lib_handle, "Detector_Register");
  DetectorStart = (Detector_Start)dlsym(lib_handle, "Detector_Start");
  DetectorDetect = (Detector_Detect)dlsym(lib_handle, "Detector_Detect");
  DetectorRegisterMain =
      (Detector_RegisterMain)dlsym(lib_handle, "Detector_RegisterMain");
  DetectorInit = (Detector_Init)dlsym(lib_handle, "Detector_Init");
}

void CloseDetector() { dlclose(lib_handle); }

void RegisterDetector(const char *lib_name) { DetectorRegister(lib_name); }

void RegisterMain() { DetectorRegisterMain(); }

void Start() { DetectorStart(); }

void Detect() { DetectorDetect(); }

void Init(const char *work_dir, int option, int output_option) {
  DetectorInit(work_dir, option, output_option);
}

void UseMemory() { int *p = new int(10); }

void UseMemory2() {
  int *p = new int(10);
  delete p;
}

pthread_mutex_t mutex;
void UseLock() {
  pthread_mutex_init(&mutex, nullptr);
  pthread_mutex_lock(&mutex);
  pthread_mutex_unlock(&mutex);
  pthread_mutex_destroy(&mutex);
}

std::mutex mutex1;
std::mutex mutex2;
void Lock1() {
  std::thread([]() {
    std::lock_guard<std::mutex> lock1(mutex1);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::lock_guard<std::mutex> lock2(mutex2);
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }).detach();
}

void Lock2() {
  std::thread([]() {
    std::lock_guard<std::mutex> lock2(mutex2);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::lock_guard<std::mutex> lock1(mutex1);
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }).detach();
}

void UseDeadLock() {
  Lock1();
  Lock2();
}

void dead_lock_thread() {
  std::thread([]() {
    std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::lock_guard<std::mutex> lock2(mutex);
  }).detach();
}

int main() {
  OpenDetector();
  Init("./", 3, 3);
  RegisterMain();
  Start();
  UseMemory();
  UseMemory2();
  UseLock();
  std::cout << "UseDeadLock" << std::endl;
  UseDeadLock();
  std::cout << "UseDeadLock end" << std::endl;
  dead_lock_thread();
  std::this_thread::sleep_for(std::chrono::seconds(10));
  std::cout << "Detect" << std::endl;
  Detect();
  std::cout << "Detect end" << std::endl;
  CloseDetector();
  return 0;
}

// clang - shared - fPIC detector.cc memory_detect.cc plthook_elf64.cc - o
// detector.so
