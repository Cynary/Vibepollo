#pragma once
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Opt-in diagnostics: retain recent events until an explicit snapshot request.
// Only the worker touches the filesystem. The stream/capture threads append to
// a bounded in-memory queue; snapshots do not stop recording.
namespace rolling_diagnostic_trace {
template<class Row, class Clock = std::chrono::steady_clock> class recorder {
  struct entry { int64_t time; Row data; };
  std::string path;
  const char* header;
  void (*format)(std::ostream&, const Row&);
  std::deque<entry> rows;
  std::mutex mutex;
  std::condition_variable wake;
  bool stopping = false;
  std::thread worker;
  static constexpr int64_t window_us = 180000000;
  static constexpr size_t maximum_rows = 2000000;

  void trim(int64_t now) {
    while (!rows.empty() && (now - rows.front().time > window_us || rows.size() > maximum_rows))
      rows.pop_front();
  }
  static int64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
      Clock::now().time_since_epoch()).count();
  }
  void snapshot() {
    std::vector<Row> copy;
    {
      std::lock_guard lock(mutex);
      trim(now_us());
      copy.reserve(rows.size());
      for (const auto& r : rows) copy.push_back(r.data);
    }
    // The request file is removed only after the output is fully closed.
    // Consumers wait for its removal before reading the CSV.
    std::ofstream out(path, std::ios::trunc);
    out << header;
    for (const auto& r : copy) format(out, r);
    out.close();
    if (out) {
      std::error_code ec;
      std::filesystem::remove(path + ".snapshot", ec);
    }
  }
  void run() {
    for (;;) {
      bool done;
      {
        std::unique_lock lock(mutex);
        wake.wait_for(lock, std::chrono::seconds(1), [&]{ return stopping; });
        done = stopping;
      }
      std::error_code ec;
      if (done || std::filesystem::exists(path + ".snapshot", ec)) snapshot();
      if (done) return;
    }
  }
public:
  recorder(const char* suffix, const char* csv_header, void (*write_row)(std::ostream&, const Row&))
    : header(csv_header), format(write_row) {
    if (const char* p = std::getenv("MOONMACHINE_HOST_FRAME_TRACE"); p && *p) {
      path = std::string(p) + suffix;
      worker = std::thread([this]{ run(); });
    }
  }
  ~recorder() {
    if (!worker.joinable()) return;
    { std::lock_guard lock(mutex); stopping = true; }
    wake.notify_one();
    worker.join();
  }
  void add(Row row) {
    if (path.empty()) return;
    const auto now = now_us();
    std::lock_guard lock(mutex);
    const auto acquired = now_us();
    if constexpr (requires { row.trace_lock_wait_us; })
      row.trace_lock_wait_us = acquired - now;
    rows.push_back({now, row});
    trim(now);
    if constexpr (requires { row.trace_append_us; })
      rows.back().data.trace_append_us = now_us() - acquired;
  }
};
}
