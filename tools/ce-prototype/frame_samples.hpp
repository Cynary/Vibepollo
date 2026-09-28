#pragma once
#include <fstream>
#include <filesystem>
#include <vector>

// Explicit, bounded pixel validation. Disabled in ordinary capture; synchronous
// readback perturbs timing, so these samples must never be used for benchmarks.
class FramePixelSamples {
  unsigned remaining=0;
  std::ofstream csv;
  std::vector<unsigned char> previous;
  ComPtr<ID3D11Texture2D> staging;
  D3D11_TEXTURE2D_DESC stagingDesc{};
 public:
  FramePixelSamples() {
    wchar_t requested[16];
    if(!GetEnvironmentVariableW(L"MOONMACHINE_CE_VALIDATE_FINAL_OUTPUT",requested,16))return;
    const int count=_wtoi(requested);
    if(count<1 || count>240)throw std::runtime_error("frame validation count outside 1..240");
    wchar_t directory[32768];
    if(!GetTempPathW(32768,directory))throw std::runtime_error("frame validation directory");
    auto path=std::filesystem::path(directory)/(L"ce-final-pixels-"+std::to_wstring(GetCurrentProcessId())+L".csv");
    csv.open(path);if(!csv)throw std::runtime_error("frame validation output");
    csv<<"frame,source_qpc,observed_qpc,flags,format,width,height,hash,changed_bytes\n";
    remaining=count;
  }
  void capture(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* source,const FrameSlot& slot) {
    if(!remaining || !(slot.captureFlags&SHARED_FRAME_CAPTURE_FINAL_PRESENTED_OUTPUT))return;
    D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);
    const unsigned pixelBytes=desc.Format==DXGI_FORMAT_R16G16B16A16_FLOAT?8:4;
    const unsigned width=std::min(256u,desc.Width),height=std::min(256u,desc.Height);
    if(!staging || stagingDesc.Format!=desc.Format || stagingDesc.Width!=width || stagingDesc.Height!=height) {
      staging.Reset();previous.clear();stagingDesc=desc;
      stagingDesc.Width=width;stagingDesc.Height=height;stagingDesc.BindFlags=0;
      stagingDesc.MiscFlags=0;stagingDesc.Usage=D3D11_USAGE_STAGING;
      stagingDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
      check(device->CreateTexture2D(&stagingDesc,nullptr,&staging),"validation staging");
    }
    const unsigned x=(desc.Width-width)/2,y=(desc.Height-height)/2;
    D3D11_BOX box{x,y,0,x+width,y+height,1};
    context->CopySubresourceRegion(staging.Get(),0,0,0,0,source,0,&box);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"validation map");
    std::vector<unsigned char> pixels(width*height*pixelBytes);
    for(unsigned row=0;row<height;row++)
      memcpy(pixels.data()+row*width*pixelBytes,static_cast<unsigned char*>(mapped.pData)+row*mapped.RowPitch,width*pixelBytes);
    context->Unmap(staging.Get(),0);
    uint64_t hash=14695981039346656037ull;unsigned changed=0;
    for(size_t i=0;i<pixels.size();i++){hash=(hash^pixels[i])*1099511628211ull;if(previous.size()==pixels.size() && pixels[i]!=previous[i])changed++;}
    LARGE_INTEGER now;QueryPerformanceCounter(&now);
    csv<<slot.frameIndex<<','<<slot.timestamp<<','<<now.QuadPart<<','<<slot.captureFlags<<','<<desc.Format<<','<<desc.Width<<','<<desc.Height<<','<<hash<<','<<changed<<'\n';
    previous=std::move(pixels);
    if(--remaining==0)csv.flush();
  }
};
