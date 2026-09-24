#include "../../src/platform/windows/wgc_event_rate.h"
#include <cassert>
using platf::dxgi::wgc_policy::event_rate_limit;
int main(){
 using namespace std::chrono;
 auto t=event_rate_limit::clock::time_point{};
 event_rate_limit steady(120);
 for(int i=0;i<10000;i++) assert(steady.admit(t+nanoseconds(i*1000000000LL/116)));
 event_rate_limit late(120);
 assert(late.admit(t)); assert(late.admit(t+milliseconds(20))); assert(late.admit(t+milliseconds(21)));
 assert(!late.admit(t+milliseconds(22))); assert(late.admit(t+milliseconds(30)));
 event_rate_limit fast(120);int n=0;
 for(int i=0;i<10000;i++) n+=fast.admit(t+milliseconds(i));
 assert(n>=1200 && n<=1202);
 assert(fast.admit(t+seconds(100)));assert(fast.admit(t+seconds(100)));assert(!fast.admit(t+seconds(100)));
 event_rate_limit zero(0);for(int i=0;i<100;i++)assert(zero.admit(t));
}
