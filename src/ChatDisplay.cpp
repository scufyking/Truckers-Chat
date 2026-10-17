// Trucker Chat
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "ChatDisplay.h"
#include "D3D11Overlay.h"
#include "Logger.h"
#include <cstdio>
#include <algorithm>
#include <cmath>

namespace {
int AnchorFromName(const std::string& p) {
    static const char* names[] = {"top-left", "top-center", "top-right", "bottom-left", "bottom-center", "bottom-right"};
    for (int i = 0; i < 6; ++i) if (p == names[i]) return i;
    return 1;
}
const uint32_t kStatusColor = 0xFFAE00;
}

void ChatDisplay::Init(const Config& cfg, const std::wstring& dataDir) {
    cfg_ = cfg;
    start_ = lastTick_ = GetTickCount64();
    glyphs_ = std::make_unique<GdiGlyphs>();
    glyphs_->Init(dataDir + L"fonts\\", cfg.display.fallbackFonts);
    std::u32string repl = DecodeUtf8(cfg.display.emojiReplacement);
    te_ = std::make_unique<TextEngine>(*glyphs_, repl.empty() ? U"?" : repl);
    filter_.Configure(cfg.filters);
    san_.emoji = cfg.display.emoji;
    san_.emojiReplacement = repl;
    san_.maxLength = (size_t)cfg.display.maxTextLength;
    card_.maxLines = cfg.display.maxLines;
    card_.scroll = cfg.display.longText == "scroll";
    card_.showPlatform = cfg.display.showPlatform;
    card_.userColors = cfg.display.userColors;
    card_.background = (float)cfg.screen.background;
    card_.textScale = (float)cfg.display.textScale;
    card_.listLines = cfg.display.listLines;
    listOpt_ = card_;
    overlayOpt_ = card_;
    overlayOpt_.background = (float)cfg.overlay.background;
    listMode_ = cfg.display.mode == "list";
    hidden_ = !cfg.display.enabled;
    pinned_ = cfg.display.alwaysVisible;
    listDirty_ = pinned_;
    anchor_ = AnchorFromName(cfg.overlay.position);

    const ScreenConfig& s = cfg.screen;
    if (s.enabled) {
        D3D11Overlay::EnableScreenHook(true);
        D3D11Overlay::SetScreenArea((float)s.areaX, (float)s.areaY, (float)s.areaW, (float)s.areaH, s.flipY, s.flipX);
        collecting_ = s.probe || s.width <= 0 || s.height <= 0;
        D3D11Overlay::SetCollectTargets(collecting_);
        nextStatAt_ = start_ + 15000; nextProbeAt_ = start_ + 20000;
        if (s.probe) Log::Write("Screen: PROBE mode - test patterns are drawn into candidate render targets one after another");
        else if (s.grid) Log::Write("Screen: GRID mode - coordinate grid on %dx%d (format %d)", s.width, s.height, s.format);
        else if (collecting_) Log::Write("Screen: no target size set - logging render targets; set screen.probe = true to find the screen");
        else {
            D3D11Overlay::SetScreenTarget(s.width, s.height, s.format);
            Log::Write("Screen: drawing into render target %dx%d (format %d), area %.2f/%.2f %.2fx%.2f -> card %dx%d px",
                       s.width, s.height, s.format, s.areaX, s.areaY, s.areaW, s.areaH, AreaW(), AreaH());
        }
    }
    overlayOn_ = cfg.overlay.enabled;
    if (s.enabled || (overlayOn_ && cfg.overlay.mode != "window")) d3d_ = D3D11Overlay::Install();
    screenOn_ = s.enabled && D3D11Overlay::ScreenHookActive();
    if (overlayOn_) {
        D3D11Overlay::SetPlacement(anchor_, (float)cfg.overlay.offsetX, (float)cfg.overlay.offsetY);
        if (cfg.overlay.mode == "window" || (cfg.overlay.mode == "auto" && !d3d_)) UseWindow(true);
        Log::Write("Overlay: enabled (%s, %s)", cfg.overlay.mode.c_str(), cfg.overlay.position.c_str());
    }
    Log::Write("Display: mode %s, %.1f s per message, min. %.1f s between messages, power source %s",
               cfg.display.mode.c_str(), cfg.display.durationSeconds, cfg.display.minIntervalSeconds, cfg.display.powerSource.c_str());
}

void ChatDisplay::Shutdown() {
    D3D11Overlay::Uninstall();
    UseWindow(false);
}

