#pragma once

#include <vector>

#include "sources/source.hpp"

namespace llmtop {

// GPU telemetry via NVML's C API, loaded at runtime with dlopen() so the
// binary runs (and degrades gracefully) on machines without an NVIDIA driver.
class NvmlSource final : public Source {
 public:
  explicit NvmlSource(AppState& state) : Source(state) {}
  ~NvmlSource() override;

  void poll() override;

 private:
  struct Memory {
    unsigned long long total = 0, free = 0, used = 0;
  };
  struct Utilization {
    unsigned int gpu = 0, memory = 0;
  };

  bool load();
  void set_status(const std::string& message);

  void* lib_ = nullptr;
  bool attempted_ = false;
  bool initialized_ = false;
  std::vector<void*> devices_;

  // NVML entry points (resolved via dlsym)
  int (*init_)() = nullptr;
  int (*shutdown_)() = nullptr;
  int (*driver_version_)(char*, unsigned) = nullptr;
  int (*device_count_)(unsigned*) = nullptr;
  int (*device_handle_)(unsigned, void**) = nullptr;
  int (*device_name_)(void*, char*, unsigned) = nullptr;
  int (*memory_info_)(void*, Memory*) = nullptr;
  int (*utilization_)(void*, Utilization*) = nullptr;
  int (*temperature_)(void*, unsigned, unsigned*) = nullptr;
  int (*power_usage_)(void*, unsigned*) = nullptr;
  int (*power_limit_)(void*, unsigned*) = nullptr;
  int (*clock_info_)(void*, unsigned, unsigned*) = nullptr;
};

}  // namespace llmtop
