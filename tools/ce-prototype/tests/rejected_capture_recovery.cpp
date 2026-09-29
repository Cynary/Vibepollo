// Native integration test: denied target, rejected DLL target, then live D3D target.
// Run with interactive process IDs; no protection is disabled by this test.
#define CE_NO_PROBE_MAIN
#include "../consumer.hpp"
int wmain(int argc,wchar_t** argv) {
 if(argc!=5)return 2;
 for(int i=1;i<=3;++i) {
   unsigned frames=0;
   auto start=GetTickCount64();
   int result=run_capture(wcstoul(argv[i],nullptr,10),argv[4],
     [&](auto*,auto*,auto*,bool,int64_t){++frames;return true;},[]{return true;},std::chrono::seconds(4));
   auto elapsed=GetTickCount64()-start;
   printf("CASE %d result=%d frames=%u elapsed_ms=%llu\n",i,result,frames,elapsed);fflush(stdout);
   if(i<3 && (result==0 || frames!=0 || elapsed>4000))return 10+i;
   if(i==3 && (result!=0 || frames<30))return 13;
 }
 return 0;
}
