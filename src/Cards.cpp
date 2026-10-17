// Trucker Chat
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "Cards.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
const Rgb kText{0.95f, 0.95f, 0.96f}, kMuted{0.62f, 0.62f, 0.66f}, kGold{1.f, 0.76f, 0.16f};

float Luma(Rgb c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }
Rgb Lighten(Rgb c, float minLuma) {   // keep dark name colours readable on the dark background
    for (int i = 0; i < 10 && Luma(c) < minLuma; ++i) { c.r += (1 - c.r) * 0.25f; c.g += (1 - c.g) * 0.25f; c.b += (1 - c.b) * 0.25f; }
    return c;
}
void Background(Image& img, float alpha) {
    if (alpha <= 0) return;
    for (int y = 0; y < img.height; ++y) {
        float c = 0.065f + 0.035f * (1.f - (float)y / img.height);
        uint32_t v = Pack(alpha, c * alpha, c * alpha, (c + 0.012f) * alpha);
        std::fill(img.pixels.begin() + (size_t)y * img.width, img.pixels.begin() + (size_t)(y + 1) * img.width, v);
    }
}
const std::u32string& Label(const CardOptions& o, Platform p) {
    return p == Platform::Twitch ? o.labelTwitch : p == Platform::YouTube ? o.labelYouTube : o.labelSystem;
}
// rounded label with white bold text; returns its width
int Badge(TextEngine& te, Image& img, const std::u32string& text, int x, int y, int h, Rgb fill, Rgb ink, int fontPx) {
    Font f{fontPx, true};
    int pad = std::max(2, h / 3), w = te.Width(text, f) + 2 * pad;
    FillRoundRect(img, x, y, w, h, h * 0.28f, fill, 1.f);
    te.DrawLine(img, text, f, x + pad, y + (h - te.LineHeight(f)) / 2, ink);
    return w;
}
std::u32string Initial(Platform p) { return p == Platform::Twitch ? U"T" : p == Platform::YouTube ? U"Y" : U"i"; }
}

uint32_t PlatformColor(Platform p) {
    switch (p) {
    case Platform::Twitch: return 0x9146FF;
    case Platform::YouTube: return 0xFF0033;
    default: return 0xFFAE00;   // same accent orange as Realistic Radio
    }
}

Rgb NameColor(const ShownMessage& m, bool userColors) {
    if (m.platform == Platform::System) return RgbFrom(0xFFAE00);
    if (userColors && m.hasColor) return Lighten(RgbFrom(m.color), 0.45f);
    // stable colour per name (Twitch-like palette) when the platform gives none
    static const uint32_t pal[] = {0xFF6B6B, 0x4FC3F7, 0x81C784, 0xFFB74D, 0xBA68C8, 0x4DD0E1, 0xF06292, 0xAED581, 0xFFD54F, 0x9FA8DA};
    uint32_t h = 2166136261u;
    for (char32_t c : m.user) { h ^= (uint32_t)c; h *= 16777619u; }
    return RgbFrom(pal[h % 10]);
}