int ChatDisplay::AreaW() const { return std::max(64, (int)std::lround(cfg_.screen.width * std::clamp(cfg_.screen.areaW, 0.01, 1.0))); }
int ChatDisplay::AreaH() const { return std::max(64, (int)std::lround(cfg_.screen.height * std::clamp(cfg_.screen.areaH, 0.01, 1.0))); }

ShownMessage ChatDisplay::Make(const ChatMessage& msg, Platform source, bool status) {
    ShownMessage m;
    m.platform = source;
    if (status) {
        m.user = U"Trucker Chat";
        m.color = kStatusColor; m.hasColor = true;
        m.text = te_->Resolve(SanitizeText(DecodeUtf8(std::string(PlatformName(source)) + ": " + msg.text), san_));
        return m;
    }
    m.user = te_->Resolve(SanitizeName(DecodeUtf8(msg.user)));
    if (m.user.empty()) m.user = te_->Resolve(SanitizeName(DecodeUtf8(msg.login.empty() ? std::string("?") : msg.login)));
    m.text = te_->Resolve(SanitizeText(DecodeUtf8(msg.text), san_));
    m.color = msg.color; m.hasColor = msg.hasColor;
    m.action = msg.action; m.highlight = msg.highlight;
    return m;
}

std::vector<ShownMessage> ChatDisplay::Newest(size_t n) const {
    std::vector<ShownMessage> v;
    size_t start = history_.size() > n ? history_.size() - n : 0;
    for (size_t i = start; i < history_.size(); ++i) v.push_back(history_[i].m);
    return v;
}

void ChatDisplay::ShowStatus(const std::string& text) {
    if (!screenOn_ || queue_.size() > 50) return;   // status lines are only shown on the truck screen
    ChatMessage m; m.text = text;
    Entry e; e.status = true; e.source = Platform::System;
    e.m = Make(m, Platform::System, true);
    e.m.text = te_->Resolve(SanitizeText(DecodeUtf8(text), san_));
    queue_.push_front(e);   // status lines go first
}

void ChatDisplay::OnEvent(const ChatEvent& ev) {
    ULONGLONG now = GetTickCount64();
    switch (ev.kind) {
    case ChatEvent::Kind::Status: {
        if (!cfg_.display.showStatus || hidden_ || !screenOn_ || queue_.size() > 50) return;
        Entry e; e.status = true; e.source = ev.platform;
        e.m = Make(ev.msg, ev.platform, true);
        queue_.push_front(e);
        return;
    }
    case ChatEvent::Kind::DeleteMessage:
        Remove([&](const Entry& e) { return !e.status && e.source == ev.platform && !e.id.empty() && e.id == ev.key; });
        return;
    case ChatEvent::Kind::DeleteUser:
        Remove([&](const Entry& e) { return !e.status && e.source == ev.platform && (e.userKey == ev.key || e.login == ev.key); });
        return;
    case ChatEvent::Kind::ClearAll:
        Remove([&](const Entry& e) { return !e.status && e.source == ev.platform; });
        return;
    case ChatEvent::Kind::Message: break;
    }
    ChatMessage msg = ev.msg;
    std::string why;
    if (!filter_.Apply(msg, &why)) {
        if (cfg_.logMessages) Log::Write("Chat [%s] %s: (hidden: %s)", PlatformName(msg.platform), msg.user.c_str(), why.c_str());
        return;
    }
    Entry e;
    e.source = msg.platform;
    e.m = Make(msg, msg.platform, false);
    if (e.m.text.empty()) return;
    e.id = msg.id; e.login = msg.login;
    e.userKey = msg.userId.empty() ? msg.login : msg.userId;
    if (cfg_.logMessages) Log::Write("Chat [%s] %s: %s", PlatformName(msg.platform), msg.user.c_str(), EncodeUtf8(e.m.text).c_str());
    history_.push_back(e);
    while (history_.size() > 50) history_.pop_front();
    if (hidden_) return;
    if (listMode_ || pinned_) {
        if (!listDirty_) listDirtyAt_ = std::max<ULONGLONG>(now, lastStart_ + (ULONGLONG)(cfg_.display.minIntervalSeconds * 1000));
        listDirty_ = true;
    } else {
        queue_.push_back(e);
        size_t nonStatus = 0;
        for (auto& q : queue_) if (!q.status) ++nonStatus;
        while (nonStatus > (size_t)cfg_.display.maxQueue) {   // busy chat: drop the oldest waiting message
            auto it = std::find_if(queue_.begin(), queue_.end(), [](const Entry& q) { return !q.status; });
            if (it == queue_.end()) break;
            queue_.erase(it); --nonStatus;
        }
    }
    if (overlayOn_ && !paused_) {
        ovDirty_ = true;
        if (now >= ovUntil_) ovShownAt_ = now;
        ovUntil_ = now + (ULONGLONG)(cfg_.overlay.durationSeconds * 1000);
    }
}

