// Trucker Chat
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "Canvas.h"
#include "TextUtil.h"
#include <algorithm>
#include <cmath>

Rgb RgbFrom(uint32_t c) { return Rgb{((c >> 16) & 255) / 255.f, ((c >> 8) & 255) / 255.f, (c & 255) / 255.f}; }

uint32_t Pack(float a, float r, float g, float b) {
    auto q = [](float v) { return (uint32_t)std::clamp((int)std::lround(v * 255.f), 0, 255); };
    return (q(a) << 24) | (q(r) << 16) | (q(g) << 8) | q(b);
}
void Over(uint32_t& dst, float a, float r, float g, float b) {
    float da = ((dst >> 24) & 255) / 255.f, dr = ((dst >> 16) & 255) / 255.f, dg = ((dst >> 8) & 255) / 255.f, db = (dst & 255) / 255.f;
    float k = 1.f - a;
    dst = Pack(a + da * k, r + dr * k, g + dg * k, b + db * k);
}
void FillRect(Image& img, int x0, int y0, int x1, int y1, Rgb c, float a) {
    x0 = std::max(0, x0); y0 = std::max(0, y0); x1 = std::min(img.width, x1); y1 = std::min(img.height, y1);
    for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) Over(img.pixels[(size_t)y * img.width + x], a, c.r * a, c.g * a, c.b * a);
}
void FillRoundRect(Image& img, int x, int y, int w, int h, float rad, Rgb c, float a) {
    rad = std::min({rad, w / 2.f, h / 2.f});
    for (int yy = std::max(0, y); yy < std::min(img.height, y + h); ++yy)
        for (int xx = std::max(0, x); xx < std::min(img.width, x + w); ++xx) {
            float px = xx - x + 0.5f, py = yy - y + 0.5f;
            float cx = std::clamp(px, rad, w - rad), cy = std::clamp(py, rad, h - rad);
            float d = std::sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy));
            float cov = std::clamp(rad - d + 0.5f, 0.f, 1.f) * a;
            if (cov > 0) Over(img.pixels[(size_t)yy * img.width + xx], cov, c.r * cov, c.g * cov, c.b * cov);
        }
}
std::shared_ptr<Image> NewImage(int w, int h, int screenH) {
    auto img = std::make_shared<Image>();
    img->width = std::max(1, w); img->height = std::max(1, h);
    img->renderedForScreenHeight = screenH > 0 ? screenH : img->height;
    img->pixels.assign((size_t)img->width * img->height, 0u);
    return img;
}

// ---------------------------------------------------------------------------------------------
int TextEngine::FaceFor(char32_t c) {
    auto it = faceCache_.find(c);
    if (it != faceCache_.end()) return it->second;
    int face = -1;
    for (int i = 0; i < b_.FaceCount(); ++i) if (b_.HasGlyph(i, c)) { face = i; break; }
    if (faceCache_.size() > 4096) faceCache_.clear();
    faceCache_[c] = face;
    return face;
}

std::u32string TextEngine::Resolve(const std::u32string& s) {
    std::u32string o;
    o.reserve(s.size());
    for (char32_t c : s) {
        if (c == U' ' || FaceFor(c) >= 0) { o += c; continue; }
        if (IsCombiningMark(c)) continue;   // a mark nobody can draw is simply dropped
        for (char32_t m : missing_) if (FaceFor(m) >= 0) o += m; else o += U'?';
    }
    return o;
}

std::vector<TextEngine::Run> TextEngine::Runs(const std::u32string& s) {
    std::vector<Run> runs;
    for (char32_t c : s) {
        int f = FaceFor(c);
        // spaces and marks stay in the current run (marks must be shaped with their base character)
        if (!runs.empty() && (c == U' ' || IsCombiningMark(c) || f < 0 || f == runs.back().face)) { runs.back().text += c; continue; }
        runs.push_back(Run{f < 0 ? 0 : f, std::u32string(1, c)});
    }
    return runs;
}

int TextEngine::Width(const std::u32string& s, const Font& f) {
    int w = 0;
    for (auto& r : Runs(s)) w += b_.Measure(r.face, f, r.text);
    return w;
}
int TextEngine::LineHeight(const Font& f) { return (int)std::lround(f.px * 1.30); }
int TextEngine::Ascent(const Font& f) {
    int a = 0, d = 0; b_.Metrics(0, f, a, d);
    int lh = LineHeight(f);
    return a + std::max(0, (lh - (a + d)) / 2);
}

std::u32string TextEngine::Ellipsize(const std::u32string& in, const Font& f, int maxW) {
    if (Width(in, f) <= maxW) return in;
    std::u32string s = in, dots = FaceFor(U'…') >= 0 ? U"…" : U"...";
    int dw = Width(dots, f);
    while (!s.empty() && Width(s, f) + dw > maxW) {
        s.pop_back();
        while (!s.empty() && IsCombiningMark(s.back())) s.pop_back();
    }
    while (!s.empty() && s.back() == U' ') s.pop_back();
    return s + dots;
}

