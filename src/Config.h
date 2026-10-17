// Trucker Chat
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
#include "Common.h"
#include "Filter.h"

enum class Action { Toggle, Replay, Pause, YouTubeLink, Menu, Pin, Count };
const char* ActionName(Action a);
struct KeyBinding { int vk = 0; bool ctrl = false, shift = false, alt = false; };
int ParseKeyName(const std::string& name);
KeyBinding ParseKeyBinding(const std::string& s);
std::string KeyBindingToString(const KeyBinding& b);   // "Ctrl+9", "Ctrl+Shift+F5", ...

struct TwitchConfig { bool enabled = false; std::string channel, username, oauthToken; };
struct YouTubeConfig {
    bool enabled = false;
    std::string mode = "auto";            // auto (API key set -> api, else web) | api | web (unofficial, no key)
    std::string apiKey, video, channel;
    double pollSeconds = 12, searchMinutes = 5;
    bool showBacklog = false;
};
struct DisplayConfig {
    bool enabled = true;                  // chat display on/off (menu checkbox)
    bool alwaysVisible = false;           // newest messages stay on the screen permanently (list)
    std::string mode = "card";            // card | list
    double durationSeconds = 7, minIntervalSeconds = 1.5, scrollSeconds = 1.6, textScale = 1.0;
    int listSize = 4, listLines = 2, maxQueue = 8, maxTextLength = 200, maxLines = 5;
    std::string longText = "truncate";    // truncate | scroll
    bool showPlatform = true, userColors = true, showStatus = true;
    std::string emoji = "keep", emojiReplacement = "\xE2\x96\xA1";
    std::string powerSource = "ignition"; // ignition | engine | always
    std::vector<std::string> fallbackFonts;
};
struct ScreenConfig {
    bool enabled = true;
    int width = 1024, height = 1024, format = 29;   // tested defaults: MAN TGX navigation, ETS2 1.61 (from Realistic Radio)
    bool flipY = true, flipX = false;
    double areaX = 0.1, areaY = 0.3, areaW = 0.8, areaH = 0.4;
    bool probe = false, grid = false;
    double probeSeconds = 5, background = 1.0;
    std::string radioCoexist = "yield";             // yield | ignore
    std::string radioModule = "realistic-radio.dll";
};
struct OverlayConfig {
    bool enabled = false;
    std::string mode = "auto";            // auto | d3d11 | window
    std::string position = "top-center";
    double offsetX = 0, offsetY = 0.02, scale = 1.0, width = 0.62, background = 0.82, durationSeconds = 10;
};

struct Config {
    TwitchConfig twitch;
    YouTubeConfig youtube;
    DisplayConfig display;
    ScreenConfig screen;
    OverlayConfig overlay;
    FilterConfig filters;
    KeyBinding keys[(int)Action::Count];
    int buttons[(int)Action::Count] = {-1, -1, -1, -1, -1, -1};
    int controllerIndex = 0;
    bool onlyWhenGameFocused = true;
    bool logMessages = false;

    // defaultsJson: the default configuration embedded in the DLL. A missing user file is created from it;
    // keys missing in the user file fall back to it. Returns false (and keeps the defaults) on a JSON syntax error.
    bool Load(const std::wstring& path, const std::string& defaultsJson);
};
