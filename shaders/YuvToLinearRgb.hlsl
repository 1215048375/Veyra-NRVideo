// YUV (NV12/P010) -> linear BT.709 working RGB (Playbook section 13.3).
// Reads plane 0 as R8/R16 UNORM and plane 1 as R8G8/R16G16 UNORM via two
// SRVs; the dispatch covers the full frame. Color metadata (range/matrix/
// transfer) arrives as root constants set per frame from FFmpeg stream data.
// V1 supports BT.709 and BT.601, limited and full range, SDR transfer only.

cbuffer YuvParams : register(b0)
{
    float4 colorParams0; // x=limitedRange y=matrix709 z=transferSRGB w=padding
    uint4 yuvDimensions; // x=width y=height
};

Texture2D<float> lumaPlane : register(t0);   // R8_UNORM or R16_UNORM
Texture2D<float2> chromaPlane : register(t1); // R8G8_UNORM or R16G16_UNORM
Texture2D<float2> chromaPlane2 : register(t2); // second chroma view (unused for NV12; reserved)
RWTexture2D<float4> linearRgb : register(u0);

float ExpandLimited(float c)
{
    // 16..235 -> 0..1 (8-bit ranges; P010 carries the same studio swing in
    // the top bits, so the normalized sample maps identically).
    return saturate((c - 16.0 / 255.0) * 255.0 / 219.0);
}

float3 YuvToRgb(float y, float2 uv)
{
    float yy = colorParams0.x > 0.5 ? ExpandLimited(y) : y;
    float uu = (uv.x - 128.0/255.0) * (colorParams0.x > 0.5 ? 255.0/224.0 : 1.0);
    float vv = (uv.y - 128.0/255.0) * (colorParams0.x > 0.5 ? 255.0/224.0 : 1.0);

    if (colorParams0.y > 0.5) {
        // BT.709.
        float r = yy + 1.5748 * vv;
        float g = yy - 0.1873 * uu - 0.4681 * vv;
        float b = yy + 1.8556 * uu;
        return float3(r, g, b);
    }
    // BT.601.
    float r = yy + 1.402 * vv;
    float g = yy - 0.3441 * uu - 0.7141 * vv;
    float b = yy + 1.772 * uu;
    return float3(r, g, b);
}

float SrgbDecode(float c)
{
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= yuvDimensions.x || dispatchThreadId.y >= yuvDimensions.y) {
        return;
    }
    const float y = lumaPlane[uint2(dispatchThreadId.x, dispatchThreadId.y)];
    const float2 uv = chromaPlane[uint2(dispatchThreadId.x / 2, dispatchThreadId.y / 2)];
    float3 rgb = YuvToRgb(y, uv);
    rgb = saturate(rgb);
    if (colorParams0.z > 2.5) {
        rgb = pow(rgb, 2.4); // BT.1886 EOTF, ideal black SDR display intent.
    } else if (colorParams0.z > 1.5) {
        rgb = float3(rgb.r < 0.081 ? rgb.r / 4.5 : pow((rgb.r + 0.099) / 1.099, 1.0/0.45),
                     rgb.g < 0.081 ? rgb.g / 4.5 : pow((rgb.g + 0.099) / 1.099, 1.0/0.45),
                     rgb.b < 0.081 ? rgb.b / 4.5 : pow((rgb.b + 0.099) / 1.099, 1.0/0.45));
    } else if (colorParams0.z > 0.5) {
        rgb = float3(SrgbDecode(rgb.r), SrgbDecode(rgb.g), SrgbDecode(rgb.b));
    }
    linearRgb[dispatchThreadId.xy] = float4(rgb, 1.0);
}
