// Trucker Chat
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "MenuWindow.h"
#include "Logger.h"
#include <commctrl.h>
#include <algorithm>
#include <cstdlib>

namespace {
enum Id {
    kChat = 101, kPinned = 102,
    kTwitchOn = 110, kTwitchChannel = 111, kTwitchStatus = 112,
    kYouTubeOn = 120, kYouTubeKey = 121, kLink = 122, kPaste = 123, kApplyLink = 124, kYouTubeStatus = 125,
    kHotkey = 130, kDuration = 131, kDurationSpin = 132, kNote = 140,
};
std::wstring W(const std::string& s) { return Utf8ToWide(s); }
}

HWND MenuWindow::FindGame() const {
    struct Ctx { DWORD pid; HWND best; long area; } c{GetCurrentProcessId(), nullptr, 0};
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* c = (Ctx*)lp; DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
        if (pid != c->pid || !IsWindowVisible(h)) return TRUE;
        wchar_t cls[64]; GetClassNameW(h, cls, 64);
        if (wcscmp(cls, Names::kMenuWindowClass) == 0 || wcscmp(cls, Names::kOverlayWindowClass) == 0 || wcscmp(cls, L"RealisticRadioOverlay") == 0) return TRUE;
        RECT r; GetClientRect(h, &r); long a = (r.right - r.left) * (r.bottom - r.top);
        if (a > c->area) { c->area = a; c->best = h; }
        return TRUE;
    }, (LPARAM)&c);
    return c.best;
}

