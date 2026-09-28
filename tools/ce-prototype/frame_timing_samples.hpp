#pragma once
#include <array>
#include <filesystem>
#include <fstream>
#include <memory>

// Keep the latest ~9 minutes at 120 FPS without file I/O in the capture loop.
// Source timestamps may be synthetic under frame generation. Only differences
// between observed/ready/published describe measured consumer work.
class FrameTimingSamples {
  struct Sample {
    uint32_t frame, flags, pending, format;
    int64_t source, observed, ready, published;
  };
  static constexpr size_t capacity = 65536;
  std::unique_ptr<std::array<Sample, capacity>> samples;
  size_t written = 0;
  std::filesystem::path path;
 public:
  explicit FrameTimingSamples(DWORD targetPid) {
    wchar_t enabled[2]{};
    if(GetEnvironmentVariableW(L"MOONMACHINE_CE_TRACE_TIMING",enabled,2)!=1 || enabled[0]!=L'1')return;
    wchar_t directory[32768];
    if(!GetTempPathW(32768,directory))throw std::runtime_error("timing output directory");
    path=std::filesystem::path(directory)/(L"ce-timing-"+std::to_wstring(GetCurrentProcessId())+
                                          L"-"+std::to_wstring(targetPid)+L".csv");
    samples=std::make_unique<std::array<Sample,capacity>>();
  }
  void record(const FrameSlot& slot,uint32_t pending,uint32_t format,
              int64_t observed,int64_t ready,int64_t published) {
    if(samples)(*samples)[written++%capacity]={slot.frameIndex,slot.captureFlags,pending,format,
                                             slot.timestamp,observed,ready,published};
  }
  ~FrameTimingSamples() noexcept {
    if(!samples)return;
    try {
      std::ofstream csv(path);
      LARGE_INTEGER frequency;QueryPerformanceFrequency(&frequency);
      csv<<"frame,flags,pending,format,source_qpc,observed_qpc,ready_qpc,published_qpc,qpc_frequency\n";
      const size_t first=written>capacity?written-capacity:0;
      for(size_t i=first;i<written;i++) {
        const auto& s=(*samples)[i%capacity];
        csv<<s.frame<<','<<s.flags<<','<<s.pending<<','<<s.format<<','<<s.source<<','
           <<s.observed<<','<<s.ready<<','<<s.published<<','<<frequency.QuadPart<<'\n';
      }
      csv.close();
      if(!csv)fprintf(stderr,"CE timing trace write failed\n");
    } catch(...) {fprintf(stderr,"CE timing trace write failed\n");}
  }
};
