#pragma once
#include "WorkspaceChrome.h"

namespace veyra::ui {
namespace live_status {
struct State {engine::EngineController* engine;int scroll=0,maxScroll=0;};
inline LRESULT CALLBACK proc(HWND h,UINT message,WPARAM wp,LPARAM lp){
    auto* state=reinterpret_cast<State*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(message==WM_NCCREATE){state=new State{static_cast<engine::EngineController*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams)};SetWindowLongPtrW(h,GWLP_USERDATA,LONG_PTR(state));}
    if(!state)return DefWindowProcW(h,message,wp,lp);
    switch(message){
    case WM_CREATE:SetTimer(h,1,250,nullptr);return 0;
    case WM_TIMER:if(IsWindowVisible(h))InvalidateRect(h,nullptr,FALSE);return 0;
    case WM_SIZE:InvalidateRect(h,nullptr,FALSE);return 0;
    case WM_MOUSEWHEEL:state->scroll=std::clamp(state->scroll-GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*50,0,state->maxScroll);InvalidateRect(h,nullptr,FALSE);return 0;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{
        PaintBuffer paint(h);fillSurface(paint.dc,paint.rect,h);
        const int width=MulDiv(paint.rect.right,96,veyra::ui::layoutDpi(h)),height=MulDiv(paint.rect.bottom,96,veyra::ui::layoutDpi(h));
        const auto s=state->engine->snapshot();const auto& f=s.metrics.flow;
        const bool playing=s.running&&!s.image&&s.transport==engine::TransportState::Playing;
        const bool xess=s.applied.frameGenerationBackend==engine::FrameGenerationBackend::XeSS&&s.applied.multiplier>1;
        auto write=[&](const std::wstring& value,int x,int y,int w,int ht,int size,COLORREF color){chromeText(paint.dc,h,value,x,y,w,ht,size,color);};
        auto ms=[](std::optional<double> v){return v?std::format(L"{:.1f} ms",*v):std::wstring(L"未测");};
        auto timing=[&](const diagnostics::TimingAggregate& a){return playing&&a.mean&&a.p95?std::format(L"{:.1f} / {:.1f}",*a.mean,*a.p95):std::wstring(L"未测");};
        auto fps=[&](double value){return playing&&!f.rateWindowReady?std::wstring(L"采样中"):std::format(L"{:.1f} fps",value);};
        write(L"实时处理状态",12,8,width-24,24,13,textColor);
        const int half=(width-24)/2;
        write(xess?L"输出提交 · XeSS SDK":L"处理产出",12,36,half,20,10,secondary);
        write(s.capture?L"软件总延迟":L"软件驻留时间",12+half,36,half,20,10,secondary);
        write(fps(xess?f.xessSdkSubmitFps:f.outputCompletedFps),12,57,half,28,18,textColor);
        write(playing?ms(f.softwareLatencyMs):L"未测",12+half,57,half,28,18,textColor);
        std::vector<std::pair<std::wstring,std::wstring>> rows;
        rows.emplace_back(L"呈现提交",fps(xess?f.xessSdkSubmitFps:f.presentSubmitFps));
        rows.emplace_back(L"源帧处理",fps(f.sourceCompletedFps));
        rows.emplace_back(xess?L"补帧完成":L"有效补帧",xess?L"SDK内部不可测":fps(f.validGeneratedFps));
        rows.emplace_back(L"输入",s.capture?std::format(L"{:.1f} fps",s.captureFps):L"文件 / 图片");
        rows.emplace_back(L"总延迟 P95",playing?ms(f.softwareLatencyP95Ms):L"未测");
        rows.emplace_back(L"采集覆盖 / 补帧过期",std::format(L"{} / {}",f.counters.mailboxOverwritten,f.counters.generatedExpiredAfterEval));
        rows.emplace_back(L"链路耗时 · ms",L"均值 / P95");
        rows.emplace_back(L"取帧 / 解码",timing(f.cpuTiming[size_t(diagnostics::CpuStage::Decode)]));
        rows.emplace_back(L"命令槽等待",timing(f.cpuTiming[size_t(diagnostics::CpuStage::SlotWait)]));
        rows.emplace_back(L"图提交",timing(f.cpuTiming[size_t(diagnostics::CpuStage::Submit)]));
        const diagnostics::GpuStage stages[]={diagnostics::GpuStage::Color,diagnostics::GpuStage::Sr,diagnostics::GpuStage::Flow,diagnostics::GpuStage::Nr,diagnostics::GpuStage::Residual,diagnostics::GpuStage::FgBatch,diagnostics::GpuStage::Blit};
        const wchar_t* names[]={L"输入颜色",L"超分 SR",L"光流队列区间",L"NR",L"残差合成",L"补帧 FG",L"输出 blit"};
        for(size_t i=0;i<std::size(stages);++i){const auto& sample=s.metrics.gpu[size_t(stages[i])];
            const auto value=xess&&stages[i]==diagnostics::GpuStage::FgBatch?L"SDK内部不可测":sample.state==diagnostics::SampleState::NotExecuted?L"未执行":timing(f.gpuTiming[size_t(stages[i])]);
            rows.emplace_back(names[i],value);
        }
        rows.emplace_back(L"GPU就绪等待",timing(f.cpuTiming[size_t(diagnostics::CpuStage::ReadyWait)]));
        rows.emplace_back(L"呈现等待",timing(f.cpuTiming[size_t(diagnostics::CpuStage::DeadlineWait)]));
        rows.emplace_back(L"Present调用",timing(f.cpuTiming[size_t(diagnostics::CpuStage::Present)]));
        rows.emplace_back(f.pairCaptureCallbacks?L"A/B采集到达间隔":L"A/B软件取帧间隔",xess?L"SDK内部不可测":timing(f.pairTiming[size_t(diagnostics::PairTiming::ArrivalInterval)]));
        rows.emplace_back(L"生成呈现距A到达",xess?L"SDK内部不可测":timing(f.pairTiming[size_t(diagnostics::PairTiming::GeneratedFromA)]));
        rows.emplace_back(L"生成呈现距B到达",xess?L"SDK内部不可测":timing(f.pairTiming[size_t(diagnostics::PairTiming::GeneratedFromB)]));
        rows.emplace_back(L"延迟样本 / 统计溢出",std::format(L"{} / {}",f.latencySamples,f.timingOverflow));
        if(f.reset.sessionId){
            const auto& r=f.reset;
            const auto outcome=r.outcome==diagnostics::ResetOutcome::Completed?L"已恢复有效输出":r.outcome==diagnostics::ResetOutcome::RolledBack?L"已回滚":r.outcome==diagnostics::ResetOutcome::Cancelled?L"已取消":r.outcome==diagnostics::ResetOutcome::Failed?L"失败":L"等待有效输出";
            rows.emplace_back(std::format(L"最近设置{} · {}",r.rebuilt?L"重建":L"重置",r.settingsRevision),outcome);
            rows.emplace_back(L"切换总耗时",ms(r.totalMs));
            const wchar_t* resetNames[]={L"排空",L"销毁资源",L"创建资源",L"首帧预热提交",L"首帧完成观测"};
            for(size_t i=0;i<r.stageMs.size();++i)rows.emplace_back(resetNames[i],ms(r.stageMs[i]));
        }
        rows.emplace_back(L"补帧方式",s.applied.multiplier<=1?L"关闭":std::format(L"{} {}X",xess?L"XeSS":L"DLSS",s.applied.multiplier));
        rows.emplace_back(L"NR内部尺寸",s.applied.nr?std::format(L"{} x {}",s.metrics.resolution.nr.width,s.metrics.resolution.nr.height):L"关闭");
        rows.emplace_back(L"NR运行版本",s.nrActive?(s.applied.nrRuntime==engine::NrRuntime::Community?L"社区兼容 · 实验":L"NVIDIA原版"):L"未运行");
        rows.emplace_back(L"显示模式",s.running?(s.applied.captureCompatible?L"直播兼容 · 实验":L"标准显示"):L"未运行");
        rows.emplace_back(L"音频同步",s.audioAvailable?(s.capture?(s.captureAudio.running?L"软件估算同步":L"等待视频锚点"):(s.audioEndpointRecovering?L"音频设备恢复中 · 时间线保持":s.audioRebuffering?L"视频过载 · 同步缓冲":L"音频主时钟")):L"无音频");
        if(!s.capture&&s.audioAvailable){
            rows.emplace_back(L"音频设备恢复次数",std::to_wstring(s.audioEndpointRecoveries));
            if(FAILED(s.audioEndpointError))rows.emplace_back(L"最近音频设备错误",std::format(L"0x{:08X}",unsigned(s.audioEndpointError)));
        }
        if(!s.capture&&s.audioAvailable)rows.emplace_back(L"声音领先 · 软件估算",std::format(L"{:.1f} ms",s.lateMs));
        if(s.capture&&s.audioAvailable){
            rows.emplace_back(L"音画偏差 · 声音领先",ms(s.captureAudio.skewMs));
            rows.emplace_back(L"声音补偿",std::format(L"{:.1f} ms{}",s.captureAudio.compensationMs,s.captureAudio.limited?L" · 已达边界":L""));
            rows.emplace_back(L"PCM队列",std::format(L"{:.1f} ms",s.captureAudio.bufferedMs));
            rows.emplace_back(L"音频设备队列",std::format(L"{:.1f} ms",s.captureAudio.endpointBufferedMs));
            rows.emplace_back(L"音频重锚 / 溢出",std::format(L"{} / {}",s.captureAudio.resets,s.captureAudio.overflows));
        }
        if(!s.backendWarning.empty())rows.emplace_back(L"后端状态",s.backendWarning);
        if(!s.captureAudio.error.empty())rows.emplace_back(L"采集音频异常",s.captureAudio.error);
        std::vector<std::pair<std::wstring,std::wstring>> wrapped;
        const auto rowFont=makeFont(h,11,FW_NORMAL);const auto oldFont=SelectObject(paint.dc,rowFont);
        auto split=[&](std::wstring value,int room){
            std::vector<std::wstring> lines;
            do{
                int fit=0;SIZE extent{};
                GetTextExtentExPointW(paint.dc,value.c_str(),int(value.size()),dip(h,room),&fit,nullptr,&extent);
                const auto count=std::min(value.size(),size_t(std::max(1,fit)));
                lines.push_back(value.substr(0,count));value.erase(0,count);
            }while(!value.empty());
            return lines;
        };
        for(const auto& row:rows){
            const auto labels=split(row.first,width/2-14),values=split(row.second,width/2-12);
            for(size_t line=0;line<std::max(labels.size(),values.size());++line)wrapped.emplace_back(line<labels.size()?labels[line]:L"",line<values.size()?values[line]:L"");
        }
        SelectObject(paint.dc,oldFont);DeleteObject(rowFont);rows=std::move(wrapped);
        const int top=96,rowHeight=25,available=std::max(0,height-top-30)/rowHeight*rowHeight;
        state->maxScroll=std::max(0,int(rows.size())*rowHeight-available);state->scroll=std::clamp(state->scroll/rowHeight*rowHeight,0,state->maxScroll);
        const int saved=SaveDC(paint.dc);IntersectClipRect(paint.dc,0,dip(h,top),paint.rect.right,dip(h,top+available));
        for(size_t i=0;i<rows.size();++i){const int y=top+int(i)*rowHeight-state->scroll;
            // glassText renders through its own DIB; a parent DC clip does not
            // constrain that buffer. Never draw partial rows into fixed text.
            if(y<top||y+rowHeight>top+available)continue;
            write(rows[i].first,12,y,width/2-14,rowHeight,11,secondary);
            write(rows[i].second,width/2,y,width/2-12,rowHeight,11,textColor);
        }
        RestoreDC(paint.dc,saved);
        if(state->maxScroll&&available>0){const int thumb=std::max(18,available*available/(int(rows.size())*rowHeight));const int y=top+(available-thumb)*state->scroll/state->maxScroll;RECT r{dip(h,width-4),dip(h,y),dip(h,width-2),dip(h,y+thumb)};FillRect(paint.dc,&r,panelBrush());}
        write(xess?L"截止代理Present返回；屏幕扫描未测":L"源帧进入软件至Present返回；非光子延迟",12,height-28,width-24,24,9,secondary);
        return 0;
    }
    case WM_NCDESTROY:KillTimer(h,1);delete state;SetWindowLongPtrW(h,GWLP_USERDATA,0);break;
    }
    return DefWindowProcW(h,message,wp,lp);
}
}
inline HWND createLiveStatusPanel(HWND parent,engine::EngineController& engine){
    WNDCLASSW wc{};wc.lpfnWndProc=live_status::proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"VeyraLiveStatus";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
    return CreateWindowExW(0,wc.lpszClassName,L"实时处理状态",WS_CHILD,0,0,1,1,parent,nullptr,wc.hInstance,&engine);
}
}
