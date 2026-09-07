#pragma once
#include <windows.h>
#include <string>
namespace veyra::ui {HWND createSubtitleOverlay(HWND);void updateSubtitleOverlay(HWND,const std::wstring&,int size);}
