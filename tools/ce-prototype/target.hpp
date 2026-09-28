#pragma once
#include <set>

// Opt-in prototype target selection. Match a complete executable path and never
// inject an arbitrary foreground window or an already-running, unhooked game.
class CeTargetSelector {
  std::wstring path;
  FILETIME started{};
  ULONGLONG nextScan=0;
  std::set<std::pair<DWORD,ULONGLONG>> attempted;
 public:
  CeTargetSelector() {
    GetSystemTimeAsFileTime(&started);
    wchar_t value[32768];
    DWORD n=GetEnvironmentVariableW(L"MOONMACHINE_CE_TARGET_PATH",value,32768);
    if(n>=32768)throw std::runtime_error("direct capture target path too long");
    if(n)path=value;
  }
  bool enabled()const{return !path.empty();}
  DWORD next() {
    if(!enabled() || GetTickCount64()<nextScan)return 0;
    nextScan=GetTickCount64()+10;
    Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0)};
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
    for(BOOL ok=Process32FirstW(snapshot.v,&entry);ok;ok=Process32NextW(snapshot.v,&entry)) {
      const wchar_t* filename=wcsrchr(path.c_str(),L'\\');filename=filename?filename+1:path.c_str();
      if(_wcsicmp(entry.szExeFile,filename))continue;
      Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,entry.th32ProcessID)};
      if(!process.v)continue;
      wchar_t actual[32768];DWORD length=32768;FILETIME creation,exit,kernel,user;
      if(!QueryFullProcessImageNameW(process.v,0,actual,&length) || _wcsicmp(actual,path.c_str()) ||
         !GetProcessTimes(process.v,&creation,&exit,&kernel,&user))continue;
      const auto identity=std::make_pair(entry.th32ProcessID,(ULONGLONG(creation.dwHighDateTime)<<32)|creation.dwLowDateTime);
      if(attempted.count(identity))continue;
      wchar_t eventName[128];GenerateInjectReactivateEventName(eventName,128,entry.th32ProcessID);
      Handle resident{OpenEventW(EVENT_MODIFY_STATE,FALSE,eventName)};
      attempted.insert(identity);
      if(CompareFileTime(&creation,&started)<0 && !resident.v) {
        BOOST_LOG(warning)<<"Direct capture target was already running without a hook; keeping desktop capture until its next launch";
        continue;
      }
      return entry.th32ProcessID;
    }
    return 0;
  }
};