std::shared_ptr<Image> RenderCard(TextEngine& te, const ShownMessage& m, int W, int H, const CardOptions& o,
                                  int scrollLine, int* totalLines, int* visibleLines) {
    auto img = NewImage(W, H);
    Background(*img, o.background);
    const float u = std::min(W / 16.f, H / 9.f) / 40.f * o.textScale;
    auto U = [&](float v) { return std::max(1, (int)std::lround(v * u)); };
    Rgb accent = RgbFrom(PlatformColor(m.platform));
    FillRect(*img, 0, 0, U(10), H, accent, 1.f);
    const int pad = U(26), x0 = U(10) + pad, right = W - pad;
    // header: platform badge, (super chat badge), name
    int hy = pad, bh = U(36), x = x0;
    if (o.showPlatform) x += Badge(te, *img, Label(o, m.platform), x, hy, bh, accent, Rgb{1, 1, 1}, U(19)) + U(14);
    if (m.highlight) x += Badge(te, *img, o.labelSuperChat, x, hy, bh, kGold, Rgb{0.1f, 0.08f, 0.f}, U(19)) + U(14);
    Font fName{U(32), true};
    Rgb nameCol = NameColor(m, o.userColors);
    te.DrawLine(*img, te.Ellipsize(m.user, fName, right - x), fName, x, hy + (bh - te.LineHeight(fName)) / 2, nameCol);
    // body: largest font that fits, otherwise cut or scroll
    const int top = hy + bh + U(20), availH = H - top - pad / 2, availW = right - x0;
    Rgb textCol = m.action ? nameCol : kText;
    Font f{U(42), false};
    TextLayout L;
    int allowed = 1;
    for (int px = U(42);; px -= std::max(1, U(2))) {
        f.px = px;
        allowed = std::max(1, std::min(o.maxLines, availH / std::max(1, te.LineHeight(f))));
        L = te.Wrap(m.text, f, availW, 0);
        if ((int)L.lines.size() <= allowed || px <= U(26)) break;
    }
    int total = (int)L.lines.size();
    if (totalLines) *totalLines = total;
    if (visibleLines) *visibleLines = std::min(total, allowed);
    if (total > allowed && !o.scroll) { L = te.Wrap(m.text, f, availW, allowed); scrollLine = 0; }
    scrollLine = std::clamp(scrollLine, 0, std::max(0, (int)L.lines.size() - allowed));
    te.DrawLayout(*img, L, f, x0, top, textCol, 1.f, scrollLine, allowed);
    if (o.scroll && total > allowed) {   // scroll position indicator on the right edge
        int th = allowed * L.lineHeight, ty = top, tw = U(5), tx = W - U(12);
        FillRoundRect(*img, tx, ty, tw, th, tw / 2.f, kMuted, 0.35f);
        int kh = std::max(U(16), th * allowed / total), ky = ty + (th - kh) * scrollLine / std::max(1, total - allowed);
        FillRoundRect(*img, tx, ky, tw, kh, tw / 2.f, accent, 1.f);
    }
    return img;
}

std::shared_ptr<Image> RenderList(TextEngine& te, const std::vector<ShownMessage>& msgs, int W, int H, const CardOptions& o,
                                  float u, int screenH) {
    auto U = [&](float v) { return std::max(1, (int)std::lround(v * u)); };
    const int pad = U(18), gap = U(12), badgeW = U(32);
    Font fName{U(28), true}, fText{U(28), false};
    const int lh = te.LineHeight(fText), innerW = W - 2 * pad - U(6);
    struct Row { const ShownMessage* m; TextLayout l; int nameX, textIndent, h; };
    std::vector<Row> rows;
    int used = 0;
    for (auto it = msgs.rbegin(); it != msgs.rend(); ++it) {   // newest first, until the height is used up
        Row r; r.m = &*it;
        r.nameX = o.showPlatform ? badgeW + U(10) : 0;
        int nameW = te.Width(it->user, fName);
        r.textIndent = std::min(r.nameX + nameW + U(12), innerW * 2 / 3);
        r.l = te.Wrap(it->text, fText, innerW, std::max(1, o.listLines), r.textIndent);
        r.h = (int)r.l.lines.size() * lh;
        int add = r.h + (rows.empty() ? 0 : gap);
        if (H > 0 && used + add > H - 2 * pad) break;
        used += add;
        rows.push_back(std::move(r));
    }
    if (H <= 0) H = used + 2 * pad;
    auto img = NewImage(W, H, screenH);
    if (screenH > 0) FillRoundRect(*img, 0, 0, W, H, (float)U(12), Rgb{0.07f, 0.07f, 0.085f}, o.background);
    else Background(*img, o.background);
    FillRect(*img, 0, screenH > 0 ? U(10) : 0, U(6), H - (screenH > 0 ? U(10) : 0), RgbFrom(PlatformColor(rows.empty() ? Platform::System : rows.front().m->platform)), 1.f);
    int y = pad, x0 = pad + U(6);
    for (auto it = rows.rbegin(); it != rows.rend(); ++it) {   // oldest at the top
        const ShownMessage& m = *it->m;
        Rgb nameCol = NameColor(m, o.userColors);
        if (o.showPlatform) {
            int bh = std::min(lh, U(32));
            Badge(te, *img, Initial(m.platform), x0, y + (lh - bh) / 2, bh, RgbFrom(PlatformColor(m.platform)), Rgb{1, 1, 1}, U(19));
        }
        te.DrawLine(*img, te.Ellipsize(m.user, fName, innerW * 2 / 3 - it->nameX - U(12)), fName, x0 + it->nameX, y, nameCol);
        te.DrawLayout(*img, it->l, fText, x0, y, m.action ? nameCol : (m.highlight ? kGold : kText));
        y += it->h + gap;
        if (std::next(it) != rows.rend()) FillRect(*img, x0, y - gap / 2, W - pad, y - gap / 2 + std::max(1, U(1)), kMuted, 0.25f);
    }
    return img;
}

