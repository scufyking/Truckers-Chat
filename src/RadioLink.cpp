// Trucker Chat
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "RadioLink.h"
#include "Common.h"
#include "Logger.h"

namespace RadioLink {
namespace {
Api g_api;
State g_state = State::NoRadio;
bool g_linked = false;

template <class F> bool Load(HMODULE m, const char* name, F& fn) { fn = (F)(void*)GetProcAddress(m, name); return fn != nullptr; }
}

State Connect(const std::wstring& moduleName, const std::function<bool()>& quit) {
    g_linked = false; g_state = State::NoRadio;
    if (moduleName.empty()) return g_state;
    HMODULE m = GetModuleHandleW(moduleName.c_str());
    if (!m) {   // is the radio installed next to us? Then the game loads it in a moment (plugin order is not fixed)
        wchar_t me[MAX_PATH] = {0};
        GetModuleFileNameW(SelfModule(), me, MAX_PATH);
        std::wstring dir = me; dir = dir.substr(0, dir.find_last_of(L"\\/") + 1);
        if (GetFileAttributesW((dir + moduleName).c_str()) == INVALID_FILE_ATTRIBUTES) {
            Log::Write("Realistic Radio: not installed - Trucker Chat draws by itself");
            return g_state;
        }
        for (int i = 0; i < 200 && !m && !quit(); ++i) { Sleep(100); m = GetModuleHandleW(moduleName.c_str()); }
        if (!m) { Log::Write("Realistic Radio: %ls is in the plugins folder but was not loaded - Trucker Chat draws by itself", moduleName.c_str()); return g_state; }
    }
    int (*version)() = nullptr;
    if (!Load(m, "rr_api_version", version) || version() != 1) {
        g_state = State::OldRadio;
        Log::Write("Realistic Radio: this version has no drawing interface (needs 1.4.3 or newer). To avoid two Direct3D hooks "
                   "Trucker Chat does not draw on the truck screen - side chat only. Please update Realistic Radio.");
        return g_state;
    }
    Api a;
    bool ok = Load(m, "rr_attach", a.attach) && Load(m, "rr_detach", a.detach) && Load(m, "rr_enable_screen_hook", a.enable_screen_hook) &&
              Load(m, "rr_chat_screen_image", a.chat_screen_image) && Load(m, "rr_chat_screen_target", a.chat_screen_target) &&
              Load(m, "rr_chat_screen_alpha", a.chat_screen_alpha) && Load(m, "rr_chat_screen_area", a.chat_screen_area) &&
              Load(m, "rr_chat_image", a.chat_image) && Load(m, "rr_chat_alpha", a.chat_alpha) && Load(m, "rr_chat_placement", a.chat_placement) &&
              Load(m, "rr_d3d11_frames", a.d3d11_frames) && Load(m, "rr_other_frames", a.other_frames) && Load(m, "rr_screen_height", a.screen_height) &&
              Load(m, "rr_screen_hook_active", a.screen_hook_active) && Load(m, "rr_render_target_calls", a.render_target_calls) &&
              Load(m, "rr_collect_targets", a.collect_targets) && Load(m, "rr_take_target_stats", a.take_target_stats) &&
              Load(m, "rr_radio_screen", a.radio_screen) && Load(m, "rr_radio_card_showing", a.radio_card_showing);
    if (!ok) {
        g_state = State::OldRadio;
        Log::Write("Realistic Radio: drawing interface incomplete - side chat only. Please update Realistic Radio.");
        return g_state;
    }
    HMODULE pin = nullptr;   // keep the radio's code loaded while we call it
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, moduleName.c_str(), &pin);
    g_api = a; g_linked = true; g_state = State::Linked;
    Log::Write("Realistic Radio: linked - the chat is drawn by Realistic Radio's Direct3D hook (no second hook)");
    return g_state;
}

State Current() { return g_state; }
const Api* Get() { return g_linked ? &g_api : nullptr; }
}
