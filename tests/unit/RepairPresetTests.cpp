#include "veyra/engine/PresetStore.h"
#include <iostream>
#include <fstream>
int main(int argc,char** argv){if(argc!=2)return 2;using namespace veyra::engine;const std::filesystem::path p=argv[1];PresetStore a(p);bool ok=a.load();EnhancementSettings s;s.videoSrQuality=2;s.protection.enabled=true;s.protection.featherPixels=3.5f;s.protection.regions[0]={.1f,.2f,.7f,.8f};s.protection.regions[3]={0,0,1,1};s.model.intensity=.375f;s.model.skin=1.5f;s.residual.darken=1.2f;s.multiplier=4;s.flow=FlowQuality::Quality;s.content=ContentRate::Fps50;s.nrPolicy=veyra::pipeline::NrSizePolicy::Native;
 ok=ok&&a.put(L"test",s)&&a.setDefault(0)&&!a.put(L"test",s);PresetStore b(p);ok=ok&&b.load()&&b.defaultSettings()==s&&b.rename(0,L"renamed")&&b.defaultSettings()==s;
 auto badSr=s;badSr.videoSrQuality=5;ok=ok&&!b.put(L"invalid video SR",badSr);
 auto invalid=s;invalid.model.tone=9;ok=ok&&!b.put(L"bad",invalid)&&b.entries().size()==1&&b.erase(0)&&b.entries().empty();
 auto badRegion=s;badRegion.protection.regions[0].left=2;ok=ok&&!b.put(L"invalid region",badRegion);
 {std::ofstream legacy(p);legacy<<"VEYRA_PRESETS 1\n\"legacy\" 1\n\"legacy\" 1 1 1 -1 0 0 0 1 1 1 1 1 1 0 1 0 1 0\n";}
 PresetStore old(p);ok=ok&&old.load()&&!old.defaultSettings().protection.enabled&&old.put(L"v2",s);
 PresetStore upgraded(p);ok=ok&&upgraded.load()&&upgraded.entries().size()==2&&upgraded.entries()[1].settings==s;
 {std::ofstream f(p);f<<"VEYRA_PRESETS 99\ncorrupt mediaPath executable must reject";}PresetStore c(p);ok=ok&&!c.load()&&!c.put(L"override",{});std::ifstream f(p);std::string data((std::istreambuf_iterator<char>(f)),{});ok=ok&&data=="VEYRA_PRESETS 99\ncorrupt mediaPath executable must reject";
 std::cout<<"preset roundtrip, all fields, duplicate, rename-default, delete, validation, unknown schema, corrupt-preservation="<<ok<<'\n';return ok?0:1;}
