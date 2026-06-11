#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace llmtop {

// Fixed-capacity time series; oldest sample is dropped when full.
// Capacity must exceed the widest realistic graph (in columns), otherwise
// the left edge of the graph can never fill.
class RingBuffer {
 public:
  explicit RingBuffer(std::size_t capacity = 600) : cap_(capacity) {
    data_.reserve(capacity);
  }

  void push(float v) {
    if (data_.size() == cap_)
      data_.erase(data_.begin());
    data_.push_back(v);
  }

  const std::vector<float>& values() const { return data_; }
  float latest() const { return data_.empty() ? 0.0f : data_.back(); }

  float max() const {
    float m = 0.0f;
    for (float v : data_)
      m = std::max(m, v);
    return m;
  }

 private:
  std::vector<float> data_;
  std::size_t cap_;
};

struct GpuDevice {
  std::string name;
  std::uint64_t mem_used = 0;
  std::uint64_t mem_total = 0;
  int util_pct = 0;
  int temp_c = 0;
  double power_w = 0;
  double power_limit_w = 0;
  int sm_clock_mhz = 0;
  int mem_clock_mhz = 0;
  RingBuffer util_history;   // percent, 0..100
  RingBuffer vram_history;   // percent, 0..100
};

struct GpuState {
  bool available = false;   // NVML loaded and at least one device found
  bool disabled = false;    // --no-nvml
  std::string driver_version;
  std::string status;       // human-readable reason when unavailable
  std::vector<GpuDevice> devices;
};

struct LlamaSlot {
  int id = 0;
  bool processing = false;
  std::int64_t n_past = 0;  // context tokens currently used
  std::int64_t n_ctx = 0;
};

struct LlamaState {
  std::string url;
  std::string model;        // loaded model name (from /props), "" if unknown
  bool online = false;
  std::string error;        // why we consider it offline
  std::string note;         // hints, e.g. "/slots disabled"
  bool have_slots = false;
  bool have_metrics = false;
  std::vector<LlamaSlot> slots;
  double gen_tps = 0;       // generated tokens/s (smoothed)
  double prompt_tps = 0;    // prompt-processing tokens/s (smoothed)
  double kv_ratio = -1;     // 0..1, <0 means unknown
  std::int64_t kv_tokens = -1;
  int requests_processing = 0;
  int requests_deferred = 0;
  RingBuffer gen_history;
  RingBuffer prompt_history;
};

struct OllamaModel {
  std::string name;
  std::string params;       // e.g. "14.8B"
  std::string quant;        // e.g. "Q4_K_M"
  std::uint64_t size_bytes = 0;
  std::uint64_t size_vram = 0;
  double expires_in_s = -1; // <0 unknown, very large means "never"
};

struct OllamaState {
  std::string url;
  bool online = false;
  std::string error;
  std::vector<OllamaModel> models;
};

// Shared between poller threads (writers) and the UI thread (reader).
// Lock `mutex` for any access to gpu/llama/ollama.
struct AppState {
  std::mutex mutex;
  GpuState gpu;
  LlamaState llama;
  OllamaState ollama;
  std::atomic<int> interval_ms{500};
  bool demo_mode = false;  // set once before threads start
};

}  // namespace llmtop
