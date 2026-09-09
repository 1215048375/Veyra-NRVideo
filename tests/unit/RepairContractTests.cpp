#include "veyra/engine/EnhancementSettings.h"
#include "veyra/diagnostics/DiagnosticEvent.h"
#include "veyra/diagnostics/FrameMetrics.h"
#include "veyra/engine/PresentationScheduler.h"
#include <iostream>
#include "veyra/engine/ContentCadence.h"
#include "veyra/diagnostics/Redaction.h"
#include <limits>
#include <vector>
#include "veyra/engine/CfrTimeline.h"
#include "veyra/Log.h"
int main(){
    using namespace veyra;int failures=0,checks=0;
    auto check=[&](bool ok,const char* name){++checks;if(!ok)++failures;std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';};
    auto p=pipeline::ResolutionPlan::make({1920,1080},true,pipeline::NrSizePolicy::Realtime,false);
    check(p.base==pipeline::Extent{3840,2160}&&p.nr==pipeline::Extent{1920,1080}&&p.output==p.base,"SR4K preserves base; NR1080");
    p=pipeline::ResolutionPlan::make({3840,2160},true,pipeline::NrSizePolicy::Realtime,true);
    check(!p.srApplied&&p.nr==p.base,"native export and 1:1 SR bypass");
    engine::EnhancementSettings s;check(s.validate().empty(),"default settings valid");
    s.model.intensity=std::numeric_limits<float>::quiet_NaN();check(!s.validate().empty(),"reject NaN transaction");
    s={};s.multiplier=5;check(!s.validate().empty(),"reject unsupported multiplier");
    check(pipeline::FrameBatch::interpolate(-200000,0,1,2)==-100000,"negative PTS midpoint");
    check(pipeline::FrameBatch::interpolate(0,200000,1,3)==66667&&pipeline::FrameBatch::interpolate(0,200000,2,3)==133333,"rational 3X PTS within tick");
    pipeline::FrameBatch b;b.identity={1,2,3};pipeline::BatchFrame f;f.identity=b.identity;f.pts100ns=1;b.append(f);f.identity.settingsRevision=3;
    bool rejected=false;try{b.append(f);}catch(...){rejected=true;}check(rejected,"reject mixed settingsRevision");
    diagnostics::FrameMetrics m;check(!m.displayFps&& !m.gpu[0].milliseconds,"unknown display/GPU is not zero");
    diagnostics::DiagnosticHistory h;diagnostics::DiagnosticEvent e;e.fingerprint="same";for(int i=0;i<100;++i)h.add(e);
    check(h.size()==1&&h.events()[0].occurrenceCount==100,"merge repeated error");
    engine::PresentationScheduler timeline;timeline.reset(3,0,1000000,200000);
    check(timeline.deadline(200000)==1400000&&timeline.deadline(400000)==1600000,"live deadlines do not drift with CPU completion");
    check(timeline.expired(0,1400000,100000)&&!timeline.anchored(4),"expire generated debt and reject epoch change");
    for(int i=0;i<100;++i){e.fingerprint=std::to_string(i);h.add(e);}check(h.size()==64,"bounded diagnostic queue");
    engine::ContentCadence cadence;for(int i=0;i<120;++i)cadence.observe(i*1000.0/60,i%2?.01:0,true);check(cadence.measuredRate()==30,"moving 30 in 60 cadence");
    cadence.reset();for(int i=0;i<120;++i)cadence.observe(i*1000.0/60,0,true);check(cadence.measuredRate()==0,"static scene cannot establish lower FPS");
    cadence.reset();for(int i=0;i<120;++i)cadence.observe(i*20.0,.01,true);check(cadence.measuredRate()==50,"moving 50fps cadence");
    cadence.reset();for(int i=0;i<120;++i)cadence.observe(i*1000.0/60,.01,true);check(cadence.measuredRate()==60,"moving 60fps cadence");check(cadence.confirmedRate(engine::ContentRate::Fps60)==60&&cadence.confirmedRate(engine::ContentRate::Fps30)==0&&cadence.conflicts(engine::ContentRate::Fps30),"manual identification target is not a resampling fiction");
    check(diagnostics::redact("failure C:\\Users\\private-user\\media file.mp4\nserial=DEVICE123\nuser=private-user").find("private-user")==std::string::npos,"redact Windows paths, usernames and serials");
    engine::CfrTimeline high(1000,1,.000001,0);bool highOk=true;for(unsigned i=0;i<1000;++i)highOk &= high.accepts(i,i/1000.0);check(highOk&&!high.accepts(1000,1.001),"1000fps preserves exact timestamps and rejects dropped frame");
    engine::CfrTimeline cfr(60,1,.001,0);bool quantized=true;
    for(unsigned i=0;i<6000;++i)quantized &= cfr.accepts(i,std::round(i*1000.0/60)/1000);
    check(quantized,"60fps millisecond quantization does not drift or reject");
    engine::CfrTimeline ntsc(30000,1001,.001,-.017);bool fractional=true;
    for(unsigned i=0;i<30000;++i)fractional &= ntsc.accepts(i,std::round((i*1001.0/30000-.017)*1000)/1000);
    check(fractional,"fractional CFR with negative origin survives quantization");
    engine::CfrTimeline gap(60,1,.001,0);gap.accepts(0,0);gap.accepts(1,.017);
    engine::CfrTimeline drift(60,1,.001,0);for(unsigned i=0;i<60;++i)drift.accepts(i,std::round(i*1000.0/60)/1000);
    engine::CfrTimeline invalid(60,1,.001,0);
    check(!gap.accepts(2,.05)&&!drift.accepts(60,1.016667)&&!invalid.accepts(0,std::numeric_limits<double>::quiet_NaN()),"VFR gap, drift and invalid PTS rejected with fresh timelines");
    check(!engine::CfrTimeline(60,1,.02,0).valid(),"time base coarser than one frame rejected");
    engine::CfrTimeline ticks(60,1,1.0/60,0);bool exactTicks=true;for(unsigned i=0;i<120;++i)exactTicks &= ticks.accepts(i,i/60.0);check(exactTicks&&!ticks.accepts(120,121.0/60),"one-tick CFR accepted but missing frame rejected");
    std::vector<double> mkv;for(unsigned i=0;i<24;++i)mkv.push_back(std::round(i*1000.0/60)/1000);
    check(engine::CfrTimeline::select(29990,499,.001,mkv)==std::pair<int,int>{60,1},"misdeclared MKV rate selects consistent standard CFR candidate");
    Logger::instance().setConsoleEnabled(false);
    log::error("nvof-session","test-only nvOFInit failed status=5");
    log::error("nvof-session","test-only caps failed st=7");
    log::error("ngx","test-only fg-backend failed result=0xBAD00005 seh=0xC0000005");
    const auto report=Logger::instance().diagnosticReport();
    check(report.find("NVOF=0x5")!=std::string::npos&&report.find("NVOF=0x7")!=std::string::npos&&report.find("NGX=0xBAD00005")!=std::string::npos&&report.find("SEH=0xC0000005")!=std::string::npos,"diagnostic report retains NVOF aliases and NGX/SEH codes (synthetic errors)");
    std::cout<<checks<<" checks "<<failures<<" failures\n";return failures?1:0;
}
