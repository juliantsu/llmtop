#pragma once

#include <chrono>
#include <string>
#include <unordered_map>

#include "sources/source.hpp"

namespace llmtop {

// Polls a llama.cpp server: GET /slots for per-slot state and GET /metrics
// (Prometheus text format) for KV-cache/queue gauges.
//
// Throughput is computed from per-slot `n_decoded` deltas between polls when
// the server reports them — the /metrics counters only flush when a request
// completes, which would render a live generation as zero followed by one
// huge spike. Counter deltas remain as fallback for servers without slot
// token counts.
class LlamaCppSource final : public Source {
 public:
  LlamaCppSource(AppState& state, std::string base_url);

  void poll() override;

 private:
  struct SlotTrack {
    std::int64_t id_task = -1;
    std::int64_t n_decoded = 0;
    bool decoding_seen = false;  // current task has produced tokens
    std::chrono::steady_clock::time_point task_start{};
  };

  std::string base_;
  std::string model_;       // fetched from /props once per online phase
  bool props_tried_ = false;
  std::unordered_map<int, SlotTrack> track_;
  std::chrono::steady_clock::time_point prev_slots_time_{};
  bool have_prev_slots_ = false;
  bool have_prev_metrics_ = false;
  double prev_predicted_total_ = 0;
  double prev_prompt_total_ = 0;
  std::chrono::steady_clock::time_point prev_metrics_time_{};
  double gen_smooth_ = 0;
  double prompt_smooth_ = 0;
};

}  // namespace llmtop
