#pragma once
#include <algorithm>
#include <cmath>
namespace veyra::engine {
struct PreviewView {
    float zoom=1,centerX=.5f,centerY=.5f;
    bool operator==(const PreviewView&)const=default;
    void wheel(float steps,float x,float y,float clientW,float clientH,float imageW,float imageH){
        if(clientW<=0||clientH<=0||imageW<=0||imageH<=0||!std::isfinite(steps))return;
        const float fit=std::min(clientW/imageW,clientH/imageH);
        const float next=std::clamp(zoom*std::pow(1.2f,steps),.05f,64.0f);
        centerX+=(x-clientW*.5f)/(imageW*fit)*(1/zoom-1/next);
        centerY+=(y-clientH*.5f)/(imageH*fit)*(1/zoom-1/next);
        zoom=next;
    }
    void pan(float dx,float dy,float clientW,float clientH,float imageW,float imageH){
        if(clientW<=0||clientH<=0||imageW<=0||imageH<=0)return;
        const float fit=std::min(clientW/imageW,clientH/imageH)*zoom;
        centerX-=dx/(imageW*fit);centerY-=dy/(imageH*fit);
    }
};
}
