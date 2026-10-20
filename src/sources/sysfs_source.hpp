// AMD GPU fallback telemetry via /sys/class/drm (kernel amdgpu driver,
// no ROCm required).
//
// Only takes ownership of the GPU panel if no other source has already
// populated it; otherwise it updates matching devices in place.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sources/source.hpp"

namespace llmtop {

class SysfsSource final : public Source {
 public:
  explicit SysfsSource(AppState& state) : Source(state) {}

  void poll() override;

 private:
  bool load();
  void set_status(const std::string& msg);

  bool initialized_ = false;
  bool attempted_ = false;

  std::vector<std::string> device_names_;  // per discovered card
  std::vector<std::string> device_bases_;  // /sys/class/drm/cardN/device
  std::vector<std::string> temp_paths_;    // per card, edge temp1_input
};

}  // namespace llmtop
