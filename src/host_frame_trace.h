#pragma once
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

// One bounded diagnostic capture per sender thread. No file I/O while recording.
namespace host_frame_trace {
using clock = std::chrono::steady_clock;
inline int64_t us(clock::time_point t) {
  return std::chrono::duration_cast<std::chrono::microseconds>(t.time_since_epoch()).count();
}
struct row {
  int64_t frame; uint32_t rtp; uint64_t bytes; bool duplicate;
  int64_t capture, host_start, encode_start, encode_end, enqueue, pop, first_send, send_end;
};
class recorder {
  std::string path;
  std::vector<row> rows;
  std::thread writer;
  bool finished = false;
  void finish() {
    if (finished || path.empty()) return;
    finished = true;
    writer = std::thread([dest = path, data = std::move(rows)] {
      std::ofstream out(dest, std::ios::trunc);
      out << "frame,rtp,bytes,duplicate,capture_us,host_start_us,encode_start_us,encode_end_us,enqueue_us,pop_us,first_send_us,send_end_us\n";
      for (const auto& r : data)
        out << r.frame << ',' << r.rtp << ',' << r.bytes << ',' << r.duplicate << ','
            << r.capture << ',' << r.host_start << ',' << r.encode_start << ',' << r.encode_end << ','
            << r.enqueue << ',' << r.pop << ',' << r.first_send << ',' << r.send_end << '\n';
    });
  }
public:
  recorder() {
    if (const char* p = std::getenv("MOONMACHINE_HOST_FRAME_TRACE")) path = p;
    if (!path.empty()) rows.reserve(12000);
  }
  ~recorder() { finish(); if (writer.joinable()) writer.join(); }
  void add(row r) {
    if (path.empty() || finished) return;
    rows.push_back(r);
    if (rows.size() >= 12000 || r.send_end - rows.front().send_end >= 90000000) finish();
  }
};
}
