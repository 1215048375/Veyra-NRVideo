Texture2D<float4> baseTex:register(t0);
RWTexture2D<float4> nrInput:register(u0);
cbuffer Dims:register(b0){uint sourceW;uint sourceH;uint outW;uint outH;float4 unused;}
// Exact area box filter for downsampling, including fractional boundary pixels.
[numthreads(16,16,1)] void main(uint3 id:SV_DispatchThreadID){
    if(id.x>=outW||id.y>=outH)return;
    float2 low=float2(id.xy)*float2(sourceW,sourceH)/float2(outW,outH);
    float2 high=float2(id.xy+1)*float2(sourceW,sourceH)/float2(outW,outH);
    float4 sum=0;float weights=0;
    for(uint y=(uint)floor(low.y);y<(uint)ceil(high.y);++y)for(uint x=(uint)floor(low.x);x<(uint)ceil(high.x);++x){
        float weight=max(0,min(high.x,x+1.0)-max(low.x,(float)x))*max(0,min(high.y,y+1.0)-max(low.y,(float)y));
        sum+=baseTex[uint2(min(x,sourceW-1),min(y,sourceH-1))]*weight;weights+=weight;
    }
    nrInput[id.xy]=sum/max(weights,1e-6);
}
