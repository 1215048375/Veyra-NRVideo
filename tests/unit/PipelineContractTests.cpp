// Pipeline contract unit tests (Playbook R2): exact timestamps, frame flags,
// fixed window with bounded lookahead, GPU handle lifetime fields, and the
// ResetCoordinator frame-boundary epoch semantics. Pure CPU - no D3D12 device.
#include <cstdio>
#include <cstring>

#include "veyra/pipeline/FramePacket.h"
#include "veyra/pipeline/FrameWindow.h"
#include "veyra/pipeline/GuidanceFrame.h"
#include "veyra/pipeline/ResetCoordinator.h"

using namespace veyra::pipeline;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const char* name, bool ok, const char* detail = "") {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  [FAIL] %s :: %s\n", name, detail);
    } else {
        std::printf("  [PASS] %s\n", name);
    }
}

FramePacket makePacket(uint64_t seq, uint64_t epoch) {
    FramePacket p;
    p.sequence = seq;
    p.pts = Rational{static_cast<int64_t>(seq) * 1001, 60000};
    p.duration = Rational{1001, 60000};
    p.sourceKind = SourceKind::File;
    p.sourceEpoch = epoch;
    p.color.resource = reinterpret_cast<ID3D12Resource*>(0x10 + seq);
    p.color.width = 3840;
    p.color.height = 2160;
    p.color.format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    p.color.expectedState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    p.color.ownerSlot = 2;
    p.color.readyFenceValue = 100 + seq;
    return p;
}

} // namespace

