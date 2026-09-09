#pragma once
#include <cmath>
#include <array>
#include <cstdint>
#include <string>
#include "veyra/pipeline/ResolutionPlan.h"
namespace veyra::engine {
enum class FrameGenerationBackend { Dlss, Fruc };
enum class FlowQuality { Performance, Balanced, Quality };
enum class ContentRate { Transport, Auto, Fps30, Fps50, Fps60 };
struct NrSettings {
    float intensity=1,tone=1,structure=1,skin=-1;
    int32_t style=0,autoMask=0,uiCorrection=0;
    bool operator==(const NrSettings&) const = default;
};
struct ResidualSettings {
    float total=1,darken=1,brighten=1,color=1,luminance=1;
    bool operator==(const ResidualSettings&) const = default;
};
struct ProtectionRect {
    float left=0,top=0,right=0,bottom=0;
    bool empty()const{return left==right||top==bottom;}
    bool operator==(const ProtectionRect&)const=default;
};
struct ProtectionSettings {
    bool enabled=false;
    float featherPixels=2;
    std::array<ProtectionRect,4> regions{};
    bool operator==(const ProtectionSettings&)const=default;
    std::string validate()const{
        if(!std::isfinite(featherPixels)||featherPixels<0||featherPixels>32)return "invalid protection feather";
        for(auto r:regions){for(float v:{r.left,r.top,r.right,r.bottom})if(!std::isfinite(v)||v<0||v>1)return "invalid protection rectangle";
            if(r.left>r.right||r.top>r.bottom)return "inverted protection rectangle";}
        return {};
    }
};
struct EnhancementSettings {
    uint64_t revision=1;
    NrSettings model;
    ResidualSettings residual;
    ProtectionSettings protection;
    bool nr=true,sr=false;
    uint32_t videoSrQuality=0; // 0 DLSS SR; 1–4 RTX Video SR
    uint32_t multiplier=1;
    FrameGenerationBackend frameGenerationBackend=FrameGenerationBackend::Dlss;
    pipeline::NrSizePolicy nrPolicy=pipeline::NrSizePolicy::Realtime;
    FlowQuality flow=FlowQuality::Balanced;
    ContentRate content=ContentRate::Transport;
    bool operator==(const EnhancementSettings&) const = default;
    std::string validate() const {
        auto range=[](float v,float hi){return std::isfinite(v)&&v>=0&&v<=hi;};
        if(auto error=protection.validate();!error.empty())return error;
        if(!revision)return "settingsRevision must be nonzero";
        if(!range(model.intensity,1)||!range(model.tone,1)||!range(model.structure,1))return "model parameter out of range";
        if(model.skin!=-1&&!range(model.skin,2))return "skin parameter out of range";
        if(model.style<0||model.style>2||model.autoMask<0||model.autoMask>1||model.uiCorrection<0||model.uiCorrection>1)return "invalid experimental parameter";
        for(float v:{residual.total,residual.darken,residual.brighten,residual.color,residual.luminance})if(!range(v,2))return "residual parameter out of range";
        if(frameGenerationBackend!=FrameGenerationBackend::Dlss&&frameGenerationBackend!=FrameGenerationBackend::Fruc)return "invalid frame generation backend";
        if(videoSrQuality>4)return "invalid video SR quality";
        if(multiplier<1||multiplier>4)return "unsupported multiplier";
        if(nrPolicy!=pipeline::NrSizePolicy::Realtime&&nrPolicy!=pipeline::NrSizePolicy::Native)return "invalid NR size policy";
        if(flow<FlowQuality::Performance||flow>FlowQuality::Quality||content<ContentRate::Transport||content>ContentRate::Fps60)return "invalid flow/content mode";
        return {};
    }
};
}
