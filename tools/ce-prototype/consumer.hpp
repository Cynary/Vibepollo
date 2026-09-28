#pragma once
// Experimental consumer for CaptureEngine v0.1.6772's shared GPU texture ABI.
// No changes to the installed streaming service or game files.
#include <windows.h>
#include <tlhelp32.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <stdexcept>
#include <new>
#include <functional>
#include <chrono>
#include <optional>
#include "common/shared_defs.h"
#include "passive_config.hpp"
using Microsoft::WRL::ComPtr;
uint32_t GetCurrentBuildNumber() noexcept { return 6772; }
const char* GetCaptureVersion() noexcept { return "probe"; }
const char* GetBuildTimestamp() noexcept { return "prototype"; }
void check(HRESULT hr,const char* what) { if(FAILED(hr)) {char b[200]; snprintf(b,sizeof(b),"%s: 0x%08lx",what,(unsigned long)hr); throw std::runtime_error(b);} }
struct Handle {
 HANDLE v=nullptr;
 void reset(HANDLE value=nullptr){if(v && v!=INVALID_HANDLE_VALUE)CloseHandle(v);v=value;}
 ~Handle(){reset();}
};
bool same_kernel_object(HANDLE a,HANDLE b) {
 using Compare=BOOL(WINAPI*)(HANDLE,HANDLE);
 static const auto compare=reinterpret_cast<Compare>(GetProcAddress(GetModuleHandleW(L"kernelbase.dll"),"CompareObjectHandles"));
 if(!compare)throw std::runtime_error("CompareObjectHandles unavailable");
 return a && b && compare(a,b);
}
struct Mapping { Handle h; void* p=nullptr; ~Mapping(){if(p) UnmapViewOfFile(p);} void create(const wchar_t* name,size_t n) {
 h.v=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,(DWORD)n,name);
 if(!h.v || GetLastError()==ERROR_ALREADY_EXISTS) throw std::runtime_error("mapping unavailable/already owned");
 p=MapViewOfFile(h.v,FILE_MAP_ALL_ACCESS,0,0,n); if(!p) throw std::runtime_error("MapViewOfFile"); } };
