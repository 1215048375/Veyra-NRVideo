// YUV (NV12/P010) -> linear BT.709 working RGB (Playbook section 13.3).
// Reads plane 0 as R8/R16 UNORM and plane 1 as R8G8/R16G16 UNORM via two
// SRVs; the dispatch covers the full frame. Color metadata (range/matrix/
// transfer) arrives as root constants set per frame from FFmpeg stream data.
// V1 supports BT.709 and BT.601, limited and full range, SDR transfer only.

cbuffer YuvParams : register(b0)
{
    float4 colorParams0; // x=limitedRange y=matrix709 z=transferSRGB w=padding
    uint4 yuvDimensions; // x=width y=height z=nativeHDR w=chroma location (0=legacy)
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
    // P010 is stored as 10 significant HIGH bits in 16-bit UNORM. Its
    // normalization is not 8-bit /255: decode legal code values explicitly.
    bool ten=colorParams0.w>0.5;
    float scale=ten?65535.0/64.0:255.0;
    float black=ten?64.0:16.0,white=ten?940.0:235.0;
    float middle=ten?512.0:128.0,span=ten?896.0:224.0;
    float maximum=ten?1023.0:255.0;
    float yy=colorParams0.x>0.5?(y*scale-black)/(white-black):y*scale/maximum;
    yy=saturate(yy);
    float uu=(uv.x*scale-middle)/(colorParams0.x>0.5?span:maximum);
    float vv=(uv.y*scale-middle)/(colorParams0.x>0.5?span:maximum);

    if(colorParams0.y>1.5)return float3(yy+1.4746*vv,yy-0.164553*uu-0.571353*vv,yy+1.8814*uu);

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

float2 ReconstructChroma(uint2 pixel)
{
    uint location=yuvDimensions.w;
    if(location==0)return chromaPlane[pixel/2];
    // Chroma sample origin in luma pixel-center coordinates. Respect MPEG
    // left, JPEG center and vertical top/bottom placement; no luma filtering.
    float2 origin=float2(0.0,0.5);
    if(location==2)origin=float2(0.5,0.5);
    else if(location==3)origin=float2(0.0,0.0);
    else if(location==4)origin=float2(0.5,0.0);
    else if(location==5)origin=float2(0.0,1.0);
    else if(location==6)origin=float2(0.5,1.0);
    float2 position=(float2(pixel)-origin)*0.5;
    int2 base=int2(floor(position));float2 fraction=frac(position);
    // Clamp to the visible chroma extent, not decoder allocation padding.
    int2 last=int2((yuvDimensions.xy+1)/2)-1;
    float2 a=chromaPlane[clamp(base,int2(0,0),last)];
    float2 b=chromaPlane[clamp(base+int2(1,0),int2(0,0),last)];
    float2 c=chromaPlane[clamp(base+int2(0,1),int2(0,0),last)];
    float2 d=chromaPlane[clamp(base+int2(1,1),int2(0,0),last)];
    return lerp(lerp(a,b,fraction.x),lerp(c,d,fraction.x),fraction.y);
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= yuvDimensions.x || dispatchThreadId.y >= yuvDimensions.y) {
        return;
    }
    const float y = lumaPlane[uint2(dispatchThreadId.x, dispatchThreadId.y)];
    const float2 uv = ReconstructChroma(dispatchThreadId.xy);
    float3 rgb = YuvToRgb(y, uv);
    rgb = saturate(rgb);
    if(colorParams0.z>3.5){
        // ST2084 EOTF to absolute cd/m2, then BT.2020 -> BT.709 linear.
        const float m1=2610.0/16384.0,m2=2523.0/32.0;
        float3 p=pow(rgb,1.0/m2);
        float3 nits=10000.0*pow(max(p-3424.0/4096.0,0.0)/max(2413.0/128.0-2392.0/128.0*p,1e-6),1.0/m1);
        float3 linear709=mul(float3x3(1.660491,-0.587641,-0.072850,-0.124550,1.132900,-0.008349,-0.018151,-0.100579,1.118730),nits);
        if(yuvDimensions.z!=0)rgb=linear709/80.0; // scRGB: 1.0 = 80 nits.
        else{
            // Stable luminance shoulder, fixed 1000-nit reference peak.
            // This is SDR mapping, never advertised as native HDR output.
            float3 c=max(linear709/203.0,0.0);float lum=dot(c,float3(.2126,.7152,.0722));
            float peak=1000.0/203.0;float mapped=lum*(1.0+lum/(peak*peak))/(1.0+lum);
            rgb=saturate(c*(mapped/max(lum,1e-6)));
        }
    } else if (colorParams0.z > 2.5) {
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
