#pragma once
#include <string>
#include <vector>
namespace veyra::engine {
struct SubtitleCue {double begin=0,end=0;std::wstring text;};
std::vector<SubtitleCue> loadSrt(const std::wstring& path);
std::wstring subtitleAt(const std::vector<SubtitleCue>&,double seconds);
}
