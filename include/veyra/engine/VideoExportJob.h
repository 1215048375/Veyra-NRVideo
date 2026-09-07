#pragma once
#include "veyra/engine/EngineController.h"
#include <functional>
namespace veyra::engine {
struct ExportCounts {uint64_t source=0,generated=0,holds=0,encoded=0;};
bool exportVideo(const std::wstring& input,const std::wstring& output,PlayerOptions,bool hevc,
                 std::atomic<bool>& cancel,const std::function<void(double,const std::wstring&)>& progress,
                 unsigned maxFrames=0,const std::function<bool()>& frameBoundary={},const std::function<void(const ExportCounts&)>& counts={});
}
