// Trucker Chat
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "InputManager.h"
#include <mmsystem.h>

bool InputManager::GameFocused() const {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0; GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();   // the DLL lives inside eurotrucks2.exe
}

static bool Down(int vk) { return vk && (GetAsyncKeyState(vk) & 0x8000) != 0; }

void InputManager::Poll(const std::function<void(Action)>& fire, bool suspended) {
    if (suspended) { for (auto& k : keyDown_) k = true; lastButtons_ = 0xFFFFFFFFu; return; }
    if (cfg_.onlyWhenGameFocused && !GameFocused()) { for (auto& k : keyDown_) k = false; return; }
    for (int i = 0; i < (int)Action::Count; ++i) {
        const KeyBinding& b = cfg_.keys[i];
        bool d = b.vk && Down(b.vk) && Down(VK_CONTROL) == b.ctrl && Down(VK_SHIFT) == b.shift && Down(VK_MENU) == b.alt;
        if (d && !keyDown_[i]) fire((Action)i);
        keyDown_[i] = d;
    }
    bool anyButton = false;
    for (int b : cfg_.buttons) if (b >= 0) anyButton = true;
    if (!anyButton) return;
    ULONGLONG now = GetTickCount64();
    if (!joyOk_ && now < nextJoyRetry_) return;
    JOYINFOEX ji{}; ji.dwSize = sizeof(ji); ji.dwFlags = JOY_RETURNBUTTONS;
    if (joyGetPosEx(JOYSTICKID1 + cfg_.controllerIndex, &ji) != JOYERR_NOERROR) { joyOk_ = false; nextJoyRetry_ = now + 3000; return; }
    joyOk_ = true;
    for (int i = 0; i < (int)Action::Count; ++i) {
        int b = cfg_.buttons[i];
        if (b < 0 || b > 31) continue;
        DWORD m = 1u << b;
        if ((ji.dwButtons & m) && !(lastButtons_ & m)) fire((Action)i);
    }
    lastButtons_ = ji.dwButtons;
}
