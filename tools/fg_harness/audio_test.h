#pragma once

// WASAPI event-mode audio probe (Playbook section 22, P6.4). Renders a
// synthesized sine to the default endpoint in shared mode with
// AUDCLNT_STREAMFLAGS_EVENTCALLBACK, measures drift/underruns, and verifies
// pause/flush semantics (Stop + Reset empties the endpoint buffer).

#include <cstdint>
#include <string>

namespace veyra::harness {

struct AudioTestArgs {
    std::string runId;
    std::wstring logFile;
    std::wstring jsonFile;
};

// Returns 0 only when: event mode active, steady playback has zero underruns,
// every planned frame was written, drift stays bounded, and Stop+Reset
// flushed the endpoint buffer.
int runAudioTest(const AudioTestArgs& args);

} // namespace veyra::harness
