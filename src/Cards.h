// Trucker Chat
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
// The pictures drawn into the truck screen and the top overlay (platform independent).
#include "Canvas.h"
#include "ChatTypes.h"

struct ShownMessage {
    Platform platform = Platform::System;
    std::u32string user, text;     // already sanitised
    std::u32string time;           // "15:16" (side chat)
    uint32_t color = 0; bool hasColor = false;
    bool action = false, highlight = false;
};

struct CardOptions {
    int maxLines = 5;              // text lines in the single-message card
    bool scroll = false;           // long text: scroll (true) or cut with "…" (false)
    int listLines = 2;             // lines per message in the list
    bool showPlatform = true;
    bool userColors = true;
    float background = 1.f;        // opacity of the dark background (screen: 1 = opaque like the navigation)
    float textScale = 1.f;
    std::u32string labelTwitch = U"TWITCH", labelYouTube = U"YOUTUBE", labelSystem = U"CHAT", labelSuperChat = U"SUPER CHAT";
};

uint32_t PlatformColor(Platform p);   // 0xRRGGBB
Rgb NameColor(const ShownMessage& m, bool userColors);

// One message, large. scrollLine = first text line shown; *totalLines/*visibleLines report the layout (for scrolling).
std::shared_ptr<Image> RenderCard(TextEngine& te, const ShownMessage& m, int W, int H, const CardOptions& o,
                                  int scrollLine = 0, int* totalLines = nullptr, int* visibleLines = nullptr);
// The newest messages as a list (oldest at the top). H = 0: the height follows the content (overlay panel).
std::shared_ptr<Image> RenderList(TextEngine& te, const std::vector<ShownMessage>& msgs, int W, int H, const CardOptions& o,
                                  float unit, int screenH = 0);
// Overlay panel at the top of the game picture, sized for the given back-buffer height.
std::shared_ptr<Image> RenderOverlayPanel(TextEngine& te, const std::vector<ShownMessage>& msgs, int screenH, double scale,
                                          double widthFraction, const CardOptions& o);
// Side chat like TruckersMP: one dark strip per line, "15:16 [YT] Name: text", oldest at the top.
struct SideOptions {
    double scale = 1.0;
    double widthFraction = 0.62;   // maximum line width in screen heights
    bool showTime = true, showTag = true, userColors = true;
    float background = 0.55f;
    int maxLinesPerMessage = 3;
};
std::shared_ptr<Image> RenderSideChat(TextEngine& te, const std::vector<ShownMessage>& msgs, int screenH, const SideOptions& o);
// Setup helpers (same idea as Realistic Radio): coordinate grid and test pattern.
std::shared_ptr<Image> RenderGrid(TextEngine& te, int W, int H, bool flipX, bool flipY);
std::shared_ptr<Image> RenderProbe(TextEngine& te, const std::u32string& label, int W, int H);