std::shared_ptr<Image> RenderOverlayPanel(TextEngine& te, const std::vector<ShownMessage>& msgs, int screenH, double scale,
                                          double widthFraction, const CardOptions& o) {
    float u = (float)std::clamp(screenH / 1080.0 * scale, 0.3, 4.0) * 0.8f * o.textScale;
    int W = std::max(200, (int)std::lround(screenH * widthFraction * scale));
    return RenderList(te, msgs, W, 0, o, u, screenH);
}

std::shared_ptr<Image> RenderGrid(TextEngine& te, int W, int H, bool flipX, bool flipY) {
    auto img = NewImage(W, H);
    FillRect(*img, 0, 0, W, H, Rgb{0.05f, 0.05f, 0.1f}, 0.55f);
    Font f{std::max(9, std::min(W, H) / 40), true};
    int lw = std::max(1, std::min(W, H) / 400);
    for (int i = 0; i <= 10; ++i) {
        int gx = std::min(W - lw, W * i / 10), gy = std::min(H - lw, H * i / 10);
        Rgb c = (i % 5 == 0) ? Rgb{1.f, 0.85f, 0.1f} : Rgb{1, 1, 1};
        FillRect(*img, gx, 0, gx + lw * (i % 5 == 0 ? 2 : 1), H, c, 0.9f);
        FillRect(*img, 0, gy, W, gy + lw * (i % 5 == 0 ? 2 : 1), c, 0.9f);
    }
    for (int r = 0; r < 10; ++r) for (int c = 0; c < 10; ++c) {
        // label = render-target coordinate of the cell's top-left corner, as used by areaX / areaY
        int tx = flipX ? 9 - c : c, ty = flipY ? 9 - r : r;
        char b[16]; snprintf(b, sizeof(b), "%d/%d", tx, ty);
        std::u32string s(b, b + strlen(b));
        te.DrawLine(*img, s, f, W * c / 10 + lw * 3, H * r / 10 + lw * 2, Rgb{0.6f, 1.f, 0.6f});
    }
    return img;
}

std::shared_ptr<Image> RenderProbe(TextEngine& te, const std::u32string& label, int W, int H) {
    auto img = NewImage(W, H);
    const int cs = std::max(8, std::min(W, H) / 8);
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x)   // magenta / dark checkerboard: impossible to miss
        img->pixels[(size_t)y * W + x] = ((x / cs + y / cs) & 1) ? Pack(1.f, 1.f, 0.f, 0.85f) : Pack(1.f, 0.12f, 0.f, 0.15f);
    FillRect(*img, 0, H / 3, W, 2 * H / 3, Rgb{0, 0, 0}, 1.f);
    Font f{std::max(10, H / 9), true};
    int w = te.Width(label, f);
    te.DrawLine(*img, label, f, (W - w) / 2, H / 2 - te.LineHeight(f) / 2, Rgb{1.f, 1.f, 0.2f});
    return img;
}
