// Trucker Chat
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
// Polls keyboard (GetAsyncKeyState) and a joystick (joyGetPosEx) from the worker thread, like Realistic Radio.
// The SCS SDK input API only lets plugins *provide* input devices; it does not report key presses to plugins.
#include "Config.h"
#include <functional>

class InputManager {
public:
    void Init(const Config& cfg) { cfg_ = cfg; }
    // suspended: nothing fires (e.g. while the menu's key field records a new key); held keys must be released first
    void Poll(const std::function<void(Action)>& fire, bool suspended = false);
private:
    bool GameFocused() const;
    Config cfg_;
    bool keyDown_[(int)Action::Count] = {};
    DWORD lastButtons_ = 0;
    bool joyOk_ = true;
    ULONGLONG nextJoyRetry_ = 0;
};
