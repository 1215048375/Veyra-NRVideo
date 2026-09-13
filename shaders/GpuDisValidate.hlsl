Texture2D<float2> backwardFlow : register(t0);
Texture2D<float2> forwardFlow : register(t1);
Texture2D<float> previousLuma : register(t2);
Texture2D<float> currentLuma : register(t3);
RWTexture2D<float2> motion : register(u0);
RWTexture2D<float> confidence : register(u1);
cbuffer Dims : register(b0) { uint width; uint height; uint2 padding; uint4 reserved; };
[numthreads(16,16,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if(id.x>=width || id.y>=height)return;
    float2 v=backwardFlow[id.xy], q=float2(id.xy)+v;
    float trust=0;
    if(all(isfinite(v)) && all(q>=0) && all(q<=float2(width-1,height-1))) {
        int2 a=int2(floor(q)), lim=int2(width-1,height-1); float2 t=frac(q);
        int2 b=min(a+int2(1,0),lim), c=min(a+int2(0,1),lim), d=min(a+1,lim);
        float2 reverse=lerp(lerp(forwardFlow[a],forwardFlow[b],t.x),lerp(forwardFlow[c],forwardFlow[d],t.x),t.y);
        float luma=lerp(lerp(previousLuma[a],previousLuma[b],t.x),lerp(previousLuma[c],previousLuma[d],t.x),t.y);
        float error=abs(currentLuma[id.xy]-luma);
        float consistency=length(v+reverse);
        trust=(1-smoothstep(.03,.15,error))*(1-smoothstep(.5,2.5,consistency));
        if(!isfinite(trust))trust=0;
    }
    motion[id.xy]=trust>0?v*trust:0;
    confidence[id.xy]=trust;
}
