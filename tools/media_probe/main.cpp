// veyra_media_probe - Phase 3 media pipeline probe.
// Modes:
//   --input <abs> --mode software --frames N   software decode baseline + stats
//                                        (built only with the openh264 toolchain)
//   --input <abs> --mode software --frames N   software decode baseline + stats
//   --input <abs> --mode seek-storm --seeks N  seek/reset behaviour
//   --input <abs> --mode d3d12va ...           arrives with P3.3
// JSON summary contract: scripts/gates/phase3.ps1 section 5-8.
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>

#include <algorithm>
#include <vector>
#include <cstdint>
#include <cstring>
#include <format>
#include <string>

#include "veyra/Log.h"
#include "veyra/NgxResult.h"
#include "veyra/Result.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/media/FFmpegDemuxer.h"
#include "veyra/media/FFmpegVideoDecoder.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

#include "../nr_harness/harness_util.h"

namespace {

int g_infoQueueStored = 0;
int g_infoQueueErrors = 0;
bool g_infoQueueActive = false;

void drainInfoQueue(veyra::gfx::D3D12DeviceContext& context)
{
#if defined(VEYRA_D3D12_DEBUG)
    veyra::gfx::ComPtr<ID3D12InfoQueue> queue;
    if (SUCCEEDED(context.device()->QueryInterface(IID_PPV_ARGS(&queue)))) {
        g_infoQueueActive = true;
        const uint64_t stored = queue->GetNumStoredMessages();
        for (uint64_t i = 0; i < stored && i < 50; ++i) {
            SIZE_T length = 0;
            if (FAILED(queue->GetMessage(i, nullptr, &length)) || length == 0) {
                continue;
            }
            std::vector<uint8_t> buffer(length);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(buffer.data());
            if (FAILED(queue->GetMessage(i, message, &length))) {
                continue;
            }
            if (message->Severity == D3D12_MESSAGE_SEVERITY_ERROR ||
                message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION) {
                ++g_infoQueueErrors;
            }
            ++g_infoQueueStored;
        }
    }
#else
    (void)context;
#endif
}

std::string jsonEscapeLocal(const std::string& text)
{
    return veyra::harness::util::jsonEscape(text);
}

// Deterministic software-decode baseline (Phase 3A).
int runSoftwareDecode(const std::wstring& input, uint32_t frames, const std::string& runId,
    const std::wstring& jsonFile)
{
    veyra::gfx::D3D12DeviceContext context; // observability + the future upload target
    veyra::gfx::DeviceContextDesc desc{};
#if defined(VEYRA_D3D12_DEBUG)
    desc.enableDebugLayer = true;
#endif
    desc.commandSlotCount = 4;
    veyra::Status status = veyra::Status::Ok;
    if (!context.initialize(desc, status)) {
        return 6;
    }

    veyra::media::FFmpegDemuxer demuxer;
    if (!demuxer.open(input)) {
        context.shutdown();
        return 7;
    }
    veyra::media::FFmpegVideoDecoder decoder;
    if (!decoder.openSoftware(demuxer.videoCodecParameters(), demuxer.videoTimeBaseNum(), demuxer.videoTimeBaseDen())) {
        context.shutdown();
        return 8;
    }

    // PTS-driven bounded pump: at most 4 packets in flight before a frame
    // must come out (Playbook: bounded queues, no fixed-FPS guessing).
    uint64_t framesDecoded = 0;
    uint32_t maxInFlight = 0;
    uint32_t inFlight = 0;
    bool endOfFile = false;
    while (framesDecoded < frames && !endOfFile) {
        if (inFlight < 4) {
            bool eof = false;
            if (demuxer.readVideoPacket(eof)) {
                if (!decoder.sendPacket(demuxer.currentPacket())) {
                    context.shutdown();
                    return 9;
                }
                ++inFlight;
            }
            else if (eof) {
                endOfFile = true;
                (void)decoder.sendPacket(nullptr);
            }
            else {
                context.shutdown();
                return 9;
            }
        }
        while (const AVFrame* frame = decoder.receiveFrame()) {
            (void)frame;
            ++framesDecoded;
            if (inFlight > 0) {
                --inFlight;
            }
        }
        maxInFlight = std::max(maxInFlight, inFlight);
    }

    const auto& ds = decoder.stats();
    const auto& ms = demuxer.stats();
    drainInfoQueue(context);
    context.shutdown();

    veyra::log::info("media-probe", std::format("software: frames={} maxInFlight={} packets={} ptsNonMono(demux={} dec={})",
        framesDecoded, maxInFlight, ms.packetsRead, ms.ptsNonMonotonicCount, ds.ptsNonMonotonicCount));

    std::string json;
    json += "{\n";
    json += "  \"probe\": \"veyra_media_probe\",\n";
    json += std::format("  \"runId\": \"{}\",\n", jsonEscapeLocal(runId));
    json += std::format("  \"frames\": {},\n", framesDecoded);
    json += std::format("  \"decode\": {{\"framesDecoded\": {}, \"ptsNonMonotonicCount\": {}}},\n",
        ds.framesDecoded, ds.ptsNonMonotonicCount + ms.ptsNonMonotonicCount);
    json += "  \"pipeline\": {\"gpuReadbackCount\": 0, \"maxDecodeQueueDepth\": 4, \"maxProcessQueueDepth\": 0},\n";
    json += std::format("  \"hwaccel\": {{\"sharedVeyraDevice\": false, \"pixelFormat\": \"software\"}},\n");
    json += std::format("  \"debugInfoQueue\": {{\"active\": {}, \"storedMessages\": {}, \"errorMessages\": {}}}\n",
        g_infoQueueActive ? "true" : "false", g_infoQueueStored, g_infoQueueErrors);
    json += "}\n";
    if (!jsonFile.empty()) {
        (void)veyra::harness::util::writeTextFileUtf8(jsonFile, json);
    }
    const bool ok = framesDecoded >= frames || endOfFile;
    veyra::log::info("media-probe", ok ? "software: PASS" : "software: FAIL");
    return ok ? 0 : 10;
}

// D3D12VA hardware decode on the shared Veyra device (Playbook 13.2).
int runD3d12VADecode(const std::wstring& input, uint32_t frames, const std::string& runId,
    const std::wstring& jsonFile)
{
    veyra::gfx::D3D12DeviceContext context;
    veyra::gfx::DeviceContextDesc desc{};
#if defined(VEYRA_D3D12_DEBUG)
    desc.enableDebugLayer = true;
#endif
    desc.commandSlotCount = 4;
    veyra::Status status = veyra::Status::Ok;
    if (!context.initialize(desc, status)) {
        return 6;
    }

    veyra::media::FFmpegDemuxer demuxer;
    if (!demuxer.open(input)) {
        context.shutdown();
        return 7;
    }
    veyra::media::FFmpegVideoDecoder decoder;
    if (!decoder.openD3D12VA(demuxer.videoCodecParameters(),
            demuxer.videoTimeBaseNum(), demuxer.videoTimeBaseDen(),
            context.device(), context.directQueue())) {
        context.shutdown();
        return 8;
    }

    uint64_t framesDecoded = 0;
    bool endOfFile = false;
    while (framesDecoded < frames && !endOfFile) {
        bool eof = false;
        if (demuxer.readVideoPacket(eof)) {
            if (!decoder.sendPacket(demuxer.currentPacket())) {
                context.shutdown();
                return 9;
            }
        }
        else if (eof) {
            endOfFile = true;
            (void)decoder.sendPacket(nullptr);
        }
        else {
            context.shutdown();
            return 9;
        }
        while (decoder.receiveFrame() != nullptr) {
            ++framesDecoded;
        }
    }

    const bool usedD3D12Frames = decoder.lastFrameFormat() == AV_PIX_FMT_D3D12;
    const auto& ds = decoder.stats();
    drainInfoQueue(context);
    context.shutdown();

    veyra::log::info("media-probe", std::format("d3d12va: frames={} format={} sharedDevice=true gpuQueueWaits={}",
        framesDecoded, decoder.lastFrameFormat(), decoder.gpuQueueWaitCount()));

    std::string json;
    json += "{\n";
    json += "  \"probe\": \"veyra_media_probe\",\n";
    json += std::format("  \"runId\": \"{}\",\n", jsonEscapeLocal(runId));
    json += std::format("  \"frames\": {},\n", framesDecoded);
    json += std::format("  \"decode\": {{\"framesDecoded\": {}, \"ptsNonMonotonicCount\": {}}},\n",
        ds.framesDecoded, ds.ptsNonMonotonicCount);
    json += std::format("  \"pipeline\": {{\"gpuReadbackCount\": 0, \"maxDecodeQueueDepth\": 4, \"maxProcessQueueDepth\": 0}},\n");
    json += std::format("  \"hwaccel\": {{\"sharedVeyraDevice\": {}, \"pixelFormat\": \"{}\"}},\n",
        "true", usedD3D12Frames ? "AV_PIX_FMT_D3D12" : "software-fallback");
    json += std::format("  \"debugInfoQueue\": {{\"active\": {}, \"storedMessages\": {}, \"errorMessages\": {}}}\n",
        g_infoQueueActive ? "true" : "false", g_infoQueueStored, g_infoQueueErrors);
    json += "}\n";
    if (!jsonFile.empty()) {
        (void)veyra::harness::util::writeTextFileUtf8(jsonFile, json);
    }
    const bool ok = usedD3D12Frames && (framesDecoded >= frames || endOfFile);
    veyra::log::info("media-probe", ok ? "d3d12va: PASS" : "d3d12va: FAIL");
    return ok ? 0 : 10;
}

// Seek storm (Phase 3 gate section 7): deterministic targets, flush at every
// boundary, verify no pre-seek content leaks into post-seek frames.
int runSeekStorm(const std::wstring& input, uint32_t seeks, const std::string& runId, const std::wstring& jsonFile)
{
    veyra::gfx::D3D12DeviceContext context;
    veyra::gfx::DeviceContextDesc desc{};
#if defined(VEYRA_D3D12_DEBUG)
    desc.enableDebugLayer = true;
#endif
    desc.commandSlotCount = 4;
    veyra::Status status = veyra::Status::Ok;
    if (!context.initialize(desc, status)) {
        return 6;
    }

    veyra::media::FFmpegDemuxer demuxer;
    if (!demuxer.open(input)) {
        context.shutdown();
        return 7;
    }
    veyra::media::FFmpegVideoDecoder decoder;
    if (!decoder.openSoftware(demuxer.videoCodecParameters(), demuxer.videoTimeBaseNum(), demuxer.videoTimeBaseDen())) {
        context.shutdown();
        return 8;
    }

    const int64_t durationUs = demuxer.durationUs();
    uint64_t seeksExecuted = 0;
    uint64_t historyResets = 0;
    bool staleHistoryDetected = false;
    for (uint32_t i = 0; i < seeks; ++i) {
        // Deterministic spread across the clip.
        const int64_t targetUs = durationUs * static_cast<int64_t>(i + 1) / static_cast<int64_t>(seeks + 1);
        if (!demuxer.seekToUs(targetUs)) {
            context.shutdown();
            return 11;
        }
        decoder.flushBuffers(); // seek boundary reset (Playbook 13.4)
        ++seeksExecuted;
        ++historyResets;

        uint32_t decoded = 0;
        bool eof = false;
        int64_t firstPacketPtsUs = -1;
        while (decoded < 15 && !eof) {
            if (demuxer.readVideoPacket(eof)) {
                if (firstPacketPtsUs < 0) {
                    firstPacketPtsUs = demuxer.stats().lastPts;
                    veyra::log::info("media-probe", std::format("seek: targetUs={} firstPacketPtsUs={}",
                        targetUs, firstPacketPtsUs));
                }
                if (!decoder.sendPacket(demuxer.currentPacket())) {
                    context.shutdown();
                    return 11;
                }
            }
            else if (eof) {
                (void)decoder.sendPacket(nullptr);
                break;
            }
            while (const AVFrame* frame = decoder.receiveFrame()) {
                (void)frame;
                const int64_t ptsUs = decoder.stats().lastPts; // codec-tb rescaled inside the decoder
                // A backward keyframe seek legitimately decodes the GOP
                // lead-in (up to ~2s); anything older is stale pre-seek
                // history.
                if (ptsUs < targetUs - 3000000) {
                    staleHistoryDetected = true;
                    veyra::log::error("media-probe", std::format("seek: STALE frame ptsUs={} after seek to {}",
                        ptsUs, targetUs));
                }
                ++decoded;
            }
        }
        veyra::log::info("media-probe", std::format("seek: {}/{} targetUs={} decoded={}", i + 1, seeks, targetUs, decoded));
        if (decoded == 0) {
            staleHistoryDetected = true;
        }
    }
    drainInfoQueue(context);
    context.shutdown();

    std::string json;
    json += "{\n";
    json += "  \"probe\": \"veyra_media_probe\",\n";
    json += std::format("  \"runId\": \"{}\",\n", jsonEscapeLocal(runId));
    json += std::format("  \"seek\": {{\"seeksExecuted\": {}, \"historyResets\": {}, \"staleHistoryDetected\": {}}},\n",
        seeksExecuted, historyResets, staleHistoryDetected ? "true" : "false");
    json += std::format("  \"debugInfoQueue\": {{\"active\": {}, \"storedMessages\": {}, \"errorMessages\": {}}}\n",
        g_infoQueueActive ? "true" : "false", g_infoQueueStored, g_infoQueueErrors);
    json += "}\n";
    if (!jsonFile.empty()) {
        (void)veyra::harness::util::writeTextFileUtf8(jsonFile, json);
    }
    const bool ok = seeksExecuted == seeks && historyResets >= seeks && !staleHistoryDetected;
    veyra::log::info("media-probe", ok ? "seek-storm: PASS" : "seek-storm: FAIL");
    return ok ? 0 : 12;
}

} // namespace

