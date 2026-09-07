Texture2D<float2> inputFlow:register(t0);
RWTexture2D<float2> outputFlow:register(u0);
cbuffer Dims:register(b0){uint sourceW;uint sourceH;uint outW;uint outH;float4 unused;}
[numthreads(16,16,1)] void main(uint3 id:SV_DispatchThreadID){
    if(id.x>=outW||id.y>=outH)return;
    uint2 at=min(uint2((float2(id.xy)+0.5)*float2(sourceW,sourceH)/float2(outW,outH)),uint2(sourceW-1,sourceH-1));
    outputFlow[id.xy]=inputFlow[at]*float2(outW,outH)/float2(sourceW,sourceH);
}