uintptr_t wow64_loader(HANDLE proc,DWORD pid) {
 Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid)};
 MODULEENTRY32W module{};module.dwSize=sizeof(module);
 for(BOOL ok=Module32FirstW(snapshot.v,&module);ok;ok=Module32NextW(snapshot.v,&module)) {
  if(_wcsicmp(module.szModule,L"kernelbase.dll"))continue;
  uintptr_t base=(uintptr_t)module.modBaseAddr;
  auto read=[&](uint32_t rva,void* out,size_t bytes){SIZE_T n=0;
   if(rva>module.modBaseSize||bytes>module.modBaseSize-rva || !ReadProcessMemory(proc,(void*)(base+rva),out,bytes,&n)||n!=bytes)throw std::runtime_error("WOW64 export read bounds");};
  IMAGE_DOS_HEADER dos{};read(0,&dos,sizeof(dos));
  if(dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<0)continue;
  IMAGE_NT_HEADERS32 nt{};read(dos.e_lfanew,&nt,sizeof(nt));
  if(nt.Signature!=IMAGE_NT_SIGNATURE||nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC)continue;
  auto dir=nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];IMAGE_EXPORT_DIRECTORY exp{};read(dir.VirtualAddress,&exp,sizeof(exp));
  if(exp.NumberOfNames>65536||exp.NumberOfFunctions>65536)throw std::runtime_error("invalid WOW64 export count");
  for(DWORD i=0;i<exp.NumberOfNames;i++) {
   DWORD rva;read(exp.AddressOfNames+4*i,&rva,4);char name[128]{};read(rva,name,sizeof(name));name[127]=0;
   if(strcmp(name,"LoadLibraryW"))continue;
   WORD ordinal;read(exp.AddressOfNameOrdinals+2*i,&ordinal,2);if(ordinal>=exp.NumberOfFunctions)throw std::runtime_error("export ordinal");
   DWORD fn;read(exp.AddressOfFunctions+4*ordinal,&fn,4);
   if(fn>=module.modBaseSize || (fn>=dir.VirtualAddress && fn-dir.VirtualAddress<dir.Size))throw std::runtime_error("forwarded WOW64 loader unsupported");
   return base+fn;
  }
 }
 return 0; // A newly created process may not have mapped kernelbase yet.
}
void inject(DWORD pid,const std::wstring& configuredPath) {
 Handle proc{OpenProcess(SYNCHRONIZE|PROCESS_CREATE_THREAD|PROCESS_QUERY_INFORMATION|PROCESS_VM_OPERATION|PROCESS_VM_WRITE|PROCESS_VM_READ,FALSE,pid)};
 if(!proc.v) throw std::runtime_error("OpenProcess inject");
 BOOL wow64=FALSE;if(!IsWow64Process(proc.v,&wow64))throw std::runtime_error("IsWow64Process");
 std::wstring path=configuredPath;uintptr_t remote=0;
 if(wow64){
 auto pos=path.rfind(L"capture_hook_x64.dll");if(pos==std::wstring::npos)throw std::runtime_error("expected pinned hook filename");path.replace(pos,20,L"capture_hook_x86.dll");
 const auto loaderDeadline=GetTickCount64()+5000;
 while(!(remote=wow64_loader(proc.v,pid))) {
   if(GetTickCount64()>=loaderDeadline)throw std::runtime_error("WOW64 loader initialization timeout");
   if(WaitForSingleObject(proc.v,10)!=WAIT_TIMEOUT)throw std::runtime_error("target exited before WOW64 loader initialization");
 }
 }
 else {
 auto fn=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW"); HMODULE owner=nullptr;
 GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)fn,&owner);
 wchar_t modulePath[MAX_PATH]; GetModuleFileNameW(owner,modulePath,MAX_PATH);
 const wchar_t* name=wcsrchr(modulePath,L'\\'); name=name?name+1:modulePath;
 // Process-start notification can precede kernelbase mapping. Wait for the
 // loader module itself, rather than delaying injection by a fixed interval.
 const auto loaderDeadline=GetTickCount64()+5000;
 while(!remote) {
   Handle snap{CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,pid)};
   MODULEENTRY32W m{};m.dwSize=sizeof(m);
   for(BOOL ok=Module32FirstW(snap.v,&m);ok;ok=Module32NextW(snap.v,&m))
     if(!_wcsicmp(m.szModule,name)) remote=(uintptr_t)m.modBaseAddr+((uintptr_t)fn-(uintptr_t)owner);
   if(remote)break;
   if(GetTickCount64()>=loaderDeadline)throw std::runtime_error("target loader module initialization timeout");
   if(WaitForSingleObject(proc.v,10)!=WAIT_TIMEOUT)throw std::runtime_error("target exited before loader initialization");
 }
 }
 size_t bytes=(path.size()+1)*sizeof(wchar_t); void* ptr=VirtualAllocEx(proc.v,nullptr,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
 if(!ptr) throw std::runtime_error("VirtualAllocEx");
 if(!WriteProcessMemory(proc.v,ptr,path.c_str(),bytes,nullptr)) throw std::runtime_error("WriteProcessMemory");
 Handle thread{CreateRemoteThread(proc.v,nullptr,0,(LPTHREAD_START_ROUTINE)remote,ptr,0,nullptr)};
 if(!thread.v) throw std::runtime_error("CreateRemoteThread");
 if(WaitForSingleObject(thread.v,15000)!=WAIT_OBJECT_0) throw std::runtime_error("loader timeout; leaving argument valid");
 DWORD result=0; GetExitCodeThread(thread.v,&result); VirtualFreeEx(proc.v,ptr,0,MEM_RELEASE);
 if(!result) throw std::runtime_error("LoadLibraryW failed");
}
#include "frame_samples.hpp"
#include "frame_timing_samples.hpp"

