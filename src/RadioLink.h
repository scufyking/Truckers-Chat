// Trucker Chat
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
// Link to Realistic Radio (realistic-radio.dll 1.4.3 or newer) in the same game process.
// Two plugins that both hook Direct3D 11 can crash the game. When Realistic Radio offers its drawing interface
// (rr_api_version etc., see its src/PluginApi.cpp), Trucker Chat does not hook Direct3D at all: it hands its pictures to
// the radio, which draws them after its own (in-cab screen layer 1, second back-buffer picture).
#include <cstdint>
#include <functional>
#include <string>

namespace RadioLink {
enum class State { NoRadio, Linked, OldRadio };
struct Api {
    int (*attach)() = nullptr;
    void (*detach)() = nullptr;
    void (*enable_screen_hook)(int) = nullptr;
    void (*chat_screen_image)(const uint32_t*, int, int, int) = nullptr;
    void (*chat_screen_target)(int, int, int) = nullptr;
    void (*chat_screen_alpha)(float) = nullptr;
    void (*chat_screen_area)(float, float, float, float, int, int) = nullptr;
    void (*chat_image)(const uint32_t*, int, int, int) = nullptr;
    void (*chat_alpha)(float) = nullptr;
    void (*chat_placement)(int, float, float) = nullptr;
    long long (*d3d11_frames)() = nullptr;
    long long (*other_frames)() = nullptr;
    int (*screen_height)() = nullptr;
    int (*screen_hook_active)() = nullptr;
    long long (*render_target_calls)() = nullptr;
    void (*collect_targets)(int) = nullptr;
    int (*take_target_stats)(long long*, int) = nullptr;
    int (*radio_screen)(int*, int*, int*, float*, float*, float*, float*, int*, int*) = nullptr;
    int (*radio_card_showing)() = nullptr;
};
// Call once from the chat thread before anything is drawn. If the radio DLL lies in the plugins folder, waits until
// the game has loaded it (at most ~20 s, `quit` ends the wait early).
State Connect(const std::wstring& moduleName, const std::function<bool()>& quit);
State Current();
const Api* Get();   // non-null while linked
}
