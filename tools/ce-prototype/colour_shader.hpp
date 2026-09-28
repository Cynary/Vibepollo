#pragma once
inline constexpr char capture_colour_shader[]=R"(
Texture2D<float4> src:register(t0);
struct Vertex { float4 position:SV_Position; float2 uv:TEXCOORD0; };
Vertex vs(uint id:SV_VertexID) {
 Vertex v;v.position=float4(id==2?3:-1,id==1?3:-1,0,1);
 v.uv=float2((v.position.x+1)*0.5,(1-v.position.y)*0.5);return v;
}
float4 ps(Vertex input):SV_Target {
 uint width,height;src.GetDimensions(width,height);
 uint2 pixel=min(uint2(saturate(input.uv)*float2(width,height)),uint2(width-1,height-1));
 float3 raw=src.Load(int3(pixel,0)).rgb;
 #ifdef SOURCE_ENCODE_SDR
 float3 v=max(raw,0);
 float3 low=v*12.92;
 float3 high=1.055*pow(v,1.0/2.4)-0.055;
 return float4(float3(v.r<=0.0031308?low.r:high.r,v.g<=0.0031308?low.g:high.g,v.b<=0.0031308?low.b:high.b),1);
 #else
 #ifdef SOURCE_LINEAR
 return float4(raw,1);
 #else
 float3 v=max(raw,0);
 #ifdef SOURCE_SDR
 float3 low=v/12.92;
 float3 high=pow((v+0.055)/1.055,2.4);
 return float4(float3(v.r<=0.04045?low.r:high.r,v.g<=0.04045?low.g:high.g,v.b<=0.04045?low.b:high.b),1);
#else
 float3 p=pow(v,1.0/78.84375);
 float3 luminance=pow(max(p-0.8359375,0)/max(18.8515625-18.6875*p,1e-6),1.0/0.1593017578125);
 float3 rgb=mul(float3x3(1.660491,-0.587641,-0.072850,-0.124550,1.132900,-0.008349,-0.018151,-0.100579,1.118730),luminance);
 return float4(rgb*125.0,1);
#endif
#endif
 #endif
})";
