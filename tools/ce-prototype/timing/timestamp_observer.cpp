#define CE_NO_PROBE_MAIN
#include "../consumer.hpp"
#include "hook/common/present_observer.h"
int wmain(int argc,wchar_t** argv) {
    if(argc!=5){fprintf(stderr,"usage: timestamp_observer PID hook.dll seconds output.csv\n");return 2;}
    const auto pid=wcstoul(argv[1],nullptr,10);const int seconds=_wtoi(argv[3]);
    if(!pid||seconds<1||seconds>120)return 2;
    try {
        Mapping mapping;wchar_t name[80];swprintf(name,80,L"Local\\CEPresentTiming-%lu",pid);
        mapping.create(name,sizeof(ce::present_observer::Shared));
        auto* shared=new(mapping.p) ce::present_observer::Shared{};
        int result=run_capture(pid,argv[2],{},[]{return true;},std::chrono::seconds(seconds),nullptr,true);
        shared->active.store(false,std::memory_order_release);
        auto events=shared->events.Snapshot();
        FILE* f=_wfopen(argv[4],L"w");if(!f)throw std::runtime_error("output file");
        fprintf(f,"qpc_us,kind,thread,id,object,a,b,c,flags\n");
        for(auto& e:events)fprintf(f,"%lld,%u,%u,%llu,%llu,%llu,%llu,%llu,%u\n",
            (long long)e.timeUs,unsigned(e.kind),e.thread,(unsigned long long)e.id,(unsigned long long)e.object,
            (unsigned long long)e.a,(unsigned long long)e.b,(unsigned long long)e.c,e.flags);
        const bool failed=ferror(f)!=0;const int close=fclose(f);
        fprintf(stderr,"observer events=%zu total=%llu dropped=%llu capture_result=%d\n",events.size(),
            (unsigned long long)shared->events.Total(),(unsigned long long)shared->events.Dropped(),result);
        return result||failed||close||events.empty()?1:0;
    }catch(std::exception const& e){fprintf(stderr,"ERROR: %s\n",e.what());return 1;}
}
