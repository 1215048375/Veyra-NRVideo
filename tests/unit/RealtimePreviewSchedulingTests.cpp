// Realtime preview scheduling policy tests (REALTIME_AV_SCHEDULING_REPAIR P2/P3).
// Pure PTS/clock mathematics: no GPU, no WASAPI. The simulations mirror the
// engine's streaming candidate drop — decode is ordered and instantaneous
// relative to enhancement cost; only the enhancement opportunity of an
// expired candidate is dropped.
#include "veyra/engine/RealtimePreviewScheduling.h"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace veyra::engine;

namespace {
int failures = 0;
void check(bool ok, const char* text) {
    std::cout << (ok ? "PASS " : "FAIL ") << text << '\n';
    failures += !ok;
}

struct Simulation {
    std::vector<double> submittedPts;
    uint64_t skipped = 0;
    std::vector<double> completionLatenessMs;
};

// Mirrors the engine loop: the source is ready at its PTS, enhancement of one
// submitted frame costs costMs, and the next selection happens when the GPU
// batch completed. Expired candidates are dropped while a newer decoded frame
// exists; the final candidate before EOF is always kept.
Simulation simulateFilePreview(double intervalMs, double costMs, double durationMs) {
    Simulation sim;
    double readyAt = 0;      // wall/media clock when the engine can select
    double nextPts = 0;      // next decoded candidate PTS
    while (nextPts < durationMs) {
        double pts = nextPts;
        while (previewCandidateExpired(readyAt, pts, intervalMs) && pts + intervalMs < durationMs) {
            ++sim.skipped;
            pts += intervalMs;
        }
        sim.submittedPts.push_back(pts);
        sim.completionLatenessMs.push_back(readyAt + costMs - pts);
        readyAt += costMs;
        nextPts = pts + intervalMs;
    }
    return sim;
}
}

int main() {
    // --- candidate expiry boundaries
    check(!previewCandidateExpired(0, 0, 16.7), "window start is not expired");
    check(!previewCandidateExpired(16.0, 0, 16.7), "inside window is not expired");
    check(!previewCandidateExpired(16.71, 0, 16.7), "grace epsilon keeps the boundary frame");
    check(previewCandidateExpired(16.72, 0, 16.7), "just past the window is expired");
    check(!previewCandidateExpired(1000, 500, 0), "unknown cadence never drops a candidate");
    check(!previewGeneratedExpired(105, 100), "generated result within 10ms grace is presentable");
    check(previewGeneratedExpired(110.2, 100), "generated result past the grace is expired");

    // --- 30fps source, 15fps capacity: uniform every-other coverage
    {
        const auto sim = simulateFilePreview(1000.0 / 30, 67.0, 3000.0);
        std::cout << "30to15 submitted=" << sim.submittedPts.size() << " skipped=" << sim.skipped << '\n';
        check(sim.submittedPts.size() >= 42 && sim.submittedPts.size() <= 48,
            "30fps at half capacity submits about half the frames");
        // Exclude the EOF tail (final candidates are always kept regardless
        // of expiry) and the warmup pair.
        bool uniform = sim.submittedPts.size() > 2;
        double maxGap = 0;
        for (size_t i = 2; i < sim.submittedPts.size(); ++i) {
            if (sim.submittedPts[i] > 3000.0 - 2 * (1000.0 / 30)) break;
            const double gap = sim.submittedPts[i] - sim.submittedPts[i - 1];
            maxGap = std::max(maxGap, gap);
            uniform &= gap > 1.5 * (1000.0 / 30) && gap < 2.5 * (1000.0 / 30);
        }
        check(uniform && maxGap > 0 && maxGap < 2.5 * (1000.0 / 30),
            "30->15 sampling spreads across the whole second, not front-loaded");
        check(sim.submittedPts.back() >= 3000.0 - 3 * (1000.0 / 30),
            "sampling reaches the media tail instead of stalling");
        double maxLate = 0;
        for (double late : sim.completionLatenessMs) maxLate = std::max(maxLate, late);
        check(maxLate < 70.0 + 2 * (1000.0 / 30) + 0.5,
            "steady-state display lateness stays bounded by one completion plus window");
    }

    // --- 60fps source, 51/60 capacity: skips distributed, lateness bounded
    {
        const auto sim = simulateFilePreview(1000.0 / 60, 1000.0 / 51, 4000.0);
        std::cout << "60underrate submitted=" << sim.submittedPts.size() << " skipped=" << sim.skipped << '\n';
        check(sim.skipped > 0 && sim.skipped < sim.submittedPts.size() / 3,
            "51/60 under-rate skips a bounded fraction, not most frames");
        double maxGap = 0;
        for (size_t i = 1; i < sim.submittedPts.size(); ++i)
            maxGap = std::max(maxGap, sim.submittedPts[i] - sim.submittedPts[i - 1]);
        check(maxGap <= 3 * (1000.0 / 60) + 0.01, "60fps under-rate never leaves a long display gap");
        double maxLate = 0;
        for (double late : sim.completionLatenessMs) maxLate = std::max(maxLate, late);
        check(maxLate < 100.0, "under-rate display lateness stays bounded (no runaway backlog)");
    }

    // --- extreme overload: still submits the freshest candidate (no black)
    {
        const auto sim = simulateFilePreview(1000.0 / 60, 200.0, 2000.0);
        std::cout << "extreme submitted=" << sim.submittedPts.size() << '\n';
        check(!sim.submittedPts.empty(), "extreme overload still presents candidates");
        bool spaced = true;
        for (size_t i = 1; i < sim.submittedPts.size(); ++i)
            spaced &= sim.submittedPts[i] - sim.submittedPts[i - 1] > 0;
        check(spaced, "submissions stay PTS-ordered and strictly increasing");
        check(sim.submittedPts.back() >= 2000.0 - 3 * (1000.0 / 60), "even extreme overload keeps covering the timeline");
    }

    // --- XeSS generation gate hysteresis
    {
        XessGenerationGate gate;
        const double interval = 1000.0 / 60;
        bool changed = false;
        for (int i = 0; i < 11; ++i) changed |= gate.observe(50.0, interval);
        check(!changed && !gate.suppressed(), "eleven late frames do not yet suppress XeSS generation");
        check(gate.observe(50.0, interval) && gate.suppressed(), "twelfth sustained late frame suppresses");
        bool flapped = false;
        for (int i = 0; i < 200 && !flapped; ++i)
            flapped = i % 2 ? gate.observe(5.0, interval) : gate.observe(50.0, interval);
        check(gate.suppressed() && !flapped, "flapping lateness while suppressed never re-enables");
        gate.observe(50.0, interval); // clear the healthy streak left by the flap tail
        bool resumed = false;
        for (int i = 0; i < 59; ++i) resumed |= gate.observe(5.0, interval);
        check(!resumed && gate.suppressed(), "fifty-nine healthy frames keep generation suppressed");
        check(gate.observe(5.0, interval) && !gate.suppressed(), "sixtieth healthy frame re-enables generation");
        gate.observe(50.0, interval);
        gate.reset();
        check(!gate.suppressed(), "reset clears suppression state");
    }

    std::cout << "failures=" << failures << '\n';
    return failures ? 1 : 0;
}
