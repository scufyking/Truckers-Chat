// Trucker Chat
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "GlyphGdi.h"
#include "Logger.h"
#include "TextUtil.h"
#include <algorithm>

namespace {
uint16_t U16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
uint32_t U32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
std::wstring W(const std::u32string& s) { std::u16string u = ToUtf16(s); return std::wstring(u.begin(), u.end()); }
}

GdiGlyphs::~GdiGlyphs() {
    for (auto& f : fonts_) DeleteObject(f.second);
    if (dc_) DeleteDC(dc_);
    for (auto& f : loadedFiles_) RemoveFontResourceExW(f.c_str(), FR_PRIVATE, nullptr);
}

void GdiGlyphs::Init(const std::wstring& fontDir, const std::vector<std::string>& fallbacks) {
    dc_ = CreateCompatibleDC(nullptr);
    for (const wchar_t* file : {L"Inter-Regular.otf", L"Inter-Bold.otf"}) {
        std::wstring p = fontDir + file;
        if (AddFontResourceExW(p.c_str(), FR_PRIVATE, nullptr) > 0) loadedFiles_.push_back(p);
        else Log::Write("Fonts: could not load %s", WideToUtf8(p).c_str());
    }
    std::vector<std::wstring> names{L"Inter"};
    for (auto& f : fallbacks) names.push_back(Utf8ToWide(f));
    std::string list;
    for (auto& n : names) {
        Face face; face.name = n;
        if (ReadCmap(face)) { faces_.push_back(std::move(face)); list += " " + WideToUtf8(n) + ";"; }
    }
    if (faces_.empty()) {   // should never happen; keep the engine usable with whatever GDI substitutes
        Face f; f.name = L"Segoe UI"; f.ranges.push_back({0x20, 0x24F}); faces_.push_back(f);
    }
    Log::Write("Fonts:%s", list.c_str());
}

bool GdiGlyphs::ReadCmap(Face& face) {
    HFONT f = CreateFontW(-32, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                          ANTIALIASED_QUALITY, DEFAULT_PITCH, face.name.c_str());
    if (!f) return false;
    HGDIOBJ old = SelectObject(dc_, f);
    wchar_t got[LF_FACESIZE] = {0};
    GetTextFaceW(dc_, LF_FACESIZE, got);
    bool ok = _wcsicmp(got, face.name.c_str()) == 0;   // GDI silently substitutes missing fonts
    std::vector<uint8_t> t;
    const DWORD tag = 0x70616D63;   // 'cmap' (byte-swapped as GetFontData expects)
    if (ok) {
        DWORD n = GetFontData(dc_, tag, 0, nullptr, 0);
        if (n != GDI_ERROR && n > 4 && n < (16u << 20)) { t.resize(n); if (GetFontData(dc_, tag, 0, t.data(), n) != n) t.clear(); }
    }
    SelectObject(dc_, old);
    DeleteObject(f);
    if (!ok || t.size() < 4) return false;
    // pick the best Unicode subtable: (3,10)/(0,4..6) format 12, else (3,1)/(0,x) format 4
    size_t best = 0; int bestScore = 0;
    uint16_t num = U16(&t[2]);
    for (uint16_t i = 0; i < num && 4 + i * 8 + 8 <= t.size(); ++i) {
        const uint8_t* r = &t[4 + i * 8];
        uint16_t pid = U16(r), eid = U16(r + 2); uint32_t off = U32(r + 4);
        if (off + 4 > t.size()) continue;
        uint16_t fmt = U16(&t[off]);
        int score = 0;
        if (fmt == 12 && ((pid == 3 && eid == 10) || pid == 0)) score = 3;
        else if (fmt == 4 && ((pid == 3 && eid == 1) || pid == 0)) score = 2;
        if (score > bestScore) { bestScore = score; best = off; }
    }
    if (!bestScore) return false;
    const uint8_t* s = &t[best];
    size_t avail = t.size() - best;
    if (U16(s) == 12 && avail >= 16) {
        uint32_t groups = U32(s + 12);
        for (uint32_t g = 0; g < groups && 16 + (size_t)g * 12 + 12 <= avail; ++g) {
            const uint8_t* e = s + 16 + g * 12;
            uint32_t a = U32(e), b = U32(e + 4);
            if (U32(e + 8) != 0 || a != b) face.ranges.push_back({a, b});   // glyph 0 = .notdef
        }
    } else if (U16(s) == 4 && avail >= 14) {
        uint16_t segX2 = U16(s + 6), segs = segX2 / 2;
        if (14 + 4 * (size_t)segX2 + 2 > avail) return false;
        const uint8_t* ends = s + 14; const uint8_t* starts = ends + segX2 + 2;
        const uint8_t* deltas = starts + segX2; const uint8_t* offs = deltas + segX2;
        for (uint16_t k = 0; k < segs; ++k) {
            uint16_t a = U16(starts + 2 * k), b = U16(ends + 2 * k), ro = U16(offs + 2 * k);
            if (a == 0xFFFF) continue;
            if (ro == 0) { face.ranges.push_back({a, b}); continue; }
            for (uint32_t c = a; c <= b; ++c) {   // glyph ids via glyphIdArray: keep only characters with a real glyph
                size_t pos = (size_t)(offs + 2 * k - s) + ro + 2 * (c - a);
                if (pos + 2 <= avail && U16(s + pos) != 0) face.ranges.push_back({c, c});
            }
        }
    } else return false;
    std::sort(face.ranges.begin(), face.ranges.end());
    return !face.ranges.empty();
}

