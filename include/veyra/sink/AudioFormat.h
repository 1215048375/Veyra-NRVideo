#pragma once
#include <windows.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>
#include <bit>
#include <cstdint>
#include <cstring>
namespace veyra::sink {
// Interleaved float PCM exchanged by the software pipeline, always 48 kHz.
// WAVE and FFmpeg native channel masks share the standard speaker bit order.
struct AudioFormat {
    unsigned channels=2;
    uint32_t mask=SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT;
    bool valid()const{return channels>=1&&channels<=8&&std::popcount(mask)==int(channels)&&(mask&~0x3ffffu)==0;}
    bool operator==(const AudioFormat&)const=default;
};
struct WavePcmFormat {
    WAVEFORMATEX wave{};
    AudioFormat layout;
    unsigned validBits=0;
    bool floating=false;
};
inline bool parseWavePcm(const void* bytes,size_t size,WavePcmFormat& out){
    if(!bytes||size<sizeof(WAVEFORMATEX))return false;
    WAVEFORMATEX w{};memcpy(&w,bytes,sizeof(w));
    if(w.cbSize>size-sizeof(w)||w.nChannels<1||w.nChannels>8||w.nSamplesPerSec<8000||w.nSamplesPerSec>192000||
       (w.wBitsPerSample!=16&&w.wBitsPerSample!=24&&w.wBitsPerSample!=32)||
       w.nBlockAlign!=w.nChannels*(w.wBitsPerSample/8)||w.nAvgBytesPerSec!=w.nBlockAlign*w.nSamplesPerSec)return false;
    WavePcmFormat result;result.wave=w;result.layout.channels=w.nChannels;result.validBits=w.wBitsPerSample;
    if(w.wFormatTag==WAVE_FORMAT_EXTENSIBLE){
        if(w.cbSize<22||size<sizeof(WAVEFORMATEXTENSIBLE))return false;
        WAVEFORMATEXTENSIBLE e{};memcpy(&e,bytes,sizeof(e));
        if(e.SubFormat!=KSDATAFORMAT_SUBTYPE_PCM&&e.SubFormat!=KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)return false;
        result.floating=e.SubFormat==KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
        result.validBits=e.Samples.wValidBitsPerSample;
        result.layout.mask=e.dwChannelMask;
    }else{
        if(w.wFormatTag!=WAVE_FORMAT_PCM&&w.wFormatTag!=WAVE_FORMAT_IEEE_FLOAT)return false;
        result.floating=w.wFormatTag==WAVE_FORMAT_IEEE_FLOAT;
        // More than stereo without a speaker mask is ambiguous (side/back).
        if(w.nChannels>2)return false;
        result.layout.mask=w.nChannels==1?SPEAKER_FRONT_CENTER:SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT;
    }
    if(!result.layout.valid()||!result.validBits||result.validBits>w.wBitsPerSample||
       (result.floating&&(w.wBitsPerSample!=32||result.validBits!=32)))return false;
    out=result;return true;
}
inline WAVEFORMATEXTENSIBLE floatWave(AudioFormat layout){
    WAVEFORMATEXTENSIBLE out{};out.Format={WAVE_FORMAT_EXTENSIBLE,WORD(layout.channels),48000,48000*layout.channels*4,WORD(layout.channels*4),32,22};
    out.Samples.wValidBitsPerSample=32;out.dwChannelMask=layout.mask;out.SubFormat=KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;return out;
}
}
