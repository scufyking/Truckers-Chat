// Trucker Chat
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
// Decides what is shown when (owned by the plugin's worker thread):
//   in-cab screen: navigation runs normally -> a new message appears for a few seconds as a card (or as a list of the
//   newest messages) -> navigation again. Optional overlay at the top of the game picture.
#include "Config.h"
#include "Cards.h"
#include "GlyphGdi.h"
#include "Filter.h"
#include "TextUtil.h"
#include <functional>
#include <deque>
#include <memory>

struct GameState {
    bool powered = true;       // see display.powerSource
    bool paused = false;       // game paused / in a menu (telemetry "paused" event)
    bool radioOnScreen = false;// Realistic Radio is drawing its station card on the same screen right now
};

class ChatDisplay {
public:
    void Init(const Config& cfg, const std::wstring& dataDir);
    void Shutdown();
    void OnEvent(const ChatEvent& e);
    void OnAction(Action a);
    void SetEnabled(bool on);         // chat display on/off (menu, toggle key)
    bool Enabled() const { return !hidden_; }
    void SetPinned(bool on);          // "Dauerhaft einblenden": newest messages stay visible as a list
    bool Pinned() const { return pinned_; }
    void SetDuration(double seconds);   // card and overlay display time
    void Tick(const GameState& gs);   // ~20x per second
private:
    struct Entry {
        ShownMessage m;
        std::string id, userKey, login;
        Platform source = Platform::System;
        bool status = false;
    };
    ShownMessage Make(const ChatMessage& msg, Platform source, bool status);
    void Remove(const std::function<bool(const Entry&)>& pred);
    std::vector<ShownMessage> Newest(size_t n) const;
    void ShowStatus(const std::string& text);
    // screen
    void TickScreen(ULONGLONG now, const GameState& gs);
    void TickSetup(ULONGLONG now);
    void StartCard(const Entry& e, ULONGLONG now);
    void ShowList(ULONGLONG now, double seconds);
    int AreaW() const, AreaH() const;
    // overlay
    void TickOverlay(ULONGLONG now, const GameState& gs);
    void RenderOverlay();
    void UseWindow(bool on);
    void UpdateWindow(float alpha);
    HWND FindGameWindow();

    Config cfg_;
    CardOptions card_, listOpt_, overlayOpt_;
    SanitizeOptions san_;
    MessageFilter filter_;
    std::unique_ptr<GdiGlyphs> glyphs_;
    std::unique_ptr<TextEngine> te_;
    bool d3d_ = false, screenOn_ = false, overlayOn_ = false, window_ = false;
    bool hidden_ = false, paused_ = false, listMode_ = false, pinned_ = false;
    std::deque<Entry> history_, queue_;
    ULONGLONG start_ = 0, lastTick_ = 0;
    // screen state
    enum class Show { None, Card, List } show_ = Show::None;
    Entry current_;
    ULONGLONG shownAt_ = 0, until_ = 0, lastStart_ = 0, nextScrollAt_ = 0, listDirtyAt_ = 0;
    bool listDirty_ = false;
    int scroll_ = 0, totalLines_ = 0, visibleLines_ = 0;
    // setup modes (probe / grid)
    bool collecting_ = false;
    ULONGLONG nextStatAt_ = 0, nextProbeAt_ = 0;
    struct Cand { int w, h, fmt; long long binds; };
    std::vector<Cand> cands_;
    int probeIdx_ = -1;
    // overlay
    ULONGLONG ovShownAt_ = 0, ovUntil_ = 0;
    int ovRenderedH_ = 0;
    bool ovDirty_ = false;
    std::shared_ptr<Image> ovImage_;
    int anchor_ = 1;
    HWND wnd_ = nullptr, game_ = nullptr;
    float lastAlpha_ = -1; const Image* lastImg_ = nullptr; POINT lastPos_{-99999, -99999};
};
