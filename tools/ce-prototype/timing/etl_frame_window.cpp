#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <tdh.h>
#include <cstdio>
#include <vector>
#include <cstring>
#include <map>
#include <cstdlib>
static long long beginQpc=0,endQpc=0;
static unsigned targetPid=0;
static std::map<unsigned,unsigned long long> counts;
static void WINAPI event(PEVENT_RECORD r){auto g=r->EventHeader.ProviderId.Data1;auto id=r->EventHeader.EventDescriptor.Id; if(g!=0x802ec45a && g!=0x9e9bba3c && g!=0xca11c036 && g!=0x8c416c79)return; if(r->EventHeader.TimeStamp.QuadPart<beginQpc||r->EventHeader.TimeStamp.QuadPart>endQpc)return;
 if(targetPid){
 if(g==0xca11c036 && (r->EventHeader.ProcessId!=targetPid || (id!=42&&id!=43)))return;
 if(g==0x8c416c79 && (r->EventHeader.ProcessId!=targetPid || id!=201))return;
 if(g==0x9e9bba3c && id!=196&&id!=467)return;
 if(g==0x802ec45a && id!=171&&id!=172&&id!=215)return;
 }
 ULONG len=0;TdhGetEventInformation(r,0,nullptr,nullptr,&len);std::vector<unsigned char> buf(len);auto info=(PTRACE_EVENT_INFO)buf.data();auto status=TdhGetEventInformation(r,0,nullptr,info,&len);
 printf("%lld,%lu,%lu,%08lx,%u,%u",r->EventHeader.TimeStamp.QuadPart,r->EventHeader.ProcessId,r->EventHeader.ThreadId,g,id,r->EventHeader.EventDescriptor.Opcode);
 if(!status){for(unsigned i=0;i<info->TopLevelPropertyCount;i++){auto &pi=info->EventPropertyInfoArray[i];auto name=(wchar_t*)(buf.data()+pi.NameOffset);PROPERTY_DATA_DESCRIPTOR d{};d.PropertyName=(ULONGLONG)name;d.ArrayIndex=ULONG_MAX;ULONG size=0;if(TdhGetPropertySize(r,0,nullptr,1,&d,&size)!=0||size>4096)continue;std::vector<unsigned char> data(size);if(TdhGetProperty(r,0,nullptr,1,&d,size,data.data())!=0)continue;printf(",%ls=",name);if(size==1||size==2||size==4||size==8){unsigned long long v=0;memcpy(&v,data.data(),size);printf("%llu",v);}else{for(auto c:data)printf("%02x",c);}}}else printf(",tdh_error=%lu",status);printf("\n");counts[g]++;}
int main(int argc,char**argv){if(argc!=4&&argc!=5)return 2;if(argc==5)targetPid=std::strtoul(argv[4],nullptr,10);beginQpc=std::strtoll(argv[2],nullptr,10);endQpc=std::strtoll(argv[3],nullptr,10);EVENT_TRACE_LOGFILEA l{};l.LogFileName=argv[1];l.ProcessTraceMode=PROCESS_TRACE_MODE_EVENT_RECORD|PROCESS_TRACE_MODE_RAW_TIMESTAMP;l.EventRecordCallback=event;auto h=OpenTraceA(&l);if(h==INVALID_PROCESSTRACE_HANDLE)return GetLastError();auto ret=ProcessTrace(&h,1,nullptr,nullptr);CloseTrace(h);for(auto [g,n]:counts)fprintf(stderr,"%08x %llu\n",g,n);return ret;}
