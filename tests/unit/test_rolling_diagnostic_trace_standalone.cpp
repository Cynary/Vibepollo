#include "../../src/rolling_diagnostic_trace.h"
#include <atomic>
#include <cassert>
#include <sstream>
struct fake_clock {
  static inline std::atomic<int64_t> ticks{0};
  static auto now() { return std::chrono::time_point<fake_clock,std::chrono::microseconds>{std::chrono::microseconds{ticks.load()}}; }
};
void write(std::ostream& o,const int& r){o<<r<<'\n';}
int main(){
 const auto path=(std::filesystem::temp_directory_path()/"moonmachine-rolling-trace-test.csv").string();
 setenv("MOONMACHINE_HOST_FRAME_TRACE",path.c_str(),1);
 auto snapshot=[&]{
   std::ofstream(path+".snapshot").close();
   const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
   while(std::filesystem::exists(path+".snapshot") && std::chrono::steady_clock::now()<end)
     std::this_thread::sleep_for(std::chrono::milliseconds(10));
   assert(!std::filesystem::exists(path+".snapshot"));
   std::ifstream f(path);return std::string(std::istreambuf_iterator<char>(f),{});
 };
 {
  rolling_diagnostic_trace::recorder<int,fake_clock> r("","value\n",write);
  // Old code stopped at 12k/60k events or 90 seconds. This must keep recording.
  for(int i=0;i<70000;++i){fake_clock::ticks=i*2000LL;r.add(i);}
  auto first=snapshot();assert(first.find("69999\n")!=std::string::npos);
  fake_clock::ticks=400000000;r.add(70000);
  assert(snapshot()=="value\n70000\n");
  r.add(70001);assert(snapshot()=="value\n70000\n70001\n");
 }
 std::filesystem::remove(path);
}
