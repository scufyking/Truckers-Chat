// Trucker Chat
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "TextUtil.h"
#include <algorithm>

std::u32string DecodeUtf8(const std::string& s) {
    std::u32string out;
    out.reserve(s.size());
    const unsigned char* p = (const unsigned char*)s.data();
    size_t n = s.size(), i = 0;
    while (i < n) {
        unsigned c = p[i];
        if (c < 0x80) { out += (char32_t)c; ++i; continue; }
        int len = 0; char32_t cp = 0, min = 0;
        if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; min = 0x80; }
        else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; min = 0x800; }
        else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; min = 0x10000; }
        else { out += U'�'; ++i; continue; }
        if (i + len > n) { out += U'�'; ++i; continue; }
        bool ok = true;
        for (int k = 1; k < len; ++k) {
            unsigned cc = p[i + k];
            if ((cc & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (!ok || cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) { out += U'�'; ++i; continue; }
        out += cp; i += len;
    }
    return out;
}

std::string EncodeUtf8(const std::u32string& s) {
    std::string o;
    o.reserve(s.size());
    for (char32_t c : s) {
        if (c >= 0xD800 && c <= 0xDFFF) c = 0xFFFD;
        if (c < 0x80) o += (char)c;
        else if (c < 0x800) { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { o += (char)(0xE0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
        else if (c <= 0x10FFFF) { o += (char)(0xF0 | (c >> 18)); o += (char)(0x80 | ((c >> 12) & 0x3F)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
    }
    return o;
}

std::u16string ToUtf16(const std::u32string& s) {
    std::u16string o;
    o.reserve(s.size());
    for (char32_t c : s) {
        if (c >= 0x10000 && c <= 0x10FFFF) { c -= 0x10000; o += (char16_t)(0xD800 + (c >> 10)); o += (char16_t)(0xDC00 + (c & 0x3FF)); }
        else if (c >= 0xD800 && c <= 0xDFFF) o += (char16_t)0xFFFD;
        else if (c < 0x10000) o += (char16_t)c;
    }
    return o;
}

bool IsEmoji(char32_t c) {
    return (c >= 0x1F000 && c <= 0x1FAFF) || (c >= 0x2600 && c <= 0x27BF) || (c >= 0x2B05 && c <= 0x2B55) ||
           (c >= 0x231A && c <= 0x231B) || (c >= 0x23E9 && c <= 0x23FA) || c == 0x24C2 || c == 0x3030 || c == 0x303D ||
           c == 0x3297 || c == 0x3299 || c == 0x2122 || c == 0x2139 || (c >= 0x2194 && c <= 0x21AA && c != 0x21A0);
}

bool IsCombiningMark(char32_t c) {
    return (c >= 0x0300 && c <= 0x036F) || (c >= 0x0483 && c <= 0x0489) || (c >= 0x0591 && c <= 0x05BD) ||
           (c >= 0x0610 && c <= 0x061A) || (c >= 0x064B && c <= 0x065F) || (c >= 0x1AB0 && c <= 0x1AFF) ||
           (c >= 0x1DC0 && c <= 0x1DFF) || (c >= 0x20D0 && c <= 0x20FF) || (c >= 0xFE20 && c <= 0xFE2F);
}

bool IsWideBreakable(char32_t c) {
    return (c >= 0x2E80 && c <= 0x9FFF) || (c >= 0xAC00 && c <= 0xD7AF) || (c >= 0xF900 && c <= 0xFAFF) ||
           (c >= 0xFF00 && c <= 0xFFEF) || (c >= 0x20000 && c <= 0x3FFFF) || (c >= 0x0E00 && c <= 0x0E7F) || IsEmoji(c);
}

char32_t SimpleLower(char32_t c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    if (c < 0xC0) return c;
    if (c <= 0xDE && c != 0xD7) return c + 32;
    if (c >= 0x100 && c <= 0x17F) {
        if (c == 0x130) return 'i';
        if (c == 0x178) return 0xFF;
        if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) return (c & 1) ? c + 1 : c;
        if (c == 0x138 || c == 0x149 || c == 0x17F) return c;
        return (c & 1) ? c : c + 1;
    }
    if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2) return c + 0x20;
    if (c >= 0x410 && c <= 0x42F) return c + 0x20;
    if (c >= 0x400 && c <= 0x40F) return c + 0x50;
    return c;
}

std::u32string Lower(const std::u32string& s) {
    std::u32string o(s);
    for (auto& c : o) c = SimpleLower(c);
    return o;
}

std::string LowerAscii(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    return s;
}

namespace {
bool IsSpace(char32_t c) {
    return c == 0x20 || c == 0x09 || c == 0x0A || c == 0x0D || c == 0x0B || c == 0x0C || c == 0xA0 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
}
// characters that are dropped entirely: controls, invisible format characters, bidi overrides (spoofing),
// emoji modifiers/selectors/tags (the GDI renderer draws emoji one by one, so sequences would fall apart)
bool IsDropped(char32_t c) {
    return c < 0x20 || (c >= 0x7F && c <= 0x9F) || c == 0xAD || (c >= 0x200B && c <= 0x200F) || (c >= 0x202A && c <= 0x202E) ||
           (c >= 0x2060 && c <= 0x206F) || c == 0xFEFF || (c >= 0xFFF0 && c <= 0xFFFB) || (c >= 0xFE00 && c <= 0xFE0F) ||
           (c >= 0x1F3FB && c <= 0x1F3FF) || (c >= 0xE0000 && c <= 0xE01EF) || c == 0x20E3 || (c >= 0xE000 && c <= 0xF8FF) ||
           (c >= 0xF0000) || (c >= 0xFDD0 && c <= 0xFDEF) || (c & 0xFFFE) == 0xFFFE || c == 0x34F || c == 0x115F || c == 0x1160 ||
           c == 0x3164 || c == 0xFFA0;   // Hangul fillers are invisible and used for "blank" names
}
std::u32string Clean(const std::u32string& in, const SanitizeOptions* opt, int maxComb) {
    std::u32string o;
    o.reserve(in.size());
    int comb = 0;
    bool lastSpace = true;   // trims leading space
    for (char32_t c : in) {
        if (IsSpace(c)) { if (!lastSpace) { o += U' '; lastSpace = true; } comb = 0; continue; }
        if (IsDropped(c)) continue;
        if (IsCombiningMark(c)) {
            if (o.empty() || lastSpace || ++comb > maxComb) continue;   // no "Zalgo" towers, no marks on nothing
            o += c; continue;
        }
        comb = 0;
        if (opt && IsEmoji(c) && c != 0x2122 && c != 0x2139) {   // ™ and ℹ are ordinary symbols in most fonts
            if (opt->emoji == "remove") continue;
            if (opt->emoji == "replace") { o += opt->emojiReplacement; lastSpace = false; continue; }
        }
        o += c; lastSpace = false;
    }
    while (!o.empty() && o.back() == U' ') o.pop_back();
    return o;
}
std::u32string Cut(std::u32string s, size_t max) {
    if (max == 0 || s.size() <= max) return s;
    s.resize(max > 1 ? max - 1 : 0);
    while (!s.empty() && (s.back() == U' ' || IsCombiningMark(s.back()))) s.pop_back();
    s += U'…';
    return s;
}
}

std::u32string SanitizeText(const std::u32string& in, const SanitizeOptions& opt) {
    std::u32string o = Clean(in, &opt, opt.maxCombiningPerChar);
    // a run of emoji replacement characters reads like noise: "□□□□□□" -> "□□□"
    if (opt.emoji == "replace" && !opt.emojiReplacement.empty()) {
        std::u32string r; size_t run = 0; const auto& rep = opt.emojiReplacement;
        for (size_t i = 0; i < o.size();) {
            if (o.compare(i, rep.size(), rep) == 0) { if (++run <= 3) r += rep; i += rep.size(); continue; }
            run = 0; r += o[i++];
        }
        o.swap(r);
    }
    return Cut(o, opt.maxLength);
}

std::u32string SanitizeName(const std::u32string& in, size_t maxLength) {
    return Cut(Clean(in, nullptr, 1), maxLength);
}
