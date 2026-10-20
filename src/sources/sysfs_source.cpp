// AMD GPU telemetry via /sys/class/drm (kernel amdgpu driver, no ROCm).
//
// This is the fallback for machines where librocm_smi64 is unavailable:
// it reads VRAM usage/total, GPU busy %, and the edge temperature of each
// amdgpu card. No power or clock data is exposed by sysfs here, so those
// fields stay 0.
//
// Ownership model: if the GPU panel is still empty when we first load, we
// populate it; afterwards we only update devices whose name matches, so we
// never clobber data owned by NvmlSource/RocmSource.
//
// Layout (per card):
//   /sys/class/drm/cardN/device/mem_info_vram_used     (bytes)
//   /sys/class/drm/cardN/device/mem_info_vram_total    (bytes)
//   /sys/class/drm/cardN/device/gpu_busy_percent       (0..100)
//   /sys/class/drm/cardN/device/vendor, /device/device (PCI ids, 0x1002=AMD)
//   /sys/class/drm/cardN/device/hwmon/<hwmonM>/temp1_input  (milli-°C, edge)

#include "sources/sysfs_source.hpp"

#include <dirent.h>

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace llmtop {

namespace {

constexpr const char* kDrmBase = "/sys/class/drm/";

bool read_sysfs_uint64(const std::string& path, std::uint64_t& out) {
  std::ifstream f(path);
  if (!f.is_open())
    return false;
  std::string line;
  if (!std::getline(f, line))
    return false;
  try {
    out = std::stoull(line);
    return true;
  } catch (...) {
    return false;
  }
}

bool read_sysfs_int64(const std::string& path, std::int64_t& out) {
  std::uint64_t v = 0;
  if (!read_sysfs_uint64(path, v))
    return false;
  out = static_cast<std::int64_t>(v);
  return true;
}

std::string amdgpu_name(std::uint32_t pci_device) {
  switch (pci_device) {
    case 0x744c:
      return "AMD Radeon RX 7900 XTX";
    case 0x744a:
      return "AMD Radeon RX 7900 XT";
    case 0x7300:
      return "AMD Radeon RX 7900 GRE";
    case 0x164e:
      return "AMD Radeon Graphics (integrated)";
    default:
      break;
  }
  char buf[32];
  snprintf(buf, sizeof(buf), "AMD GPU (0x%04x)", pci_device);
  return buf;
}

// Returns the hwmon directory inside cardN/device/hwmon, or "".
std::string find_hwmon(const std::string& card_name) {
  std::string base = std::string(kDrmBase) + card_name + "/device/hwmon/";
  DIR* dir = opendir(base.c_str());
  if (!dir)
    return "";
  std::string result;
  struct dirent* ent = nullptr;
  while ((ent = readdir(dir)) != nullptr) {
    if (ent->d_name[0] != '\0' && ent->d_name[0] != '.') {
      result = base + ent->d_name;
      break;
    }
  }
  closedir(dir);
  return result;
}

}  // namespace

void SysfsSource::set_status(const std::string& message) {
  std::scoped_lock lock(state_.mutex);
  state_.gpu.available = false;
  state_.gpu.status = message;
}

bool SysfsSource::load() {
  struct Card {
    std::string name;
    std::string base;  // /sys/class/drm/cardN/device
    std::string temp_path;
  };
  std::vector<Card> cards;

  for (int i = 0; i < 32; ++i) {
    std::string card = "card" + std::to_string(i);
    std::string base = std::string(kDrmBase) + card + "/device";

    std::uint64_t vendor = 0, device = 0;
    if (!read_sysfs_uint64(base + "/vendor", vendor) ||
        !read_sysfs_uint64(base + "/device", device))
      continue;
    if (vendor != 0x1002)
      continue;

    Card c;
    c.name = amdgpu_name(static_cast<std::uint32_t>(device));
    c.base = base;
    c.temp_path = find_hwmon(card) + "/temp1_input";
    cards.push_back(std::move(c));
  }

  if (cards.empty()) {
    set_status("sysfs AMD GPU not available — no AMD card under /sys/class/drm");
    return false;
  }

  initialized_ = true;
  device_names_.clear();
  device_bases_.clear();
  temp_paths_.clear();
  for (const auto& c : cards) {
    device_names_.push_back(c.name);
    device_bases_.push_back(c.base);
    temp_paths_.push_back(c.temp_path);
  }

  std::scoped_lock lock(state_.mutex);
  if (state_.gpu.devices.empty()) {
    state_.gpu.available = true;
    state_.gpu.status.clear();
    for (const auto& n : device_names_) {
      GpuDevice d;
      d.name = n;
      state_.gpu.devices.push_back(std::move(d));
    }
  }
  return true;
}

void SysfsSource::poll() {
  if (!initialized_) {
    if (attempted_)
      return;
    attempted_ = true;
    load();
  }
  if (!initialized_)
    return;

  struct Reading {
    std::uint64_t mem_used = 0;
    std::uint64_t mem_total = 0;
    int busy = 0;
    int temp_c = 0;
  };
  std::vector<Reading> readings(device_bases_.size());
  for (std::size_t i = 0; i < device_bases_.size(); ++i) {
    Reading& r = readings[i];
    std::uint64_t v = 0;
    if (read_sysfs_uint64(device_bases_[i] + "/mem_info_vram_used", v))
      r.mem_used = v;
    if (read_sysfs_uint64(device_bases_[i] + "/mem_info_vram_total", v))
      r.mem_total = v;
    if (read_sysfs_uint64(device_bases_[i] + "/gpu_busy_percent", v))
      r.busy = static_cast<int>(v);
    std::int64_t t = 0;
    if (!temp_paths_[i].empty() && read_sysfs_int64(temp_paths_[i], t))
      r.temp_c = static_cast<int>(t / 1000);  // milli-°C
  }

  std::scoped_lock lock(state_.mutex);
  if (state_.gpu.devices.empty())
    return;
  for (auto& d : state_.gpu.devices) {
    for (std::size_t i = 0; i < device_names_.size(); ++i) {
      if (d.name != device_names_[i])
        continue;
      d.mem_used = readings[i].mem_used;
      d.mem_total = readings[i].mem_total;
      d.util_pct = readings[i].busy;
      d.temp_c = readings[i].temp_c;
      d.util_history.push(static_cast<float>(d.util_pct));
      d.vram_history.push(d.mem_total
                             ? 100.0f * static_cast<float>(d.mem_used) /
                                   static_cast<float>(d.mem_total)
                             : 0.0f);
      break;
    }
  }
}

}  // namespace llmtop
