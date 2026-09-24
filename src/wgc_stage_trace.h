#pragma once
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
namespace wgc_stage_trace {
struct event { uint64_t qpc; int64_t time; const char* stage; };
class recorder {
 std::string path;
 std::mutex mutex;
 std::vector<event> events;
 std::thread writer;
 bool finished=false;
 void finish() {
  if(finished || path.empty()) return;
  finished=true;
  writer=std::thread([dest=path,data=std::move(events)] {
   std::ofstream out(dest,std::ios::trunc);out<<"source_100ns,steady_us,stage\n";
   for(auto& e:data)out<<e.qpc<<','<<e.time<<','<<e.stage<<'\n';
  });
 }
public:
 recorder(const char* suffix=".wgc.csv"){if(auto p=std::getenv("MOONMACHINE_HOST_FRAME_TRACE")){path=std::string(p)+suffix;events.reserve(60000);}}
 ~recorder(){finish();if(writer.joinable())writer.join();}
 void add(uint64_t qpc,const char* stage){
  if(path.empty())return;
  auto now=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
  std::lock_guard lock(mutex);
  if(finished)return;
  events.push_back({qpc,now,stage});
  if(events.size()>=60000 || now-events.front().time>=90000000)finish();
 }
};
inline void record(uint64_t qpc,const char* stage){static recorder r;r.add(qpc,stage);}
inline void capture(const char* stage){static recorder r(".capture.csv");r.add(0,stage);}
}
