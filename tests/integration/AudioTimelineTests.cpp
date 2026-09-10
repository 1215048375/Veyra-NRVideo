#include "veyra/sink/WasapiAudioSink.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
#include <functional>

using namespace veyra::sink;
using namespace std::chrono_literals;
namespace {
int failures = 0;
void check(bool ok, const char* text) {
    std::cout << (ok ? "PASS " : "FAIL ") << text << '\n';
    failures += !ok;
}
bool until(const std::function<bool()>& ready) {
    const auto limit = std::chrono::steady_clock::now() + 3s;
    while (!ready() && std::chrono::steady_clock::now() < limit) std::this_thread::sleep_for(2ms);
    return ready();
}
void fixture(const std::filesystem::path& file, uint32_t rate) {
    std::ofstream out(file, std::ios::binary);
    auto word = [&](uint32_t v, unsigned bytes) { for (unsigned i=0;i<bytes;++i) out.put(char(v >> (8*i))); };
    const uint32_t frames = rate * 3 / 4;
    out.write("RIFF",4); word(36+frames*2,4); out.write("WAVEfmt ",8); word(16,4);
    word(1,2); word(1,2); word(rate,4); word(rate*2,4); word(2,2); word(16,2);
    out.write("data",4); word(frames*2,4);
    for (uint32_t i=0;i<frames;++i) word(uint16_t(int16_t(12000*std::sin(i*0.031))),2);
}
void decode(const std::filesystem::path& file) {
    AudioPipeline pipe;
    check(pipe.open(file.wstring()), "open PCM fixture");
    pipe.startThread(nullptr);
    check(until([&] { return pipe.decodingComplete(); }), "decoder and resampler fully drained");
    check(std::abs(pipe.headPtsMs()) < 0.05, "first PCM timestamp is block start at zero");
    check(std::abs(pipe.tailPtsMs()-750) < 0.05, "resampled tail preserves 750ms duration");
    size_t total=0; double pts=0, maxError=0; std::vector<float> block(2*137);
    while (auto n=pipe.pull(block.data(),137,&pts)) {
        maxError=std::max(maxError,std::abs(pts-1000.0*total/kAudioRate)); total+=n;
    }
    std::cout << "samples=" << total << " maxPtsErrorMs=" << maxError << '\n';
    check(total==36000 && maxError<0.05, "no samples lost and PTS contiguous across packet boundaries");
    const double seek=pipe.requestSeek(123.25);
    check(seek>=123.25-0.001 && seek<123.25+1000.0/kAudioRate+0.001, "seek trims within a block at sample precision");
    check(until([&]{return pipe.decodingComplete();}), "seek drains through end");
    check(std::abs(pipe.tailPtsMs()-750)<0.05, "seek resets resampler history and preserves end PTS");
    check(pipe.overruns()==0, "bounded PCM ring did not overflow");
    pipe.stopThread();
}
}
int wmain(int argc, wchar_t** argv) {
    if (argc!=2) return 2;
    const std::filesystem::path dir=argv[1]; std::filesystem::create_directories(dir);
    for(uint32_t rate:{48000u,44100u}) { const auto file=dir/(std::to_string(rate)+".wav"); fixture(file,rate); decode(file); }
    AudioRenderer renderer;
    check(!std::isfinite(renderer.mediaTimeMs()), "unstarted clock is invalid");
    if (!renderer.start()) { check(false,"WASAPI endpoint initialization"); renderer.shutdown(); return 1; }
    renderer.setGain(0);
    AudioPipeline pipe; check(pipe.open((dir/"48000.wav").wstring()),"open renderer fixture");
    pipe.startThread(&renderer);
    check(until([&]{return renderer.started();}),"WASAPI prefilled and started");
    check(renderer.framesWritten()>0 && std::isfinite(renderer.mediaTimeMs()),"valid clock only after actual PCM submission");
    std::this_thread::sleep_for(80ms); pipe.setPaused(true); std::this_thread::sleep_for(100ms);
    const auto before=renderer.mediaTimeMs(); std::this_thread::sleep_for(70ms);
    check(std::abs(renderer.mediaTimeMs()-before)<2,"paused device clock remains fixed");
    const auto seek=pipe.requestSeek(250);
    std::this_thread::sleep_for(30ms);
    check(std::abs(seek-250)<0.05 && std::abs(renderer.mediaTimeMs()-250)<20,"paused seek reanchors actual PCM");
    pipe.stopThread(); renderer.stopAndReset();
    check(!std::isfinite(renderer.mediaTimeMs()),"reset invalidates clock");
    renderer.shutdown(); return failures ? 1 : 0;
}