void ChatDisplay::Remove(const std::function<bool(const Entry&)>& pred) {
    size_t before = history_.size();
    history_.erase(std::remove_if(history_.begin(), history_.end(), pred), history_.end());
    queue_.erase(std::remove_if(queue_.begin(), queue_.end(), pred), queue_.end());
    ULONGLONG now = GetTickCount64();
    if (show_ == Show::Card && pred(current_) && until_ > now + 400) until_ = now + 400;   // fade out a deleted message
    if (history_.size() != before) {
        Log::Write("Moderation: removed %d message(s)", (int)(before - history_.size()));
        if (show_ == Show::List || pinned_) { listDirty_ = true; listDirtyAt_ = now; }
        ovDirty_ = true;
    }
}

void ChatDisplay::OnAction(Action a) {
    ULONGLONG now = GetTickCount64();
    switch (a) {
    case Action::Toggle:
        SetEnabled(hidden_);
        break;
    case Action::Pause:
        paused_ = !paused_;
        Log::Write("Chat display %s", paused_ ? "paused" : "resumed");
        ShowStatus(paused_ ? "Chat pausiert" : "Chat fortgesetzt");
        break;
    case Action::Replay:
        if (history_.empty()) { ShowStatus("Noch keine Nachrichten"); break; }
        if (screenOn_) ShowList(now, std::max(cfg_.display.durationSeconds, 8.0));
        if (overlayOn_) { ovDirty_ = true; ovShownAt_ = now; ovUntil_ = now + (ULONGLONG)(cfg_.overlay.durationSeconds * 1000); }
        break;
    default: break;
    }
}

void ChatDisplay::SetEnabled(bool on) {
    if (on == !hidden_) return;
    hidden_ = !on;
    Log::Write("Chat display %s", hidden_ ? "off" : "on");
    if (hidden_) { queue_.clear(); show_ = Show::None; ovUntil_ = 0; listDirty_ = false; }
    else ShowStatus("Chat-Anzeige an");
}

void ChatDisplay::SetDuration(double seconds) {
    cfg_.display.durationSeconds = seconds;
    cfg_.overlay.durationSeconds = seconds;
}

void ChatDisplay::SetPinned(bool on) {
    if (on == pinned_) return;
    pinned_ = on;
    ULONGLONG now = GetTickCount64();
    Log::Write("Always visible %s", on ? "on" : "off");
    if (on) {
        queue_.erase(std::remove_if(queue_.begin(), queue_.end(), [](const Entry& e) { return !e.status; }), queue_.end());
        listDirty_ = true; listDirtyAt_ = now;
        ovDirty_ = true; ovShownAt_ = now;
    } else {
        if (show_ == Show::List && until_ > now + 400) until_ = now + 400;   // fade out, navigation comes back
        if (ovUntil_ > now + 400) ovUntil_ = now + 400;
        listDirty_ = false;
    }
}

void ChatDisplay::StartCard(const Entry& e, ULONGLONG now) {
    current_ = e;
    scroll_ = 0; totalLines_ = visibleLines_ = 0;
    D3D11Overlay::SetScreenImage(RenderCard(*te_, e.m, AreaW(), AreaH(), card_, 0, &totalLines_, &visibleLines_));
    D3D11Overlay::SetScreenTarget(cfg_.screen.width, cfg_.screen.height, cfg_.screen.format);
    double secs = cfg_.display.durationSeconds;
    if (e.status) secs = 3.0;
    else {
        size_t waiting = 0;
        for (auto& q : queue_) if (!q.status) ++waiting;
        if (waiting >= 2) secs = std::max({3.0, cfg_.display.minIntervalSeconds, secs / (1.0 + waiting / 3.0)});   // busy: shorter
        if (card_.scroll && totalLines_ > visibleLines_) {
            secs = std::max(secs, 1.5 + (totalLines_ - visibleLines_) * cfg_.display.scrollSeconds + 2.5);
            nextScrollAt_ = now + 1500;
        }
    }
    shownAt_ = now; lastStart_ = now;
    until_ = now + (ULONGLONG)(secs * 1000);
    show_ = Show::Card;
}