TextLayout TextEngine::Wrap(const std::u32string& text, const Font& f, int maxW, int maxLines, int firstIndent) {
    TextLayout L;
    L.lineHeight = LineHeight(f); L.ascent = Ascent(f);
    // tokens: words, or single wide characters (CJK, emoji) which may break anywhere; each with "space before"
    struct Tok { std::u32string t; bool spaceBefore; };
    std::vector<Tok> toks;
    bool space = false;
    for (size_t i = 0; i < text.size(); ++i) {
        char32_t c = text[i];
        if (c == U' ') { space = true; continue; }
        bool wide = IsWideBreakable(c);
        if (IsCombiningMark(c) && !toks.empty()) { toks.back().t += c; continue; }
        if (wide || toks.empty() || space || IsWideBreakable(toks.back().t.back())) toks.push_back(Tok{std::u32string(1, c), space});
        else toks.back().t += c;
        space = false;
    }
    const int spaceW = std::max(1, Width(U" ", f));
    TextLine cur; cur.indent = firstIndent;
    auto lineMax = [&](const TextLine& l) { return maxW - l.indent; };
    auto flush = [&]() { L.lines.push_back(cur); cur = TextLine(); };
    for (auto& tk : toks) {
        int w = Width(tk.t, f);
        int add = (cur.text.empty() ? 0 : (tk.spaceBefore ? spaceW : 0)) + w;
        if (cur.width + add <= lineMax(cur)) {
            if (!cur.text.empty() && tk.spaceBefore) cur.text += U' ';
            cur.text += tk.t; cur.width += add; continue;
        }
        if (!cur.text.empty()) flush();
        if (w <= lineMax(cur)) { cur.text = tk.t; cur.width = w; continue; }
        // a single word longer than the line: break it by characters (keeping marks with their base)
        for (size_t i = 0; i < tk.t.size();) {
            size_t j = i + 1;
            while (j < tk.t.size() && IsCombiningMark(tk.t[j])) ++j;
            std::u32string cl = tk.t.substr(i, j - i);
            int cw = Width(cl, f);
            if (!cur.text.empty() && cur.width + cw > lineMax(cur)) flush();
            cur.text += cl; cur.width += cw; i = j;
        }
    }
    if (!cur.text.empty() || L.lines.empty()) L.lines.push_back(cur);
    if (maxLines > 0 && (int)L.lines.size() > maxLines) {
        L.truncated = true;
        L.lines.resize(maxLines);
        TextLine& last = L.lines.back();
        std::u32string s = last.text, dots = FaceFor(U'…') >= 0 ? U"…" : U"...";
        int dw = Width(dots, f);
        while (!s.empty() && Width(s, f) + dw > maxW - last.indent) {
            s.pop_back();
            while (!s.empty() && IsCombiningMark(s.back())) s.pop_back();
        }
        while (!s.empty() && s.back() == U' ') s.pop_back();
        last.text = s + dots;
        last.width = Width(last.text, f);
    }
    return L;
}

void TextEngine::DrawRuns(std::vector<uint8_t>& mask, int w, int h, const std::u32string& s, const Font& f, int x, int baseline) {
    for (auto& r : Runs(s)) {
        b_.Draw(r.face, f, r.text, x, baseline, mask, w, h);
        x += b_.Measure(r.face, f, r.text);
    }
}

void TextEngine::Blend(Image& img, const std::vector<uint8_t>& mask, Rgb c, float a) {
    for (size_t i = 0; i < mask.size(); ++i) {
        if (!mask[i]) continue;
        float cov = mask[i] / 255.f * a;
        Over(img.pixels[i], cov, c.r * cov, c.g * cov, c.b * cov);
    }
}

void TextEngine::DrawLine(Image& img, const std::u32string& s, const Font& f, int x, int y, Rgb c, float a) {
    if (s.empty()) return;
    std::vector<uint8_t> mask((size_t)img.width * img.height, 0);
    DrawRuns(mask, img.width, img.height, s, f, x, y + Ascent(f));
    Blend(img, mask, c, a);
}

void TextEngine::DrawLayout(Image& img, const TextLayout& l, const Font& f, int x, int y, Rgb c, float a, int first, int maxLines) {
    std::vector<uint8_t> mask((size_t)img.width * img.height, 0);
    int n = 0;
    for (size_t i = (size_t)std::max(0, first); i < l.lines.size(); ++i) {
        if (maxLines > 0 && n >= maxLines) break;
        DrawRuns(mask, img.width, img.height, l.lines[i].text, f, x + l.lines[i].indent, y + n * l.lineHeight + l.ascent);
        ++n;
    }
    Blend(img, mask, c, a);
}
