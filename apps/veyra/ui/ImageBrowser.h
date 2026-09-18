#pragma once
#include <windows.h>
#include <string>
#include <vector>
namespace veyra::ui {
constexpr UINT ImageBrowserPick=WM_APP+72;
void selectImageBrowser(HWND browser,size_t index);
HWND createImageBrowser(HWND owner);
void loadImageBrowser(HWND browser,const std::vector<std::wstring>& files,size_t selected=0);
}
