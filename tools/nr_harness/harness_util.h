#pragma once

// Shared harness helpers: path/text conversion, JSON escaping, the minimal
// stored-deflate PNG writer, and RGBA statistics.

#include <cstdint>
#include <string>

namespace veyra::harness::util {

std::string narrowText(const std::wstring& text);
std::string jsonEscape(const std::string& text);
bool writeTextFileUtf8(const std::wstring& path, const std::string& content);
bool writeRgbaPng(const std::wstring& path, const uint8_t* pixels, uint32_t width, uint32_t height, size_t sourceRowPitch);
std::wstring ownExePath();
std::string osBuildString();

struct RgbaStats {
    double meanLuma = 0.0;
    double minLuma = 1.0;
    double maxLuma = 0.0;
    double stddev = 0.0;
    bool allZero = true;
    bool constant = true;
    std::string sha256;
};

RgbaStats analyzeRgba(const uint8_t* pixels, uint32_t width, uint32_t height, size_t rowPitch);

} // namespace veyra::harness::util
