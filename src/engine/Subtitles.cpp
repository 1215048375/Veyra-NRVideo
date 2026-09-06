#include "veyra/engine/Subtitles.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
namespace veyra::engine {
std::vector<SubtitleCue> loadSrt(const std::wstring& path){
    std::ifstream in(std::filesystem::path(path),std::ios::binary);if(!in)return {};
    in.seekg(0,std::ios::end);if(in.tellg()>8*1024*1024)return {};in.seekg(0);
    std::string data((std::istreambuf_iterator<char>(in)),{});std::wstring wide;
    if(data.size()>2&&uint8_t(data[0])==255&&uint8_t(data[1])==254){for(size_t i=2;i+1<data.size();i+=2)wide.push_back(wchar_t(uint8_t(data[i])|uint8_t(data[i+1])<<8));}
    else {const char* start=data.data();int size=int(data.size());if(size>=3&&uint8_t(start[0])==239&&uint8_t(start[1])==187&&uint8_t(start[2])==191){start+=3;size-=3;}int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,start,size,nullptr,0);if(n<=0)return {};wide.resize(n);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,start,size,wide.data(),n);}
    std::wistringstream lines(wide);std::wstring line;std::vector<SubtitleCue> cues;
    while(std::getline(lines,line)){int h,m,s,ms,h2,m2,s2,ms2;if(swscanf_s(line.c_str(),L"%d:%d:%d,%d --> %d:%d:%d,%d",&h,&m,&s,&ms,&h2,&m2,&s2,&ms2)!=8)continue;
        SubtitleCue cue{h*3600.0+m*60+s+ms/1000.0,h2*3600.0+m2*60+s2+ms2/1000.0,{}};
        while(std::getline(lines,line)){if(!line.empty()&&line.back()==L'\r')line.pop_back();if(line.empty())break;if(!cue.text.empty())cue.text+=L"\n";cue.text+=line;}
        if(cue.end>cue.begin&&cue.text.size()<8192)cues.push_back(std::move(cue));if(cues.size()>=50000)break;
    }return cues;
}
std::wstring subtitleAt(const std::vector<SubtitleCue>& cues,double t){for(const auto& c:cues)if(c.begin<=t&&t<c.end)return c.text;return {};}
}
