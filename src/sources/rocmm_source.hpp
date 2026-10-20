// AMD GPU telemetry via the ROCm SMI C API (librocm_smi64.so), loaded at
// runtime with dlopen() so the binary runs (and degrades gracefully) on
// machines without a ROCm driver.
//
// Mirrors NvmlSource: same data model, same public interface.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "sources/source.hpp"

namespace llmtop {

class RocmSource final : public Source {
 public:
  explicit RocmSource(AppState& state) : Source(state) {}
  ~RocmSource() override;

  void poll() override;

 private:
  bool load();
  void set_status(const std::string& msg);

  // rsmi_frequencies_t layout (mirrors rocm_smi.h; not linked against it).
  struct Freq {
    bool has_deep_sleep;
    std::uint32_t num_supported;
    std::uint32_t current;
    std::uint64_t frequency[33];  // RSMI_MAX_NUM_FREQUENCIES
  };

  bool initialized_ = false;
  bool attempted_ = false;
  void* lib_ = nullptr;

  // Function pointers resolved from librocm_smi64.so via dlsym.
  int (*rsmi_init_)(std::uint64_t) = nullptr;
  void (*rsmi_shutdown_)() = nullptr;
  int (*rsmi_num_monitor_devices_)(std::uint32_t*) = nullptr;
  int (*rsmi_dev_name_get_)(std::uint32_t, char*, std::size_t) = nullptr;
  int (*rsmi_dev_mem_usage_get_)(std::uint32_t, std::uint32_t, std::uint64_t*) =
      nullptr;
  int (*rsmi_dev_mem_total_get_)(std::uint32_t, std::uint32_t, std::uint64_t*) =
      nullptr;
  int (*rsmi_dev_temp_metric_get_)(std::uint32_t, std::uint32_t, std::uint32_t,
                                   std::int64_t*) = nullptr;
  int (*rsmi_dev_power_ave_get_)(std::uint32_t, std::uint32_t, std::uint64_t*) =
      nullptr;
  int (*rsmi_dev_power_cap_get_)(std::uint32_t, std::uint32_t, std::uint64_t*) =
      nullptr;
  int (*rsmi_dev_busy_percent_get_)(std::uint32_t, std::uint32_t*) = nullptr;
  int (*rsmi_dev_gpu_clk_freq_get_)(std::uint32_t, std::uint32_t, Freq*) =
      nullptr;

  std::uint32_t device_count_ = 0;
  std::vector<std::string> device_names_;
};

}  // namespace llmtop