void ChatDisplay::ShowList(ULONGLONG now, double seconds) {
    auto msgs = Newest((size_t)cfg_.display.listSize);
    if (msgs.empty() && pinned_) {   // nothing received yet: show that the chat is ready
        ShownMessage w; w.platform = Platform::System; w.user = U"Trucker Chat";
        w.text = te_->Resolve(U"Warte auf Chat-Nachrichten\u2026"); w.color = kStatusColor; w.hasColor = true;
        msgs.push_back(w);
    }
    if (msgs.empty()) return;
    int W = AreaW(), H = AreaH();
    float u = std::min(W / 16.f, H / 9.f) / 40.f * listOpt_.textScale;
    D3D11Overlay::SetScreenImage(RenderList(*te_, msgs, W, H, listOpt_, u));
    D3D11Overlay::SetScreenTarget(cfg_.screen.width, cfg_.screen.height, cfg_.screen.format);
    if (show_ != Show::List || now >= until_) shownAt_ = now;
    show_ = Show::List;
    lastStart_ = now;
    until_ = now + (ULONGLONG)(seconds * 1000);
    listDirty_ = false;
}

void ChatDisplay::TickSetup(ULONGLONG now) {
    const ScreenConfig& s = cfg_.screen;
    if (collecting_ && now >= nextStatAt_) {   // merge and log the render targets the game used
        nextStatAt_ = now + 15000;
        for (auto& st : D3D11Overlay::TakeTargetStats()) {
            auto it = std::find_if(cands_.begin(), cands_.end(), [&](const Cand& c) { return c.w == st.w && c.h == st.h && c.fmt == st.format; });
            if (it == cands_.end()) cands_.push_back({st.w, st.h, st.format, st.binds}); else it->binds += st.binds;
        }
        std::string list;
        for (auto& c : cands_) { char b[64]; snprintf(b, sizeof(b), " %dx%d(fmt %d)", c.w, c.h, c.fmt); list += b; }
        Log::Write("Screen: %lld render-target changes so far; targets seen:%s", D3D11Overlay::RenderTargetCalls(), list.empty() ? " none yet" : list.c_str());
    }
    if (s.grid && s.width > 0 && s.height > 0) {   // grid on the whole configured target
        if (probeIdx_ != -2) {
            probeIdx_ = -2;
            D3D11Overlay::SetScreenArea(0, 0, 1, 1, s.flipY, s.flipX);
            D3D11Overlay::SetScreenImage(RenderGrid(*te_, s.width, s.height, s.flipX, s.flipY));
            D3D11Overlay::SetScreenTarget(s.width, s.height, s.format);
        }
        D3D11Overlay::SetScreenAlpha(1.f);
        return;
    }
    if (s.probe) {
        if (now < nextProbeAt_ || cands_.empty()) return;
        nextProbeAt_ = now + (ULONGLONG)(s.probeSeconds * 1000);
        probeIdx_ = (probeIdx_ + 1) % (int)cands_.size();
        const Cand& c = cands_[probeIdx_];
        char label[96];
        snprintf(label, sizeof(label), "%d/%d  %dx%d  f%d", probeIdx_ + 1, (int)cands_.size(), c.w, c.h, c.fmt);
        D3D11Overlay::SetScreenArea(0, 0, 1, 1, false, false);
        D3D11Overlay::SetScreenImage(RenderProbe(*te_, DecodeUtf8(label), std::min(c.w, 1024), std::min(c.h, 1024)));
        D3D11Overlay::SetScreenTarget(c.w, c.h, c.fmt);
        D3D11Overlay::SetScreenAlpha(1.f);
        Log::Write("Screen probe %d/%d: marking render target %dx%d (format %d) - is it visible on a truck screen?",
                   probeIdx_ + 1, (int)cands_.size(), c.w, c.h, c.fmt);
    }
}

