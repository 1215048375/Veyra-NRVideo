#include "../../apps/veyra/ui/UiSessionState.h"
#include "../../apps/veyra/ui/WorkspaceChrome.h"
#include "../../apps/veyra/ui/WorkspaceTransition.h"
#include "../../apps/veyra/ui/TransportLayout.h"
#include "../../apps/veyra/ui/UiPreferenceStore.h"
#include "veyra/sink/AudioGain.h"
#include "veyra/engine/PreviewView.h"
#include <iostream>
#include <vector>
#include <stdexcept>
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int wmain(int argc,wchar_t** argv){try{
    using namespace veyra;
    {engine::PreviewView v;v.wheel(3,800,300,1000,600,1920,1080);require(std::abs(v.zoom-1.728f)<.0001f,"wheel scale");
    const float anchored=(800-500)/(1920.0f*(1000.0f/1920)*v.zoom)+v.centerX;require(std::abs(anchored-.8f)<.0001f,"pointer anchor fixed");
    v.wheel(-3,800,300,1000,600,1920,1080);require(std::abs(v.zoom-1)<.0001f&&std::abs(v.centerX-.5f)<.0001f,"inverse wheel returns same view");
    v.pan(100,0,1000,600,1920,1080);require(std::abs(v.centerX-.4f)<.0001f,"pan measured in displayed pixels");}
    ui::UiSessionState state;require(!state.enhanced&&!state.configured.nr&&!state.configured.sr&&state.configured.multiplier==1,"fresh startup effects disabled");state.configured.forceSdrPreview=true;state.enhanced=true;state.configured.nr=true;state.configured.multiplier=4;state.configured.sr=true;state.configured.model.intensity=.37f;state.configured.residual.color=1.4f;
    auto before=state.effective();require(state.mode==ui::Mode::Daily,"startup Daily");state.mode=ui::Mode::Professional;require(state.effective()==before,"mode cannot alter settings");state.enhanced=false;auto bypass=state.effective();require(!bypass.nr&&!bypass.sr&&bypass.multiplier==1,"true full bypass");require(bypass.forceSdrPreview,"master bypass retains SDR display choice");state.enhanced=true;require(state.effective()==before,"restore all settings");
    for(bool input:{false,true})for(bool display:{false,true})for(bool force:{false,true}){engine::EnhancementSettings s;s.forceSdrPreview=force;require(s.useHdrPreview(input,display)==(input&&display&&!force),"HDR output policy");}
    ui::ChromeLayout daily(1440,900,false,false);require(daily.left==0&&daily.top==0&&daily.viewWidth==1440&&daily.viewHeight==764&&daily.bottom==764,"daily video owns all space above transport");
    for(int width:{688,768,868,928,1008,1040,1148,1248,1888,2528}){
        ui::TransportLayout t(width,true);std::vector<ui::TransportSlot> slots={t.open,t.capture,t.recent,t.master,t.sr,t.stop,t.play,t.mute,t.volume,t.subtitle,t.fullscreen,t.mode,t.minimize,t.close};
        for(size_t i=0;i<slots.size();++i){require(slots[i].width>0&&slots[i].x>=0&&slots[i].x+slots[i].width<=width,"all daily actions visible and contained");for(size_t j=0;j<i;++j)require(slots[i].x>=slots[j].x+slots[j].width||slots[j].x>=slots[i].x+slots[i].width,"direct daily actions do not overlap");}
    }
    for(int width:{290,400,480,508,700,1000}){ui::TransportLayout t(width,false);require(t.play.x+t.play.width<=t.mute.x&&t.stop.x+t.stop.width<=t.mute.x,"compact professional transport leaves room for audio and subtitle controls");}
    for(int width:{282,290,400,431,432,480,688,1000,1888}){ui::QueueTransportLayout t(width);std::vector<ui::TransportSlot> slots={t.previous,t.next,t.restart,t.mode,t.loop};for(size_t i=0;i<slots.size();++i){require(slots[i].width>=32&&slots[i].x>=0&&slots[i].x+slots[i].width<=width,"queue actions have usable targets inside narrow transport");for(size_t j=0;j<i;++j)require(slots[i].x>=slots[j].x+slots[j].width,"queue actions never overlap");}}
    ui::WorkspaceTransition animation;animation.start(true,1000);animation.sample(1120);const auto halfway=animation.value;require(halfway>.49&&halfway<.51,"visible intermediate expansion");animation.start(false,1120);require(animation.value==halfway,"reverse without jump");animation.sample(1240);require(animation.value>0&&animation.value<halfway,"panel collapses progressively");animation.sample(1360);require(!animation.running&&animation.value==0,"collapse ends exactly");
    using pipeline::ResolutionPlan;using pipeline::NrSizePolicy;using pipeline::Extent;
    require(ResolutionPlan::make({1448,1086},true,NrSizePolicy::Native,true).output==Extent{2880,2160},"4:3 SR preserves aspect");
    require(ResolutionPlan::make({1080,1920},true,NrSizePolicy::Native,true).output==Extent{1214,2160},"portrait SR even dimensions");
    require(!ResolutionPlan::make({2880,2160},true,NrSizePolicy::Native,true).srApplied,"already maximum edge skips SR");
    require(ResolutionPlan::make({1920,1080},true,NrSizePolicy::Realtime,false).output==Extent{3840,2160},"16:9 SR unchanged");
    require(ResolutionPlan::make({3840,2160},false,NrSizePolicy::Realtime,false).flow==Extent{1920,1080},"realtime 4K uses bounded flow extent");
    require(ResolutionPlan::make({3840,2160},false,NrSizePolicy::Native,false).flow==Extent{3840,2160},"native 4K retains native flow extent");
    for(int dpi:{96,120,144,192})for(int width:{720,900,960,1180,1280,1920})for(int height:{540,720,800,1080})for(bool pro:{false,true})for(bool drawer:{false,true}){
        ui::ChromeLayout l(width,height,pro,drawer);require(l.left>=0&&l.top>=0&&l.viewWidth>0&&l.viewHeight>0,"nonnegative viewport");require(l.left+l.viewWidth<=width&&l.top+l.viewHeight<=height,"viewport contained");if(pro&&(width>=960||drawer))require(l.left+l.viewWidth<l.right&&l.right+l.panelWidth<=width,"disjoint inspector");
        require(MulDiv(l.viewWidth,dpi,96)>0,"DPI physical extent");
    }
    std::vector<float> tone(4800*2,1);float gain=1;sink::applyStereoGain(tone.data(),4800,.5f,gain);require(std::abs(gain-.5f)<1e-6,"gain ramp reaches half");for(size_t i=480;i<tone.size();++i)require(std::abs(tone[i]-.5f)<1e-6,"actual PCM scaled");tone.assign(tone.size(),1);sink::applyStereoGain(tone.data(),4800,0,gain);require(gain==0,"mute reaches zero");for(size_t i=480;i<tone.size();++i)require(tone[i]==0,"muted PCM exactly zero");
    if(argc>1){auto dir=std::filesystem::path(argv[1]);ui::UiPreferenceStore prefs(dir);const auto fresh=prefs.startup({});require(!fresh.nr&&!fresh.sr&&fresh.multiplier==1,"missing preferences do not enable effects");ui::UiPreferences p;p.volume=.45f;p.muted=true;p.inspector=3;p.width=1180;require(prefs.save(p,&before),"atomic preferences save");ui::UiPreferenceStore loaded(dir);auto restored=loaded.load();require(restored.volume==p.volume&&restored.muted&&restored.inspector==3,"preferences round trip");auto setting=loaded.startup({});before.revision=setting.revision;require(setting==before,"all confirmed parameters round trip");auto path=dir/"ui-preferences.v1";{std::ofstream bad(path);bad<<"UNKNOWN 9 invalid";}ui::UiPreferenceStore damaged(dir);damaged.load();require(!damaged.save(p,nullptr),"corrupt preference cannot be overwritten");std::ifstream f(path);std::string data((std::istreambuf_iterator<char>(f)),{});require(data=="UNKNOWN 9 invalid","corrupt original preserved");}
    std::cout<<"PASS: mode/bypass, 384 layout cases at four DPI scales, actual PCM gain/mute, confirmed settings persistence/corruption\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
