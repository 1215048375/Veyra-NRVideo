#pragma once
#include "UiSessionState.h"
#include "veyra/engine/PresetStore.h"
#include <filesystem>
#include <fstream>
#include <sstream>
namespace veyra::ui {
struct UiPreferences {
    float volume=1;bool muted=false,subtitles=true;
    int inspectorWidth=320;bool keepImageDevice=false,cacheRenderedImages=false;bool comparisonLine=true;int comparisonLineColor=0;
    int width=1280,height=800,x=0,y=0,inspector=0,subtitleSize=22;bool positioned=false;
};
class UiPreferenceStore {
    std::filesystem::path folder_;
    bool corrupt_=false;
public:
    explicit UiPreferenceStore(std::filesystem::path folder):folder_(std::move(folder)){}
    UiPreferences load(){UiPreferences result;auto path=folder_/"ui-preferences.v1";if(!std::filesystem::exists(path))return result;
        if(std::filesystem::file_size(path)>2048){corrupt_=true;return result;}std::ifstream file(path);std::string magic;int version=0,mute,sub,pos;UiPreferences read;
        if(!(file>>magic>>version>>read.volume>>mute>>sub>>read.width>>read.height>>read.x>>read.y>>pos>>read.inspector>>read.subtitleSize)||magic!="VEYRA_UI"||(version!=1&&version!=2&&version!=3&&version!=4&&version!=5)||!std::isfinite(read.volume)||read.volume<0||read.volume>1||mute<0||mute>1||sub<0||sub>1||pos<0||pos>1||read.width<720||read.width>10000||read.height<540||read.height>10000||abs(int64_t(read.x))>100000||abs(int64_t(read.y))>100000||read.inspector<0||read.inspector>3||read.subtitleSize<18||read.subtitleSize>36){corrupt_=true;return result;}
        if(version>=2&&(!(file>>read.inspectorWidth)||read.inspectorWidth<296||read.inspectorWidth>420)){corrupt_=true;return result;}if(version>=3){int line;if(!(file>>line>>read.comparisonLineColor)||line<0||line>1||read.comparisonLineColor<0||read.comparisonLineColor>2){corrupt_=true;return result;}read.comparisonLine=line!=0;}if(version>=4){int keep;if(!(file>>keep)||keep<0||keep>1){corrupt_=true;return result;}read.keepImageDevice=keep!=0;}if(version>=5){int cache;if(!(file>>cache)||cache<0||cache>1){corrupt_=true;return result;}read.cacheRenderedImages=cache!=0;}file>>std::ws;if(!file.eof()){corrupt_=true;return result;}read.muted=mute;read.subtitles=sub;read.positioned=pos;return read;
    }
    engine::EnhancementSettings startup(engine::EnhancementSettings fallback){engine::PresetStore last(folder_/"last-applied.v1");if(!last.load()||last.entries().empty())return fallback;return last.entries().front().settings;}
    bool save(const UiPreferences& p,const engine::EnhancementSettings* applied){
        bool ok=true;std::filesystem::create_directories(folder_);if(applied){engine::PresetStore last(folder_/"last-applied.v1");ok=last.load()&&last.put(L"Last applied",*applied,true);}
        if(corrupt_)return false;std::ostringstream out;out<<"VEYRA_UI 5\n"<<p.volume<<' '<<p.muted<<' '<<p.subtitles<<' '<<p.width<<' '<<p.height<<' '<<p.x<<' '<<p.y<<' '<<p.positioned<<' '<<p.inspector<<' '<<p.subtitleSize<<' '<<p.inspectorWidth<<' '<<p.comparisonLine<<' '<<p.comparisonLineColor<<' '<<p.keepImageDevice<<' '<<p.cacheRenderedImages<<'\n';
        auto target=folder_/"ui-preferences.v1",tmp=folder_/(L"ui-preferences.tmp-"+std::to_wstring(GetCurrentProcessId()));auto data=out.str();HANDLE file=CreateFileW(tmp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)return false;DWORD written=0;bool saved=WriteFile(file,data.data(),DWORD(data.size()),&written,nullptr)&&written==data.size()&&FlushFileBuffers(file);CloseHandle(file);if(saved)saved=MoveFileExW(tmp.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;return ok&&saved;
    }
};
}
