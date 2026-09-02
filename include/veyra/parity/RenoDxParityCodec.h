#pragma once

// RenoDX-equivalent parity codec, CPU reference (Playbook section 9).
// The exact shader math implemented as pure double-precision functions:
// this is the golden reference the GPU shaders must match and the numeric
// anchor for the Phase 2 gates.

#include <cstdint>

namespace veyra::parity {

struct RgbF {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
};

struct ParitySettings {
    double paperWhiteScale = 1.0; // V1 neutral engineering baseline (1.0)
    double transferStrength = 1.0;
    double colorStrength = 1.0;
};

// --- sRGB transfer (Playbook 9.2) ---
double srgbEncode(double c);
double srgbDecode(double c);

// --- soft-clip shoulder (constant 5.7780 from the observed addon codec) ---
double shoulder(double x);

// --- Proxy encode: linear BT.709 RGB (Original) -> sRGB-encoded proxy linear
// value ready for the RGBA8 Proxy texture. Negative components clamp to 0
// after PaperWhiteScale division; alpha passes through unchanged.
struct Rgba8 {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 0;
};
Rgba8 encodeProxy(const RgbF& original, double alpha, const ParitySettings& settings);

// Stage values for tests/captures: encode without the final 8-bit quantize.
RgbF encodeProxyLinear(const RgbF& original, const ParitySettings& settings);

// --- OkLab / AP1 building blocks (Playbook 9.3, exact matrices) ---
RgbF toOkLab(const RgbF& rgb);
RgbF fromOkLab(const RgbF& lab);
RgbF clampAp1(const RgbF& rgb);
double luminance(const RgbF& rgb);
RgbF hueOkLab(const RgbF& incorrect, const RgbF& correct);

// --- Parity decode: Original + Proxy + Neural -> Final (linear FP16 target).
// Inputs are linear working RGB (Original) and sRGB-encoded 8-bit values
// decoded back to linear (Proxy/Neural), as the shader would sample them.
RgbF parityDecode(const RgbF& originalLinear, const RgbF& proxyLinear,
    const RgbF& neuralLinear, const ParitySettings& settings);

} // namespace veyra::parity
