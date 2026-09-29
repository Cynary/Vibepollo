#pragma once
#include "rolling_diagnostic_trace.h"
#include <cstdint>
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#endif
namespace wgc_stage_trace {
struct event { uint64_t qpc; int64_t time; const char* stage; uint64_t callback_id; uint32_t call_index; uint32_t process_id; uint32_t thread_id; int64_t trace_lock_wait_us; int64_t trace_append_us; };
class recorder {
 static void write(std::ostream& out, const event& e) {
  out << e.qpc << ',' << e.time << ',' << e.stage << ',' << e.callback_id << ',' << e.call_index << ',' << e.process_id << ',' << e.thread_id << ',' << e.trace_lock_wait_us << ',' << e.trace_append_us << '\n';
 }
 rolling_diagnostic_trace::recorder<event> trace;
public:
 recorder(const char* suffix=".wgc.csv") : trace(suffix,"source_100ns,steady_us,stage,callback_id,call_index,process_id,thread_id,trace_lock_wait_us,trace_append_us\n",write) {}
 void add(uint64_t qpc, const char* stage, uint64_t callback_id = 0, uint32_t call_index = 0) {
  const auto now=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
  #ifdef _WIN32
  trace.add({qpc,now,stage,callback_id,call_index,GetCurrentProcessId(),GetCurrentThreadId(),0,0});
#else
  trace.add({qpc,now,stage,callback_id,call_index,0,0,0,0});
#endif
 }
};
inline void record(uint64_t qpc,const char* stage,uint64_t callback_id=0,uint32_t call_index=0){static recorder r;r.add(qpc,stage,callback_id,call_index);}
// Only the helper writes .wgc.csv; all main-process capture stages share one recorder.
inline recorder& capture_recorder(){static recorder r(".capture.csv");return r;}
inline void capture(const char* stage){capture_recorder().add(0,stage);}
inline void capture_frame(uint64_t qpc,const char* stage){capture_recorder().add(qpc,stage);}

}
