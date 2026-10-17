// Trucker Chat
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "Config.h"
#include "Logger.h"
#include "json.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>

using nlohmann::json;

static const char* kActionNames[] = {"toggle", "replay", "pause", "youtubeLink", "menu", "pin"};
const char* ActionName(Action a) { return kActionNames[(int)a]; }

int ParseKeyName(const std::string& in) {
    std::string n = in; std::transform(n.begin(), n.end(), n.begin(), ::tolower);
    if (n.empty()) return 0;
    if (n.size() == 1 && ((n[0] >= 'a' && n[0] <= 'z') || (n[0] >= '0' && n[0] <= '9'))) return ::toupper(n[0]);
    if (n.size() == 2 && n[0] == 'd' && isdigit((unsigned char)n[1])) return n[1];
    if (n[0] == 'f' && n.size() <= 3 && n.size() >= 2 && isdigit((unsigned char)n[1])) { int f = atoi(n.c_str() + 1); if (f >= 1 && f <= 24) return VK_F1 + f - 1; }
    if (n.rfind("numpad", 0) == 0 && n.size() == 7 && isdigit((unsigned char)n[6])) return VK_NUMPAD0 + (n[6] - '0');
    if (n.rfind("0x", 0) == 0) return (int)strtol(n.c_str(), nullptr, 16);
    static const std::map<std::string, int> m = {
        {"pageup", VK_PRIOR}, {"pagedown", VK_NEXT}, {"end", VK_END}, {"home", VK_HOME}, {"insert", VK_INSERT},
        {"delete", VK_DELETE}, {"left", VK_LEFT}, {"right", VK_RIGHT}, {"up", VK_UP}, {"down", VK_DOWN}, {"pause", VK_PAUSE},
        {"scroll", VK_SCROLL}, {"space", VK_SPACE}, {"tab", VK_TAB}, {"enter", VK_RETURN}, {"backspace", VK_BACK},
        {"oemplus", VK_OEM_PLUS}, {"oemminus", VK_OEM_MINUS}, {"oemcomma", VK_OEM_COMMA}, {"oemperiod", VK_OEM_PERIOD},
        {"add", VK_ADD}, {"subtract", VK_SUBTRACT}, {"multiply", VK_MULTIPLY}, {"divide", VK_DIVIDE}, {"decimal", VK_DECIMAL}};
    auto it = m.find(n);
    if (it != m.end()) return it->second;
    Log::Write("Unknown key name '%s'", in.c_str());
    return 0;
}

KeyBinding ParseKeyBinding(const std::string& s) {
    KeyBinding b;
    std::stringstream ss(s); std::string part;
    while (std::getline(ss, part, '+')) {
        part.erase(0, part.find_first_not_of(" \t")); part.erase(part.find_last_not_of(" \t") + 1);
        std::string l = part; std::transform(l.begin(), l.end(), l.begin(), ::tolower);
        if (l == "ctrl" || l == "control") b.ctrl = true;
        else if (l == "shift") b.shift = true;
        else if (l == "alt") b.alt = true;
        else if (!l.empty()) b.vk = ParseKeyName(part);
    }
    return b;
}

std::string KeyBindingToString(const KeyBinding& b) {
    if (!b.vk) return "";
    std::string s;
    if (b.ctrl) s += "Ctrl+";
    if (b.shift) s += "Shift+";
    if (b.alt) s += "Alt+";
    int vk = b.vk;
    char buf[16];
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) { buf[0] = (char)vk; buf[1] = 0; }
    else if (vk >= VK_F1 && vk <= VK_F24) snprintf(buf, sizeof(buf), "F%d", vk - VK_F1 + 1);
    else if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) snprintf(buf, sizeof(buf), "Numpad%d", vk - VK_NUMPAD0);
    else {
        static const std::pair<int, const char*> names[] = {
            {VK_PRIOR, "PageUp"}, {VK_NEXT, "PageDown"}, {VK_END, "End"}, {VK_HOME, "Home"}, {VK_INSERT, "Insert"},
            {VK_DELETE, "Delete"}, {VK_LEFT, "Left"}, {VK_RIGHT, "Right"}, {VK_UP, "Up"}, {VK_DOWN, "Down"}, {VK_PAUSE, "Pause"},
            {VK_SCROLL, "Scroll"}, {VK_SPACE, "Space"}, {VK_TAB, "Tab"}, {VK_RETURN, "Enter"}, {VK_BACK, "Backspace"},
            {VK_ADD, "Add"}, {VK_SUBTRACT, "Subtract"}, {VK_MULTIPLY, "Multiply"}, {VK_DIVIDE, "Divide"}, {VK_DECIMAL, "Decimal"}};
        snprintf(buf, sizeof(buf), "0x%02X", vk);
        for (auto& n : names) if (n.first == vk) snprintf(buf, sizeof(buf), "%s", n.second);
    }
    return s + buf;
}

