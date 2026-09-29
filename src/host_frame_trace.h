#pragma once
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
#include "rolling_diagnostic_trace.h"

// Rolling diagnostic capture per sender thread; file I/O runs on a worker.
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
  static void write(std::ostream& out, const row& r) {
    out << r.frame << ',' << r.rtp << ',' << r.bytes << ',' << r.duplicate << ','
        << r.capture << ',' << r.host_start << ',' << r.encode_start << ',' << r.encode_end << ','
        << r.enqueue << ',' << r.pop << ',' << r.first_send << ',' << r.send_end << '\n';
  }
  rolling_diagnostic_trace::recorder<row> trace {
    "", "frame,rtp,bytes,duplicate,capture_us,host_start_us,encode_start_us,encode_end_us,enqueue_us,pop_us,first_send_us,send_end_us\n", write
  };
public:
  void add(row r) { trace.add(r); }
};
}
