#pragma once
#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
namespace veyra::ui {
// Folder navigation is independent of the video playlist and decoder.
struct ImageFolder {
    std::vector<std::wstring> files;
    size_t index=0;
    int wheelDelta=0;
    uint64_t lastWheel=0;
    bool wheeled=false;
    static std::wstring lower(std::wstring s){for(auto& c:s)c=wchar_t(std::towlower(c));return s;}
    static bool supported(const std::filesystem::path& p){auto e=lower(p.extension().wstring());return e==L".png"||e==L".jpg"||e==L".jpeg";}
    bool load(const std::filesystem::path& folder,std::error_code& ec){
        std::vector<std::wstring> found;
        for(std::filesystem::directory_iterator it(folder,ec),end;!ec&&it!=end;it.increment(ec)){
            if(supported(it->path())&&it->is_regular_file(ec))found.push_back(it->path().wstring());
        }
        if(ec||found.empty())return false;
        std::sort(found.begin(),found.end(),[](const auto& a,const auto& b){auto x=lower(a),y=lower(b);return x==y?a<b:x<y;});
        files=std::move(found);index=0;wheelDelta=0;wheeled=false;return true;
    }
    bool active(const std::wstring& path)const{return !files.empty()&&index<files.size()&&files[index]==path;}
    void opened(const std::wstring& path){auto it=std::find(files.begin(),files.end(),path);if(it==files.end()){files.clear();index=0;}else index=size_t(it-files.begin());}
    std::optional<size_t> adjacent(int direction)const{if(files.empty())return {};if(direction<0&&index>0)return index-1;if(direction>0&&index+1<files.size())return index+1;return {};}
    std::optional<size_t> wheel(int delta,uint64_t now){
        if(wheeled&&now-lastWheel<450){wheelDelta=0;return {};}
        if((wheelDelta<0&&delta>0)||(wheelDelta>0&&delta<0))wheelDelta=0;
        wheelDelta=std::clamp(wheelDelta+delta,-120,120);
        if(wheelDelta>-120&&wheelDelta<120)return {};
        const int direction=wheelDelta>0?-1:1;wheelDelta=0;lastWheel=now;wheeled=true;return adjacent(direction);
    }
};
}
