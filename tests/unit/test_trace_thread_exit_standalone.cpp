#include "../../src/host_frame_trace.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

int main(int argc, char** argv) {
  // Run --legacy-tls only as a separate child with an external timeout.
  const bool legacy = argc > 1 && std::string(argv[1]) == "--legacy-tls";
  auto path = (std::filesystem::temp_directory_path() / "moonmachine-thread-exit.csv").string();
#ifdef _WIN32
  _putenv_s("MOONMACHINE_HOST_FRAME_TRACE", path.c_str());
#else
  setenv("MOONMACHINE_HOST_FRAME_TRACE", path.c_str(), 1);
#endif
  for (int i=0; i<25; ++i) {
    std::thread sender([&] {
      if (legacy) {
        static thread_local host_frame_trace::recorder trace;
        trace.add({});
      } else {
        host_frame_trace::recorder trace;
        trace.add({});
      }
    });
    sender.join();
    std::ifstream out(path);
    std::string header, row;
    assert(std::getline(out, header));
    assert(std::getline(out, row));
    assert(row.starts_with("0,0,"));
  }
  std::filesystem::remove(path);
}
