#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <thread>
#include <functional>

// Opt-in bounded CPU timestamps. No I/O during encoding; saved at thread exit.
// Submission is the API call boundary, not the encoder engine's GPU start time.
namespace nvenc::submission_trace {
using clock = std::chrono::steady_clock;
inline int64_t us(clock::time_point t) {
  return std::chrono::duration_cast<std::chrono::microseconds>(t.time_since_epoch()).count();
}
struct sample { uint64_t frame; int64_t begin, submit, complete; };
class recorder {
  static constexpr size_t capacity = 60000;
  std::unique_ptr<std::array<sample, capacity>> rows;
  std::string path;
  size_t written = 0;
public:
  recorder() {
    if (const auto* p = std::getenv("MOONMACHINE_NVENC_SUBMIT_TRACE"); p && *p) {
      path = std::string(p) + "." + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) + ".csv";
      if (!path.empty()) rows = std::make_unique<std::array<sample, capacity>>();
    }
  }
  void add(uint64_t frame, clock::time_point begin, clock::time_point submit, clock::time_point complete) {
    if (rows) (*rows)[written++ % capacity] = {frame, us(begin), us(submit), us(complete)};
  }
  ~recorder() noexcept {
    if (!rows) return;
    try {
      std::ofstream out(path);
      out << "frame,encode_entry_us,nvenc_submit_us,bitstream_ready_us\n";
      for (size_t i = written > capacity ? written - capacity : 0; i < written; ++i) {
        const auto& r = (*rows)[i % capacity];
        out << r.frame << ',' << r.begin << ',' << r.submit << ',' << r.complete << '\n';
      }
      out.close();
      if (!out) std::fprintf(stderr, "NVENC submission trace write failed\n");
    } catch (...) { std::fprintf(stderr, "NVENC submission trace save failed\n"); }
  }
};
}
