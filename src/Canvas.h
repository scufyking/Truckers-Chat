// Trucker Chat
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
// Software canvas and text layout (platform independent). Glyphs come from a GlyphBackend:
// GDI on Windows (GlyphGdi.cpp), FreeType in the Linux preview tool (tools/preview).
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <map>

// Premultiplied BGRA, top-down (0xAARRGGBB in memory order B,G,R,A). Used for the D3D11 texture and UpdateLayeredWindow.
struct Image {
    int width = 0, height = 0;
    int renderedForScreenHeight = 1080;
    std::vector<uint32_t> pixels;
};

struct Rgb { float r = 1, g = 1, b = 1; };
Rgb RgbFrom(uint32_t rrggbb);
uint32_t Pack(float a, float r, float g, float b);              // premultiplied 0..1
void Over(uint32_t& dst, float a, float r, float g, float b);   // src premultiplied
void FillRect(Image& img, int x0, int y0, int x1, int y1, Rgb c, float a);
void FillRoundRect(Image& img, int x, int y, int w, int h, float radius, Rgb c, float a);
std::shared_ptr<Image> NewImage(int w, int h, int screenH = 0);

struct Font { int px = 20; bool bold = false; };

class GlyphBackend {
public:
    virtual ~GlyphBackend() = default;
    virtual int FaceCount() const = 0;                              // fallback chain, face 0 = primary
    virtual bool HasGlyph(int face, char32_t c) = 0;
    virtual void Metrics(int face, const Font& f, int& ascent, int& descent) = 0;
    virtual int Measure(int face, const Font& f, const std::u32string& run) = 0;
    // Adds white coverage (0..255, max-combined) of `run` with its baseline at (x, y) into mask (w x h).
    virtual void Draw(int face, const Font& f, const std::u32string& run, int x, int y, std::vector<uint8_t>& mask, int w, int h) = 0;
};

struct TextLine { std::u32string text; int width = 0; int indent = 0; };
struct TextLayout {
    std::vector<TextLine> lines;
    bool truncated = false;
    int lineHeight = 0, ascent = 0;
};

class TextEngine {
public:
    explicit TextEngine(GlyphBackend& b, std::u32string missing = U"□") : b_(b), missing_(std::move(missing)) {}
    int FaceFor(char32_t c);
    std::u32string Resolve(const std::u32string& s);   // characters without any glyph -> replacement
    int Width(const std::u32string& s, const Font& f);
    int LineHeight(const Font& f);
    int Ascent(const Font& f);
    // Greedy word wrap. firstIndent shortens the first line (inline prefix). maxLines 0 = unlimited.
    TextLayout Wrap(const std::u32string& text, const Font& f, int maxWidth, int maxLines, int firstIndent = 0);
    std::u32string Ellipsize(const std::u32string& s, const Font& f, int maxWidth);
    // Draws one line with its top at y.
    void DrawLine(Image& img, const std::u32string& s, const Font& f, int x, int y, Rgb c, float a = 1.f);
    void DrawLayout(Image& img, const TextLayout& l, const Font& f, int x, int y, Rgb c, float a = 1.f, int firstLine = 0, int maxLines = 0);
private:
    struct Run { int face; std::u32string text; };
    std::vector<Run> Runs(const std::u32string& s);
    void DrawRuns(std::vector<uint8_t>& mask, int w, int h, const std::u32string& s, const Font& f, int x, int baseline);
    void Blend(Image& img, const std::vector<uint8_t>& mask, Rgb c, float a);
    GlyphBackend& b_;
    std::u32string missing_;
    std::map<char32_t, int> faceCache_;
};
