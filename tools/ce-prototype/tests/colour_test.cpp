#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <algorithm>
#include "../colour_shader.hpp"
#include "../colour_view.hpp"
using Microsoft::WRL::ComPtr;
ComPtr<ID3DBlob> compilerError;
void ck(HRESULT h){if(FAILED(h)){if(compilerError)fprintf(stderr,"%s\n",(char*)compilerError->GetBufferPointer());throw std::runtime_error("D3D failure "+std::to_string((unsigned)h));}}
double half(unsigned h){int e=(h>>10)&31;double v=e?std::ldexp(1.+(h&1023)/1024.,e-15):std::ldexp(double(h&1023),-24);return h&32768?-v:v;}
double pq(double x){double p=std::pow(x,1./78.84375);return std::pow(std::max(p-.8359375,0.)/(18.8515625-18.6875*p),1./.1593017578125);}
int main(){try{
 ComPtr<ID3D11Device>d;ComPtr<ID3D11DeviceContext>c;ck(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,nullptr,&c));
 ComPtr<ID3DBlob>b,e;ComPtr<ID3D11VertexShader>vs;ck(D3DCompile(capture_colour_shader,sizeof(capture_colour_shader),nullptr,nullptr,nullptr,"vs","vs_5_0",0,0,&b,&compilerError));ck(d->CreateVertexShader(b->GetBufferPointer(),b->GetBufferSize(),nullptr,&vs));
 D3D11_TEXTURE2D_DESC td{1,1,1,1,DXGI_FORMAT_R16G16B16A16_FLOAT,{1,0},D3D11_USAGE_DEFAULT,D3D11_BIND_RENDER_TARGET,0,0};
 ComPtr<ID3D11Texture2D>out,read;ck(d->CreateTexture2D(&td,nullptr,&out));ComPtr<ID3D11RenderTargetView>rtv;ck(d->CreateRenderTargetView(out.Get(),nullptr,&rtv));
 td.Usage=D3D11_USAGE_STAGING;td.BindFlags=0;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ck(d->CreateTexture2D(&td,nullptr,&read));
 int count=0;
 for(auto format:{DXGI_FORMAT_R10G10B10A2_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,DXGI_FORMAT_B8G8R8X8_UNORM_SRGB})
 for(bool sdr:{false,true})
 for(bool encodedOutput:{false,true}){
  if(encodedOutput && (!sdr || !capture_view_linearizes(format)))continue;
  if(!sdr && format!=DXGI_FORMAT_R10G10B10A2_UNORM)continue;
  b.Reset();e.Reset();const D3D_SHADER_MACRO macros[]={{encodedOutput?"SOURCE_ENCODE_SDR":(capture_view_linearizes(format)?"SOURCE_LINEAR":"SOURCE_SDR"),"1"},{nullptr,nullptr}};
  ck(D3DCompile(capture_colour_shader,sizeof(capture_colour_shader),nullptr,sdr?macros:nullptr,nullptr,"ps","ps_5_0",0,0,&b,&compilerError));ComPtr<ID3D11PixelShader>ps;ck(d->CreatePixelShader(b->GetBufferPointer(),b->GetBufferSize(),nullptr,&ps));
  unsigned values[][3]={{0,0,0},{257,513,1},{512,512,512},{1023,1023,1023},{800,200,500}};
  for(auto &v:values){
   unsigned packed=v[0]|(v[1]<<10)|(v[2]<<20)|(3u<<30);
   double channels[3];for(int j=0;j<3;j++)channels[j]=v[j]/1023.;
   if(format!=DXGI_FORMAT_R10G10B10A2_UNORM){
    unsigned bytes[3];for(int j=0;j<3;j++){bytes[j]=unsigned(std::round(channels[j]*255));channels[j]=bytes[j]/255.;}
    bool bgra=format!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    packed=bytes[bgra?2:0]|(bytes[1]<<8)|(bytes[bgra?0:2]<<16)|(255u<<24);
   }
   D3D11_SUBRESOURCE_DATA data{&packed,4,4};
   D3D11_TEXTURE2D_DESC sd{1,1,1,1,format,{1,0},D3D11_USAGE_IMMUTABLE,D3D11_BIND_SHADER_RESOURCE,0,0};
   ComPtr<ID3D11Texture2D>src;ComPtr<ID3D11ShaderResourceView>srv;ck(d->CreateTexture2D(&sd,&data,&src));ck(d->CreateShaderResourceView(src.Get(),nullptr,&srv));
   auto t=rtv.Get();auto input=srv.Get();c->OMSetRenderTargets(1,&t,nullptr);c->PSSetShaderResources(0,1,&input);c->VSSetShader(vs.Get(),nullptr,0);c->PSSetShader(ps.Get(),nullptr,0);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);D3D11_VIEWPORT vp{0,0,1,1,0,1};c->RSSetViewports(1,&vp);c->Draw(3,0);c->OMSetRenderTargets(0,nullptr,nullptr);input=nullptr;c->PSSetShaderResources(0,1,&input);c->CopyResource(read.Get(),out.Get());
   D3D11_MAPPED_SUBRESOURCE map{};ck(c->Map(read.Get(),0,D3D11_MAP_READ,0,&map));unsigned short got[4];memcpy(got,map.pData,8);c->Unmap(read.Get(),0);
   double l[3];for(int j=0;j<3;j++){double x=channels[j];l[j]=sdr?(x<=.04045?x/12.92:std::pow((x+.055)/1.055,2.4)):pq(x);}
   double matrix[3][3]={{1.660491,-.587641,-.072850},{-.124550,1.132900,-.008349},{-.018151,-.100579,1.118730}};
   for(int j=0;j<3;j++){double want=encodedOutput?channels[j]:l[j];if(!sdr){want=0;for(int k=0;k<3;k++)want+=matrix[j][k]*l[k]*125.;}double actual=half(got[j]);if(std::abs(actual-want)>std::max(.0002,std::abs(want)*.002)){printf("FAIL sdr=%d channel=%d got=%g want=%g\n",sdr,j,actual,want);return 1;}}
   count++;
  }
 }
 printf("PASS %d GPU colour vectors: SDR, sRGB texture views and HDR10 to scRGB, including negative gamut values\n",count);return 0;
}catch(std::exception&e){puts(e.what());return 2;}}
