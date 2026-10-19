// Trucker Chat
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// SCS Telemetry SDK entry points. ETS2/ATS load every DLL in <game>\bin\win_x64\plugins\ and call
// scs_telemetry_init / scs_telemetry_shutdown. DllMain does nothing (loader lock!). All work runs on our own
// "chat thread"; the game's callbacks only store a value and return. Network I/O runs on one thread per chat source.
#include "Common.h"
#include "Logger.h"
#include "Config.h"
#include "EmbeddedData.h"
#include "ChatDisplay.h"
#include "ChatSource.h"
#include "TwitchSource.h"
#include "YouTubeSource.h"
#include "InputManager.h"
#include "D3D11Overlay.h"
#include "YouTubeProtocol.h"
#include "MenuWindow.h"
#include "RadioLink.h"
#include "json.hpp"
#include <fstream>
#include <filesystem>   // wide paths (libc++ has no wstring constructor)
#include <sstream>
#include <scssdk_telemetry.h>
#include <eurotrucks2/scssdk_eut2.h>
#include <eurotrucks2/scssdk_telemetry_eut2.h>
#include <amtrucks/scssdk_ats.h>
#include <common/scssdk_telemetry_common_channels.h>
#include <common/scssdk_telemetry_truck_common_channels.h>
#include <memory>
#include <algorithm>
#include <cmath>

namespace {
std::atomic<bool> g_electric{false}, g_engine{false}, g_paused{true};
std::atomic<bool> g_quit{false};
HANDLE g_thread = nullptr;
HANDLE g_instance = nullptr;   // per-process single-instance mutex (own name, does not collide with Realistic Radio)

SCSAPI_VOID OnBool(const scs_string_t, const scs_u32_t, const scs_value_t* const value, const scs_context_t ctx) {
    bool v = value && value->type == SCS_VALUE_TYPE_bool && value->value_bool.value != 0;
    (ctx ? g_engine : g_electric) = v;
}
SCSAPI_VOID OnEvent(const scs_event_t event, const void* const, const scs_context_t) {
    if (event == SCS_TELEMETRY_EVENT_paused) g_paused = true;
    else if (event == SCS_TELEMETRY_EVENT_started) g_paused = false;
}



// --- YouTube stream link without editing the config: youtube-stream.txt or Ctrl+Shift+Y (clipboard) -------------
const wchar_t* kStreamFile = L"youtube-stream.txt";
const char* kStreamTemplate =
    "# Trucker Chat - Link zum aktuellen YouTube-Livestream\r\n"
    "# Link in die Zeile unter diesen Kommentaren einfuegen und speichern - wird sofort uebernommen (kein Neustart).\r\n"
    "# Schneller: Link kopieren und im Spiel Strg+Umschalt+Y druecken.\r\n"
    "# Paste the link of your current YouTube live stream below and save, or copy it and press Ctrl+Shift+Y in the game.\r\n"
    "\r\n";

std::string ReadStreamFile(const std::wstring& path, FILETIME* written) {
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a)) return "";
    if (written) *written = a.ftLastWriteTime;
    std::ifstream in(std::filesystem::path(path), std::ios::binary);
    std::stringstream ss; ss << in.rdbuf();
    std::string line, all = ss.str();
    std::istringstream ls(all);
    while (std::getline(ls, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t f = line.find_first_not_of(" \t\xEF\xBB\xBF");   // also skips a UTF-8 BOM
        if (f == std::string::npos || line[f] == '#') continue;
        return YouTube::VideoIdFromText(line.substr(f));
    }
    return "";
}