void ChatDisplay::TickScreen(ULONGLONG now, const GameState& gs) {
    const ScreenConfig& s = cfg_.screen;
    ULONGLONG dt = now - lastTick_;
    if (s.probe || s.grid || collecting_) { TickSetup(now); if (s.probe || s.grid || collecting_) return; }
    if (hidden_ || !gs.powered) {
        if (show_ != Show::None || !queue_.empty()) { show_ = Show::None; queue_.clear(); listDirty_ = false; }
        D3D11Overlay::SetScreenAlpha(0.f);
        return;
    }
    bool yieldToRadio = s.radioCoexist == "yield" && gs.radioOnScreen;
    if (gs.paused || yieldToRadio) {   // keep the remaining display time for later
        if (show_ != Show::None) { until_ += dt; shownAt_ = std::min<ULONGLONG>(now, shownAt_ + dt); nextScrollAt_ += dt; }
        D3D11Overlay::SetScreenAlpha(0.f);
        return;
    }
    if (show_ != Show::None && now >= until_) show_ = Show::None;
    if (!pinned_ && !paused_ && listMode_ && listDirty_ && now >= listDirtyAt_ && (show_ != Show::Card || !current_.status)) ShowList(now, cfg_.display.durationSeconds);
    if ((show_ == Show::None || (pinned_ && show_ == Show::List)) && !queue_.empty()) {   // status lines interrupt the pinned list briefly
        auto it = queue_.begin();
        if (paused_) it = std::find_if(queue_.begin(), queue_.end(), [](const Entry& e) { return e.status; });
        bool gap = it != queue_.end() && (it->status || now >= lastStart_ + (ULONGLONG)(cfg_.display.minIntervalSeconds * 1000));
        if (gap) { Entry e = *it; queue_.erase(it); StartCard(e, now); }
    }
    if (pinned_ && show_ != Show::Card && (show_ == Show::None || (!paused_ && listDirty_ && now >= listDirtyAt_))) {
        bool wasVisible = show_ == Show::List;
        ShowList(now, 24.0 * 3600.0);
        if (wasVisible) shownAt_ = now - 1000;   // content update: no fade
    }
    if (show_ == Show::Card && card_.scroll && totalLines_ > visibleLines_ && scroll_ < totalLines_ - visibleLines_ && now >= nextScrollAt_) {
        ++scroll_;
        nextScrollAt_ = now + (ULONGLONG)(cfg_.display.scrollSeconds * 1000);
        D3D11Overlay::SetScreenImage(RenderCard(*te_, current_.m, AreaW(), AreaH(), card_, scroll_));
    }
    float a = 0.f;
    if (show_ != Show::None && now < until_) a = std::min({1.f, (now - shownAt_) / 200.f, (until_ - now) / 400.f});
    D3D11Overlay::SetScreenAlpha(a);
}

// ---------------------------------------------------------------------------------------------
// Overlay at the top of the game picture (default off)
void ChatDisplay::RenderOverlay() {
    int h = D3D11Overlay::ScreenHeight();
    if (!h && window_) { if (!game_ || !IsWindow(game_)) game_ = FindGameWindow(); RECT r{}; if (game_ && GetClientRect(game_, &r)) h = r.bottom - r.top; }
    if (h < 200) h = 1080;
    ovRenderedH_ = h;
    auto msgs = Newest((size_t)cfg_.display.listSize);
    if (msgs.empty()) { ovImage_.reset(); D3D11Overlay::SetImage(nullptr); return; }
    ovImage_ = RenderOverlayPanel(*te_, msgs, h, cfg_.overlay.scale, cfg_.overlay.width, overlayOpt_);
    D3D11Overlay::SetImage(ovImage_);
    lastImg_ = nullptr;
}

void ChatDisplay::TickOverlay(ULONGLONG now, const GameState& gs) {
    if (cfg_.overlay.mode == "auto" && d3d_) {
        bool noD3D11 = D3D11Overlay::D3D11Frames() == 0 && (D3D11Overlay::OtherFrames() > 30 || now - start_ > 30000);
        if (noD3D11 && !window_) { Log::Write("Overlay: game does not render with D3D11 here, using window overlay"); UseWindow(true); }
        if (!noD3D11 && window_ && D3D11Overlay::D3D11Frames() > 0) UseWindow(false);
    }
    bool allowed = !hidden_ && gs.powered && !gs.paused;
    if (allowed && !paused_ && ovDirty_) { RenderOverlay(); ovDirty_ = false; }
    int h = D3D11Overlay::ScreenHeight();
    if (ovImage_ && h >= 200 && std::abs(h - ovRenderedH_) > 2) RenderOverlay();   // resolution changed -> crisp text again
    float a = 0.f;
    if (allowed && ovImage_ && pinned_) a = std::min(1.f, (now - ovShownAt_) / 250.f);
    else if (allowed && ovImage_ && now < ovUntil_) a = std::min({1.f, (now - ovShownAt_) / 250.f, (ovUntil_ - now) / 400.f});
    D3D11Overlay::SetAlpha(d3d_ && !window_ ? a : 0.f);
    if (window_) UpdateWindow(a);
}

