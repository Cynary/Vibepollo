// Experimental capture path. Only enabled by an explicit helper environment variable.
// CaptureEngine hook/ABI pinned to v0.1.6772 (MIT); see LICENSE.capture-engine.
#define CE_EMBEDDED
#include "consumer.hpp"
#include <d3dcompiler.h>
#include "colour_shader.hpp"
#include "colour_view.hpp"
#include "../../src/wgc_stage_trace.h"

class CeOutput {
  SharedResourceManager ownedResources;
  SharedResourceManager& resources;
  bool externalResources=false;
  const bool stageTracing=[] {
    const char* enabled=std::getenv("MOONMACHINE_CE_TRACE_TIMING");
    return enabled && std::string_view(enabled)=="1";
  }();
  ComPtr<ID3D11VertexShader> vs;
  ComPtr<ID3D11PixelShader> ps, sdrPs, linearPs, encodeSdrPs;
  ComPtr<ID3D11RenderTargetView> rtv;
  ComPtr<ID3D11Fence> complete;
  ComPtr<ID3D11DeviceContext4> ctx4;
  Handle done{CreateEventW(nullptr,FALSE,FALSE,nullptr)};
  uint64_t serial=0;
  D3D11_TEXTURE2D_DESC initial{};
  bool initialHdr=false;
  bool initialized=false;
  D3D11_TEXTURE2D_DESC outputDesc{};
 public:
  CeOutput():resources(ownedResources){}
  explicit CeOutput(SharedResourceManager& existing):resources(existing),externalResources(true){}
  bool publish(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* source,
               bool hdr,int64_t timestamp,AsyncNamedPipe& pipe) {
    // Keep the raw producer QPC in callback_id so all bridge checkpoints can
    // be joined to the CE timing CSV without treating a scheduled FG timestamp
    // as a measurement. The recorder's steady_us measures checkpoint time.
    auto trace=[&](const char* stage){if(stageTracing)wgc_stage_trace::record(0,stage,timestamp);};
    trace("ce_bridge_enter");
    if(!pipe.is_connected()) return false;
    D3D11_TEXTURE2D_DESC desc;source->GetDesc(&desc);
    const bool pq=hdr && desc.Format==DXGI_FORMAT_R10G10B10A2_UNORM;
    if(hdr && !pq && desc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT)
      throw std::runtime_error("unsupported HDR source; refusing implicit 8-bit conversion");
    if(!initialized) {
      initial=desc;initialHdr=hdr;
      winrt::com_ptr<ID3D11Device> wd;wd.copy_from(device);
      auto format=g_config.dynamic_range?DXGI_FORMAT_R16G16B16A16_FLOAT:desc.Format;
      if(!externalResources && !resources.initialize_all(wd,desc.Width,desc.Height,format))
        throw std::runtime_error("bridge output allocation");
      resources.get_shared_texture()->GetDesc(&outputDesc);
      ComPtr<ID3D11Device5>d5;check(device->QueryInterface(IID_PPV_ARGS(&d5)),"completion device");
      check(d5->CreateFence(0,D3D11_FENCE_FLAG_NONE,IID_PPV_ARGS(&complete)),"completion fence");
      check(context->QueryInterface(IID_PPV_ARGS(&ctx4)),"context4");
      {
        ComPtr<ID3DBlob>blob,errors;
        check(D3DCompile(capture_colour_shader,sizeof(capture_colour_shader),nullptr,nullptr,nullptr,"vs","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&errors),"PQ vertex shader");
        check(device->CreateVertexShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&vs),"vertex shader");blob.Reset();errors.Reset();
        check(D3DCompile(capture_colour_shader,sizeof(capture_colour_shader),nullptr,nullptr,nullptr,"ps","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&errors),"PQ pixel shader");
        check(device->CreatePixelShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&ps),"pixel shader");
        blob.Reset();errors.Reset();
        const D3D_SHADER_MACRO sdrDefines[]={{"SOURCE_SDR","1"},{nullptr,nullptr}};
        check(D3DCompile(capture_colour_shader,sizeof(capture_colour_shader),nullptr,sdrDefines,nullptr,"ps","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&errors),"SDR pixel shader");
        check(device->CreatePixelShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&sdrPs),"SDR pixel shader object");
        blob.Reset();errors.Reset();
        const D3D_SHADER_MACRO linearDefines[]={{"SOURCE_LINEAR","1"},{nullptr,nullptr}};
        check(D3DCompile(capture_colour_shader,sizeof(capture_colour_shader),nullptr,linearDefines,nullptr,"ps","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&errors),"linear pixel shader");
        check(device->CreatePixelShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&linearPs),"linear pixel shader object");
        blob.Reset();errors.Reset();
        const D3D_SHADER_MACRO encodeDefines[]={{"SOURCE_ENCODE_SDR","1"},{nullptr,nullptr}};
        check(D3DCompile(capture_colour_shader,sizeof(capture_colour_shader),nullptr,encodeDefines,nullptr,"ps","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&errors),"sRGB encoding shader");
        check(device->CreatePixelShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&encodeSdrPs),"sRGB encoding shader object");
        check(device->CreateRenderTargetView(resources.get_shared_texture().get(),nullptr,&rtv),"PQ RTV");
      }
      auto data=resources.get_shared_handle_data();
      if(!externalResources) pipe.send(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data),sizeof(data)));
      BOOST_LOG(info)<<"CaptureEngine prototype: "<<desc.Width<<'x'<<desc.Height<<" sourceFormat="<<desc.Format<<" HDR="<<hdr<<" PQ-to-scRGB="<<pq;
      initialized=true;
    }
    const bool outputLinear=outputDesc.Format==DXGI_FORMAT_R16G16B16A16_FLOAT;
    if(hdr && !outputLinear)throw std::runtime_error("HDR source requires an HDR-capable capture surface");
    if(desc.Width!=initial.Width||desc.Height!=initial.Height||desc.Format!=initial.Format) {
      BOOST_LOG(info)<<"CaptureEngine source changed: "<<desc.Width<<'x'<<desc.Height<<" format="<<desc.Format;
      initial=desc;
    }
    if(hdr!=initialHdr) {
      BOOST_LOG(info)<<"CaptureEngine colour transition: HDR="<<hdr<<" format="<<desc.Format;
      initialHdr=hdr;
    }
    trace("ce_bridge_setup_done");
    auto mutex=resources.get_keyed_mutex();
    trace("ce_bridge_lock_begin");
    if(mutex->AcquireSync(0,1000)!=S_OK) throw std::runtime_error("output texture busy/abandoned");
    trace("ce_bridge_lock_end");
    auto release=util::fail_guard([&]{mutex->ReleaseSync(0);});
    if(desc.Width!=outputDesc.Width || desc.Height!=outputDesc.Height || desc.Format!=outputDesc.Format || (outputLinear && desc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT)) {
      ComPtr<ID3D11ShaderResourceView>srv;check(device->CreateShaderResourceView(source,nullptr,&srv),"capture SRV");
      trace("ce_bridge_srv_ready");
      auto input=srv.Get();auto target=rtv.Get();
      context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      context->VSSetShader(vs.Get(),nullptr,0);
      auto shader=linearPs.Get();
      if(outputLinear && desc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT && !capture_view_linearizes(desc.Format))
        shader=pq?ps.Get():sdrPs.Get();
      else if(!outputLinear && !capture_view_linearizes(outputDesc.Format) && capture_view_linearizes(desc.Format))
        shader=encodeSdrPs.Get();
      context->PSSetShader(shader,nullptr,0);
      context->PSSetShaderResources(0,1,&input);context->OMSetRenderTargets(1,&target,nullptr);
      const float scale=std::min(float(outputDesc.Width)/desc.Width,float(outputDesc.Height)/desc.Height);
      const float width=desc.Width*scale,height=desc.Height*scale;
      D3D11_VIEWPORT viewport{(outputDesc.Width-width)*0.5f,(outputDesc.Height-height)*0.5f,width,height,0,1};
      if(width!=outputDesc.Width || height!=outputDesc.Height){const float black[4]={0,0,0,1};context->ClearRenderTargetView(target,black);}
      context->RSSetViewports(1,&viewport);
      context->Draw(3,0);
      input=nullptr;context->PSSetShaderResources(0,1,&input);context->OMSetRenderTargets(0,nullptr,nullptr);
    } else context->CopyResource(resources.get_shared_texture().get(),source);
    trace("ce_bridge_commands_recorded");
    // Acknowledge producer ownership only after our GPU read finishes. This
    // conservative prototype wait can later become an asynchronous lease.
    check(ctx4->Signal(complete.Get(),++serial),"completion signal");
    trace("ce_bridge_signal_done");
    context->Flush();
    trace("ce_bridge_flush_done");
    check(complete->SetEventOnCompletion(serial,done.v),"completion event");
    trace("ce_bridge_wait_begin");
    if(WaitForSingleObject(done.v,2000)!=WAIT_OBJECT_0)throw std::runtime_error("bridge GPU timeout");
    trace("ce_bridge_wait_end");
    resources.publish_frame_metadata(timestamp, true);
    trace("ce_bridge_metadata_done");
    release.disable();check(mutex->ReleaseSync(0),"output release");
    trace("ce_bridge_unlock_done");
    resources.signal_frame_ready();
    trace("ce_bridge_notify_done");
    return pipe.is_connected();
  }
};

static int run_ce_bridge(DWORD target,const wchar_t* hook,AsyncNamedPipe& pipe) {
  CeOutput output;
  return run_capture(target,hook,[&](auto* dev,auto* ctx,auto* texture,bool hdr,int64_t timestamp){
    return output.publish(dev,ctx,texture,hdr,timestamp,pipe);
  }, [&]{return pipe.is_connected();});
}

// Stop and join desktop delivery only when the first direct frame is ready.
static int run_ce_bridge_shared(DWORD target,const wchar_t* hook,AsyncNamedPipe& pipe,
                                SharedResourceManager& resources,ID3D11Device* device,
                                std::function<void()> takeOutput,std::function<bool()> alive) {
  CeOutput output(resources);
  bool publishing=false;
  return run_capture(target,hook,[&](auto* dev,auto* ctx,auto* texture,bool hdr,int64_t timestamp){
    if(!alive())return false;
    if(!publishing){takeOutput();publishing=true;}
    return output.publish(dev,ctx,texture,hdr,timestamp,pipe);
  },alive,std::nullopt,device);
}
