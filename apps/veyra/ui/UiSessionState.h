#pragma once
#include "veyra/engine/EnhancementSettings.h"
namespace veyra::ui {
enum class Mode { Daily, Professional };
struct UiSessionState {
    Mode mode=Mode::Daily;
    bool enhanced=true,subtitles=true,diagnostics=false,drawer=false;
    int inspector=0,preferredComparison=0;
    engine::EnhancementSettings configured;
    // An explicit user toggle creates a settings transaction. Mode changes do
    // not call this function and cannot change enhancement configuration.
    engine::EnhancementSettings effective()const{auto result=configured;if(!enhanced){result.nr=false;result.sr=false;result.multiplier=1;}return result;}
};
struct WorkspaceGeometry {
    int width,height,header=52,bottom=96,rail=0,inspector=0,diagnostics=0;
    WorkspaceGeometry(int w,int h,bool pro,bool details,bool drawer):width(w),height(h){if(pro){rail=w<1180?56:72;inspector=w>=960? (w<1180?296:328):(drawer?296:0);diagnostics=details?180:36;}}
};
}
