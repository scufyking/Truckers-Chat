// Trucker Chat
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
// Settings menu (default key Ctrl+9). A normal Win32 window owned by the chat thread, shown topmost over the game.
// While it is the active window the game does not get mouse/keyboard input, so it can be used with the mouse.
// Built only from standard Windows controls (buttons, edit fields, the common-controls "hotkey" field).
#include "Config.h"
#include <functional>

struct MenuSettings {
    bool chatOn = true;
    bool pinned = false;            // "Dauerhaft einblenden"
    bool twitchOn = false;
    std::string twitchChannel;
    bool youtubeOn = false;
    std::string youtubeKey;
    std::string streamLink;
    KeyBinding menuKey;
    int durationSeconds = 7;        // how long one message stays visible (truck screen card and overlay)
};

class MenuWindow {
public:
    std::function<void(bool)> onChat;                        // checkbox "Chat anzeigen" (applies at once)
    std::function<void(bool)> onPinned;                      // checkbox "Dauerhaft einblenden" (applies at once)
    std::function<void(int)> onDuration;                     // "Anzeigedauer" in seconds (applies at once)
    std::function<void(const std::string&)> onLink;          // text of the stream link field (applies at once)
    std::function<void(const MenuSettings&)> onSave;         // "Speichern"
    ~MenuWindow() { Destroy(); }
    void Open(const MenuSettings& s);
    void Close();
    bool IsOpen() const { return wnd_ && IsWindowVisible(wnd_); }
    bool Active() const { return IsOpen() && GetForegroundWindow() == wnd_; }
    bool CapturingKey() const;                               // the key field has the focus (records a new key)
    void SetStatus(const std::string& twitch, const std::string& youtube);
    void SetLink(const std::string& link);
    void SetChat(bool on);
    void SetPinned(bool on);
    void SetNote(const std::wstring& text);
    HWND Hwnd() const { return wnd_; }
    void Destroy();
private:
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(UINT, WPARAM, LPARAM);
    void Create();
    void Fill(const MenuSettings& s);
    MenuSettings Read() const;
    std::string Text(int id) const;
    HWND Ctl(int id) const { return GetDlgItem(wnd_, id); }
    HWND FindGame() const;
    HWND wnd_ = nullptr, game_ = nullptr;
    HFONT font_ = nullptr, bold_ = nullptr, small_ = nullptr;
    bool classRegistered_ = false, filling_ = false;
    std::string stTwitch_, stYoutube_;
};
