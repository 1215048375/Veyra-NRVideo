#include "veyra/parity/RenoDxParityCodec.h"

#include <algorithm>
#include <cmath>

namespace veyra::parity {

namespace {

// Playbook 9.3 matrices, row-major, used as mul(matrix, vector).
constexpr double kRgbToLms[3][3] = {
    { 0.4122214708, 0.5363325363, 0.0514459929 },
    { 0.2119034982, 0.6806995451, 0.1073969566 },
    { 0.0883024619, 0.2817188376, 0.6299787005 },
};
constexpr double kLabFromLmsCbrt[3][3] = {
    { 0.2104542553, 0.7936177850, -0.0040720468 },
    { 1.9779984951, -2.4285922050, 0.4505937099 },
    { 0.0259040371, 0.7827717662, -0.8086757660 },
};
constexpr double kLmsFromLabCubed[3][3] = {
    { 1.0, 0.3963377774, 0.2158037573 },
    { 1.0, -0.1055613458, -0.0638541728 },
    { 1.0, -0.0894841775, -1.2914855480 },
};
constexpr double kRgbFromLms[3][3] = {
    { 4.0767416621, -3.3077115913, 0.2309699292 },
    { -1.2684380046, 2.6097574011, -0.3413193965 },
    { -0.0041960863, -0.7034186147, 1.7076147010 },
};
constexpr double kBt709ToAp1[3][3] = {
    { 0.613097, 0.339523, 0.047379 },
    { 0.070194, 0.916354, 0.013452 },
    { 0.020616, 0.109570, 0.869815 },
};
constexpr double kAp1ToBt709[3][3] = {
    { 1.705051, -0.621792, -0.083259 },
    { -0.130256, 1.140805, -0.010548 },
    { -0.024003, -0.128969, 1.152972 },
};

RgbF mulMatrix(const double m[3][3], const RgbF& v)
{
    const double input[3] = { v.r, v.g, v.b };
    double output[3] = {};
    for (int row = 0; row < 3; ++row) {
        output[row] = m[row][0] * input[0] + m[row][1] * input[1] + m[row][2] * input[2];
    }
    return { output[0], output[1], output[2] };
}

double signedCbrt(double x)
{
    return std::copysign(std::cbrt(std::fabs(x)), x);
}

double clampd(double x, double lo, double hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

} // namespace

double srgbEncode(double c)
{
    c = std::max(c, 0.0);
    return c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
}

double srgbDecode(double c)
{
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double shoulder(double x)
{
    return 0.75 + 0.25 * (1.0 - std::exp(-5.7780 * (x - 0.75)));
}

RgbF encodeProxyLinear(const RgbF& original, const ParitySettings& settings)
{
    const RgbF linear{
        std::max(original.r, 0.0) / settings.paperWhiteScale,
        std::max(original.g, 0.0) / settings.paperWhiteScale,
        std::max(original.b, 0.0) / settings.paperWhiteScale,
    };
    const auto map = [](double x) {
        return x <= 0.75 ? x : shoulder(x);
    };
    return { srgbEncode(map(linear.r)), srgbEncode(map(linear.g)), srgbEncode(map(linear.b)) };
}

Rgba8 encodeProxy(const RgbF& original, double alpha, const ParitySettings& settings)
{
    const RgbF encoded = encodeProxyLinear(original, settings);
    const auto quantize = [](double c) {
        return static_cast<uint8_t>(clampd(std::lround(c * 255.0), 0.0, 255.0));
    };
    return { quantize(encoded.r), quantize(encoded.g), quantize(encoded.b), quantize(alpha) };
}

RgbF toOkLab(const RgbF& rgb)
{
    const RgbF lms = mulMatrix(kRgbToLms, rgb);
    const RgbF cbrt{ signedCbrt(lms.r), signedCbrt(lms.g), signedCbrt(lms.b) };
    return mulMatrix(kLabFromLmsCbrt, cbrt);
}

RgbF fromOkLab(const RgbF& lab)
{
    // OkLab -> LMS' (leading 1.0 column applies to L), cube each component,
    // then back to RGB.
    const RgbF lmsPrime = mulMatrix(kLmsFromLabCubed, lab);
    const RgbF lmsCubed{ lmsPrime.r * lmsPrime.r * lmsPrime.r,
        lmsPrime.g * lmsPrime.g * lmsPrime.g,
        lmsPrime.b * lmsPrime.b * lmsPrime.b };
    return mulMatrix(kRgbFromLms, lmsCubed);
}

RgbF clampAp1(const RgbF& rgb)
{
    const RgbF ap1 = mulMatrix(kBt709ToAp1, rgb);
    const RgbF clamped{ std::max(ap1.r, 0.0), std::max(ap1.g, 0.0), std::max(ap1.b, 0.0) };
    return mulMatrix(kAp1ToBt709, clamped);
}

double luminance(const RgbF& rgb)
{
    return 0.212639 * rgb.r + 0.715169 * rgb.g + 0.072192 * rgb.b;
}

RgbF hueOkLab(const RgbF& incorrect, const RgbF& correct)
{
    const RgbF incorrectLab = toOkLab(incorrect);
    const RgbF correctLab = toOkLab(correct);
    const double incorrectChroma = std::hypot(incorrectLab.g, incorrectLab.b);
    const double correctChroma = std::hypot(correctLab.g, correctLab.b);
    const double ratio = correctChroma == 0.0 ? 1.0 : incorrectChroma / correctChroma;
    const RgbF adjusted{ incorrectLab.r, correctLab.g * ratio, correctLab.b * ratio };
    return clampAp1(fromOkLab(adjusted));
}

RgbF parityDecode(const RgbF& originalLinear, const RgbF& proxyLinear,
    const RgbF& neuralLinear, const ParitySettings& settings)
{
    const RgbF original{
        std::max(originalLinear.r, 0.0) / settings.paperWhiteScale,
        std::max(originalLinear.g, 0.0) / settings.paperWhiteScale,
        std::max(originalLinear.b, 0.0) / settings.paperWhiteScale,
    };
    const RgbF proxy = proxyLinear;
    const RgbF neural = neuralLinear;

    const double originalY = luminance(original);
    const double proxyY = luminance(proxy);
    const double neuralY = luminance(neural);

    double ratio = 0.0;
    if (originalY < proxyY) {
        ratio = originalY / proxyY;
    }
    else {
        const double newY = neuralY + std::max(0.0, originalY - proxyY);
        ratio = neuralY > 0.0 ? newY / neuralY : 0.0;
    }

    const RgbF scaled = hueOkLab({ neural.r * ratio, neural.g * ratio, neural.b * ratio }, neural);
    const RgbF upgraded{
        original.r + (scaled.r - original.r) * settings.transferStrength,
        original.g + (scaled.g - original.g) * settings.transferStrength,
        original.b + (scaled.b - original.b) * settings.transferStrength,
    };

    const double upgradedY = luminance(upgraded);
    const double finalRatio = originalY == 0.0 ? 1.0 : upgradedY / originalY;
    const RgbF luminanceOnly{
        original.r * finalRatio,
        original.g * finalRatio,
        original.b * finalRatio,
    };
    const RgbF result{
        luminanceOnly.r + (upgraded.r - luminanceOnly.r) * settings.colorStrength,
        luminanceOnly.g + (upgraded.g - luminanceOnly.g) * settings.colorStrength,
        luminanceOnly.b + (upgraded.b - luminanceOnly.b) * settings.colorStrength,
    };
    return {
        result.r * settings.paperWhiteScale,
        result.g * settings.paperWhiteScale,
        result.b * settings.paperWhiteScale,
    };
}

} // namespace veyra::parity
