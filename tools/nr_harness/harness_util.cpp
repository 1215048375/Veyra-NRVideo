#include "harness_util.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <fstream>
#include <vector>

#include "veyra/FileIdentity.h"

namespace veyra::harness::util {

std::string narrowText(const std::wstring& text)
{
    if (text.empty()) {
        return {};
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        return {};
    }
    std::string converted(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), converted.data(), length, nullptr, nullptr);
    return converted;
}

std::string jsonEscape(const std::string& text)
{
    std::string escaped;
    escaped.reserve(text.size() + 8);
    for (const char c : text) {
        switch (c) {
        case '"': escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                escaped += std::format("\\u{:04x}", static_cast<unsigned char>(c));
            }
            else {
                escaped.push_back(c);
            }
            break;
        }
    }
    return escaped;
}

bool writeTextFileUtf8(const std::wstring& path, const std::string& content)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const BOOL ok = WriteFile(file, content.data(), static_cast<DWORD>(content.size()), &written, nullptr);
    CloseHandle(file);
    return ok != FALSE && written == content.size();
}

namespace {

uint32_t crc32Of(const uint8_t* data, size_t size)
{
    static uint32_t table[256];
    static bool initialized = false;
    if (!initialized) {
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            table[n] = c;
        }
        initialized = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

uint32_t adler32Of(const uint8_t* data, size_t size)
{
    uint32_t a = 1;
    uint32_t b = 0;
    for (size_t i = 0; i < size; ++i) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

} // namespace

bool writeRgbaPng(const std::wstring& path, const uint8_t* pixels, uint32_t width, uint32_t height, size_t sourceRowPitch)
{
    const size_t rowBytes = static_cast<size_t>(width) * 4;
    std::vector<uint8_t> raw((rowBytes + 1) * height);
    for (uint32_t y = 0; y < height; ++y) {
        raw[y * (rowBytes + 1)] = 0; // filter: none
        memcpy(raw.data() + y * (rowBytes + 1) + 1, pixels + y * sourceRowPitch, rowBytes);
    }

    std::vector<uint8_t> zlibStream;
    zlibStream.push_back(0x78);
    zlibStream.push_back(0x01);
    size_t offset = 0;
    while (offset < raw.size()) {
        const size_t chunk = std::min<size_t>(raw.size() - offset, 65535);
        const bool finalBlock = offset + chunk >= raw.size();
        zlibStream.push_back(finalBlock ? 1 : 0);
        zlibStream.push_back(static_cast<uint8_t>(chunk & 0xFF));
        zlibStream.push_back(static_cast<uint8_t>((chunk >> 8) & 0xFF));
        zlibStream.push_back(static_cast<uint8_t>(~chunk & 0xFF));
        zlibStream.push_back(static_cast<uint8_t>((~chunk >> 8) & 0xFF));
        zlibStream.insert(zlibStream.end(), raw.begin() + static_cast<ptrdiff_t>(offset),
            raw.begin() + static_cast<ptrdiff_t>(offset + chunk));
        offset += chunk;
    }
    const uint32_t adler = adler32Of(raw.data(), raw.size());
    zlibStream.push_back(static_cast<uint8_t>((adler >> 24) & 0xFF));
    zlibStream.push_back(static_cast<uint8_t>((adler >> 16) & 0xFF));
    zlibStream.push_back(static_cast<uint8_t>((adler >> 8) & 0xFF));
    zlibStream.push_back(static_cast<uint8_t>(adler & 0xFF));

    std::vector<uint8_t> png{ 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    const auto appendChunk = [&png](const char type[5], const std::vector<uint8_t>& payload) {
        const uint32_t length = static_cast<uint32_t>(payload.size());
        png.push_back(static_cast<uint8_t>((length >> 24) & 0xFF));
        png.push_back(static_cast<uint8_t>((length >> 16) & 0xFF));
        png.push_back(static_cast<uint8_t>((length >> 8) & 0xFF));
        png.push_back(static_cast<uint8_t>(length & 0xFF));
        std::vector<uint8_t> body(type, type + 4);
        body.insert(body.end(), payload.begin(), payload.end());
        png.insert(png.end(), body.begin(), body.end());
        const uint32_t crc = crc32Of(body.data(), body.size());
        png.push_back(static_cast<uint8_t>((crc >> 24) & 0xFF));
        png.push_back(static_cast<uint8_t>((crc >> 16) & 0xFF));
        png.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
        png.push_back(static_cast<uint8_t>(crc & 0xFF));
    };
    std::vector<uint8_t> ihdr(13);
    ihdr[0] = static_cast<uint8_t>((width >> 24) & 0xFF);
    ihdr[1] = static_cast<uint8_t>((width >> 16) & 0xFF);
    ihdr[2] = static_cast<uint8_t>((width >> 8) & 0xFF);
    ihdr[3] = static_cast<uint8_t>(width & 0xFF);
    ihdr[4] = static_cast<uint8_t>((height >> 24) & 0xFF);
    ihdr[5] = static_cast<uint8_t>((height >> 16) & 0xFF);
    ihdr[6] = static_cast<uint8_t>((height >> 8) & 0xFF);
    ihdr[7] = static_cast<uint8_t>(height & 0xFF);
    ihdr[8] = 8;  // bit depth
    ihdr[9] = 6;  // RGBA
    appendChunk("IHDR", ihdr);
    appendChunk("IDAT", zlibStream);
    appendChunk("IEND", {});

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const BOOL ok = WriteFile(file, png.data(), static_cast<DWORD>(png.size()), &written, nullptr);
    CloseHandle(file);
    return ok != FALSE && written == png.size();
}

std::wstring ownExePath()
{
    wchar_t buffer[MAX_PATH * 2]{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    if (length == 0 || length >= std::size(buffer)) {
        return {};
    }
    return std::wstring(buffer, length);
}

std::string osBuildString()
{
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr) {
        return {};
    }
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    const auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")));
    if (rtlGetVersion == nullptr) {
        return {};
    }
    RTL_OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    if (rtlGetVersion(&info) != 0) {
        return {};
    }
    return std::format("{}.{}.{}", info.dwMajorVersion, info.dwMinorVersion, info.dwBuildNumber);
}

RgbaStats analyzeRgba(const uint8_t* pixels, uint32_t width, uint32_t height, size_t rowPitch)
{
    RgbaStats stats;
    double sum = 0.0;
    double sumSquares = 0.0;
    uint64_t count = 0;
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* row = pixels + y * rowPitch;
        for (uint32_t x = 0; x < width; ++x) {
            const double r = row[x * 4 + 0] / 255.0;
            const double g = row[x * 4 + 1] / 255.0;
            const double b = row[x * 4 + 2] / 255.0;
            const double luma = 0.2126 * r + 0.7152 * g + 0.0722 * b;
            sum += luma;
            sumSquares += luma * luma;
            stats.minLuma = std::min(stats.minLuma, luma);
            stats.maxLuma = std::max(stats.maxLuma, luma);
            if (row[x * 4] != 0 || row[x * 4 + 1] != 0 || row[x * 4 + 2] != 0) {
                stats.allZero = false;
            }
            ++count;
        }
    }
    const double mean = count > 0 ? sum / static_cast<double>(count) : 0.0;
    const double variance = count > 0 ? std::max(0.0, sumSquares / static_cast<double>(count) - mean * mean) : 0.0;
    stats.meanLuma = mean;
    stats.stddev = std::sqrt(variance);
    stats.constant = stats.maxLuma == stats.minLuma;
    std::vector<uint8_t> packed(static_cast<size_t>(width) * height * 4);
    for (uint32_t y = 0; y < height; ++y) {
        memcpy(packed.data() + static_cast<size_t>(y) * width * 4, pixels + y * rowPitch, static_cast<size_t>(width) * 4);
    }
    stats.sha256 = sha256Hex(packed.data(), packed.size());
    return stats;
}

} // namespace veyra::harness::util
