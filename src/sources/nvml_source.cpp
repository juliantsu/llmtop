#include "sources/nvml_source.hpp"

#include <dlfcn.h>

#include <mutex>
#include <string>
#include <vector>

namespace llmtop {

namespace {

constexpr int kNvmlSuccess = 0;
constexpr unsigned kNvmlTemperatureGpu = 0;  // NVML_TEMPERATURE_GPU
constexpr unsigned kNvmlClockSm = 1;         // NVML_CLOCK_SM
constexpr unsigned kNvmlClockMem = 2;        // NVML_CLOCK_MEM

template <typename T>
bool resolve(void* lib, const char* name, T& fn) {
  fn = reinterpret_cast<T>(dlsym(lib, name));
  return fn != nullptr;
}

}  // namespace

NvmlSource::~NvmlSource() {
  if (initialized_ && shutdown_)
    shutdown_();
  if (lib_)
    dlclose(lib_);
}

void NvmlSource::set_status(const std::string& message) {
  std::scoped_lock lock(state_.mutex);
  state_.gpu.available = false;
  state_.gpu.status = message;
}

bool NvmlSource::load() {
  for (const char* name : {"libnvidia-ml.so.1", "libnvidia-ml.so"}) {
    lib_ = dlopen(name, RTLD_LAZY | RTLD_LOCAL);
    if (lib_)
      break;
  }
  if (!lib_) {
    set_status("NVML not available — libnvidia-ml not found (no NVIDIA driver?)");
    return false;
  }

  bool ok = true;
  if (!resolve(lib_, "nvmlInit_v2", init_))
    ok = resolve(lib_, "nvmlInit", init_);
  ok = ok && resolve(lib_, "nvmlShutdown", shutdown_);
  ok = ok && resolve(lib_, "nvmlSystemGetDriverVersion", driver_version_);
  if (!resolve(lib_, "nvmlDeviceGetCount_v2", device_count_))
    ok = ok && resolve(lib_, "nvmlDeviceGetCount", device_count_);
  if (!resolve(lib_, "nvmlDeviceGetHandleByIndex_v2", device_handle_))
    ok = ok && resolve(lib_, "nvmlDeviceGetHandleByIndex", device_handle_);
  ok = ok && resolve(lib_, "nvmlDeviceGetName", device_name_);
  ok = ok && resolve(lib_, "nvmlDeviceGetMemoryInfo", memory_info_);
  ok = ok && resolve(lib_, "nvmlDeviceGetUtilizationRates", utilization_);
  ok = ok && resolve(lib_, "nvmlDeviceGetTemperature", temperature_);
  ok = ok && resolve(lib_, "nvmlDeviceGetPowerUsage", power_usage_);
  // optional
  resolve(lib_, "nvmlDeviceGetEnforcedPowerLimit", power_limit_);
  resolve(lib_, "nvmlDeviceGetClockInfo", clock_info_);

  if (!ok) {
    set_status("NVML found but required symbols are missing");
    return false;
  }
  if (init_() != kNvmlSuccess) {
    set_status("nvmlInit failed (driver/library mismatch?)");
    return false;
  }
  initialized_ = true;

  char driver[96] = {};
  driver_version_(driver, sizeof(driver));

  unsigned count = 0;
  if (device_count_(&count) != kNvmlSuccess || count == 0) {
    set_status("NVML initialized but no NVIDIA GPU detected");
    std::scoped_lock lock(state_.mutex);
    state_.gpu.driver_version = driver;
    return false;
  }

  std::vector<std::string> names;
  for (unsigned i = 0; i < count; ++i) {
    void* handle = nullptr;
    if (device_handle_(i, &handle) != kNvmlSuccess)
      continue;
    devices_.push_back(handle);
    char name[96] = {};
    device_name_(handle, name, sizeof(name));
    names.emplace_back(name[0] ? name : "NVIDIA GPU");
  }
  if (devices_.empty()) {
    set_status("NVML initialized but no device handle could be acquired");
    return false;
  }

  std::scoped_lock lock(state_.mutex);
  state_.gpu.available = true;
  state_.gpu.status.clear();
  state_.gpu.driver_version = driver;
  state_.gpu.devices.clear();
  for (auto& name : names) {
    GpuDevice d;
    d.name = name;
    state_.gpu.devices.push_back(std::move(d));
  }
  return true;
}

void NvmlSource::poll() {
  if (!attempted_) {
    attempted_ = true;
    load();
  }
  if (!initialized_ || devices_.empty())
    return;

  struct Reading {
    Memory mem;
    Utilization util;
    unsigned temp = 0, power_mw = 0, limit_mw = 0, sm_clock = 0, mem_clock = 0;
  };
  std::vector<Reading> readings(devices_.size());

  for (std::size_t i = 0; i < devices_.size(); ++i) {
    void* dev = devices_[i];
    Reading& r = readings[i];
    memory_info_(dev, &r.mem);
    utilization_(dev, &r.util);
    temperature_(dev, kNvmlTemperatureGpu, &r.temp);
    power_usage_(dev, &r.power_mw);
    if (power_limit_)
      power_limit_(dev, &r.limit_mw);
    if (clock_info_) {
      clock_info_(dev, kNvmlClockSm, &r.sm_clock);
      clock_info_(dev, kNvmlClockMem, &r.mem_clock);
    }
  }

  std::scoped_lock lock(state_.mutex);
  for (std::size_t i = 0; i < devices_.size() && i < state_.gpu.devices.size(); ++i) {
    GpuDevice& d = state_.gpu.devices[i];
    const Reading& r = readings[i];
    d.mem_used = r.mem.used;
    d.mem_total = r.mem.total;
    d.util_pct = static_cast<int>(r.util.gpu);
    d.temp_c = static_cast<int>(r.temp);
    d.power_w = r.power_mw / 1000.0;
    d.power_limit_w = r.limit_mw / 1000.0;
    d.sm_clock_mhz = static_cast<int>(r.sm_clock);
    d.mem_clock_mhz = static_cast<int>(r.mem_clock);
    d.util_history.push(static_cast<float>(d.util_pct));
    d.vram_history.push(
        d.mem_total ? 100.0f * static_cast<float>(d.mem_used) /
                          static_cast<float>(d.mem_total)
                    : 0.0f);
  }
}

}  // namespace llmtop