bool GdiGlyphs::HasGlyph(int face, char32_t c) {
    const auto& r = faces_[face].ranges;
    auto it = std::upper_bound(r.begin(), r.end(), std::make_pair((uint32_t)c, 0xFFFFFFFFu));
    return it != r.begin() && (uint32_t)c <= std::prev(it)->second;
}

HFONT GdiGlyphs::Get(int face, const Font& f) {
    long long key = ((long long)face << 32) | ((long long)f.px << 1) | (f.bold ? 1 : 0);
    auto it = fonts_.find(key);
    if (it != fonts_.end()) return it->second;
    if (fonts_.size() > 64) { for (auto& e : fonts_) DeleteObject(e.second); fonts_.clear(); }
    HFONT h = CreateFontW(-f.px, 0, 0, 0, f.bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                          CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, faces_[face].name.c_str());
    fonts_[key] = h;
    return h;
}

void GdiGlyphs::Metrics(int face, const Font& f, int& ascent, int& descent) {
    HGDIOBJ old = SelectObject(dc_, Get(face, f));
    TEXTMETRICW tm{}; GetTextMetricsW(dc_, &tm);
    SelectObject(dc_, old);
    ascent = tm.tmAscent; descent = tm.tmDescent;
}

int GdiGlyphs::Measure(int face, const Font& f, const std::u32string& run) {
    std::wstring w = W(run);
    if (w.empty()) return 0;
    HGDIOBJ old = SelectObject(dc_, Get(face, f));
    SIZE sz{0, 0}; GetTextExtentPoint32W(dc_, w.c_str(), (int)w.size(), &sz);
    SelectObject(dc_, old);
    return sz.cx;
}

void GdiGlyphs::Draw(int face, const Font& f, const std::u32string& run, int x, int y, std::vector<uint8_t>& mask, int mw, int mh) {
    std::wstring w = W(run);
    if (w.empty()) return;
    int asc = 0, desc = 0; Metrics(face, f, asc, desc);
    const int pad = f.px / 2 + 2;
    int bw = Measure(face, f, run) + 2 * pad, bh = asc + desc + 2 * pad;
    BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = bw; bi.bmiHeader.biHeight = -bh;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    uint32_t* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc_, &bi, DIB_RGB_COLORS, (void**)&bits, nullptr, 0);
    if (!bmp) return;
    HGDIOBJ oldBmp = SelectObject(dc_, bmp);
    HGDIOBJ oldFont = SelectObject(dc_, Get(face, f));
    GdiFlush();
    std::fill(bits, bits + (size_t)bw * bh, 0u);
    SetBkMode(dc_, TRANSPARENT); SetTextColor(dc_, RGB(255, 255, 255));
    UINT oldAlign = SetTextAlign(dc_, TA_BASELINE | TA_LEFT | TA_NOUPDATECP);
    TextOutW(dc_, pad, pad + asc, w.c_str(), (int)w.size());
    SetTextAlign(dc_, oldAlign);
    GdiFlush();
    int ox = x - pad, oy = y - asc - pad;
    for (int yy = 0; yy < bh; ++yy) {
        int my = oy + yy;
        if (my < 0 || my >= mh) continue;
        for (int xx = 0; xx < bw; ++xx) {
            int mx = ox + xx;
            if (mx < 0 || mx >= mw) continue;
            uint32_t p = bits[(size_t)yy * bw + xx];
            uint8_t cov = (uint8_t)std::max({(p >> 16) & 255u, (p >> 8) & 255u, p & 255u});
            uint8_t& m = mask[(size_t)my * mw + mx];
            if (cov > m) m = cov;
        }
    }
    SelectObject(dc_, oldFont);
    SelectObject(dc_, oldBmp);
    DeleteObject(bmp);
}
