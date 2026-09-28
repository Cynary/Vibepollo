// Prototype wrapper for MoonDeckStream: publish Buddy's heartbeat only once
// the capture helper is ready to observe the game's creation. No fixed delay.
#include <windows.h>
#include <string>
#include <stdexcept>
#include "stream_ready.hpp"

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR commandLine,int) {
  if(!commandLine || !*commandLine)return ERROR_INVALID_PARAMETER;
  HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,ce_stream_ready_event);
  if(!ready)return static_cast<int>(GetLastError());
  const DWORD result=WaitForSingleObject(ready,45000);
  const DWORD waitError=result==WAIT_FAILED?GetLastError():ERROR_SUCCESS;
  CloseHandle(ready);
  if(result!=WAIT_OBJECT_0)return result==WAIT_TIMEOUT?ERROR_TIMEOUT:static_cast<int>(waitError);

  // Forward the original quoted command line unchanged, including arguments.
  std::wstring command(commandLine);
  STARTUPINFOW startup{};startup.cb=sizeof(startup);
  PROCESS_INFORMATION child{};
  if(!CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&child))
    return static_cast<int>(GetLastError());
  CloseHandle(child.hThread);
  WaitForSingleObject(child.hProcess,INFINITE);
  DWORD code=1;GetExitCodeProcess(child.hProcess,&code);CloseHandle(child.hProcess);
  return static_cast<int>(code);
}
