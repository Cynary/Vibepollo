#pragma once

#include <chrono>
#include <optional>

namespace video::capture_timing {
  // The WGC path may snap compositor times to its negotiated cadence. Hook
  // timestamps already describe the source frame and must survive unchanged.
  template<class Time, class Duration>
  Time encode_timestamp(Time captured, bool direct, std::optional<Time> &next, Duration period) {
    if (direct) {
      next.reset(); // Returning to WGC starts a new prediction at its first frame.
      return captured;
    }
    if (!next) next = captured;
    const auto distance = captured > *next ? captured - *next : *next - captured;
    const auto result = distance < period / 4 ? *next : captured;
    next = result + period;
    return result;
  }

  template<class Time, class Refine>
  Time send_timestamp(Time captured, bool direct, Refine &&refine) {
    return direct ? captured : refine(captured);
  }
}
