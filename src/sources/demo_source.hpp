#pragma once

#include <array>
#include <chrono>
#include <random>

#include "sources/source.hpp"

namespace llmtop {

// Fake data generator for `llmtop --demo`: simulates a 24 GiB GPU running a
// llama.cpp server with four slots plus one Ollama-resident model. Requests
// arrive randomly, ramp prompt processing then generation, and the GPU
// thermals/power follow with realistic lag. Lets the UI be developed and
// recorded without any real inference server.
class DemoSource final : public Source {
 public:
  explicit DemoSource(AppState& state);

  void poll() override;

 private:
  enum class Phase { Idle, Prompt, Gen };
  struct SlotSim {
    Phase phase = Phase::Idle;
    double phase_left = 2.0;   // seconds remaining in current phase
    double n_past = 0;         // context tokens used
    std::int64_t n_ctx = 8192;
    double prompt_rate = 0;    // tokens/s while in Prompt
    double gen_base = 0;       // tokens/s baseline while in Gen
  };

  double uniform(double a, double b);
  double gauss(double mean, double sd);

  std::mt19937 rng_{20260610};
  std::chrono::steady_clock::time_point last_;
  std::array<SlotSim, 4> slots_;
  double util_ = 4;
  double temp_ = 47;
  double power_ = 62;
  double gen_smooth_ = 0;
  double prompt_smooth_ = 0;
  double unload_in_ = 300;
};

}  // namespace llmtop
