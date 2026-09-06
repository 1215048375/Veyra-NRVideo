// NvofDensify: converts NVOF raw SHORT2 (signed S10.5, int16x2) flow at
// hardware-grid extent into full-working-extent R16G16_FLOAT motion in
// workingExtent pixel units, plus an R8_UNORM confidence derived INVERSELY
// from the NVOF cost (higher cost = less reliable -> lower confidence).
//
// Contract (user directive 2026-09-04 s6):
//   - S10.5 -> float is EXACTLY float2(raw) / 32.0 (no grid division).
//   - Direction: NVOF outputs current->previous; `negate` is the single
//     explicit flip applied to match the DLSSG convention, proven by the
//     displacement test matrix (not inferred).
//   - Confidence = (255 - cost) / 255: NVIDIA cost rises with unreliability.
//   - Cells with cost >= costThreshold have their motion zeroed.
//   - Densify replicates each grid cell to its gridSize x gridSize block
//     (nearest), matching the official sample's upsampling shader.

Texture2D<int2> rawFlow : register(t0);   // grid-extent SHORT2
Texture2D<uint> rawCost : register(t1);   // grid-extent cost (0..255, higher = worse)

RWTexture2D<float2> flowOut : register(u0);  // working extent, pixels
RWTexture2D<float> confOut : register(u1);   // working extent [0,1], INVERSE cost

cbuffer NvofDensifyConstants : register(b0)
{
    uint gridW;            // raw flow width  = ceil(workingW / grid)
    uint gridH;            // raw flow height = ceil(workingH / grid)
    uint workingW;
    uint workingH;
    uint gridSize;         // hardware grid (4 per capability list)
    uint negate;           // 1 = single explicit direction flip
    uint costThreshold;    // cells with cost >= this are zeroed (0..255)
    uint pad0;
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
    const float confidence = float(255u - min(cost, 255u)) / 255.0;
    if (cost >= costThreshold) v = float2(0.0, 0.0);
    flowOut[globalId.xy] = v;
    confOut[globalId.xy] = (cost >= costThreshold) ? 0.0 : confidence;
}