namespace {
template <class T> void Get(const json& j, const char* k, T& v) {
    auto it = j.find(k);
    if (it == j.end() || it->is_null()) return;
    try { v = it->get<T>(); } catch (...) { Log::Write("Config: '%s' has the wrong type - default kept", k); }
}
void GetList(const json& j, const char* k, std::vector<std::string>& v) {
    auto it = j.find(k);
    if (it == j.end() || !it->is_array()) return;
    v.clear();
    for (auto& e : *it) if (e.is_string()) v.push_back(e.get<std::string>());
}
const json& Sec(const json& j, const char* k) {
    static const json empty = json::object();
    auto it = j.find(k);
    return (it != j.end() && it->is_object()) ? *it : empty;
}
template <class T> T Clamp(T v, T lo, T hi) { return std::min(hi, std::max(lo, v)); }
}

bool Config::Load(const std::wstring& path, const std::string& defaultsJson) {
    json merged = json::parse(defaultsJson, nullptr, false);
    if (merged.is_discarded()) { Log::Write("Config: embedded defaults are invalid"); merged = json::object(); }
    bool ok = true;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::ofstream out(path, std::ios::binary);
        out << defaultsJson;
        Log::Write("Config: created %s with default values - enter your Twitch channel / YouTube data there", WideToUtf8(path).c_str());
    } else {
        std::stringstream ss; ss << in.rdbuf();
        json user = json::parse(ss.str(), nullptr, false, true /*comments*/);
        if (user.is_discarded() || !user.is_object()) {
            ok = false;
            try { (void)json::parse(ss.str(), nullptr, true, true); }
            catch (const std::exception& e) { Log::Write("Config: %s is not valid JSON (%s) - using defaults", WideToUtf8(path).c_str(), e.what()); }
        } else merged.merge_patch(user);
    }
    const json& t = Sec(merged, "twitch");
    Get(t, "enabled", twitch.enabled); Get(t, "channel", twitch.channel); Get(t, "username", twitch.username); Get(t, "oauthToken", twitch.oauthToken);
    const json& y = Sec(merged, "youtube");
    Get(y, "enabled", youtube.enabled); Get(y, "mode", youtube.mode); Get(y, "apiKey", youtube.apiKey); Get(y, "video", youtube.video); Get(y, "channel", youtube.channel);
    Get(y, "pollSeconds", youtube.pollSeconds); Get(y, "searchMinutes", youtube.searchMinutes); Get(y, "showBacklog", youtube.showBacklog);
    youtube.pollSeconds = Clamp(youtube.pollSeconds, 2.0, 120.0); youtube.searchMinutes = Clamp(youtube.searchMinutes, 1.0, 120.0);
    const json& d = Sec(merged, "display");
    Get(d, "enabled", display.enabled); Get(d, "alwaysVisible", display.alwaysVisible); Get(d, "mode", display.mode); Get(d, "durationSeconds", display.durationSeconds); Get(d, "minIntervalSeconds", display.minIntervalSeconds);
    Get(d, "listSize", display.listSize); Get(d, "listLines", display.listLines); Get(d, "maxQueue", display.maxQueue);
    Get(d, "maxTextLength", display.maxTextLength); Get(d, "maxLines", display.maxLines); Get(d, "longText", display.longText);
    Get(d, "scrollSeconds", display.scrollSeconds); Get(d, "showPlatform", display.showPlatform); Get(d, "userColors", display.userColors);
    Get(d, "showStatus", display.showStatus); Get(d, "emoji", display.emoji); Get(d, "emojiReplacement", display.emojiReplacement);
    Get(d, "powerSource", display.powerSource); Get(d, "textScale", display.textScale); GetList(d, "fallbackFonts", display.fallbackFonts);
    display.durationSeconds = Clamp(display.durationSeconds, 1.0, 120.0);
    display.minIntervalSeconds = Clamp(display.minIntervalSeconds, 0.0, 60.0);
    display.listSize = Clamp(display.listSize, 1, 10); display.listLines = Clamp(display.listLines, 1, 4);
    display.maxQueue = Clamp(display.maxQueue, 1, 100); display.maxTextLength = Clamp(display.maxTextLength, 10, 500);
    display.maxLines = Clamp(display.maxLines, 1, 10); display.scrollSeconds = Clamp(display.scrollSeconds, 0.3, 10.0);
    display.textScale = Clamp(display.textScale, 0.5, 2.0);
    const json& s = Sec(merged, "screen");
    Get(s, "enabled", screen.enabled); Get(s, "width", screen.width); Get(s, "height", screen.height); Get(s, "format", screen.format);
    Get(s, "flipY", screen.flipY); Get(s, "flipX", screen.flipX); Get(s, "areaX", screen.areaX); Get(s, "areaY", screen.areaY);
    Get(s, "areaW", screen.areaW); Get(s, "areaH", screen.areaH); Get(s, "probe", screen.probe); Get(s, "probeSeconds", screen.probeSeconds);
    Get(s, "grid", screen.grid); Get(s, "background", screen.background); Get(s, "radioCoexist", screen.radioCoexist); Get(s, "radioModule", screen.radioModule);
    screen.areaX = Clamp(screen.areaX, 0.0, 1.0); screen.areaY = Clamp(screen.areaY, 0.0, 1.0);
    screen.areaW = Clamp(screen.areaW, 0.01, 1.0 - screen.areaX); screen.areaH = Clamp(screen.areaH, 0.01, 1.0 - screen.areaY);
    screen.background = Clamp(screen.background, 0.0, 1.0); screen.probeSeconds = Clamp(screen.probeSeconds, 1.0, 60.0);
    const json& o = Sec(merged, "overlay");
    Get(o, "enabled", overlay.enabled); Get(o, "mode", overlay.mode); Get(o, "position", overlay.position); Get(o, "offsetX", overlay.offsetX);
    Get(o, "offsetY", overlay.offsetY); Get(o, "scale", overlay.scale); Get(o, "width", overlay.width); Get(o, "background", overlay.background);
    Get(o, "durationSeconds", overlay.durationSeconds);
    overlay.scale = Clamp(overlay.scale, 0.3, 4.0); overlay.width = Clamp(overlay.width, 0.2, 1.7); overlay.background = Clamp(overlay.background, 0.0, 1.0);
    const json& f = Sec(merged, "filters");
    Get(f, "hideCommands", filters.hideCommands); GetList(f, "commandPrefixes", filters.commandPrefixes); GetList(f, "ignoreUsers", filters.ignoreUsers);
    GetList(f, "blockedWords", filters.blockedWords); Get(f, "blockedWordsAction", filters.blockedWordsAction); Get(f, "links", filters.links);
    Get(f, "linkText", filters.linkText); Get(f, "showSuperChats", filters.showSuperChats);
    const json& k = Sec(merged, "keys");
    const json& b = Sec(merged, "buttons");
    for (int i = 0; i < (int)Action::Count; ++i) {
        std::string key; Get(k, kActionNames[i], key); keys[i] = ParseKeyBinding(key);
        Get(b, kActionNames[i], buttons[i]);
    }
    Get(merged, "controllerIndex", controllerIndex); Get(merged, "onlyWhenGameFocused", onlyWhenGameFocused); Get(merged, "logMessages", logMessages);
    return ok;
}
