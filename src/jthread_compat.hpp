#pragma once

// std::jthread/std::stop_token where the standard library provides them
// (libstdc++, recent libc++); a minimal std::thread-based stand-in otherwise
// (e.g. AppleClang before Xcode 26). Only the subset llmtop uses is
// implemented: construction with a callable taking a stop token,
// request_stop(), and join-on-destruction.

#include <version>

#if defined(__cpp_lib_jthread) && !defined(LLMTOP_FORCE_JTHREAD_FALLBACK)

#include <stop_token>
#include <thread>

namespace llmtop {
using Jthread = std::jthread;
using StopToken = std::stop_token;
}  // namespace llmtop

#else

#include <atomic>
#include <memory>
#include <thread>
#include <utility>

namespace llmtop {

class StopToken {
 public:
  StopToken() = default;
  explicit StopToken(std::shared_ptr<std::atomic<bool>> flag)
      : flag_(std::move(flag)) {}
  bool stop_requested() const { return flag_ && flag_->load(); }

 private:
  std::shared_ptr<std::atomic<bool>> flag_;
};

class Jthread {
 public:
  Jthread() = default;

  template <typename F>
  explicit Jthread(F&& f)
      : flag_(std::make_shared<std::atomic<bool>>(false)),
        thread_([flag = flag_, fn = std::forward<F>(f)]() mutable {
          fn(StopToken(flag));
        }) {}

  Jthread(Jthread&&) = default;
  Jthread& operator=(Jthread&&) = delete;
  Jthread(const Jthread&) = delete;
  Jthread& operator=(const Jthread&) = delete;

  ~Jthread() {
    if (thread_.joinable()) {
      request_stop();
      thread_.join();
    }
  }

  void request_stop() {
    if (flag_)
      flag_->store(true);
  }

 private:
  std::shared_ptr<std::atomic<bool>> flag_;
  std::thread thread_;
};

}  // namespace llmtop

#endif
