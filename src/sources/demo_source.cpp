#include "sources/demo_source.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <vector>

namespace llmtop {

namespace {
constexpr std::uint64_t kGiB = 1024ull * 1024 * 1024;
constexpr std::uint64_t kVramTotal = 24 * kGiB;
constexpr double kWeightsBytes = 16.6 * static_cast<double>(kGiB);
constexpr double kKvFullBytes = 3.1 * static_cast<double>(kGiB);
}  // namespace

DemoSource::DemoSource(AppState& state) : Source(state) {
  slots_[2].n_ctx = 16384;
  slots_[3].n_ctx = 16384;
  for (auto& s : slots_)
    s.phase_left = uniform(0.3, 5.0);
  last_ = std::chrono::steady_clock::now();
}

double DemoSource::uniform(double a, double b) {
  return std::uniform_real_distribution<double>(a, b)(rng_);
}

double DemoSource::gauss(double mean, double sd) {
  return std::normal_distribution<double>(mean, sd)(rng_);
}

void DemoSource::poll() {
  auto now = std::chrono::steady_clock::now();
  double dt = std::chrono::duration<double>(now - last_).count();
  last_ = now;
  dt = std::clamp(dt, 0.01, 2.0);

  double gen_total = 0, prompt_total = 0;
  int busy = 0;

  for (auto& s : slots_) {
    s.phase_left -= dt;
    switch (s.phase) {
      case Phase::Idle:
        if (s.phase_left <= 0) {
          s.phase = Phase::Prompt;
          s.phase_left = uniform(0.4, 2.4);
          s.prompt_rate = std::max(300.0, gauss(1150, 180));
          if (uniform(0, 1) < 0.5)
            s.n_past = 0;  // half the requests reuse the prompt cache
        }
        break;
      case Phase::Prompt: {
        double rate = s.prompt_rate * (1.0 + gauss(0, 0.06));
        s.n_past += rate * dt;
        prompt_total += rate;
        if (s.phase_left <= 0) {
          s.phase = Phase::Gen;
          s.phase_left = uniform(3.0, 16.0);
          s.gen_base = std::max(20.0, gauss(47, 5));
        }
        break;
      }
      case Phase::Gen: {
        // throughput sags slightly as the context fills up
        double rate = s.gen_base *
                      (1.0 - 0.25 * s.n_past / static_cast<double>(s.n_ctx)) *
                      (1.0 + gauss(0, 0.04));
        rate = std::max(5.0, rate);
        s.n_past += rate * dt;
        gen_total += rate;
        if (s.phase_left <= 0 || s.n_past > 0.95 * static_cast<double>(s.n_ctx)) {
          s.phase = Phase::Idle;
          s.phase_left = uniform(1.0, 9.0);
        }
        break;
      }
    }
    s.n_past = std::min(s.n_past, static_cast<double>(s.n_ctx));
    busy += s.phase == Phase::Idle ? 0 : 1;
  }

  double util_target = busy ? std::min(99.0, 80 + 4.0 * busy + gauss(0, 4))
                            : std::max(0.0, 3 + gauss(0, 1.5));
  util_ += (util_target - util_) * std::min(1.0, dt * 2.5);
  util_ = std::clamp(util_, 0.0, 99.0);

  temp_ += ((44 + util_ * 0.43) - temp_) * (1.0 - std::exp(-dt / 9.0));
  double power_target = std::clamp(55 + util_ * 3.55 + gauss(0, 6), 15.0, 450.0);
  power_ += (power_target - power_) * std::min(1.0, dt * 1.8);

  double kv_tokens = 0, kv_capacity = 0;
  for (const auto& s : slots_) {
    kv_tokens += s.n_past;
    kv_capacity += static_cast<double>(s.n_ctx);
  }
  double kv_ratio = kv_capacity > 0 ? kv_tokens / kv_capacity : 0;
  auto vram_used = static_cast<std::uint64_t>(
      1.15 * static_cast<double>(kGiB) + kWeightsBytes + kv_ratio * kKvFullBytes);

  gen_smooth_ += (gen_total - gen_smooth_) * 0.5;
  prompt_smooth_ += (prompt_total - prompt_smooth_) * 0.5;
  unload_in_ = busy ? 300.0 : std::max(0.0, unload_in_ - dt);

  std::scoped_lock lock(state_.mutex);

  GpuState& g = state_.gpu;
  g.available = true;
  g.driver_version = "560.35.03";
  if (g.devices.empty()) {
    GpuDevice d;
    d.name = "NVIDIA GeForce RTX 4090";
    g.devices.push_back(std::move(d));
  }
  GpuDevice& d = g.devices[0];
  d.mem_used = vram_used;
  d.mem_total = kVramTotal;
  d.util_pct = static_cast<int>(util_);
  d.temp_c = static_cast<int>(temp_);
  d.power_w = power_;
  d.power_limit_w = 450;
  d.sm_clock_mhz = busy ? 2730 + static_cast<int>(gauss(0, 15)) : 210;
  d.mem_clock_mhz = busy ? 10501 : 405;
  d.util_history.push(static_cast<float>(util_));
  d.vram_history.push(static_cast<float>(
      100.0 * static_cast<double>(vram_used) / static_cast<double>(kVramTotal)));

  LlamaState& L = state_.llama;
  L.url = "demo";
  L.model = "llama-3.1-8b-instruct-Q4_K_M";
  L.online = true;
  L.have_slots = true;
  L.have_metrics = true;
  L.slots.clear();
  int id = 0;
  for (const auto& s : slots_) {
    LlamaSlot ls;
    ls.id = id++;
    ls.processing = s.phase != Phase::Idle;
    ls.n_past = static_cast<std::int64_t>(s.n_past);
    ls.n_ctx = s.n_ctx;
    L.slots.push_back(ls);
  }
  L.gen_tps = gen_smooth_;
  L.prompt_tps = prompt_smooth_;
  L.kv_ratio = kv_ratio;
  L.kv_tokens = static_cast<std::int64_t>(kv_tokens);
  L.requests_processing = busy;
  L.gen_history.push(static_cast<float>(gen_smooth_));
  L.prompt_history.push(static_cast<float>(prompt_smooth_));

  OllamaState& o = state_.ollama;
  o.url = "demo";
  o.online = true;
  o.models.clear();
  OllamaModel m;
  m.name = "qwen2.5:14b-instruct-q4_K_M";
  m.params = "14.8B";
  m.quant = "Q4_K_M";
  m.size_bytes = static_cast<std::uint64_t>(9.0 * static_cast<double>(kGiB));
  m.size_vram = m.size_bytes;
  m.expires_in_s = unload_in_;
  o.models.push_back(std::move(m));
  OllamaModel e;
  e.name = "nomic-embed-text:latest";
  e.params = "137M";
  e.quant = "F16";
  e.size_bytes = 274 * 1024 * 1024;
  e.size_vram = e.size_bytes;
  e.expires_in_s = 86400.0 * 400;  // keep_alive=-1 → "never"
  o.models.push_back(std::move(e));
}

}  // namespace llmtop