HWND ChatDisplay::FindGameWindow() {
    struct Ctx { DWORD pid; HWND best; long area; } c{GetCurrentProcessId(), nullptr, 0};
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* c = (Ctx*)lp; DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
        if (pid != c->pid || !IsWindowVisible(h)) return TRUE;
        wchar_t cls[64]; GetClassNameW(h, cls, 64);
        if (wcscmp(cls, Names::kOverlayWindowClass) == 0 || wcscmp(cls, Names::kMenuWindowClass) == 0 ||
            wcscmp(cls, L"RealisticRadioOverlay") == 0) return TRUE;   // overlays, not the game
        RECT r; GetClientRect(h, &r); long a = (r.right - r.left) * (r.bottom - r.top);
        if (a > c->area) { c->area = a; c->best = h; }
        return TRUE;
    }, (LPARAM)&c);
    return c.best;
}

void ChatDisplay::UseWindow(bool on) {
    if (on == window_) return;
    window_ = on;
    if (on) {
        WNDCLASSEXW wc{sizeof(wc)}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = SelfModule(); wc.lpszClassName = Names::kOverlayWindowClass;
        RegisterClassExW(&wc);
        wnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                               wc.lpszClassName, L"Trucker Chat", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, wc.hInstance, nullptr);
        Log::Write(wnd_ ? "Overlay: window mode active" : "Overlay: window creation failed");
        ovDirty_ = true;
    } else if (wnd_) { DestroyWindow(wnd_); wnd_ = nullptr; }
}

void ChatDisplay::UpdateWindow(float alpha) {
    if (!wnd_) return;
    if (!game_ || !IsWindow(game_)) game_ = FindGameWindow();
    HWND fg = GetForegroundWindow();
    bool gameActive = game_ && (fg == game_ || fg == wnd_);
    if (alpha <= 0.003f || !ovImage_ || !gameActive || IsIconic(game_)) { if (IsWindowVisible(wnd_)) ShowWindow(wnd_, SW_HIDE); lastAlpha_ = -1; return; }
    RECT cr; GetClientRect(game_, &cr);
    POINT tl{0, 0}; ClientToScreen(game_, &tl);
    float sh = (float)(cr.bottom - cr.top), sw = (float)(cr.right - cr.left);
    if (std::abs(sh - ovImage_->renderedForScreenHeight) > 2 && sh >= 200) { RenderOverlay(); if (!ovImage_) return; }
    float w = (float)ovImage_->width, h = (float)ovImage_->height, ox = (float)cfg_.overlay.offsetX * sh, oy = (float)cfg_.overlay.offsetY * sh;
    int a = anchor_;
    int x = tl.x + (int)((a % 3 == 0) ? ox : (a % 3 == 1) ? (sw - w) / 2 + ox : sw - w - ox);
    int y = tl.y + (int)((a < 3) ? oy : sh - h - oy);
    if (std::abs(alpha - lastAlpha_) < 0.01f && lastImg_ == ovImage_.get() && lastPos_.x == x && lastPos_.y == y && IsWindowVisible(wnd_)) return;
    lastAlpha_ = alpha; lastImg_ = ovImage_.get(); lastPos_ = POINT{x, y};
    HDC screen = GetDC(nullptr), mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = ovImage_->width; bi.bmiHeader.biHeight = -ovImage_->height;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bmp) {
        memcpy(bits, ovImage_->pixels.data(), ovImage_->pixels.size() * 4);
        HGDIOBJ old = SelectObject(mem, bmp);
        POINT pos{x, y}, src{0, 0}; SIZE size{ovImage_->width, ovImage_->height};
        BLENDFUNCTION bf{AC_SRC_OVER, 0, (BYTE)std::clamp((int)(alpha * 255), 0, 255), AC_SRC_ALPHA};
        UpdateLayeredWindow(wnd_, screen, &pos, &size, mem, &src, 0, &bf, ULW_ALPHA);
        SelectObject(mem, old); DeleteObject(bmp);
        if (!IsWindowVisible(wnd_)) ShowWindow(wnd_, SW_SHOWNOACTIVATE);
        SetWindowPos(wnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    DeleteDC(mem); ReleaseDC(nullptr, screen);
}

void ChatDisplay::Tick(const GameState& gs) {
    ULONGLONG now = GetTickCount64();
    if (screenOn_) TickScreen(now, gs);
    if (overlayOn_) TickOverlay(now, gs);
    lastTick_ = now;   // window messages of this thread are pumped by the chat thread (Plugin.cpp)
}