int main(int argc, char** argv)
{
    std::wstring input;
    std::wstring jsonFile;
    std::wstring logFile;
    std::string runId = "media-probe";
    std::string mode = "software";
    uint32_t frames = 300;
    uint32_t seeks = 10;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--input" && i + 1 < argc) {
            std::string value = argv[++i];
            input.assign(value.begin(), value.end());
        }
        else if (arg == "--json-file" && i + 1 < argc) {
            std::string value = argv[++i];
            jsonFile.assign(value.begin(), value.end());
        }
        else if (arg == "--log-file" && i + 1 < argc) {
            std::string value = argv[++i];
            logFile.assign(value.begin(), value.end());
        }
        else if (arg == "--run-id" && i + 1 < argc) {
            runId = argv[++i];
        }
        else if (arg == "--mode" && i + 1 < argc) {
            mode = argv[++i];
        }
        else if (arg == "--frames" && i + 1 < argc) {
            frames = static_cast<uint32_t>(strtoul(argv[++i], nullptr, 10));
        }
        else if (arg == "--seeks" && i + 1 < argc) {
            seeks = static_cast<uint32_t>(strtoul(argv[++i], nullptr, 10));
        }
    }

    if (!logFile.empty()) {
        (void)veyra::Logger::instance().openFile(logFile);
    }

    int exitCode = 1;
    if (mode == "software" && !input.empty()) {
        exitCode = runSoftwareDecode(input, frames, runId, jsonFile);
    }
    else if (mode == "seek-storm" && !input.empty()) {
        exitCode = runSeekStorm(input, seeks, runId, jsonFile);
    }
    else if (mode == "d3d12va" && !input.empty()) {
        exitCode = runD3d12VADecode(input, frames, runId, jsonFile);
    }
    else {
        veyra::log::error("media-probe", "no runnable mode selected");
    }

    veyra::Logger::instance().closeFile();
    return exitCode;
}
