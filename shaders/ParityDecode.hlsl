// RenoDX-equivalent parity decode (Playbook section 9.3).
// Reads Original (linear FP16), Proxy and Neural (sRGB-encoded UNORM) and
// reconstructs Final (linear FP16) with luminance restoration, OkLab hue
// correction and the AP1 gamut clamp. Must stay identical to
// src/parity/RenoDxParityCodec.cpp.

#include "HdrColor.hlsli"
cbuffer ParityParams : register(b0)
{
    float4 paritySettings; // x=paperWhiteScale y=transferStrength z=colorStrength w=unused
    uint4 parityDimensions; // x=width y=height
};

Texture2D<float4> originalTex : register(t0);
Texture2D<float4> proxyTex : register(t1);
Texture2D<float4> neuralTex : register(t2);
RWTexture2D<float4> finalTex : register(u0);

float SrgbDecode1(float c)
{
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

float3 SrgbDecode(float3 c)
{
    return float3(SrgbDecode1(c.r), SrgbDecode1(c.g), SrgbDecode1(c.b));
}

float Luminance(float3 rgb)
{
    return dot(rgb, float3(0.212639, 0.715169, 0.072192));
}

float SignedCbrt(float x)
{
    return sign(x) * pow(abs(x), 1.0 / 3.0);
}

float3 Mul3(float3x3 m, float3 v)
{
    return mul(m, v);
}

static const float3x3 kRgbToLms = {
    0.4122214708, 0.5363325363, 0.0514459929,
    0.2119034982, 0.6806995451, 0.1073969566,
    0.0883024619, 0.2817188376, 0.6299787005
};

static const float3x3 kLabFromLmsCbrt = {
    0.2104542553, 0.7936177850, -0.0040720468,
    1.9779984951, -2.4285922050, 0.4505937099,
    0.0259040371, 0.7827717662, -0.8086757660
};

static const float3x3 kLmsFromLabCubed = {
    1.0, 0.3963377774, 0.2158037573,
    1.0, -0.1055613458, -0.0638541728,
    1.0, -0.0894841775, -1.2914855480
};

static const float3x3 kRgbFromLms = {
    4.0767416621, -3.3077115913, 0.2309699292,
    -1.2684380046, 2.6097574011, -0.3413193965,
    -0.0041960863, -0.7034186147, 1.7076147010
};

static const float3x3 kBt709ToAp1 = {
    0.613097, 0.339523, 0.047379,
    0.070194, 0.916354, 0.013452,
    0.020616, 0.109570, 0.869815
};

static const float3x3 kAp1ToBt709 = {
    1.705051, -0.621792, -0.083259,
    -0.130256, 1.140805, -0.010548,
    -0.024003, -0.128969, 1.152972
};

float3 ToOkLab(float3 rgb)
{
    float3 lms = Mul3(kRgbToLms, rgb);
    float3 cbrt = float3(SignedCbrt(lms.r), SignedCbrt(lms.g), SignedCbrt(lms.b));
    return Mul3(kLabFromLmsCbrt, cbrt);
}

float3 FromOkLab(float3 lab)
{
    float3 lmsPrime = Mul3(kLmsFromLabCubed, lab);
    float3 lmsCubed = lmsPrime * lmsPrime * lmsPrime;
    return Mul3(kRgbFromLms, lmsCubed);
}

float3 ClampAp1(float3 rgb)
{
    float3 ap1 = Mul3(kBt709ToAp1, rgb);
    return Mul3(kAp1ToBt709, max(ap1, 0.0));
}

float3 HueOkLab(float3 incorrect, float3 correct)
{
    float3 incorrectLab = ToOkLab(incorrect);
    float3 correctLab = ToOkLab(correct);
    float incorrectChroma = length(incorrectLab.gb);
    float correctChroma = length(correctLab.gb);
    incorrectLab.gb = correctLab.gb * (correctChroma == 0.0 ? 1.0 : incorrectChroma / correctChroma);
    return ClampAp1(FromOkLab(incorrectLab));
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= parityDimensions.x || dispatchThreadId.y >= parityDimensions.y) {
        return;
    }

    const float4 originalSample = originalTex[dispatchThreadId.xy];
    const float3 original = max(originalSample.rgb, 0.0) / paritySettings.x;
    const float3 proxy = SrgbDecode(proxyTex[dispatchThreadId.xy].rgb);
    const float3 neural = SrgbDecode(neuralTex[dispatchThreadId.xy].rgb);
    if(paritySettings.w>0.5){
        finalTex[dispatchThreadId.xy]=float4(HdrRestore(originalSample.rgb,proxy,neural),originalSample.a);return;
    }

    const float originalY = Luminance(original);
    const float proxyY = Luminance(proxy);
    const float neuralY = Luminance(neural);

    float ratio;
    if (originalY < proxyY) {
        ratio = originalY / proxyY;
    }
    else {
        const float newY = neuralY + max(0.0, originalY - proxyY);
        ratio = neuralY > 0.0 ? newY / neuralY : 0.0;
    }

    const float3 scaled = HueOkLab(neural * ratio, neural);
    const float3 upgraded = lerp(original, scaled, paritySettings.y);

    const float upgradedY = Luminance(upgraded);
    const float finalRatio = originalY == 0.0 ? 1.0 : upgradedY / originalY;
    const float3 luminanceOnly = original * finalRatio;
    const float3 result = lerp(luminanceOnly, upgraded, paritySettings.z);

    finalTex[dispatchThreadId.xy] = float4(result * paritySettings.x, originalSample.a);
}
