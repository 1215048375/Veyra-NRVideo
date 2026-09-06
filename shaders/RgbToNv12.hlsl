Texture2D<float4> rgb : register(t0);
RWTexture2D<float> yPlane : register(u0);
RWTexture2D<float2> uvPlane : register(u1);
cbuffer Constants : register(b0) { uint width; uint height; uint2 reserved; float4 padding; };
float3 convert(float3 c) {
    float y=dot(c,float3(0.2126,0.7152,0.0722));
    return float3((16+219*y)/255.0, (128+224*(c.b-y)/1.8556)/255.0, (128+224*(c.r-y)/1.5748)/255.0);
}
[numthreads(16,16,1)]
void main(uint3 id:SV_DispatchThreadID){
    if(id.x>=width||id.y>=height)return;
    yPlane[id.xy]=convert(saturate(rgb[id.xy].rgb)).x;
    if((id.x&1)==0&&(id.y&1)==0){
        float3 c=(rgb[id.xy].rgb+rgb[id.xy+uint2(1,0)].rgb+rgb[id.xy+uint2(0,1)].rgb+rgb[id.xy+uint2(1,1)].rgb)*0.25;
        uvPlane[id.xy/2]=convert(saturate(c)).yz;
    }
}