int main() {
    std::printf("pipeline-contract: rational timestamps\n");
    {
        const Rational a{1001, 60000};
        const Rational b{2002, 120000};
        const Rational neg{-5, 3};
        const Rational unk = Rational::unknown();
        check("rational:cross-equal", a.equals(b));
        check("rational:negative-representable", neg.isNegative() && neg.to100ns() < 0);
        check("rational:unknown-distinct-from-zero", unk.isUnknown() && !unk.isNegative());
        check("rational:unknown-not-equal-known-zero", !unk.equals(Rational{0, 1}));
        check("rational:100ns-rounding", Rational{1, 3}.to100ns() == 3333333
            && Rational{2, 3}.to100ns() == 6666667);
    }

    std::printf("pipeline-contract: color description + flags\n");
    {
        ColorDescription cd;
        cd.pixelFormat = SourcePixelFormat::NV12;
        cd.range = ColorRange::Limited;
        cd.matrix = YuvMatrix::BT709;
        cd.matrixAssumed = true;   // HD SDR documented default
        cd.rotationDegrees = 0;
        check("color:hdr-path-detected", [&] {
            ColorDescription hdr; hdr.pixelFormat = SourcePixelFormat::P010;
            return hdr.isHdrPath();
        }());
        check("color:assumed-flag-survives", cd.matrixAssumed && !cd.rangeAssumed);

        constexpr FrameFlags seekCut =
            static_cast<uint32_t>(FrameFlagBits::Seek) | static_cast<uint32_t>(FrameFlagBits::Cut);
        check("flags:seek-breaks-history", breaksHistory(static_cast<uint32_t>(FrameFlagBits::Seek)));
        check("flags:duplicate-does-not-break-history",
            !breaksHistory(static_cast<uint32_t>(FrameFlagBits::Duplicate)));
        check("flags:composite-breaks", breaksHistory(seekCut));
        check("flags:hasflag", hasFrameFlag(seekCut, FrameFlagBits::Cut)
            && !hasFrameFlag(seekCut, FrameFlagBits::Eos));
    }

    std::printf("pipeline-contract: gpu texture handle ownership\n");
    {
        FramePacket p = makePacket(7, 1);
        check("handle:present-requires-format-extent", [&] {
            GpuTextureHandle h = p.color;
            h.format = DXGI_FORMAT_UNKNOWN;
            return !h.present();
        }());
        check("handle:fence-ownership-fields", p.color.ownerSlot == 2 && p.color.readyFenceValue == 107
            && p.color.expectedState == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        check("packet:valid-requires-known-pts", [&] {
            FramePacket q = makePacket(1, 1);
            q.pts = Rational::unknown();
            return !q.valid();
        }());
    }

    std::printf("pipeline-contract: frame window bounded\n");
    {
        FrameWindow w;
        check("window:default-zero-lookahead", w.lookaheadFrames == 0 && w.consistentLookahead());
        w.lookaheadFrames = 1;
        check("window:fg-mode-1-ok", w.consistentLookahead());
        w.lookaheadFrames = 2;
        check("window:buffered-mode-2-ok", w.consistentLookahead());
        w.lookaheadFrames = 3;
        check("window:cap-3-rejected", !w.consistentLookahead());
        w.lookaheadFrames = 0xFFFFFFFFu;
        check("window:cap-overflow-rejected", !w.consistentLookahead());
        check("window:next-null-not-error", [&] {
            const FramePacket cur = makePacket(2, 4);
            FrameWindow v;
            v.current = &cur;
            v.lookaheadFrames = 1;   // mode says pair, but stream end has no C yet
            return v.valid() && v.consistentLookahead();
        }());

        const FramePacket a = makePacket(1, 4);
        const FramePacket b = makePacket(2, 4);
        const FramePacket c = makePacket(3, 4);
        FrameWindow ok;
        ok.prev = &a; ok.current = &b; ok.next = &c; ok.lookaheadFrames = 2;
        check("window:same-epoch", ok.sameSourceEpoch() && ok.valid());
        FramePacket stale = makePacket(0, 3);
        FrameWindow mixed;
        mixed.prev = &stale; mixed.current = &b;
        check("window:mixed-epoch-rejected", !mixed.sameSourceEpoch());
    }

    std::printf("pipeline-contract: guidance frame\n");
    {
        GuidanceFrame g;
        g.motion.resource = reinterpret_cast<ID3D12Resource*>(0x1);
        g.motion.format = DXGI_FORMAT_R16G16_FLOAT;
        g.depth.resource = reinterpret_cast<ID3D12Resource*>(0x2);
        g.depth.format = DXGI_FORMAT_R32_FLOAT;
        g.confidence.resource = reinterpret_cast<ID3D12Resource*>(0x3);
        g.confidence.format = DXGI_FORMAT_R8_UNORM;
        g.motion.width = g.depth.width = g.confidence.width = 3840;
        g.motion.height = g.depth.height = g.confidence.height = 2160;
        g.provenance = GuidanceProvenance::NvofDav2;
        g.sourceSequence = 42;
        g.sourceEpoch = 5;
        g.depthAgeFrames = 3;
        check("guidance:valid", g.valid());
        check("guidance:extent-match", g.extentsMatch(3840, 2160) && !g.extentsMatch(1920, 1080));
        check("guidance:provenance-name", std::strcmp(guidanceProvenanceName(GuidanceProvenance::NvofDav2), "NvofDav2") == 0);
    }

    std::printf("pipeline-contract: reset coordinator epochs\n");
    {
        ResetCoordinator rc;
        check("reset:initial-epoch-1", rc.epoch() == 1 && !rc.hasPendingReset());

        // All eight event reasons stage and advance the epoch.
        const ResetReason reasons[] = {
            ResetReason::Open, ResetReason::Seek, ResetReason::SceneCut,
            ResetReason::FrameDrop, ResetReason::Resize, ResetReason::SourceSwitch,
            ResetReason::PauseResume, ResetReason::DeviceLost,
        };
        uint64_t seq = 1;
        uint64_t expectedEpoch = 1;
        for (const ResetReason r : reasons) {
            rc.notifyReset(r);
            check("reset:pending-staged", rc.hasPendingReset());
            const uint64_t frameEpoch = rc.beginFrame(seq++);
            ++expectedEpoch;
            check("reset:epoch-advanced-once", frameEpoch == expectedEpoch
                && rc.epoch() == expectedEpoch && !rc.hasPendingReset());
        }
        check("reset:eight-reasons-counted", rc.totalResets() == 8);

        // Multiple events between boundaries collapse into one bump.
        rc.notifyReset(ResetReason::Seek);
        rc.notifyReset(ResetReason::SceneCut);
        rc.notifyReset(ResetReason::Resize);
        const uint64_t before = rc.epoch();
        const uint64_t frameEpoch = rc.beginFrame(seq);
        check("reset:collapse-one-bump", frameEpoch == before + 1 && rc.epoch() == before + 1);
        check("reset:all-events-counted", rc.totalResets() == 11);
        check("reset:first-reason-recorded", rc.lastReason() == ResetReason::Seek);

        // No pending event: epoch unchanged at the boundary.
        const uint64_t stable = rc.beginFrame(seq + 100);
        check("reset:stable-without-events", stable == rc.epoch());

        // Stale history detection.
        check("reset:stale-old-epoch", rc.isStale(1) && rc.isStale(rc.epoch() - 1));
        check("reset:current-not-stale", !rc.isStale(rc.epoch()));
        check("reset:none-ignored", [&] {
            ResetCoordinator r2;
            r2.notifyReset(ResetReason::None);
            return !r2.hasPendingReset() && r2.beginFrame(1) == 1;
        }());
    }

    std::printf("pipeline-contract: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
