// Diagnostic only: known frame identities validate capture timestamps before
// using them as latency origins. All output is buffered until capture stops.
#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_6.h>
#include <windows.graphics.capture.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <wrl/client.h>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <vector>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
extern "C" HRESULT __stdcall CreateDirect3D11DeviceFromDXGIDevice(IDXGIDevice*, IInspectable**);
struct SurfaceAccess : IUnknown { virtual HRESULT __stdcall GetInterface(REFIID, void**) = 0; };
static const GUID surfaceGuid{0xa9b3d012,0x3df2,0x4ee3,{0xb8,0xd1,0x86,0x95,0xf4,0x57,0xd3,0xc1}};
static long long now() { LARGE_INTEGER v; QueryPerformanceCounter(&v); return v.QuadPart; }
static void check(HRESULT hr) { if (FAILED(hr)) throw winrt::hresult_error(hr); }
struct Present { unsigned id,count; long long begin,end,sync,ready; unsigned shown; HRESULT stats; };
struct Captured { long long entry,received,source,readback; unsigned id; bool valid; };
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR args,int) {
    struct Completion { HANDLE h=CreateEventW(nullptr,TRUE,FALSE,L"Global\\MoonmachineFrameProbeDone"); ~Completion(){if(h){SetEvent(h);CloseHandle(h);}} } done;
    const bool validate=wcsstr(args,L"--validate")!=nullptr;
    const bool immediate=wcsstr(args,L"--present0")!=nullptr;
    const bool capture=wcsstr(args,L"--no-capture")==nullptr;
    const bool hdr=wcsstr(args,L"--sdr")==nullptr;
    std::vector<Present> presents; presents.reserve(20000);
    std::vector<Captured> captured; captured.reserve(20000);
    std::mutex mutex; std::atomic<unsigned> skipped{0},errors{0};
    try {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        LARGE_INTEGER freq; QueryPerformanceFrequency(&freq);
        WNDCLASSW wc{}; wc.hInstance=instance; wc.lpfnWndProc=DefWindowProcW; wc.lpszClassName=L"FrameTimingIdentity";
        RegisterClassW(&wc);
        RECT r{};
        EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR h,HDC,LPRECT,LPARAM p)->BOOL {
            MONITORINFO m{sizeof(m)}; GetMonitorInfoW(h,&m);
            if(m.rcMonitor.right-m.rcMonitor.left==3840 && m.rcMonitor.bottom-m.rcMonitor.top==2160) {
                *reinterpret_cast<RECT*>(p)=m.rcMonitor;return FALSE;
            }return TRUE;
        },reinterpret_cast<LPARAM>(&r));
        if(r.right-r.left!=3840)throw std::runtime_error("4K monitor not present");
        HWND window=CreateWindowW(wc.lpszClassName,L"Capture timing identity test",WS_POPUP|WS_VISIBLE,r.left,r.top,r.right-r.left,r.bottom-r.top,nullptr,nullptr,instance,nullptr);
        ShowWindow(window,SW_SHOW);
        SetWindowPos(window,HWND_TOPMOST,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_SHOWWINDOW);
        SetForegroundWindow(window);
        ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> ctx;
        check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&ctx));
        ComPtr<ID3D11DeviceContext1> ctx1; check(ctx.As(&ctx1));
        ComPtr<IDXGIDevice> dxgi; check(device.As(&dxgi));
        ComPtr<IDXGIAdapter> adapter; check(dxgi->GetAdapter(&adapter));
        ComPtr<IDXGIFactory2> factory; check(adapter->GetParent(IID_PPV_ARGS(&factory)));
        DXGI_SWAP_CHAIN_DESC1 sd{}; sd.Width=r.right-r.left; sd.Height=r.bottom-r.top; sd.Format=DXGI_FORMAT_R10G10B10A2_UNORM;
        sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount=3; sd.SampleDesc.Count=1; sd.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> swap; check(factory->CreateSwapChainForHwnd(device.Get(),window,&sd,nullptr,nullptr,&swap));
        ComPtr<IDXGISwapChain3> swap3; check(swap.As(&swap3));
        check(swap3->SetColorSpace1(hdr?DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020:DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709));
        ComPtr<IDXGIDevice1> dxgi1; check(device.As(&dxgi1)); check(dxgi1->SetMaximumFrameLatency(1));
        ComPtr<ID3D11Texture2D> buffer; check(swap->GetBuffer(0,IID_PPV_ARGS(&buffer)));
        ComPtr<ID3D11RenderTargetView> rtv; check(device->CreateRenderTargetView(buffer.Get(),nullptr,&rtv));
        // Separate device/context: the callback never contends with rendering.
        ComPtr<ID3D11Device> capDev; ComPtr<ID3D11DeviceContext> capCtx;
        check(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&capDev,nullptr,&capCtx));
        ComPtr<IDXGIDevice> capDxgi; check(capDev.As(&capDxgi));
        winrt::com_ptr<IInspectable> inspectable;
        check(CreateDirect3D11DeviceFromDXGIDevice(capDxgi.Get(),inspectable.put()));
        auto capDevice=inspectable.as<IDirect3DDevice>();
        ComPtr<ID3D11Texture2D> staging;
        if(validate) {
            D3D11_TEXTURE2D_DESC desc{}; desc.Width=512; desc.Height=1; desc.MipLevels=desc.ArraySize=1; desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
            desc.SampleDesc.Count=1; desc.Usage=D3D11_USAGE_STAGING; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            check(capDev->CreateTexture2D(&desc,nullptr,&staging));
        }
        auto item=winrt::get_activation_factory<GraphicsCaptureItem,IGraphicsCaptureItemInterop>();
        GraphicsCaptureItem target{nullptr}; check(item->CreateForMonitor(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),winrt::guid_of<GraphicsCaptureItem>(),winrt::put_abi(target)));
        auto pool=Direct3D11CaptureFramePool::CreateFreeThreaded(capDevice,DirectXPixelFormat::R16G16B16A16Float,3,target.Size());
        auto session=pool.CreateCaptureSession(target); session.IsCursorCaptureEnabled(false); session.MinUpdateInterval(winrt::Windows::Foundation::TimeSpan{0});
        auto token=pool.FrameArrived([&](auto const& sender,auto const&) {
            const auto entry=now();
            try {
                auto frame=sender.TryGetNextFrame(); if(!frame)return;
                Captured c{entry,now(),frame.SystemRelativeTime().count(),0,0,false};
                if(validate) {
                    ComPtr<SurfaceAccess> access; ComPtr<ID3D11Texture2D> tex;
                    check(reinterpret_cast<IUnknown*>(winrt::get_abi(frame.Surface()))->QueryInterface(surfaceGuid,reinterpret_cast<void**>(access.GetAddressOf())));
                    check(access->GetInterface(IID_ID3D11Texture2D,reinterpret_cast<void**>(tex.GetAddressOf())));
                    D3D11_BOX box{0,16,0,512,17,1}; capCtx->CopySubresourceRegion(staging.Get(),0,0,0,0,tex.Get(),0,&box);
                    D3D11_MAPPED_SUBRESOURCE mapped{}; check(capCtx->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));
                    auto pixels=static_cast<const unsigned short*>(mapped.pData); c.valid=true;
                    for(unsigned bit=0;bit<16;++bit) {
                        auto a=pixels[(bit*32+8)*4],b=pixels[(bit*32+24)*4];
                        if(a==b || (a&0x8000) || (b&0x8000))c.valid=false;
                        if(a>b)c.id|=1u<<bit;
                    }
                    capCtx->Unmap(staging.Get(),0); c.readback=now();
                }
                std::unique_lock lock(mutex,std::try_to_lock);
                if(lock && captured.size()<20000)captured.push_back(c); else ++skipped;
            } catch(...) { ++errors; }
        });
        if(capture)session.StartCapture();
        const auto origin=now(),end=origin+freq.QuadPart*30; MSG msg;
        HANDLE timer=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);
        if(!timer)throw std::runtime_error("timer");
        for(unsigned id=1;now()<end && id<20000;++id) {
            while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
            if(immediate) {
                auto remaining=origin+(id-1)*freq.QuadPart/116-now();
                if(remaining>0) { LARGE_INTEGER due;due.QuadPart=-remaining*10000000/freq.QuadPart;
                    if(!SetWaitableTimer(timer,&due,0,nullptr,nullptr,FALSE)||WaitForSingleObject(timer,1000)!=WAIT_OBJECT_0)throw std::runtime_error("pace wait"); }
            }
            const float gray[]{.2f,.2f,.2f,1.f}; ctx->ClearRenderTargetView(rtv.Get(),gray);
            for(unsigned bit=0;bit<16;++bit)for(unsigned inverse=0;inverse<2;++inverse){
                const bool high=bool(id&(1u<<bit)) != bool(inverse); float v=high?.7f:.1f;
                const float color[]{v,v,v,1}; D3D11_RECT rect{LONG(bit*32+inverse*16),0,LONG(bit*32+inverse*16+16),32};
                ctx1->ClearView(rtv.Get(),color,&rect,1);
            }
            Present p{}; p.id=id; p.begin=now();check(swap->Present(immediate?0:1,0));p.end=now();
            swap->GetLastPresentCount(&p.count); DXGI_FRAME_STATISTICS stats{}; p.stats=swap->GetFrameStatistics(&stats);
            p.sync=stats.SyncQPCTime.QuadPart; p.shown=stats.PresentCount; presents.push_back(p);
        }
        session.Close(); pool.FrameArrived(token); pool.Close();
        // Closing the pool stops callbacks; lock also serializes any last writer.
        std::lock_guard lock(mutex);
        FILE* f=fopen("frame-probe-present.csv","w"); if(!f)throw std::runtime_error("present output");
        fprintf(f,"id,present_count,begin_qpc,end_qpc,shown_count,sync_qpc,stats_hr\n");
        for(auto& p:presents)fprintf(f,"%u,%u,%lld,%lld,%u,%lld,%ld\n",p.id,p.count,p.begin,p.end,p.shown,p.sync,long(p.stats)); fclose(f);
        f=fopen("frame-probe-wgc.csv","w");if(!f)throw std::runtime_error("WGC output");
        fprintf(f,"entry_qpc,received_qpc,source_100ns,readback_qpc,id,valid\n");
        for(auto& c:captured)fprintf(f,"%lld,%lld,%lld,%lld,%u,%d\n",c.entry,c.received,c.source,c.readback,c.id,c.valid);fclose(f);
        f=fopen("frame-probe-result.txt","w");if(!f)throw std::runtime_error("result output");
        fprintf(f,"PASS frequency=%lld present=%zu captured=%zu skipped=%u errors=%u validate=%d hdr=%d width=%u height=%u pid=%lu immediate=%d\n",freq.QuadPart,presents.size(),captured.size(),skipped.load(),errors.load(),validate,hdr,sd.Width,sd.Height,GetCurrentProcessId(),immediate);fclose(f);
        CloseHandle(timer);DestroyWindow(window);return errors?2:0;
    } catch(winrt::hresult_error const& e) { FILE* f=fopen("frame-probe-result.txt","w");if(f){fprintf(f,"FAIL HRESULT=%08lx\n",(unsigned long)e.code());fclose(f);}return 1; }
      catch(...) { FILE* f=fopen("frame-probe-result.txt","w");if(f){fputs("FAIL exception\n",f);fclose(f);}return 1; }
}
