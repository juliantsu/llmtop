// AMD GPU telemetry via the ROCm SMI C API (librocm_smi64.so), loaded at
// runtime with dlopen() so the binary runs (and degrades gracefully) on
// machines without a ROCm driver.
//
// Mirrors NvmlSource: same data model, same public interface.

#include "sources/rocmm_source.hpp"

#include <dlfcn.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace llmtop {

namespace {

// rsmi_status_t values (rocm_smi.h)
constexpr int kRsmiSuccess = 0;

// rsmi_memory_type_t
constexpr std::uint32_t kRsmiMemTypeVram = 0;

// rsmi_temperature_type_t / rsmi_temperature_metric_t
constexpr std::uint32_t kRsmiTempTypeEdge = 0;
constexpr std::uint32_t kRsmiTempCurrent = 0;

// rsmi_clk_type_t (rocm_smi.h): SYS=0, DF=1, DCEF=2, SOC=3, MEM=4, PCIE=5
constexpr std::uint32_t kRsmiClkTypeSys = 0;
constexpr std::uint32_t kRsmiClkTypeMem = 4;

template <typename Fn>
bool resolve(void* lib, const char* name, Fn& fn) {
  fn = reinterpret_cast<Fn>(dlsym(lib, name));
  return fn != nullptr;
}

}  // namespace

RocmSource::~RocmSource() {
  if (initialized_)
    rsmi_shutdown_();
  if (lib_)
    dlclose(lib_);
}

void RocmSource::set_status(const std::string& message) {
  std::scoped_lock lock(state_.mutex);
  state_.gpu.available = false;
  state_.gpu.status = message;
}

bool RocmSource::load() {
  const char* candidates[] = {
      "librocm_smi64.so", "librocm_smi64.so.1", "librcsmi.so",
      "/opt/rocm/lib/librocm_smi64.so", "/opt/rocm/lib/librocm_smi64.so.1"};
  for (const char* name : candidates) {
    lib_ = dlopen(name, RTLD_LAZY | RTLD_LOCAL);
    if (lib_)
      break;
  }
  if (!lib_) {
    set_status("ROCm not available — librocm_smi64 not found");
    return false;
  }

  bool ok = true;
  ok = resolve(lib_, "rsmi_init", rsmi_init_);
  ok &= resolve(lib_, "rsmi_shut_down", rsmi_shutdown_);
  ok &= resolve(lib_, "rsmi_num_monitor_devices", rsmi_num_monitor_devices_);
  ok &= resolve(lib_, "rsmi_dev_name_get", rsmi_dev_name_get_);
  ok &= resolve(lib_, "rsmi_dev_memory_usage_get", rsmi_dev_mem_usage_get_);
  ok &= resolve(lib_, "rsmi_dev_memory_total_get", rsmi_dev_mem_total_get_);
  ok &= resolve(lib_, "rsmi_dev_temp_metric_get", rsmi_dev_temp_metric_get_);
  ok &= resolve(lib_, "rsmi_dev_power_ave_get", rsmi_dev_power_ave_get_);
  ok &= resolve(lib_, "rsmi_dev_power_cap_get", rsmi_dev_power_cap_get_);
  ok &= resolve(lib_, "rsmi_dev_busy_percent_get",
                rsmi_dev_busy_percent_get_);
  ok &= resolve(lib_, "rsmi_dev_gpu_clk_freq_get",
                rsmi_dev_gpu_clk_freq_get_);
  if (!ok) {
    dlclose(lib_);
    lib_ = nullptr;
    set_status("ROCm found but required rsmi symbols are missing");
    return false;
  }

  if (rsmi_init_(0) != kRsmiSuccess) {
    dlclose(lib_);
    lib_ = nullptr;
    set_status("rsmi_init failed (no AMD GPU?)");
    return false;
  }
  initialized_ = true;

  if (rsmi_num_monitor_devices_(&device_count_) != kRsmiSuccess ||
      device_count_ == 0) {
    set_status("ROCm initialized but no AMD GPU detected");
    return false;
  }

  for (std::uint32_t i = 0; i < device_count_; ++i) {
    char name[128] = {};
    if (rsmi_dev_name_get_(i, name, sizeof(name)) == kRsmiSuccess && name[0])
      device_names_.emplace_back(name);
    else
      device_names_.emplace_back("AMD GPU");
  }
  if (device_names_.empty()) {
    set_status("ROCm initialized but no device could be named");
    return false;
  }

  std::scoped_lock lock(state_.mutex);
  state_.gpu.available = true;
  state_.gpu.status.clear();
  state_.gpu.devices.clear();
  for (auto& name : device_names_) {
    GpuDevice d;
    d.name = std::move(name);
    state_.gpu.devices.push_back(std::move(d));
  }
  return true;
}

void RocmSource::poll() {
  if (!attempted_) {
    attempted_ = true;
    load();
  }
  if (!initialized_ || device_count_ == 0)
    return;

  struct Reading {
    std::uint64_t mem_used = 0, mem_total = 0;
    std::uint32_t busy = 0;
    std::int64_t temp = 0;
    std::uint64_t power_uw = 0, cap_uw = 0;
    int sm_clock = 0, mem_clock = 0;
  };
  std::vector<Reading> readings(device_count_);

  for (std::uint32_t i = 0; i < device_count_; ++i) {
    Reading& r = readings[i];
    rsmi_dev_mem_usage_get_(i, kRsmiMemTypeVram, &r.mem_used);
    rsmi_dev_mem_total_get_(i, kRsmiMemTypeVram, &r.mem_total);
    rsmi_dev_busy_percent_get_(i, &r.busy);
    rsmi_dev_temp_metric_get_(i, kRsmiTempTypeEdge, kRsmiTempCurrent,
                              &r.temp);
    rsmi_dev_power_ave_get_(i, 0, &r.power_uw);
    rsmi_dev_power_cap_get_(i, 0, &r.cap_uw);
    Freq f{};
    if (rsmi_dev_gpu_clk_freq_get_(i, kRsmiClkTypeSys, &f) == kRsmiSuccess &&
        f.current < f.num_supported)
      r.sm_clock = static_cast<int>(f.frequency[f.current] / 1000000);
    Freq mf{};
    if (rsmi_dev_gpu_clk_freq_get_(i, kRsmiClkTypeMem, &mf) == kRsmiSuccess &&
        mf.current < mf.num_supported)
      r.mem_clock = static_cast<int>(mf.frequency[mf.current] / 1000000);
  }

  std::scoped_lock lock(state_.mutex);
  if (state_.gpu.devices.empty())
    return;
  for (std::size_t i = 0;
       i < state_.gpu.devices.size() && i < readings.size(); ++i) {
    GpuDevice& d = state_.gpu.devices[i];
    const Reading& r = readings[i];
    d.mem_used = r.mem_used;
    d.mem_total = r.mem_total;
    d.util_pct = static_cast<int>(r.busy);
    d.temp_c = static_cast<int>(r.temp / 1000);  // rsmi reports milli-degrees
    d.power_w = static_cast<double>(r.power_uw) / 1e6;
    d.power_limit_w = static_cast<double>(r.cap_uw) / 1e6;
    d.sm_clock_mhz = r.sm_clock;
    d.mem_clock_mhz = r.mem_clock;
    d.util_history.push(static_cast<float>(d.util_pct));
    d.vram_history.push(d.mem_total
                            ? 100.0f * static_cast<float>(d.mem_used) /
                                  static_cast<float>(d.mem_total)
                            : 0.0f);
  }
}

}  // namespace llmtop