void MenuWindow::Create() {
    HINSTANCE hi = SelfModule();
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_HOTKEY_CLASS | ICC_UPDOWN_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    if (!classRegistered_) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = Proc; wc.hInstance = hi; wc.lpszClassName = Names::kMenuWindowClass;
        wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        classRegistered_ = RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }
    HDC dc = GetDC(nullptr);
    int dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(nullptr, dc);
    auto S = [&](int v) { return MulDiv(v, dpi, 96); };
    font_ = CreateFontW(-S(15), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    bold_ = CreateFontW(-S(20), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    small_ = CreateFontW(-S(13), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU, ex = WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
    RECT r{0, 0, S(470), S(502)};
    AdjustWindowRectEx(&r, style, FALSE, ex);
    wnd_ = CreateWindowExW(ex, Names::kMenuWindowClass, L"Trucker Chat " TC_WIDEN(TC_VERSION), style, 0, 0, r.right - r.left, r.bottom - r.top,
                           nullptr, nullptr, hi, this);
    if (!wnd_) { Log::Write("Menu: window could not be created (error %lu)", GetLastError()); return; }
    auto add = [&](const wchar_t* cls, const wchar_t* text, DWORD st, int id, int x, int y, int w, int h, HFONT f = nullptr) {
        DWORD exs = (wcscmp(cls, L"EDIT") == 0 || wcscmp(cls, HOTKEY_CLASSW) == 0) ? WS_EX_CLIENTEDGE : 0;
        HWND c = CreateWindowExW(exs, cls, text, WS_CHILD | WS_VISIBLE | st, S(x), S(y), S(w), S(h), wnd_, (HMENU)(INT_PTR)id, hi, nullptr);
        SendMessageW(c, WM_SETFONT, (WPARAM)(f ? f : font_), TRUE);
        return c;
    };
    const DWORD edit = WS_TABSTOP | ES_AUTOHSCROLL, check = WS_TABSTOP | BS_AUTOCHECKBOX, button = WS_TABSTOP | BS_PUSHBUTTON;
    add(L"STATIC", L"Trucker Chat", SS_LEFT, -1, 16, 10, 300, 28, bold_);
    add(L"STATIC", W(TC_VERSION).c_str(), SS_RIGHT, -1, 330, 16, 124, 20, small_);
    add(L"BUTTON", L"Chat im LKW anzeigen", check, kChat, 18, 44, 190, 24);
    add(L"BUTTON", L"Dauerhaft einblenden", check, kPinned, 220, 44, 236, 24);

    add(L"BUTTON", L"Twitch", BS_GROUPBOX, -1, 10, 76, 450, 92);
    add(L"BUTTON", L"aktiv", check, kTwitchOn, 24, 98, 80, 22);
    add(L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS, kTwitchStatus, 110, 100, 340, 20, small_);
    add(L"STATIC", L"Kanal:", SS_LEFT, -1, 24, 132, 90, 20);
    SendMessageW(add(L"EDIT", L"", edit, kTwitchChannel, 120, 128, 326, 26), EM_LIMITTEXT, 200, 0);

    add(L"BUTTON", L"YouTube", BS_GROUPBOX, -1, 10, 176, 450, 168);
    add(L"BUTTON", L"aktiv", check, kYouTubeOn, 24, 198, 80, 22);
    add(L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS, kYouTubeStatus, 110, 200, 340, 20, small_);
    add(L"STATIC", L"API-Key (opt.):", SS_LEFT, -1, 24, 232, 94, 20);
    HWND key = add(L"EDIT", L"", edit | ES_PASSWORD, kYouTubeKey, 120, 228, 326, 26);
    SendMessageW(key, EM_LIMITTEXT, 200, 0);
    SendMessageW(key, 0x1501 /* EM_SETCUEBANNER */, FALSE, (LPARAM)L"leer = ohne API-Key (inoffiziell)");
    add(L"STATIC", L"Stream-Link:", SS_LEFT, -1, 24, 268, 94, 20);
    SendMessageW(add(L"EDIT", L"", edit, kLink, 120, 264, 326, 26), EM_LIMITTEXT, 2000, 0);
    add(L"BUTTON", L"Aus Zwischenablage einfügen", button, kPaste, 120, 300, 200, 30);
    add(L"BUTTON", L"Link übernehmen", button, kApplyLink, 326, 300, 120, 30);

    add(L"BUTTON", L"Bedienung", BS_GROUPBOX, -1, 10, 352, 450, 96);
    add(L"STATIC", L"Menü-Taste:", SS_LEFT, -1, 24, 378, 94, 20);
    HWND hk = add(HOTKEY_CLASSW, L"", WS_TABSTOP, kHotkey, 120, 374, 150, 26);
    SendMessageW(hk, HKM_SETRULES, (WPARAM)HKCOMB_NONE, MAKELPARAM(HOTKEYF_CONTROL, 0));   // a bare key gets Ctrl added
    add(L"STATIC", L"Feld anklicken, neue Tasten drücken", SS_LEFT, -1, 280, 378, 176, 20, small_);

    add(L"STATIC", L"Anzeigedauer:", SS_LEFT, -1, 24, 414, 94, 20);
    HWND dur = add(L"EDIT", L"7", edit | ES_NUMBER | ES_RIGHT, kDuration, 120, 410, 56, 26);
    SendMessageW(dur, EM_LIMITTEXT, 3, 0);
    HWND spin = CreateWindowExW(0, UPDOWN_CLASSW, L"", WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_NOTHOUSANDS,
                                0, 0, 0, 0, wnd_, (HMENU)(INT_PTR)kDurationSpin, hi, nullptr);
    SendMessageW(spin, UDM_SETBUDDY, (WPARAM)dur, 0);
    SendMessageW(spin, UDM_SETRANGE32, 1, 120);
    add(L"STATIC", L"Sekunden pro Nachricht (1\u2013120)", SS_LEFT, -1, 186, 414, 260, 20, small_);

    add(L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS, kNote, 16, 466, 200, 22, small_);
    add(L"BUTTON", L"Speichern", button | BS_DEFPUSHBUTTON, IDOK, 240, 460, 104, 32);
    add(L"BUTTON", L"Schließen", button, IDCANCEL, 352, 460, 104, 32);
}

void MenuWindow::Destroy() {
    if (wnd_) { DestroyWindow(wnd_); wnd_ = nullptr; }
    if (classRegistered_) { UnregisterClassW(Names::kMenuWindowClass, SelfModule()); classRegistered_ = false; }
    for (HFONT* f : {&font_, &bold_, &small_}) if (*f) { DeleteObject(*f); *f = nullptr; }
}

void MenuWindow::Fill(const MenuSettings& s) {
    filling_ = true;
    SendMessageW(Ctl(kDurationSpin), UDM_SETPOS32, 0, std::clamp(s.durationSeconds, 1, 120));
    CheckDlgButton(wnd_, kChat, s.chatOn ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd_, kPinned, s.pinned ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd_, kTwitchOn, s.twitchOn ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd_, kYouTubeOn, s.youtubeOn ? BST_CHECKED : BST_UNCHECKED);
    SetWindowTextW(Ctl(kTwitchChannel), W(s.twitchChannel).c_str());
    SetWindowTextW(Ctl(kYouTubeKey), W(s.youtubeKey).c_str());
    SetWindowTextW(Ctl(kLink), W(s.streamLink).c_str());
    BYTE mods = (s.menuKey.ctrl ? HOTKEYF_CONTROL : 0) | (s.menuKey.shift ? HOTKEYF_SHIFT : 0) | (s.menuKey.alt ? HOTKEYF_ALT : 0);
    SendMessageW(Ctl(kHotkey), HKM_SETHOTKEY, MAKEWORD((BYTE)s.menuKey.vk, mods), 0);
    SetNote(L"");
    filling_ = false;
}

std::string MenuWindow::Text(int id) const {
    HWND c = Ctl(id);
    int n = GetWindowTextLengthW(c);
    std::wstring w(n + 1, L'\0');
    GetWindowTextW(c, &w[0], n + 1);
    w.resize(n);
    std::string s = WideToUtf8(w);
    s.erase(0, s.find_first_not_of(" \t\r\n"));
    s.erase(s.find_last_not_of(" \t\r\n") + 1);
    return s;
}

MenuSettings MenuWindow::Read() const {
    MenuSettings s;
    s.chatOn = IsDlgButtonChecked(wnd_, kChat) == BST_CHECKED;
    s.pinned = IsDlgButtonChecked(wnd_, kPinned) == BST_CHECKED;
    s.twitchOn = IsDlgButtonChecked(wnd_, kTwitchOn) == BST_CHECKED;
    s.youtubeOn = IsDlgButtonChecked(wnd_, kYouTubeOn) == BST_CHECKED;
    s.twitchChannel = Text(kTwitchChannel);
    s.youtubeKey = Text(kYouTubeKey);
    s.durationSeconds = std::clamp(atoi(Text(kDuration).c_str()), 1, 120);
    s.streamLink = Text(kLink);
    WORD hk = (WORD)SendMessageW(Ctl(kHotkey), HKM_GETHOTKEY, 0, 0);
    BYTE mods = HIBYTE(hk);
    s.menuKey.vk = LOBYTE(hk);
    s.menuKey.ctrl = (mods & HOTKEYF_CONTROL) != 0;
    s.menuKey.shift = (mods & HOTKEYF_SHIFT) != 0;
    s.menuKey.alt = (mods & HOTKEYF_ALT) != 0;
    return s;
}

void MenuWindow::Open(const MenuSettings& s) {
    if (!wnd_) Create();
    if (!wnd_) return;
    Fill(s);
    SetStatus(stTwitch_, stYoutube_);
    game_ = FindGame();
    RECT wr; GetWindowRect(wnd_, &wr);
    int w = wr.right - wr.left, h = wr.bottom - wr.top;
    RECT area{};
    if (game_) GetWindowRect(game_, &area);
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
    int x = area.left + ((area.right - area.left) - w) / 2, y = area.top + ((area.bottom - area.top) - h) / 2;
    SetWindowPos(wnd_, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(wnd_);
    ClipCursor(nullptr);   // in case the game confined the mouse to its window
    SetFocus(Ctl(kChat));
    Log::Write("Menu opened");
}

void MenuWindow::Close() {
    if (!IsOpen()) return;
    ShowWindow(wnd_, SW_HIDE);
    if (game_ && IsWindow(game_)) SetForegroundWindow(game_);
    Log::Write("Menu closed");
}

bool MenuWindow::CapturingKey() const { return IsOpen() && GetFocus() == Ctl(kHotkey); }

void MenuWindow::SetStatus(const std::string& twitch, const std::string& youtube) {
    stTwitch_ = twitch; stYoutube_ = youtube;
    if (!wnd_) return;
    SetWindowTextW(Ctl(kTwitchStatus), W(twitch).c_str());
    SetWindowTextW(Ctl(kYouTubeStatus), W(youtube).c_str());
}
void MenuWindow::SetLink(const std::string& link) { if (wnd_) SetWindowTextW(Ctl(kLink), W(link).c_str()); }
void MenuWindow::SetPinned(bool on) { if (wnd_) CheckDlgButton(wnd_, kPinned, on ? BST_CHECKED : BST_UNCHECKED); }
void MenuWindow::SetChat(bool on) { if (wnd_) CheckDlgButton(wnd_, kChat, on ? BST_CHECKED : BST_UNCHECKED); }
void MenuWindow::SetNote(const std::wstring& t) { if (wnd_) SetWindowTextW(Ctl(kNote), t.c_str()); }

LRESULT CALLBACK MenuWindow::Proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_NCCREATE) {
        auto* self = (MenuWindow*)((CREATESTRUCTW*)lp)->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
        self->wnd_ = h;
    }
    auto* self = (MenuWindow*)GetWindowLongPtrW(h, GWLP_USERDATA);
    return self ? self->Handle(m, wp, lp) : DefWindowProcW(h, m, wp, lp);
}

LRESULT MenuWindow::Handle(UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_CLOSE: Close(); return 0;
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == kDuration && HIWORD(wp) == EN_CHANGE) {
            int v = atoi(Text(kDuration).c_str());
            if (!filling_ && v >= 1 && v <= 120 && onDuration) onDuration(v);
            return 0;
        }
        if (HIWORD(wp) != BN_CLICKED && id != IDOK && id != IDCANCEL) break;
        switch (id) {
        case kChat: if (onChat) onChat(IsDlgButtonChecked(wnd_, kChat) == BST_CHECKED); return 0;
        case kPinned: if (onPinned) onPinned(IsDlgButtonChecked(wnd_, kPinned) == BST_CHECKED); return 0;
        case kPaste: {
            std::string text;
            if (OpenClipboard(wnd_)) {
                if (HANDLE hd = GetClipboardData(CF_UNICODETEXT)) {
                    if (auto* p = (const wchar_t*)GlobalLock(hd)) { text = WideToUtf8(std::wstring(p, wcsnlen(p, 2000))); GlobalUnlock(hd); }
                }
                CloseClipboard();
            }
            text.erase(0, text.find_first_not_of(" \t\r\n"));
            text.erase(text.find_last_not_of(" \t\r\n") + 1);
            if (text.empty()) { SetNote(L"Zwischenablage ist leer"); return 0; }
            SetLink(text);
            if (onLink) onLink(text);
            return 0;
        }
        case kApplyLink: if (onLink) onLink(Text(kLink)); return 0;
        case IDOK:
            if (GetFocus() == Ctl(kLink)) { if (onLink) onLink(Text(kLink)); return 0; }   // Enter in the link field
            if (onSave) onSave(Read());
            return 0;
        case IDCANCEL: Close(); return 0;
        }
        break;
    }
    }
    return DefWindowProcW(wnd_, m, wp, lp);
}
