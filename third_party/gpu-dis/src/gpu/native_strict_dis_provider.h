// Distribution note (2026-09-09): project-owned additions and modifications
// are licensed under Apache-2.0. Upstream portions retain their original
// licenses and attribution; see NOTICE, PROVENANCE.md and licenses/.
// OpenCV DIS/reference-derived portions retain Intel (2000-2008), Willow
// Garage (2009) and other upstream copyrights and BSD/Apache terms.
// See licenses/OPENCV_DIS_BSD_HEADER.txt and OPENCV_APACHE_LICENSE.txt.

#pragma once
#include <memory>
#include <string>
#include <cstdint>

namespace xess_gpu {
class MotionProvider;
enum class StrictDisPreset { Fast, Medium };
enum class StrictDisInput { RawLuma8, Ffmpeg71RgbGrayLimited };
// Frozen CPU FAST/MEDIUM semantics, no caller-provided temporal initial flow.
// The caller owns producer waits, command submission, completion signaling,
// and source pool leases. NV12 Y and R8_UNORM crop views are supported.
std::unique_ptr<MotionProvider> make_strict_dis_provider(
    const std::string& shader_directory, StrictDisPreset preset=StrictDisPreset::Fast,
    StrictDisInput input=StrictDisInput::RawLuma8);
uint64_t strict_dis_shader_dispatches(const MotionProvider& provider) noexcept;
}
