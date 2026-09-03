#pragma once

// Phase 4 SR test: bypass at 1:1, upscale at 540p->1080p, resize/recreate.
// (No NGX includes here to avoid /W4 warnings in consumers.)
#include <cstdint>
#include <string>

#include "frame_loop.h"

namespace veyra::harness {

int runSrTest(const FrameLoopArgs& args);

} // namespace veyra::harness