using FrameConsumer = std::function<bool(ID3D11Device*, ID3D11DeviceContext*, ID3D11Texture2D*, bool, int64_t)>;
int run_capture(DWORD pid, const wchar_t* hookPath, FrameConsumer consumer,
                std::function<bool()> alive,
                std::optional<std::chrono::milliseconds> duration = std::nullopt,
                ID3D11Device* importDevice = nullptr,
                bool timestampOnly = false) {
 SharedMemoryLayout* shm=nullptr;
 try {
 FramePixelSamples pixelSamples;
 FrameTimingSamples timingSamples(pid);
  if(duration && duration->count() <= 0) throw std::runtime_error("capture duration must be positive");
 Mapping mainMap,discMap; wchar_t name[128]; GenerateSharedMemName(name,128,GetCurrentProcessId());mainMap.create(name,sizeof(SharedMemoryLayout));
 shm=new(mainMap.p) SharedMemoryLayout{}; shm->SetHostPID(GetCurrentProcessId());
 initialize_passive_capture_config(shm->graphicsConfig);
 shm->runtimeState.captureRequested.store(!timestampOnly);shm->runtimeState.SetRuntimeFlag(kCaptureRuntimeFlagInjectVideoCaptureRequested,!timestampOnly);
 shm->fpsLimiter.SetCaptureFps(timestampOnly ? 0 : 240);shm->SetDebugLogging(true);shm->SetLogLevel(static_cast<LogLevel>(3));
 shm->structSize.store(sizeof(*shm));shm->abiSignature.store(SHARED_MEMORY_ABI_SIGNATURE);shm->SetMagic(SHARED_MEMORY_MAGIC);
 discMap.create(SHARED_MEM_DISCOVERY,sizeof(DiscoveryInfo));auto disc=new(discMap.p) DiscoveryInfo{};
 struct StopCapture {
   SharedMemoryLayout* s; DiscoveryInfo* d; DWORD pid; bool stopped=false;
   void shutdown() noexcept {
     if(stopped)return;
     stopped=true;
     s->runtimeState.captureRequested.store(false);
     s->SetRequestExit(true);
     wchar_t eventName[128]; GenerateInjectDormantEventName(eventName,128,pid);
     Handle dormant{OpenEventW(SYNCHRONIZE,FALSE,eventName)};
     Handle process{OpenProcess(SYNCHRONIZE,FALSE,pid)};
     if(dormant.v && process.v) {
       HANDLE events[]={dormant.v,process.v};
       DWORD result=WaitForMultipleObjects(2,events,FALSE,2000);
       if(result==WAIT_TIMEOUT)fprintf(stderr,"CE capture shutdown: hook did not become dormant in 2s\n");
     }
     d->SetMagic(0);
   }
   ~StopCapture(){shutdown();}
 } stop{shm,disc,pid};
 Handle nameProcess{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid)};
 wchar_t exePath[32768];DWORD pathLength=32768;
 if(!nameProcess.v||!QueryFullProcessImageNameW(nameProcess.v,0,exePath,&pathLength))throw std::runtime_error("target name");
 const wchar_t* exe=wcsrchr(exePath,L'\\');exe=exe?exe+1:exePath;
 WideCharToMultiByte(CP_UTF8,0,exe,-1,disc->processWhitelist,sizeof(disc->processWhitelist)-1,nullptr,nullptr);
 GetTempPathA(sizeof(disc->logsPath),disc->logsPath);
 snprintf(shm->logFilePath,sizeof(shm->logFilePath),"%s\\probe-hook.log",disc->logsPath);
 disc->profileTargetPid.store(pid);disc->SetAbiSignature(SHARED_MEMORY_ABI_SIGNATURE);disc->SetInjectPid(GetCurrentProcessId());disc->SetMagic(DISCOVERY_MAGIC);
 GenerateInjectFrameReadyEventName(name,128,GetCurrentProcessId()); Handle ready{CreateEventW(nullptr,FALSE,FALSE,name)};
 Handle target{OpenProcess(PROCESS_DUP_HANDLE|PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid)};
 if(!target.v||!ready.v) throw std::runtime_error("target/event open failed");
 wchar_t path[32768]; if(!GetFullPathNameW(hookPath,32768,path,nullptr)) throw std::runtime_error("hook path");
 GenerateInjectReactivateEventName(name,128,pid);
 Handle resident{OpenEventW(EVENT_MODIFY_STATE,FALSE,name)};
 if(resident.v) {
   if(!SetEvent(resident.v)) throw std::runtime_error("resident hook reactivation");
 } else {
   if(GetLastError()!=ERROR_FILE_NOT_FOUND) throw std::runtime_error("hook lifecycle event access");
   inject(pid,path);
 }
 Handle patternStart{OpenEventW(EVENT_MODIFY_STATE,FALSE,L"Local\\MoonmachineCapturePatternStart")};
 if(patternStart.v) {
 auto deadline=GetTickCount64()+15000;
 while(shm->GetSourcePid()!=pid && GetTickCount64()<deadline) { if(WaitForSingleObject(target.v,10)==WAIT_OBJECT_0) throw std::runtime_error("target exited during initialization"); }
 if(shm->GetSourcePid()!=pid) throw std::runtime_error("hook initialization timeout");
 SetEvent(patternStart.v); }
 if(timestampOnly) {
   if(!duration)throw std::runtime_error("timestamp observer requires a bounded duration");
   // No GPU device, texture import, recording request, or capture-rate override.
   WaitForSingleObject(target.v,static_cast<DWORD>(duration->count()));
   if(shm->frameRing.load_write_index_acquire()!=0)throw std::runtime_error("observer unexpectedly captured textures");
   return 0;
 }
 ComPtr<ID3D11Device> device;ComPtr<ID3D11Device1> device1;ComPtr<ID3D11Device5> device5;ComPtr<ID3D11DeviceContext> context;
 ComPtr<ID3D11Fence> fence; uint64_t fenceRemote=0; ComPtr<ID3D11Texture2D> textures[SHARED_TEXTURE_SLOT_COUNT];
 Handle textureObjects[SHARED_TEXTURE_SLOT_COUNT], fenceObject;
 Handle fenceEvent{CreateEventW(nullptr,FALSE,FALSE,nullptr)};
 // Stop producer work before releasing this consumer's imported GPU resources.
 struct QuiesceBeforeResources { StopCapture& stop; ~QuiesceBeforeResources(){stop.shutdown();} } quiesce{stop};
 LARGE_INTEGER freq; QueryPerformanceFrequency(&freq);
 const auto started = std::chrono::steady_clock::now();
 unsigned count=0; ULONGLONG diagnosticAt=GetTickCount64()+1000;
 puts("frame,published_qpc,observed_qpc,ready_qpc,width,height,dxgi_format,hdr,final_output");fflush(stdout);
 // Streaming ends with the session or target process, never an arbitrary probe timeout.
 while(alive() && (!duration || std::chrono::steady_clock::now()-started < *duration) &&
       WaitForSingleObject(target.v,0)==WAIT_TIMEOUT) {
 auto &ring=shm->frameRing;auto r=ring.load_read_index_acquire();auto w=ring.load_write_index_acquire();
 if(r==w){
 if(count==0 && std::chrono::steady_clock::now()-started>=std::chrono::seconds(30))
   throw std::runtime_error("direct capture produced no frame within 30 seconds");
#ifdef CE_EMBEDDED
 if(GetTickCount64()>=diagnosticAt){BOOST_LOG(info)<<"CE waiting: source="<<shm->GetSourcePid()<<" format="<<shm->GetFormat()<<" write="<<w<<" read="<<r<<" abi="<<SHARED_MEMORY_ABI_SIGNATURE;diagnosticAt=GetTickCount64()+5000;}
#endif
 WaitForSingleObject(ready.v,250);continue;}if(w-r>FRAME_RING_SIZE)throw std::runtime_error("invalid ring distance");
 auto &slot=ring.slots[r%FRAME_RING_SIZE];if(!slot.valid.load(std::memory_order_acquire)) throw std::runtime_error("unpublished slot");
 int idx=slot.textureIndex;if(idx<0||idx>=SHARED_TEXTURE_SLOT_COUNT||slot.sourcePid!=pid)throw std::runtime_error("invalid source/slot");
 LARGE_INTEGER observed,done;QueryPerformanceCounter(&observed);
 auto duplicate=[&](uint64_t h){HANDLE copy=nullptr;if(!DuplicateHandle(target.v,(HANDLE)(uintptr_t)h,GetCurrentProcess(),&copy,0,FALSE,DUPLICATE_SAME_ACCESS))throw std::runtime_error("DuplicateHandle");return copy;};
 if(!device) {
 // The texture handle is authoritative; asynchronous metrics publication may
 // still contain the previous session's (or zero) adapter identifier.
 ComPtr<IDXGIFactory4> factory;check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"factory");
 Handle firstTexture{duplicate(shm->GetSharedHandle(idx))};LUID producerLuid{};
 check(factory->GetSharedResourceAdapterLuid(firstTexture.v,&producerLuid),"shared texture GPU");
 ComPtr<IDXGIAdapter1> adapter;
 check(factory->EnumAdapterByLuid(producerLuid,IID_PPV_ARGS(&adapter)),"producer GPU lookup");
 if(!adapter)throw std::runtime_error("producer GPU not found");
 if(importDevice) {
   ComPtr<IDXGIDevice> dxgiDevice;check(importDevice->QueryInterface(IID_PPV_ARGS(&dxgiDevice)),"import device GPU");
   ComPtr<IDXGIAdapter> outputAdapter;check(dxgiDevice->GetAdapter(&outputAdapter),"import adapter");
   DXGI_ADAPTER_DESC outputDesc{};check(outputAdapter->GetDesc(&outputDesc),"import adapter descriptor");
   if(outputDesc.AdapterLuid.LowPart!=producerLuid.LowPart || outputDesc.AdapterLuid.HighPart!=producerLuid.HighPart)
     throw std::runtime_error("direct capture source and stream use different GPUs");
   device=importDevice;device->GetImmediateContext(&context);
 } else {
   check(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context),"device");
 }
 check(device.As(&device1),"device1");check(device.As(&device5),"device5");
 }

 // Numeric NT handle values can be reused after a producer resize. Retain
 // duplicated handles and compare object identity, not the producer's number.
 // The producer keeps the current generation alive until this slot is acked.
 auto th=shm->GetSharedHandle(idx);Handle textureCopy{duplicate(th)};
 if(!textures[idx] || !same_kernel_object(textureObjects[idx].v,textureCopy.v)) {
   textures[idx].Reset();
   check(device1->OpenSharedResource1(textureCopy.v,IID_PPV_ARGS(&textures[idx])),"texture import");
   textureObjects[idx].reset(textureCopy.v);textureCopy.v=nullptr;
 }
 auto fh=shm->GetFenceShareHandle();ComPtr<IDXGIKeyedMutex> mutex;
 if(fh) {
   Handle fenceCopy{duplicate(fh)};
   if(!fence || !same_kernel_object(fenceObject.v,fenceCopy.v)) {
     fence.Reset();check(device5->OpenSharedFence(fenceCopy.v,IID_PPV_ARGS(&fence)),"fence import");
     fenceObject.reset(fenceCopy.v);fenceCopy.v=nullptr;fenceRemote=fh;
     fprintf(stderr,"CE fence import handle=%llu frame=%u required=%llu completed=%llu\n",
       (unsigned long long)fh,slot.frameIndex,(unsigned long long)slot.fenceValue,
       (unsigned long long)fence->GetCompletedValue());
   }
   if(fence->GetCompletedValue()<slot.fenceValue) {
     check(fence->SetEventOnCompletion(slot.fenceValue,fenceEvent.v),"fence event");
     if(WaitForSingleObject(fenceEvent.v,2000)!=WAIT_OBJECT_0) {
       fprintf(stderr,"CE fence timeout frame=%u ring=%u imported=%llu current=%llu required=%llu completed=%llu\n",
         slot.frameIndex,r,(unsigned long long)fenceRemote,(unsigned long long)shm->GetFenceShareHandle(),
         (unsigned long long)slot.fenceValue,(unsigned long long)fence->GetCompletedValue());
       throw std::runtime_error("GPU fence timeout");
     }
   }
 } else {
   check(textures[idx].As(&mutex),"mutex");
   HRESULT hr=mutex->AcquireSync(1,2000);
   if(hr!=S_OK)throw std::runtime_error("keyed mutex timeout/abandoned");
 }
 QueryPerformanceCounter(&done); D3D11_TEXTURE2D_DESC desc;textures[idx]->GetDesc(&desc);
 // Readback is validation only, on the first three frames; never part of a streaming path.
 if(count<3 && patternStart.v && !consumer) {
 auto stagingDesc=desc;stagingDesc.Usage=D3D11_USAGE_STAGING;stagingDesc.BindFlags=0;stagingDesc.MiscFlags=0;stagingDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
 ComPtr<ID3D11Texture2D> staging;check(device->CreateTexture2D(&stagingDesc,nullptr,&staging),"validation staging");
 context->CopyResource(staging.Get(),textures[idx].Get());D3D11_MAPPED_SUBRESOURCE map{};check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map),"validation readback");
 uint32_t pixel=*(uint32_t*)map.pData;context->Unmap(staging.Get(),0);
 fprintf(stderr,"pixel[%u]=R%u G%u B%u A%u format=%u HDR=%u\n",count,pixel&1023,(pixel>>10)&1023,(pixel>>20)&1023,pixel>>30,desc.Format,shm->GetIsHDR());
 if(desc.Format!=DXGI_FORMAT_R10G10B10A2_UNORM || (pixel&1023)!=257 || ((pixel>>10)&1023)!=513 || !shm->GetIsHDR())throw std::runtime_error("HDR10 pixel preservation failed");
 }
 if(!consumer) printf("%u,%lld,%lld,%lld,%u,%u,%u,%u,%u\n",slot.frameIndex,(long long)slot.timestamp,(long long)observed.QuadPart,(long long)done.QuadPart,desc.Width,desc.Height,desc.Format,shm->GetIsHDR(),!!(slot.captureFlags&SHARED_FRAME_CAPTURE_FINAL_PRESENTED_OUTPUT));
 bool keep=true; if(consumer) keep=consumer(device.Get(),context.Get(),textures[idx].Get(),shm->GetIsHDR(),slot.timestamp);
 LARGE_INTEGER published;QueryPerformanceCounter(&published);
 timingSamples.record(slot,w-r,desc.Format,observed.QuadPart,done.QuadPart,published.QuadPart);
 pixelSamples.capture(device.Get(),context.Get(),textures[idx].Get(),slot);
 if(mutex)check(mutex->ReleaseSync(0),"release mutex");slot.valid.store(0,std::memory_order_release);ring.store_ingest_index_release(r+1);ring.store_read_index_release(r+1);count++;
