#define CE_NO_PROBE_MAIN
#include "../consumer.hpp"
#include "hook/common/present_observer.h"
int wmain(int argc,wchar_t** argv) {
    if(argc!=5){fprintf(stderr,"usage: timestamp_observer PID hook.dll|--listen seconds output.csv\n");return 2;}
    const auto pid=wcstoul(argv[1],nullptr,10);const int seconds=_wtoi(argv[3]);
    if(!pid||seconds<1||seconds>120)return 2;
    try {
        Mapping mapping;wchar_t name[80];swprintf(name,80,L"Local\\CEPresentTiming-%lu",pid);
        const bool listen=wcscmp(argv[2],L"--listen")==0;
        ce::present_observer::Shared* shared=nullptr;
        if(listen) mapping.h.v=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,name);
        if(mapping.h.v) {
            mapping.p=MapViewOfFile(mapping.h.v,FILE_MAP_ALL_ACCESS,0,0,sizeof(ce::present_observer::Shared));
            if(!mapping.p)throw std::runtime_error("observer mapping view");
            shared=static_cast<ce::present_observer::Shared*>(mapping.p);
            if(!ce::present_observer::Accept(*shared)||shared->active.exchange(true))
                throw std::runtime_error("observer layout mismatch or reader still active");
        } else {
            mapping.create(name,sizeof(ce::present_observer::Shared));
            shared=new(mapping.p) ce::present_observer::Shared{};
        }
        struct DisableOnExit {
            ce::present_observer::Shared* shared;
            ~DisableOnExit(){shared->active.store(false,std::memory_order_release);}
        } disable{shared};
        int result=0;
        if(listen) {
            // The real capture helper owns injection/control in this mode.
            // Create this mapping first, then signal the orchestration process.
            wchar_t readyName[100];swprintf(readyName,100,L"Global\\CEPresentObserverReady-%lu",pid);
            Handle ready;ready.v=CreateEventW(nullptr,TRUE,FALSE,readyName);
            Handle target;target.v=OpenProcess(SYNCHRONIZE,FALSE,pid);
            if(!ready.v||!target.v)throw std::runtime_error("observer listen handles");
            SetEvent(ready.v);
            if(WaitForSingleObject(target.v,seconds*1000)==WAIT_FAILED)throw std::runtime_error("observer wait");
        } else {
            result=run_capture(pid,argv[2],{},[]{return true;},std::chrono::seconds(seconds),nullptr,true);
        }
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
