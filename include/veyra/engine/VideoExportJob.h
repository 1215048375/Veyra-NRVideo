#pragma once
#include "veyra/engine/EngineController.h"
#include <functional>
namespace veyra::engine {
bool exportVideo(const std::wstring& input,const std::wstring& output,PlayerOptions,bool hevc,
                 std::atomic<bool>& cancel,const std::function<void(double,const std::wstring&)>& progress,
                 unsigned maxFrames=0);
}
