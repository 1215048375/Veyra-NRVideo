// NVOF current->previous, source-space pixel units. FlowAdapt rescales
// for SR/NR/FG consumers. /32 converts S10.5; grid replication is nearest.
// Cost is a rejection signal, not an absolute probability. Optional spatial
// validation rejects out-of-frame reprojections and gates luma disagreement.
Texture2D<int2> rawFlow : register(t0);   // grid-extent SHORT2
Texture2D<float4> previousColor : register(t2);
Texture2D<float4> currentColor : register(t3);
Texture2D<uint> rawCost : register(t1);   // grid-extent cost (0..255, higher = worse)

RWTexture2D<float2> flowOut : register(u0);  // source extent, pixels
RWTexture2D<float> confOut : register(u1);   // source extent [0,1], cost and reprojection

cbuffer NvofDensifyConstants : register(b0)
{
    uint gridW;            // raw flow width  = ceil(workingW / grid)
    uint gridH;            // raw flow height = ceil(workingH / grid)
    uint workingW;
    uint workingH;
    uint gridSize;         // hardware grid (4 per capability list)
    uint negate;           // 1 = single explicit direction flip
    uint costThreshold;    // cells with cost >= this are zeroed (0..255)
    uint validationFlags;  // bit0 bounds, bit1 photometric; zero only for A/B diagnostics
};

[numthreads(16, 16, 1)]
void main(uint3 globalId : SV_DispatchThreadID)
{
    if (globalId.x >= workingW || globalId.y >= workingH) return;
    const uint gx = min(globalId.x / gridSize, gridW - 1);
    const uint gy = min(globalId.y / gridSize, gridH - 1);
    const int2 raw = rawFlow[uint2(gx, gy)];
    float2 v = float2(raw) / 32.0;          // S10.5 -> float pixels (fixed)
    if (negate != 0) v = -v;
    const uint cost = rawCost[uint2(gx, gy)];
    // Higher cost = less reliable: confidence falls as cost rises.
    float confidence = float(255u - min(cost, 255u)) / 255.0;
    if (cost >= costThreshold) { v = 0; confidence = 0; }
    else if (validationFlags != 0) {
        float2 q=float2(globalId.xy)+v;
        bool outside=any(q<0)||any(q>float2(workingW-1,workingH-1));
        if ((validationFlags&1)!=0 && outside) {v=0;confidence=0;}
        else if ((validationFlags&2)!=0) {
            int2 a=(int2)floor(q);float2 t=frac(q);
            int2 limit=int2(workingW-1,workingH-1);
            float3 top=lerp(previousColor[clamp(a,int2(0,0),limit)].rgb,previousColor[clamp(a+int2(1,0),int2(0,0),limit)].rgb,t.x);
            float3 bottom=lerp(previousColor[clamp(a+int2(0,1),int2(0,0),limit)].rgb,previousColor[clamp(a+int2(1,1),int2(0,0),limit)].rgb,t.x);
            float error=abs(dot(currentColor[globalId.xy].rgb-lerp(top,bottom,t.y),float3(.2126,.7152,.0722)));
            // Allow small quantization/compression/exposure noise; reject large mismatch.
            float trust=1-smoothstep(.03,.15,error);
            v*=trust;confidence*=trust;
        }
    }
    flowOut[globalId.xy] = v;
    confOut[globalId.xy] = (cost >= costThreshold) ? 0.0 : confidence;
}
