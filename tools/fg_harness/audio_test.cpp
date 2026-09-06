#include "audio_test.h"

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>

#include <cmath>
#include <format>
#include <string>
#include <vector>

#include "../nr_harness/harness_util.h"
#include "veyra/Log.h"

namespace veyra::harness {

namespace {

using util::jsonEscape;
using util::writeTextFileUtf8;

constexpr double kDurationSeconds = 4.0;
constexpr double kFrequencyHz = 440.0;

template <typename T>
void releaseCom(T*& p)
{
    if (p != nullptr) {
        p->Release();
        p = nullptr;
    }
}

} // namespace

int runAudioTest(const AudioTestArgs& args)
{
    bool eventMode = false;
    int64_t framesPlanned = 0;
    int64_t framesWritten = 0;
    int64_t underrunsSteady = 0;
    double maxAbsDriftMs = 0.0;
    bool pauseFlushWorks = false;
    uint32_t sampleRate = 0;
    uint32_t channels = 0;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool comInited = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* client = nullptr;
    IAudioRenderClient* render = nullptr;
    IAudioClock* audioClockPtrForCleanup = nullptr;
    HANDLE eventHandle = nullptr;

    if (comInited) {
        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator));
    }
    if (SUCCEEDED(hr) && enumerator != nullptr) {
        hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
        if (FAILED(hr)) {
            log::error("audio", std::format("GetDefaultAudioEndpoint hr=0x{:X}", static_cast<unsigned>(hr)));
        }
    }
    if (SUCCEEDED(hr) && device != nullptr) {
        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
            reinterpret_cast<void**>(&client));
        if (FAILED(hr)) {
            log::error("audio", std::format("device Activate hr=0x{:X}", static_cast<unsigned>(hr)));
        }
    }

    WAVEFORMATEX* mixFormat = nullptr;
    if (SUCCEEDED(hr) && client != nullptr) {
        hr = client->GetMixFormat(&mixFormat);
        if (SUCCEEDED(hr) && mixFormat != nullptr) {
            sampleRate = mixFormat->nSamplesPerSec;
            channels = mixFormat->nChannels;
            log::info("audio", std::format("mix format: {}Hz {}ch {}bits",
                mixFormat->nSamplesPerSec, mixFormat->nChannels, mixFormat->wBitsPerSample));
        } else {
            log::error("audio", std::format("GetMixFormat hr=0x{:X}", static_cast<unsigned>(hr)));
        }
    }

    // Event-mode shared stream.
    if (SUCCEEDED(hr) && client != nullptr && mixFormat != nullptr) {
        const REFERENCE_TIME bufferDuration = 10 * 10000; // 10 ms
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK, bufferDuration, 0, mixFormat, nullptr);
        if (SUCCEEDED(hr)) {
            eventMode = true;
        } else {
            log::error("audio", std::format("Initialize(event) hr=0x{:X}", static_cast<unsigned>(hr)));
        }
    }
    if (eventMode) {
        eventHandle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        hr = client->SetEventHandle(eventHandle);
        if (FAILED(hr)) {
            log::error("audio", std::format("SetEventHandle hr=0x{:X}", static_cast<unsigned>(hr)));
            eventMode = false;
        }
    }
    UINT32 bufferFrameCount = 0;
    if (eventMode && SUCCEEDED(hr)) {
        hr = client->GetBufferSize(&bufferFrameCount);
        if (FAILED(hr)) {
            log::error("audio", std::format("GetBufferSize hr=0x{:X}", static_cast<unsigned>(hr)));
            eventMode = false;
        } else {
            log::info("audio", std::format("endpoint buffer {} frames", bufferFrameCount));
        }
    }
    if (eventMode && SUCCEEDED(hr)) {
        hr = client->GetService(__uuidof(IAudioRenderClient),
            reinterpret_cast<void**>(&render));
        if (FAILED(hr)) {
            log::error("audio", std::format("GetService(render) hr=0x{:X}", static_cast<unsigned>(hr)));
            eventMode = false;
        }
    }

    DWORD avrtTaskIndex = 0;
    HANDLE avrtTask = nullptr;
    double phase = 0.0;
    const double phaseStep = 2.0 * 3.14159265358979323846 * kFrequencyHz / sampleRate;

    if (eventMode && render != nullptr) {
        // Audio clock for honest drift measurement (device clock vs QPC).
        // Position and frequency share device-clock units (frames or bytes
        // depending on the stream), so normalize by GetFrequency.
        IAudioClock*& audioClock = audioClockPtrForCleanup;
        uint64_t clockFrequency = 0;
        const bool hasClock = SUCCEEDED(client->GetService(__uuidof(IAudioClock),
            reinterpret_cast<void**>(&audioClock))) &&
            SUCCEEDED(audioClock->GetFrequency(&clockFrequency)) && clockFrequency > 0;
        UINT64 clockPos0 = 0, clockQpc0 = 0;
        if (hasClock) {
            (void)audioClock->GetPosition(&clockPos0, &clockQpc0);
            log::info("audio", std::format("audio clock frequency={} units/s", clockFrequency));
        }

        // Prefill the whole endpoint buffer with silence, then start.
        BYTE* data = nullptr;
        if (SUCCEEDED(render->GetBuffer(bufferFrameCount, &data))) {
            std::memset(data, 0, static_cast<size_t>(bufferFrameCount) * channels * 4);
            (void)render->ReleaseBuffer(bufferFrameCount, 0);
        }
        hr = client->Start();
        avrtTask = AvSetMmThreadCharacteristicsW(L"Pro Audio", &avrtTaskIndex);
        log::info("audio", "stream started (event mode, Pro Audio priority)");

        framesPlanned = static_cast<int64_t>(kDurationSeconds * sampleRate);
        // Drift samples for a least-squares rate estimate: IAudioClock
        // positions quantize to the engine pass (~10 ms), so single samples
        // cannot distinguish drift from quantization. The fitted slope
        // (deviation per second) times the run length is the honest drift.
        std::vector<double> driftWallMs;
        std::vector<double> driftOffsetMs;
        driftWallMs.reserve(512);
        driftOffsetMs.reserve(512);
        while (framesWritten < framesPlanned) {
            if (WaitForSingleObject(eventHandle, 2000) != WAIT_OBJECT_0) {
                ++underrunsSteady;
                log::error("audio", "event wait timeout (2000 ms)");
                break;
            }
            UINT32 padding = 0;
            if (FAILED(client->GetCurrentPadding(&padding))) {
                ++underrunsSteady;
                break;
            }
            const UINT32 available = bufferFrameCount - padding;
            if (available == 0) continue;
            const int64_t remaining = framesPlanned - framesWritten;
            const UINT32 toWrite = static_cast<UINT32>(
                std::min<int64_t>(available, remaining));
            BYTE* dest = nullptr;
            if (FAILED(render->GetBuffer(toWrite, &dest))) {
                ++underrunsSteady;
                break;
            }
            for (UINT32 i = 0; i < toWrite; ++i) {
                const float sample = 0.2f * static_cast<float>(std::sin(phase));
                phase += phaseStep;
                for (UINT32 ch = 0; ch < channels; ++ch) {
                    std::memcpy(dest + (static_cast<size_t>(i) * channels + ch) * 4, &sample, 4);
                }
            }
            (void)render->ReleaseBuffer(toWrite, 0);
            framesWritten += toWrite;

            // Collect (wall time, played-vs-wall offset) samples.
            if (hasClock) {
                UINT64 pos = 0, qpc = 0;
                if (SUCCEEDED(audioClock->GetPosition(&pos, &qpc))) {
                    const double playedMs = 1000.0 * static_cast<double>(pos - clockPos0) /
                        static_cast<double>(clockFrequency);
                    const double wallMs = static_cast<double>(qpc - clockQpc0) / 10000.0;
                    driftWallMs.push_back(wallMs);
                    driftOffsetMs.push_back(playedMs - wallMs);
                }
            }
        }

        // Drain: wait until the device finishes what we wrote.
        {
            for (int guard = 0; guard < 2000; ++guard) {
                UINT32 padding = 0;
                if (FAILED(client->GetCurrentPadding(&padding)) || padding == 0) break;
                Sleep(1);
            }
        }

        // Least-squares slope of offset vs wall time over the steady window
        // (skip the first 500 ms of startup). Drift = |slope| * run length.
        if (driftWallMs.size() >= 16) {
            double sumW = 0, sumO = 0, sumWW = 0, sumWO = 0;
            uint64_t n = 0;
            for (size_t i = 0; i < driftWallMs.size(); ++i) {
                if (driftWallMs[i] < 500.0) continue;
                const double w = driftWallMs[i];
                const double o = driftOffsetMs[i];
                sumW += w; sumO += o; sumWW += w * w; sumWO += w * o;
                ++n;
            }
            if (n >= 8) {
                const double denom = static_cast<double>(n) * sumWW - sumW * sumW;
                if (std::fabs(denom) > 1e-9) {
                    const double slope = (static_cast<double>(n) * sumWO - sumW * sumO) / denom;
                    const double lastWall = driftWallMs.back();
                    maxAbsDriftMs = std::fabs(slope) * lastWall;
                    double minO = 1e9, maxO = -1e9;
                    for (size_t i = 0; i < driftWallMs.size(); ++i) {
                        if (driftWallMs[i] < 500.0) continue;
                        minO = std::min(minO, driftOffsetMs[i]);
                        maxO = std::max(maxO, driftOffsetMs[i]);
                    }
                    log::info("audio", std::format("clock fit: n={} slope={:.6f} ms/s -> drift={:.3f} ms over {:.0f} ms; offset range [{:.2f}, {:.2f}] ms",
                        n, slope, maxAbsDriftMs, lastWall, minO, maxO));
                }
            }
        }

        // Pause/flush semantics: Stop + Reset must empty the endpoint buffer.
        hr = client->Stop();
        hr = client->Reset();
        UINT32 paddingAfterFlush = 1;
        if (SUCCEEDED(client->GetCurrentPadding(&paddingAfterFlush)) && paddingAfterFlush == 0) {
            pauseFlushWorks = true;
        }
        log::info("audio", std::format("Stop+Reset -> padding={} (0 expected) hr=0x{:X}",
            paddingAfterFlush, static_cast<unsigned>(hr)));

        client->Stop();
    }

    if (avrtTask != nullptr) AvRevertMmThreadCharacteristics(avrtTask);
    if (eventHandle != nullptr) CloseHandle(eventHandle);
    releaseCom(render);
    if (audioClockPtrForCleanup != nullptr) audioClockPtrForCleanup->Release();
    if (mixFormat != nullptr) CoTaskMemFree(mixFormat);
    releaseCom(client);
    releaseCom(device);
    releaseCom(enumerator);
    if (comInited && hr != RPC_E_CHANGED_MODE) CoUninitialize();

    const bool pass = eventMode && framesWritten >= framesPlanned && framesPlanned > 0 &&
                      underrunsSteady == 0 && maxAbsDriftMs <= 5.0 && pauseFlushWorks;

    std::string json;
    json += "{\n";
    json += std::format("  \"probe\": \"veyra_audio_test\",\n");
    json += std::format("  \"runId\": \"{}\",\n", jsonEscape(args.runId));
    json += std::format("  \"eventMode\": {},\n", eventMode ? "true" : "false");
    json += std::format("  \"sampleRate\": {},\n", sampleRate);
    json += std::format("  \"channels\": {},\n", channels);
    json += std::format("  \"framesPlanned\": {},\n", framesPlanned);
    json += std::format("  \"framesWritten\": {},\n", framesWritten);
    json += std::format("  \"underrunsSteady\": {},\n", underrunsSteady);
    json += std::format("  \"maxAbsDriftMs\": {:.4f},\n", maxAbsDriftMs);
    json += std::format("  \"pauseFlushWorks\": {}\n", pauseFlushWorks ? "true" : "false");
    json += "}\n";
    if (!args.jsonFile.empty()) (void)writeTextFileUtf8(args.jsonFile, json);

    log::info("audio", std::format("audio-test: {} event={} written={}/{} underruns={} drift={:.2f}ms flush={}",
        pass ? "PASS" : "FAIL", eventMode, framesWritten, framesPlanned,
        underrunsSteady, maxAbsDriftMs, pauseFlushWorks));
    return pass ? 0 : 12;
}

} // namespace veyra::harness
