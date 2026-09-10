#include <windows.h>
#include "veyra/Log.h"
#include "veyra/RuntimePaths.h"
int runVeyraApp(HINSTANCE,int);
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show){
    const auto logPath=(veyra::runtime::logsDirectory()/L"veyra-app.log").wstring();
    (void)veyra::Logger::instance().openFile(logPath);
    veyra::log::info("app", "Veyra GUI session started");
    const int result=runVeyraApp(instance,show);
    veyra::log::info("app", "Veyra GUI session stopped");
    veyra::Logger::instance().flush();
    return result;
}
