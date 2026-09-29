#pragma once
#include <set>
#include "target_policy.hpp"

// Opt-in target selection. Match a complete executable path and never
// inject an arbitrary foreground window or an already-running, unhooked game.
class CeTargetSelector {
  std::vector<std::wstring> paths;
  FILETIME started{};
  ULONGLONG nextScan=0;
  std::set<std::pair<DWORD,ULONGLONG>> attempted;
 public:
  explicit CeTargetSelector(std::wstring configured = {}) {
    GetSystemTimeAsFileTime(&started);
    paths=direct_capture::parse_paths(configured);
  }
  bool enabled()const{return !paths.empty();}
  DWORD next() {
    if(!enabled() || GetTickCount64()<nextScan)return 0;
    nextScan=GetTickCount64()+10;
    Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0)};
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
    for(BOOL ok=Process32FirstW(snapshot.v,&entry);ok;ok=Process32NextW(snapshot.v,&entry)) {
      const auto selected=std::find_if(paths.begin(),paths.end(),[&](const std::wstring& path){
        const auto filename=path.substr(path.find_last_of(L'\\')+1);
        return _wcsicmp(entry.szExeFile,filename.c_str())==0;
      });
      if(selected==paths.end())continue;
      Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,entry.th32ProcessID)};
      if(!process.v)continue;
      wchar_t actual[32768];DWORD length=32768;FILETIME creation,exit,kernel,user;
      if(!QueryFullProcessImageNameW(process.v,0,actual,&length) || std::none_of(paths.begin(),paths.end(),[&](const std::wstring& path){return _wcsicmp(actual,path.c_str())==0;}) ||
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
