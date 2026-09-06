#pragma once
#include <windows.h>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
namespace veyra::engine {
struct PlayerOptions { bool nr=true,sr=false,fg=false,realtime=true; };
struct PlayerSnapshot {
    std::wstring status=L"Open a video or image";
    double position=0,duration=0,fps=0,lateMs=0,lateP95Ms=0;
    uint64_t frames=0,generated=0;
    bool running=false,failed=false,image=false;
};
class EngineController {
public:
    ~EngineController(){stop();}
    void open(HWND video,const std::wstring& path,PlayerOptions options);
    void stop();
    void pause(bool p){paused_.store(p);}
    void seek(double seconds){seekSeconds_.store(seconds);}
    void saveFrame(const std::wstring& path);
    void startExport(const std::wstring& input,const std::wstring& output,PlayerOptions,bool hevc);
    PlayerSnapshot snapshot()const;
private:
    void run(HWND,std::wstring,PlayerOptions);
    void status(const std::wstring&,bool failed=false);
    mutable std::mutex mutex_;
    PlayerSnapshot snapshot_;
    std::wstring savePath_;
    std::thread worker_;
    std::atomic<bool> stop_{false},paused_{false};
    std::atomic<double> seekSeconds_{-1};
};
}
