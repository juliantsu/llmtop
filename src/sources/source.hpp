#pragma once

#include "state.hpp"

namespace llmtop {

// A data source polled on its own thread. poll() must never throw and
// must do all its slow work (network, NVML) outside state_.mutex.
class Source {
 public:
  explicit Source(AppState& state) : state_(state) {}
  virtual ~Source() = default;

  Source(const Source&) = delete;
  Source& operator=(const Source&) = delete;

  virtual void poll() = 0;

 protected:
  AppState& state_;
};

}  // namespace llmtop
