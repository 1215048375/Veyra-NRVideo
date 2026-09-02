// RenoDX-equivalent proxy encode (Playbook section 9.2).
// Original = linear BT.709 working RGB (R16G16B16A16_FLOAT, may exceed 1.0)
// Proxy    = sRGB-encoded soft-clipped value in R8G8B8A8_UNORM.
// The math must stay identical to src/parity/RenoDxParityCodec.cpp.

cbuffer ParityParams : register(b0)
{
    float4 paritySettings; // x=paperWhiteScale y=transferStrength z=colorStrength w=unused
    uint4 parityDimensions; // x=width y=height
};

Texture2D<float4> originalTex : register(t0);
RWTexture2D<unorm float4> proxyTex : register(u0);

float SrgbEncode1(float c)
{
    c = max(c, 0.0);
    return c <= 0.0031308 ? 12.92 * c : 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}

float3 SrgbEncode(float3 c)
{
    return float3(SrgbEncode1(c.r), SrgbEncode1(c.g), SrgbEncode1(c.b));
}

float Shoulder(float x)
{
    return 0.75 + 0.25 * (1.0 - exp(-5.7780 * (x - 0.75)));
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= parityDimensions.x || dispatchThreadId.y >= parityDimensions.y) {
        return;
    }

    const float4 original = originalTex[dispatchThreadId.xy];
    const float3 linearRgb = max(original.rgb, 0.0) / paritySettings.x;
    // shoulder(x) = 0.75 + 0.25 * (1 - exp(-5.7780 * (x - 0.75))); component-wise.
    const float3 shouldered = 0.75 + 0.25 * (1.0 - exp(-5.7780 * (linearRgb - 0.75)));
    const float3 mapped = select(linearRgb <= 0.75, linearRgb, shouldered);
    proxyTex[dispatchThreadId.xy] = float4(SrgbEncode(mapped), original.a);
}
