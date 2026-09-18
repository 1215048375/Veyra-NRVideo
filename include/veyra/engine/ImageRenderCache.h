#pragma once
#include "veyra/engine/EnhancementSettings.h"
#include "veyra/sink/ImageExportSink.h"
#include <filesystem>
namespace veyra::engine {
// Optional, lossless SDR result cache. Never overwrites the source image.
class ImageRenderCache {
public:
    static std::string sourceKey(const std::wstring&,const sink::RgbaImage&);
    static std::string settingsKey(const EnhancementSettings&);
    static std::filesystem::path entry(const std::wstring&,const std::string&,const EnhancementSettings&);
    static bool load(const std::filesystem::path&,sink::RgbaImage&);
    static bool save(const std::filesystem::path&,const sink::RgbaImage&);
};
}
