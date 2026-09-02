#pragma once

// Phase 2 parity comparison: Original (linear FP16) -> ParityEncode ->
// Feature 18 Evaluate -> ParityDecode -> Final, with GPU-vs-CPU deltas,
// four-stage captures and the neutral-baseline record (Playbook sections
// 9.1-9.5).
#include <cstdint>
#include <string>

#include "frame_loop.h"

namespace veyra::harness {

int runParityCompare(const FrameLoopArgs& args);

} // namespace veyra::harness
