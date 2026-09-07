#pragma once
#include <regex>
#include <string>
namespace veyra::diagnostics {
inline std::string redact(std::string s){
    // Fail closed on unknown path formats: remove from a Windows absolute
    // path to end of line, even when the path contains spaces.
    s=std::regex_replace(s,std::regex(R"([A-Za-z]:[\\/][^\r\n]*)"),"[本地路径已隐藏]");
    s=std::regex_replace(s,std::regex(R"(\\\\[^\r\n]*)"),"[设备或网络路径已隐藏]");
    s=std::regex_replace(s,std::regex(R"((serial|serialnumber|devicepath|username|user)[=: ]+[^\s,;]+)",std::regex::icase),"$1=[已隐藏]");
    return s;
}
}
