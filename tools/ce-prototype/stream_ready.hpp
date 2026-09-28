#pragma once

// Local namespace keeps readiness within the interactive Windows session.
inline constexpr wchar_t ce_stream_ready_event[] = L"Local\\MoonmachineDirectCaptureReady";

class CeStreamReady {
  HANDLE event=nullptr;
 public:
  explicit CeStreamReady(bool enabled) {
    if(!enabled)return;
    event=CreateEventW(nullptr,TRUE,FALSE,ce_stream_ready_event);
    if(!event)throw std::runtime_error("direct capture readiness event");
    if(!SetEvent(event)){CloseHandle(event);event=nullptr;throw std::runtime_error("direct capture readiness signal");}
  }
  ~CeStreamReady(){if(event){ResetEvent(event);CloseHandle(event);}}
};
