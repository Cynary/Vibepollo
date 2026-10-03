#include "src/capture_timestamp_policy.h"
#include <cassert>
#include <iostream>

int main() {
  using namespace std::chrono;
  using time = steady_clock::time_point;
  using video::capture_timing::encode_timestamp;
  using video::capture_timing::send_timestamp;
  auto at = [](int64_t us) { return time(microseconds(us)); };
  const auto period = nanoseconds(8333333);
  std::optional<time> next;
  // WGC keeps nominal cadence within the original quarter-frame tolerance.
  assert(encode_timestamp(at(100000), false, next, period) == at(100000));
  assert(encode_timestamp(at(109000), false, next, period) == at(100000) + period);
  auto direct = at(116000);
  assert(encode_timestamp(direct, true, next, period) == direct);
  assert(!next);
  assert(encode_timestamp(at(124901), true, next, period) == at(124901));
  // Focus returns to WGC: no old nominal slot survives the direct interval.
  assert(encode_timestamp(at(130500), false, next, period) == at(130500));
  // A genuine WGC discontinuity reanchors as before.
  assert(encode_timestamp(at(160000), false, next, period) == at(160000));
  int calls = 0;
  auto refine = [&](time t) { ++calls; return t - microseconds(1700); };
  assert(send_timestamp(direct, true, refine) == direct && calls == 0);
  assert(send_timestamp(direct, false, refine) == direct - microseconds(1700) && calls == 1);
  // Repeated backend transitions cannot leak the prior frame's decision.
  for (int i = 0; i < 1000; ++i) {
    bool isDirect = i % 3 != 0;
    auto raw = at(200000 + i * 8701);
    auto encoded = encode_timestamp(raw, isDirect, next, period);
    auto wire = send_timestamp(encoded, isDirect, refine);
    if (isDirect) assert(wire == raw);
  }
  // Differential check: all-WGC input must exactly preserve the old policy,
  // including large gaps, backward timestamps and quarter-period boundaries.
  std::optional<time> legacy;
  next.reset();
  uint64_t random = 17;
  for (int i = 0; i < 100000; ++i) {
    random = random * 6364136223846793005ULL + 1;
    auto raw = at(1000000 + i * 8333 + int64_t(random % 20001) - 10000);
    if (!legacy) legacy = raw;
    auto distance = raw > *legacy ? raw - *legacy : *legacy - raw;
    auto expected = distance < period / 4 ? *legacy : raw;
    legacy = expected + period;
    assert(encode_timestamp(raw, false, next, period) == expected);
  }
  std::cout << "capture timestamp policy: passed\n";
}
