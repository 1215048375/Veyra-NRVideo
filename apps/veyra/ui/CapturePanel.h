#pragma once
#include <windows.h>
#include <string>
#include <functional>
namespace veyra::ui {void showCapturePanel(HWND,std::function<void(const std::wstring&)>);}
