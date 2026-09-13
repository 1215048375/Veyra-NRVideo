// Encoded RGB from the existing flow ingress -> full-range 8-bit BT.709 luma.
// Guidance only; this conversion never alters the displayed image.
Texture2D<float4> color : register(t0);
RWTexture2D<float> luma : register(u0);
cbuffer Dims : register(b0) { uint width; uint height; uint2 padding; uint4 reserved; };
[numthreads(16,16,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if(id.x>=width || id.y>=height)return;
    luma[id.xy]=dot(color[id.xy].rgb,float3(.2126,.7152,.0722));
}
