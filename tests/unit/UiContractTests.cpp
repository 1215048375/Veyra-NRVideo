#include "../../apps/veyra/ui/UiSessionState.h"
#include "../../apps/veyra/ui/WorkspaceChrome.h"
#include "../../apps/veyra/ui/UiPreferenceStore.h"
#include "veyra/sink/AudioGain.h"
#include <iostream>
#include <vector>
#include <stdexcept>
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int wmain(int argc,wchar_t** argv){try{
    using namespace veyra;
    ui::UiSessionState state;state.configured.multiplier=4;state.configured.sr=true;state.configured.model.intensity=.37f;state.configured.residual.color=1.4f;
    auto before=state.effective();require(state.mode==ui::Mode::Daily,"startup Daily");state.mode=ui::Mode::Professional;require(state.effective()==before,"mode cannot alter settings");state.enhanced=false;auto bypass=state.effective();require(!bypass.nr&&!bypass.sr&&bypass.multiplier==1,"true full bypass");state.enhanced=true;require(state.effective()==before,"restore all settings");
    for(int dpi:{96,120,144,192})for(int width:{720,900,960,1180,1280,1920})for(int height:{540,720,800,1080})for(bool pro:{false,true})for(bool drawer:{false,true}){
        ui::ChromeLayout l(width,height,pro,drawer);require(l.left>=0&&l.top>=0&&l.viewWidth>0&&l.viewHeight>0,"nonnegative viewport");require(l.left+l.viewWidth<=width&&l.top+l.viewHeight<=height,"viewport contained");if(pro&&(width>=960||drawer))require(l.left+l.viewWidth<l.right&&l.right+l.panelWidth<=width,"disjoint inspector");
        require(MulDiv(l.viewWidth,dpi,96)>0,"DPI physical extent");
    }
    std::vector<float> tone(4800*2,1);float gain=1;sink::applyStereoGain(tone.data(),4800,.5f,gain);require(std::abs(gain-.5f)<1e-6,"gain ramp reaches half");for(size_t i=480;i<tone.size();++i)require(std::abs(tone[i]-.5f)<1e-6,"actual PCM scaled");tone.assign(tone.size(),1);sink::applyStereoGain(tone.data(),4800,0,gain);require(gain==0,"mute reaches zero");for(size_t i=480;i<tone.size();++i)require(tone[i]==0,"muted PCM exactly zero");
    if(argc>1){auto dir=std::filesystem::path(argv[1]);ui::UiPreferenceStore prefs(dir);ui::UiPreferences p;p.volume=.45f;p.muted=true;p.inspector=3;p.width=1180;require(prefs.save(p,&before),"atomic preferences save");ui::UiPreferenceStore loaded(dir);auto restored=loaded.load();require(restored.volume==p.volume&&restored.muted&&restored.inspector==3,"preferences round trip");auto setting=loaded.startup({});before.revision=setting.revision;require(setting==before,"all confirmed parameters round trip");auto path=dir/"ui-preferences.v1";{std::ofstream bad(path);bad<<"UNKNOWN 9 invalid";}ui::UiPreferenceStore damaged(dir);damaged.load();require(!damaged.save(p,nullptr),"corrupt preference cannot be overwritten");std::ifstream f(path);std::string data((std::istreambuf_iterator<char>(f)),{});require(data=="UNKNOWN 9 invalid","corrupt original preserved");}
    std::cout<<"PASS: mode/bypass, 384 layout cases at four DPI scales, actual PCM gain/mute, confirmed settings persistence/corruption\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
