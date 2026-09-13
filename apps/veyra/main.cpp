#include <windows.h>
#include "veyra/Log.h"
#include "veyra/RuntimePaths.h"
int runVeyraApp(HINSTANCE,int);
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show){
    wchar_t logOverride[32768]{};
    const auto length=GetEnvironmentVariableW(L"VEYRA_LOG_FILE",logOverride,32768);
    auto logPath=length>0&&length<32768?std::filesystem::path(logOverride):veyra::runtime::logsDirectory()/L"veyra-app.log";
    // Preserve the failure preceding a restart. Concurrent GUI/test instances
    // must not truncate the active log or silently lose their own diagnostics.
    if(!veyra::Logger::instance().openFile(logPath.wstring(),true)){
        logPath=logPath.parent_path()/(L"veyra-app-"+std::to_wstring(GetCurrentProcessId())+L".log");
        (void)veyra::Logger::instance().openFile(logPath.wstring(),true);
    }
    veyra::log::info("app", "Veyra GUI session started");
    veyra::Logger::instance().flush();
    const int result=runVeyraApp(instance,show);
    veyra::log::info("app", "Veyra GUI session stopped");
    veyra::Logger::instance().flush();
    return result;
}