void WriteStreamFile(const std::wstring& path, const std::string& url) {
    std::ofstream out(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    out << kStreamTemplate << url << "\r\n";
}

std::string ReadClipboardText() {
    std::string text;
    if (!OpenClipboard(nullptr)) return text;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (auto* p = (const wchar_t*)GlobalLock(h)) {
            size_t n = wcsnlen(p, 4096);
            text = WideToUtf8(std::wstring(p, n));
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return text;
}

void StatusCard(ChatHub& hub, const std::string& text) {
    Log::Write("YouTube: %s", text.c_str());
    ChatEvent e; e.kind = ChatEvent::Kind::Status; e.platform = Platform::YouTube;
    e.msg.text = text; e.msg.receivedMs = NowMs();
    hub.Push(std::move(e));
}

// Writes the menu's settings into the user's trucker-chat.json (other keys and values stay as they are).
bool SaveSettings(const std::wstring& path, const MenuSettings& m, std::string& error) {
    nlohmann::json j = nlohmann::json::object();
    std::ifstream in(std::filesystem::path(path), std::ios::binary);
    if (in) {
        std::stringstream ss; ss << in.rdbuf();
        j = nlohmann::json::parse(ss.str(), nullptr, false, true);
        if (j.is_discarded() || !j.is_object()) { error = "trucker-chat.json enth\xC3\xA4lt einen Fehler - nicht gespeichert"; return false; }
    } else {
        j = nlohmann::json::parse(EmbeddedText(kResDefaultConfig), nullptr, false);
        if (!j.is_object()) j = nlohmann::json::object();
    }
    in.close();
    for (const char* k : {"display", "twitch", "youtube", "keys"}) if (!j.contains(k) || !j[k].is_object()) j[k] = nlohmann::json::object();
    j["display"]["enabled"] = m.chatOn;
    j["display"]["alwaysVisible"] = m.pinned;
    if (!j.contains("overlay") || !j["overlay"].is_object()) j["overlay"] = nlohmann::json::object();
    j["display"]["durationSeconds"] = m.durationSeconds;
    static const char* kTargets[] = {"screen", "side", "both"};
    j["display"]["target"] = kTargets[std::clamp(m.target, 0, 2)];
    j["overlay"]["durationSeconds"] = m.durationSeconds;
    j["twitch"]["enabled"] = m.twitchOn;
    j["twitch"]["channel"] = m.twitchChannel;
    j["youtube"]["enabled"] = m.youtubeOn;
    j["youtube"]["apiKey"] = m.youtubeKey;
    j["keys"]["menu"] = KeyBindingToString(m.menuKey);
    std::string text = j.dump(2);
    std::wstring tmp = path + L".tmp";
    {
        std::ofstream out(std::filesystem::path(tmp), std::ios::binary | std::ios::trunc);
        out << text;
        if (!out) { error = "trucker-chat.json konnte nicht geschrieben werden"; return false; }
    }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) { DeleteFileW(tmp.c_str()); error = "trucker-chat.json ist gesperrt"; return false; }
    return true;
}

DWORD ChatThread(void*) {
    std::wstring dir = PluginDataDir();
    const std::wstring configFile = dir + Names::kConfigFile;
    Config cfg;
    if (!cfg.Load(configFile, EmbeddedText(kResDefaultConfig)))
        Log::Write("Fix the JSON error in %ls and restart the game (the file was NOT overwritten)", Names::kConfigFile);
    // Realistic Radio 1.4.3+ in the game? Then it draws the chat with its own Direct3D hook (two hooks can crash the game)
    RadioLink::State link = RadioLink::Connect(Utf8ToWide(cfg.screen.radioModule), [] { return g_quit.load(); });
    if (link == RadioLink::State::OldRadio && cfg.display.target != "side") {
        Log::Write("Display: side chat for this session (truck screen needs Realistic Radio 1.4.3 or newer)");
        cfg.display.target = "side";
    }
    auto* hub = new ChatHub();   // deleted at the end only if every source thread has finished
    ChatDisplay display;
    display.Init(cfg, dir);
    InputManager input;
    input.Init(cfg);
    MenuWindow menu;

    const std::wstring streamFile = dir + kStreamFile;
    FILETIME streamWritten{};
    if (GetFileAttributesW(streamFile.c_str()) == INVALID_FILE_ATTRIBUTES) WriteStreamFile(streamFile, "");
    std::string streamId = ReadStreamFile(streamFile, &streamWritten);

    std::vector<ChatSource*> sources;
    YouTubeSource* youtube = nullptr;
    bool leaked = false;
    std::string stTwitch, stYouTube;
    auto startSources = [&]() {
        stTwitch = cfg.twitch.enabled ? "verbinde\xE2\x80\xA6" : "aus";
        stYouTube = cfg.youtube.enabled ? "startet\xE2\x80\xA6" : "aus";
        if (cfg.twitch.enabled) sources.push_back(new TwitchSource(cfg.twitch));
        if (cfg.youtube.enabled) {
            youtube = new YouTubeSource(cfg.youtube);
            if (!streamId.empty()) { Log::Write("YouTube: stream from %ls: %s", kStreamFile, streamId.c_str()); youtube->SetStream(streamId); }
            sources.push_back(youtube);
        }
        if (sources.empty()) Log::Write("No chat source enabled - open the menu (%s) or edit %ls", KeyBindingToString(cfg.keys[(int)Action::Menu]).c_str(), Names::kConfigFile);
        for (auto* s : sources) s->Start(hub);
        menu.SetStatus(stTwitch, stYouTube);
    };
    auto stopSources = [&]() {
        for (auto* s : sources) s->RequestStop();
        for (auto* s : sources) { if (s->Stop()) delete s; else leaked = true; }   // a still-busy source is left alive on purpose
        sources.clear();
        youtube = nullptr;
        for (auto& e : hub->Take()) (void)e;   // events of the old sources
    };
    startSources();

    ULONGLONG nextStreamCheck = 0;

    // returns false if the text holds no YouTube link
    auto useLinkText = [&](const std::string& text, const char* from) {
        std::string id = YouTube::VideoIdFromText(text);
        if (id.empty()) return false;
        streamId = id;
        WriteStreamFile(streamFile, "https://www.youtube.com/watch?v=" + id);   // remembered for the next start
        ReadStreamFile(streamFile, &streamWritten);
        menu.SetLink("https://www.youtube.com/watch?v=" + id);
        Log::Write("YouTube: new stream link (%s): %s", from, id.c_str());
        if (youtube) youtube->SetStream(id);
        else StatusCard(*hub, "Link gespeichert - YouTube ist aber aus (im Men\xC3\xBC aktivieren)");
        return true;
    };
    auto currentSettings = [&]() {
        MenuSettings m;
        m.chatOn = display.Enabled();
        m.pinned = display.Pinned();
        m.durationSeconds = (int)std::lround(cfg.display.durationSeconds);
        m.target = display.Target() == "side" ? 1 : display.Target() == "both" ? 2 : 0;
        m.twitchOn = cfg.twitch.enabled; m.twitchChannel = cfg.twitch.channel;
        m.youtubeOn = cfg.youtube.enabled; m.youtubeKey = cfg.youtube.apiKey;
        m.streamLink = streamId.empty() ? "" : "https://www.youtube.com/watch?v=" + streamId;
        m.menuKey = cfg.keys[(int)Action::Menu];
        return m;
    };
    menu.onChat = [&](bool on) {
        display.SetEnabled(on);
        cfg.display.enabled = on;
        MenuSettings m = currentSettings();
        std::string err;
        menu.SetNote(SaveSettings(configFile, m, err) ? (on ? L"Chat an" : L"Chat aus") : Utf8ToWide(err));
    };
    menu.onPinned = [&](bool on) {
        display.SetPinned(on);
        cfg.display.alwaysVisible = on;
        std::string err;
        menu.SetNote(SaveSettings(configFile, currentSettings(), err) ? (on ? L"Dauerhaft eingeblendet" : L"Nur neue Nachrichten") : Utf8ToWide(err));
    };
    menu.onTarget = [&](int t) {
        static const char* kTargets[] = {"screen", "side", "both"};
        cfg.display.target = kTargets[std::clamp(t, 0, 2)];
        display.SetTarget(cfg.display.target);
        static const wchar_t* kNames[] = {L"Anzeige: Navi", L"Anzeige: Seitenchat", L"Anzeige: Navi + Seitenchat"};
        std::string err;
        menu.SetNote(SaveSettings(configFile, currentSettings(), err) ? kNames[std::clamp(t, 0, 2)] : Utf8ToWide(err));
    };
    menu.onDuration = [&](int secs) {
        cfg.display.durationSeconds = secs;
        cfg.overlay.durationSeconds = secs;
        display.SetDuration(secs);
        std::string err;
        menu.SetNote(SaveSettings(configFile, currentSettings(), err) ? L"Anzeigedauer: " + std::to_wstring(secs) + L" s" : Utf8ToWide(err));
    };
    menu.onLink = [&](const std::string& text) {
        if (text.empty()) { menu.SetNote(L"Erst einen Link einfügen"); return; }
        if (!useLinkText(text, "menu")) { menu.SetNote(L"Kein YouTube-Link erkannt"); return; }
        menu.SetNote(youtube ? L"Link übernommen ✓" : L"Link gespeichert - YouTube ist aus");
    };
    menu.onSave = [&](const MenuSettings& m) {
        std::string err;
        if (!SaveSettings(configFile, m, err)) { menu.SetNote(Utf8ToWide(err)); return; }
        bool restart = m.twitchOn != cfg.twitch.enabled || m.twitchChannel != cfg.twitch.channel ||
                       m.youtubeOn != cfg.youtube.enabled || m.youtubeKey != cfg.youtube.apiKey;
        cfg.display.enabled = m.chatOn;
        display.SetEnabled(m.chatOn);
        cfg.display.alwaysVisible = m.pinned;
        display.SetPinned(m.pinned);
        cfg.display.durationSeconds = cfg.overlay.durationSeconds = m.durationSeconds;
        display.SetDuration(m.durationSeconds);
        {
            static const char* kTargets[] = {"screen", "side", "both"};
            cfg.display.target = kTargets[std::clamp(m.target, 0, 2)];
            if (display.Target() != cfg.display.target) display.SetTarget(cfg.display.target);
        }
        cfg.twitch.enabled = m.twitchOn; cfg.twitch.channel = m.twitchChannel;
        cfg.youtube.enabled = m.youtubeOn; cfg.youtube.apiKey = m.youtubeKey;
        if (m.menuKey.vk) cfg.keys[(int)Action::Menu] = m.menuKey;
        input.Init(cfg);
        if (!m.streamLink.empty() && YouTube::VideoIdFromText(m.streamLink) != streamId) useLinkText(m.streamLink, "menu");
        if (restart) { Log::Write("Menu: chat sources changed - restarting them"); stopSources(); startSources(); }
        Log::Write("Menu: settings saved (menu key %s)", KeyBindingToString(cfg.keys[(int)Action::Menu]).c_str());
        menu.SetNote(L"Gespeichert ✓");
    };
    auto onAction = [&](Action a) {
        if (menu.Active() && a != Action::Menu) return;   // typing in the menu must not trigger chat actions
        Log::Write("Action: %s", ActionName(a));
        switch (a) {
        case Action::Menu: if (menu.IsOpen()) menu.Close(); else menu.Open(currentSettings()); return;
        case Action::YouTubeLink:
            if (!useLinkText(ReadClipboardText(), "clipboard")) StatusCard(*hub, "kein YouTube-Link in der Zwischenablage");
            return;
        case Action::Toggle:
            display.OnAction(a);
            menu.SetChat(display.Enabled());
            return;
        case Action::Pin:
            display.SetPinned(!display.Pinned());
            menu.SetPinned(display.Pinned());
            return;
        default: display.OnAction(a); return;
        }
    };
    Log::Write("Chat thread running - menu key: %s", KeyBindingToString(cfg.keys[(int)Action::Menu]).c_str());
    while (!g_quit) {
        ULONGLONG now = GetTickCount64();
        input.Poll(onAction, menu.CapturingKey());
        if (now >= nextStreamCheck) {   // youtube-stream.txt saved by the streamer?
            nextStreamCheck = now + 1500;
            WIN32_FILE_ATTRIBUTE_DATA a{};
            if (GetFileAttributesExW(streamFile.c_str(), GetFileExInfoStandard, &a) && CompareFileTime(&a.ftLastWriteTime, &streamWritten) != 0) {
                std::string id = ReadStreamFile(streamFile, &streamWritten);
                if (!id.empty() && id != streamId) useLinkText(id, "youtube-stream.txt");
                else if (id.empty()) Log::Write("YouTube: %ls changed but contains no YouTube link", kStreamFile);
            }
        }
        bool statusChanged = false;
        for (auto& e : hub->Take()) {
            if (e.kind == ChatEvent::Kind::Status) {
                if (e.platform == Platform::Twitch) stTwitch = e.msg.text;
                if (e.platform == Platform::YouTube) stYouTube = e.msg.text;
                statusChanged = true;
            }
            display.OnEvent(e);
        }
        if (statusChanged) menu.SetStatus(stTwitch, stYouTube);
        for (auto* s : sources) s->Tick();
        GameState gs;
        const std::string& ps = cfg.display.powerSource;
        gs.powered = ps == "always" || (ps == "engine" ? g_engine.load() : g_electric.load());
        gs.paused = g_paused;
        if (auto* r = RadioLink::Get()) {   // linked with Realistic Radio: same screen as the radio, wait while it shows a card
            int w, h, f, fy, fx; float x, y, aw, ah;
            if (cfg.screen.followRadio && r->radio_screen(&w, &h, &f, &x, &y, &aw, &ah, &fy, &fx))
                display.FollowScreen(w, h, f, x, y, aw, ah, fy != 0, fx != 0);
            gs.radioOnScreen = r->radio_card_showing() != 0;
        }
        display.Tick(gs);
        // wait up to 50 ms, but wake up at once for window messages (menu, overlay window) and pump them
        MsgWaitForMultipleObjectsEx(0, nullptr, 50, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        MSG msg;
        for (int n = 0; n < 200 && PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE); ++n) {
            if (menu.Hwnd() && IsDialogMessageW(menu.Hwnd(), &msg)) continue;   // Tab, Enter, Esc in the menu
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    menu.Destroy();
    stopSources();
    display.Shutdown();
    if (!leaked) delete hub;
    return 0;
}

void WarnAboutCopies() {
    wchar_t me[MAX_PATH] = {0};
    GetModuleFileNameW(SelfModule(), me, MAX_PATH);
    std::wstring mine = me, folder = mine.substr(0, mine.find_last_of(L"\\/") + 1);
    Log::Write("Plugin file: %s", WideToUtf8(mine).c_str());
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((folder + L"*.dll").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring n = fd.cFileName, l = n;
        for (auto& ch : l) ch = towlower(ch);
        if (folder + n != mine && l.find(L"trucker-chat") != std::wstring::npos)
            Log::Write("WARNING: another Trucker Chat DLL is in the plugins folder: %s - only one copy runs", WideToUtf8(n).c_str());
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
}

SCSAPI_RESULT scs_telemetry_init(const scs_u32_t version, const scs_telemetry_init_params_t* const params) {
    if (version != SCS_TELEMETRY_VERSION_1_00 && version != SCS_TELEMETRY_VERSION_1_01) return SCS_RESULT_unsupported;
    const auto* p = static_cast<const scs_telemetry_init_params_v100_t*>(params);
    wchar_t mtxName[96];
    swprintf(mtxName, 96, L"%ls%lu", Names::kMutexPrefix, GetCurrentProcessId());
    g_instance = CreateMutexW(nullptr, FALSE, mtxName);
    if (g_instance && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(g_instance); g_instance = nullptr;
        p->common.log(SCS_LOG_TYPE_warning, "[trucker-chat] another copy of the Trucker Chat plugin is already loaded - this copy stays inactive");
        return SCS_RESULT_ok;
    }
    std::wstring dir = PluginDataDir();
    CreateDirectoryW(dir.c_str(), nullptr);
    Log::Init(dir + Names::kLogFile);
    ExtractEmbeddedData(dir);
    std::string gid = p->common.game_id ? p->common.game_id : "";
    Log::Write("Trucker Chat " TC_VERSION " loaded by %s (game id '%s', telemetry 0x%x)", p->common.game_name ? p->common.game_name : "?", gid.c_str(), version);
    WarnAboutCopies();
    p->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_electric_enabled, SCS_U32_NIL, SCS_VALUE_TYPE_bool,
                            SCS_TELEMETRY_CHANNEL_FLAG_no_value, OnBool, nullptr);
    p->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_engine_enabled, SCS_U32_NIL, SCS_VALUE_TYPE_bool,
                            SCS_TELEMETRY_CHANNEL_FLAG_no_value, OnBool, (scs_context_t)1);
    p->register_for_event(SCS_TELEMETRY_EVENT_paused, OnEvent, nullptr);
    p->register_for_event(SCS_TELEMETRY_EVENT_started, OnEvent, nullptr);
    p->common.log(SCS_LOG_TYPE_message, "[trucker-chat] Trucker Chat plugin initialised");
    g_quit = false;
    g_paused = true;   // the game sends "started" when the simulation runs
    g_thread = StartPinnedThread(ChatThread, nullptr);
    return g_thread ? SCS_RESULT_ok : SCS_RESULT_generic_error;
}

SCSAPI_VOID scs_telemetry_shutdown(void) {
    if (!g_instance) return;   // inactive duplicate copy
    g_quit = true;
    if (g_thread) {
        if (WaitForSingleObject(g_thread, 5000) != WAIT_OBJECT_0) Log::Write("Chat thread did not finish in time");
        CloseHandle(g_thread); g_thread = nullptr;
    }
    g_electric = false; g_engine = false;
    Log::Write("Plugin shut down");
    Log::Shutdown();
    CloseHandle(g_instance); g_instance = nullptr;
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }
