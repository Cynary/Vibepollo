#pragma once
#include <algorithm>
#include <chrono>

namespace platf::dxgi::wgc_policy {
// Bound sustained work without holding an available frame for a timer slot.
// One spare token admits a catch-up frame after a delayed arrival. Idle time
// cannot accumulate more than two frames of credit.
class event_rate_limit {
 public:
  using clock = std::chrono::steady_clock;
  explicit event_rate_limit(double fps): fps_(fps) {}
  bool admit(clock::time_point now) {
    if (fps_ <= 0) return true;
    if (initialized_) {
      tokens_ = std::min(2.0, tokens_ + std::max(0.0, std::chrono::duration<double>(now - last_).count()) * fps_);
    }
    initialized_ = true;
    last_ = now;
    if (tokens_ + 1e-9 < 1.0) return false;
    tokens_ = std::max(0.0, tokens_ - 1.0);
    return true;
  }
 private:
  double fps_;
  double tokens_ = 2.0;
  clock::time_point last_ {};
  bool initialized_ = false;
};
}
