// Native integration checks, run only with --smoke-repair-ui[-reject].
// BM_CLICK deliberately traverses the same child/control/parent handlers as UI input.
bool smokeRepair=false,smokeRepairReject=false;int repairStep=0;ULONGLONG repairTick=0,repairSettledTick=0;uint64_t repairRevision=0;
void tickRepairChecks(HWND hwnd,const veyra::engine::PlayerSnapshot& s){
    using namespace veyra;using namespace veyra::ui;
    if(!smokeRepair||repairStep<0||repairStep==10||!s.frames||s.applying||masterPendingRevision||transition.running)return;
    auto checkControl=[&](int id){return SendMessageW(settingsControlForTest(id),BM_GETCHECK,0,0)==BST_CHECKED;};
    auto require=[&](bool ok,const char* message){log::info("ui-repair-test",std::format("{}={}",message,ok));if(!ok)repairStep=-1;return ok;};
    auto click=[&](int id){SendMessageW(settingsControlForTest(id),BM_CLICK,0,0);repairTick=GetTickCount64();repairRevision=engine.snapshot().desired.revision;};
    if(repairStep>0&&GetTickCount64()-repairTick<500)return; // allow both UI state timers to observe rollback
    wchar_t draft[32]{};GetWindowTextW(settingsControlForTest(100),draft,32);
    switch(repairStep){
    case 0:if(uiState.mode==Mode::Daily){switchMode();return;}selectInspector(0);SetWindowTextW(settingsControlForTest(100),L"NaN");click(200);repairStep=1;break;
    case 1:if(smokeRepairReject){if(!s.rejectedRevision)return;
        // Rollback can rebuild the GPU graph for longer than the click delay.
        // Start the bounded UI timer allowance after the backend has settled.
        auto observe=[&]{log::info("ui-repair-observe",std::format("rollback appliedNR={} desiredNR={} checkboxNR={} draftPreserved={}",s.applied.nr,s.desired.nr,checkControl(200),std::wstring(draft)==L"NaN"));};
        if(!repairSettledTick){repairSettledTick=GetTickCount64();observe();return;}
        if(GetTickCount64()-repairSettledTick<500)return;observe();if(require(s.applied.nr&&checkControl(200)&&std::wstring(draft)==L"NaN","rejected_NR_checkbox_rollback_keeps_draft"))repairStep=10;break;}
        if(s.applied.revision!=repairRevision)return;if(!require(!s.applied.nr&&!checkControl(200)&&std::wstring(draft)==L"NaN","NR_off_click_works_with_invalid_draft"))break;click(200);repairStep=2;break;
    case 2:if(s.applied.revision!=repairRevision)return;if(!require(s.applied.nr&&checkControl(200)&&std::wstring(draft)==L"NaN","NR_on_click_works_with_invalid_draft"))break;click(201);repairStep=3;break;
    case 3:if(s.applied.revision!=repairRevision)return;{auto expected=pipeline::ResolutionPlan::make(s.metrics.resolution.source,true,pipeline::NrSizePolicy::Native,true).output;if(!require(s.applied.sr&&checkControl(201)&&s.metrics.resolution.output==expected&&std::wstring(draft)==L"NaN","SR_click_applies_aspect_preserving_output"))break;
        log::info("ui-repair-test",std::format("output={}x{} NR-evaluated={}",expected.width,expected.height,s.nrEvaluated));if(!smokeSave.empty())engine.saveFrame(smokeSave);SendMessageW(hwnd,WM_COMMAND,Master,0);{const auto pending=engine.snapshot().desired.revision;SetWindowTextW(settingsControlForTest(100),L"0.4");SendMessageW(settingsControlForTest(210),BM_CLICK,0,0);if(!require(engine.snapshot().desired.revision==pending,"master_pending_blocks_overlapping_numeric_submit"))break;SetWindowTextW(settingsControlForTest(100),L"NaN");}repairTick=GetTickCount64();repairStep=4;break;}
    case 4:if(!require(!uiState.enhanced&&!s.applied.nr&&!s.applied.sr&&!checkControl(200)&&!checkControl(201),"master_off_syncs_independent_switches"))break;click(200);repairStep=5;break;
    case 5:if(s.applied.revision!=repairRevision)return;if(!require(uiState.enhanced&&s.applied.nr&&checkControl(200)&&std::wstring(draft)==L"NaN","NR_click_enables_master_and_preserves_draft"))break;{
        auto body=GetParent(settingsControlForTest(100));RECT br{},hr{};GetWindowRect(body,&br);GetWindowRect(settingsControlForTest(400),&hr);SendMessageW(inspector,WM_MOUSEWHEEL,MAKEWPARAM(0,-WHEEL_DELTA*6),0);if(!require(body!=inspector&&br.top>=hr.bottom&&GetParent(body)==inspector,"scroll_controls_clipped_by_separate_viewport"))break;
        showDiagnostics=true;layout();toggleFullscreen();repairTick=GetTickCount64();repairStep=6;break;}
    case 6:{RECT vr{},wr{};GetWindowRect(video,&vr);GetWindowRect(hwnd,&wr);MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&mi);
        if(!require(full&&EqualRect(&vr,&mi.rcMonitor)&&EqualRect(&wr,&mi.rcMonitor)&&!IsWindowVisible(inspector)&&(s.image||IsWindowVisible(seekBar)),"fullscreen_covers_monitor_and_diagnostics_keeps_seek"))break;
        toggleFullscreen();showDiagnostics=false;layout();repairStep=10;log::info("ui-repair-test","PASS native controls, drafts, master sync, SR extent, scroll viewport, fullscreen");break;}
    }
}