#ifdef CE_EMBEDDED
 if(count%600==0) BOOST_LOG(info)<<"CE frames="<<count<<" final-output="<<!!(slot.captureFlags&SHARED_FRAME_CAPTURE_FINAL_PRESENTED_OUTPUT);
#endif
 if(!keep) break;
 }
 shm->runtimeState.captureRequested.store(false);shm->SetRequestExit(true);disc->SetMagic(0);
 fprintf(stderr,"frames=%u qpc_frequency=%lld abi=%08x\n",count,(long long)freq.QuadPart,SHARED_MEMORY_ABI_SIGNATURE);return count?0:3;
 } catch(const std::exception&e){fprintf(stderr,"ERROR: %s\n",e.what());
#ifdef CE_EMBEDDED
 BOOST_LOG(error) << "CaptureEngine prototype: " << e.what() << " Win32=" << GetLastError();
#endif
 return 1;}
}

#if !defined(CE_EMBEDDED) && !defined(CE_NO_PROBE_MAIN)
int wmain(int argc,wchar_t**argv) {
 if(argc!=4){fprintf(stderr,"usage: probe target-pid hook-dll seconds\n");return 2;}
 const int seconds=_wtoi(argv[3]);
 if(seconds<1 || seconds>600){fprintf(stderr,"seconds outside 1..600\n");return 2;}
 return run_capture(wcstoul(argv[1],nullptr,10),argv[2],{},[]{return true;},
                    std::chrono::seconds(seconds));
}
#endif
