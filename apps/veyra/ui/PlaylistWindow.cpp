#include "PlaylistWindow.h"
#include "Theme.h"
#include <commdlg.h>
#include <shellapi.h>

namespace veyra::ui {
namespace {
HWND window = nullptr, list = nullptr;
HFONT listFont = nullptr;
engine::Playlist* queue = nullptr;
std::function<void(size_t)> playEntry;
std::function<void()> openRecent;
enum { Items = 710, Add, PlayItem, Previous, Next, Up, Down, Remove, Clear, Mode, Summary, RecentFile };
int selection() { return int(SendMessageW(list, LB_GETCURSEL, 0, 0)); }
void select(int index) { SendMessageW(list, LB_SETCURSEL, WPARAM(index), 0); }
void refresh(int selected) {
    if (!window) return;
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    int width = 0;
    auto dc = GetDC(list);auto old = SelectObject(dc, listFont);
    for (size_t i = 0; i < queue->entries.size(); ++i) {
        auto label = (queue->current == i ? L"▶ " : L"   ") + std::to_wstring(i + 1) + L". "
            + std::filesystem::path(queue->entries[i]).filename().wstring() + L"    — " + queue->entries[i];
        SendMessageW(list, LB_ADDSTRING, 0, LPARAM(label.c_str()));
        SIZE extent{};GetTextExtentPoint32W(dc, label.c_str(), int(label.size()), &extent);
        width = std::max(width, int(extent.cx));
    }
    SelectObject(dc, old);ReleaseDC(list, dc);
    SendMessageW(list, LB_SETHORIZONTALEXTENT, width + dip(window, 20), 0);
    select(std::min(selected, int(queue->entries.size()) - 1));
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);InvalidateRect(list, nullptr, TRUE);
    const auto text = queue->entries.empty() ? L"列表为空 · 添加或拖入视频文件" : std::to_wstring(queue->entries.size()) + L" 个视频 · 双击播放 · 移除不会删除文件";
    SetDlgItemTextW(window, Summary, text.c_str());
    SendDlgItemMessageW(window, Mode, CB_SETCURSEL, int(queue->mode), 0);
    for (int id : {PlayItem, Up, Down, Remove, Clear, Previous, Next}) EnableWindow(GetDlgItem(window, id), !queue->entries.empty());
}
void append(const std::vector<std::wstring>& paths) {
    size_t skipped = 0;
    for (const auto& path : paths) if (!queue->add(path)) ++skipped;
    refresh(selection());
    if (skipped) MessageBoxW(window, L"部分文件未加入：请选择视频文件，列表最多容纳 4096 项。", L"播放列表", MB_OK | MB_ICONINFORMATION);
}
void resize() {
    RECT r{};GetClientRect(window, &r);
    const int w = MulDiv(r.right, 96, layoutDpi(window)), h = MulDiv(r.bottom, 96, layoutDpi(window));
    auto place = [&](int id, int x, int y, int width, int height) {
        MoveWindow(GetDlgItem(window, id), dip(window,x), dip(window,y), dip(window,width), dip(window,height), TRUE);
    };
    place(Summary, 16, 12, w-32, 24);
    place(Add, 16, 44, 92, 32);place(RecentFile, 116, 44, 92, 32);
    place(Mode, w-180, 44, 164, 180);
    place(Items, 16, 88, w-32, std::max(40,h-192));
    place(Previous, 16, h-92, 84, 32);place(PlayItem, 108, h-92, 100, 32);place(Next, 216, h-92, 84, 32);
    place(Up, 16, h-48, 64, 32);place(Down, 88, h-48, 64, 32);
    place(Remove, 160, h-48, 80, 32);place(Clear, 248, h-48, 80, 32);
}
LRESULT CALLBACK proc(HWND h, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
    case WM_CREATE: {
        window = h;titleTheme(h);listFont = makeFont(h, 14, FW_NORMAL);
        auto create = [&](const wchar_t* cls, const wchar_t* label, int id, DWORD style) {
            auto child = CreateWindowExW(0, cls, label, WS_CHILD|WS_VISIBLE|style, 0,0,1,1,h,HMENU(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
            SendMessageW(child, WM_SETFONT, WPARAM(listFont), TRUE);
            if (id != Items) themeControl(child);
            return child;
        };
        create(L"STATIC", L"", Summary, SS_LEFT);
        create(L"BUTTON", L"添加视频", Add, WS_TABSTOP|BS_PUSHBUTTON);
        create(L"BUTTON", L"最近打开", RecentFile, WS_TABSTOP|BS_PUSHBUTTON);
        auto mode = create(L"COMBOBOX", L"播放方式", Mode, WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL);
        for (auto label : {L"顺序播放", L"列表循环", L"单项循环"}) SendMessageW(mode, CB_ADDSTRING, 0, LPARAM(label));
        list = create(L"LISTBOX", L"视频播放列表", Items, WS_TABSTOP|WS_BORDER|WS_VSCROLL|WS_HSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT);
        for (auto [id,label] : {std::pair{Previous,L"上一项"}, {PlayItem,L"播放选中"}, {Next,L"下一项"}, {Up,L"上移"}, {Down,L"下移"}, {Remove,L"移除"}, {Clear,L"清空"}})
            create(L"BUTTON", label, id, WS_TABSTOP|BS_PUSHBUTTON);
        DragAcceptFiles(h, TRUE);resize();refresh(queue->current ? int(*queue->current) : 0);return 0;
    }
    case WM_SIZE: resize();return 0;
    case WM_GETMINMAXINFO: {
        auto info = reinterpret_cast<MINMAXINFO*>(lp);info->ptMinTrackSize = {dip(h,500),dip(h,360)};return 0;
    }
    case WM_DPICHANGED: {
        auto r = reinterpret_cast<RECT*>(lp);
        auto old = listFont;listFont = makeFont(h,14,FW_NORMAL);
        for (int id = Items; id <= RecentFile; ++id) SendDlgItemMessageW(h,id,WM_SETFONT,WPARAM(listFont),TRUE);
        DeleteObject(old);SetWindowPos(h,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);
        refresh(selection());return 0;
    }
    case WM_DROPFILES: append(droppedPlaylistFiles(wp));return 0;
    case WM_COMMAND: {
        auto selected = selection();
        switch (LOWORD(wp)) {
        case Add: append(choosePlaylistFiles(h));break;
        case RecentFile: openRecent();break;
        case Items: if (HIWORD(wp) != LBN_DBLCLK) break;[[fallthrough]];
        case PlayItem: if (selected >= 0) playEntry(size_t(selected));break;
        case Previous: if (auto index = queue->adjacent(-1)) playEntry(*index);break;
        case Next: if (auto index = queue->adjacent(1)) playEntry(*index);break;
        case Up: if (selected > 0) {queue->move(selected,selected-1);refresh(selected-1);}break;
        case Down: if (selected >= 0 && size_t(selected+1) < queue->entries.size()) {queue->move(selected,selected+1);refresh(selected+1);}break;
        case Remove: if (selected >= 0) {queue->remove(selected);refresh(selected);}break;
        case Clear: queue->clear();refresh(-1);break;
        case Mode: if (HIWORD(wp) == CBN_SELCHANGE) queue->mode = engine::PlaylistMode(SendDlgItemMessageW(h,Mode,CB_GETCURSEL,0,0));break;
        }
        return 0;
    }
    case WM_CTLCOLORLISTBOX: case WM_CTLCOLORSTATIC:
        SetTextColor(HDC(wp),RGB(230,233,235));SetBkColor(HDC(wp),RGB(24,27,30));
        SetDCBrushColor(HDC(wp),RGB(24,27,30));return LRESULT(GetStockObject(DC_BRUSH));
    case WM_ERASEBKGND: {
        RECT r{};GetClientRect(h,&r);SetDCBrushColor(HDC(wp),RGB(24,27,30));FillRect(HDC(wp),&r,HBRUSH(GetStockObject(DC_BRUSH)));return 1;
    }
    case WM_CLOSE: ShowWindow(h, SW_HIDE);return 0;
    case WM_DESTROY: window = list = nullptr;DeleteObject(listFont);listFont = nullptr;return 0;
    }
    return DefWindowProcW(h,message,wp,lp);
}
}

std::vector<std::wstring> choosePlaylistFiles(HWND owner) {
    std::vector<wchar_t> names(1024*1024);
    OPENFILENAMEW d{sizeof(d)};d.hwndOwner = owner;d.lpstrFile = names.data();d.nMaxFile = DWORD(names.size());
    d.lpstrFilter = L"视频文件\0*.mp4;*.mkv;*.mov;*.avi;*.ts;*.m4v;*.mts;*.m2ts;*.webm\0";
    d.Flags = OFN_EXPLORER|OFN_ALLOWMULTISELECT|OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&d)) {
        if (CommDlgExtendedError()) MessageBoxW(owner,L"无法读取所选文件，请减少一次选择的文件数量后重试。",L"播放列表",MB_OK|MB_ICONERROR);
        return {};
    }
    std::filesystem::path first(names.data());auto next = names.data()+wcslen(names.data())+1;
    if (!*next) return {first.wstring()};
    std::vector<std::wstring> result;
    for (; *next; next += wcslen(next)+1) result.push_back((first/next).wstring());
    return result;
}
std::vector<std::wstring> droppedPlaylistFiles(WPARAM value) {
    auto drop = HDROP(value);std::vector<std::wstring> result;
    const auto count = DragQueryFileW(drop,0xffffffff,nullptr,0);
    for (UINT i = 0; i < count; ++i) {
        std::wstring path(DragQueryFileW(drop,i,nullptr,0)+1,L'\0');
        DragQueryFileW(drop,i,path.data(),UINT(path.size()));path.pop_back();result.push_back(std::move(path));
    }
    DragFinish(drop);return result;
}
HWND showPlaylistWindow(HWND owner, engine::Playlist& model, std::function<void(size_t)> play, std::function<void()> recent) {
    queue = &model;playEntry = std::move(play);openRecent = std::move(recent);
    if (!window) {
        WNDCLASSW wc{};wc.lpfnWndProc = proc;wc.hInstance = GetModuleHandleW(nullptr);wc.lpszClassName = L"Veyra.Playlist";wc.hCursor = LoadCursorW(nullptr,IDC_ARROW);
        RegisterClassW(&wc);
        CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_CONTROLPARENT,wc.lpszClassName,L"播放列表",WS_OVERLAPPEDWINDOW,
                        CW_USEDEFAULT,CW_USEDEFAULT,dip(owner,640),dip(owner,480),owner,nullptr,wc.hInstance,nullptr);
    }
    ShowWindow(window,SW_SHOWNORMAL);SetForegroundWindow(window);SetFocus(list);
    return window;
}
void refreshPlaylistWindow() { if (window) refresh(selection()); }
bool playlistWindowMessage(MSG& message) {
    if (!window || (message.hwnd != window && !IsChild(window,message.hwnd))) return false;
    if (message.message == WM_KEYDOWN) {
        if (message.wParam == VK_ESCAPE) {ShowWindow(window,SW_HIDE);return true;}
        if (message.hwnd == list && (message.wParam == VK_RETURN || message.wParam == VK_DELETE)) {
            SendMessageW(window,WM_COMMAND,message.wParam == VK_RETURN ? PlayItem : Remove,0);return true;
        }
    }
    return IsDialogMessageW(window,&message) != FALSE;
}
}
