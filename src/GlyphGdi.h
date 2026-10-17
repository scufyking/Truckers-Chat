// Trucker Chat
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
// GDI glyph backend. Face 0 is the embedded Inter font (loaded privately with AddFontResourceExW(FR_PRIVATE),
// visible only inside this process), followed by the configured Windows fallback fonts. Which face draws a
// character is decided from each font's own 'cmap' table (GetFontData), so nothing depends on GDI font linking;
// characters no font has are replaced (see TextEngine::Resolve) instead of showing empty boxes.
#include "Common.h"
#include "Canvas.h"

class GdiGlyphs : public GlyphBackend {
public:
    GdiGlyphs() = default;
    ~GdiGlyphs() override;
    void Init(const std::wstring& fontDir, const std::vector<std::string>& fallbacks);   // display thread only
    int FaceCount() const override { return (int)faces_.size(); }
    bool HasGlyph(int face, char32_t c) override;
    void Metrics(int face, const Font& f, int& ascent, int& descent) override;
    int Measure(int face, const Font& f, const std::u32string& run) override;
    void Draw(int face, const Font& f, const std::u32string& run, int x, int y, std::vector<uint8_t>& mask, int w, int h) override;
private:
    struct Face { std::wstring name; std::vector<std::pair<uint32_t, uint32_t>> ranges; };
    HFONT Get(int face, const Font& f);
    bool ReadCmap(Face& face);
    HDC dc_ = nullptr;
    std::vector<Face> faces_;
    std::map<long long, HFONT> fonts_;
    std::vector<std::wstring> loadedFiles_;
};
