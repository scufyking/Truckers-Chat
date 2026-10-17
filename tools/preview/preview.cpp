// Trucker Chat - preview tool (Linux/macOS, development only; not part of the plugin)
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Renders the plugin's real card/list/overlay code (src/Cards.cpp, src/Canvas.cpp, src/TextUtil.cpp) with a FreeType
// glyph backend instead of GDI, and writes PNG files. The layout is identical; glyph rasterisation and the fallback
// fonts differ from Windows (DejaVu/Noto CJK here, Segoe UI family on Windows), and emoji are not drawn here.
//
// usage: preview <font dir with Inter-Regular.otf/Inter-Bold.otf> <output dir> [fallback font files...]
#include "Cards.h"
#include "TextUtil.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <zlib.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
class FtBackend : public GlyphBackend {
public:
    FtBackend() { FT_Init_FreeType(&lib_); }
    bool Add(const std::string& regular, const std::string& bold) {
        Face f;
        if (FT_New_Face(lib_, regular.c_str(), 0, &f.reg)) return false;
        if (bold.empty() || FT_New_Face(lib_, bold.c_str(), 0, &f.bold)) f.bold = f.reg;
        faces_.push_back(f);
        return true;
    }
    int FaceCount() const override { return (int)faces_.size(); }
    bool HasGlyph(int face, char32_t c) override { return FT_Get_Char_Index(faces_[face].reg, c) != 0; }
    void Metrics(int face, const Font& f, int& a, int& d) override {
        FT_Face fc = Get(face, f);
        a = (int)(fc->size->metrics.ascender >> 6); d = (int)(-fc->size->metrics.descender >> 6);
    }
    int Measure(int face, const Font& f, const std::u32string& run) override {
        FT_Face fc = Get(face, f);
        long w = 0;
        for (char32_t c : run) if (!FT_Load_Char(fc, c, FT_LOAD_DEFAULT)) w += fc->glyph->advance.x;
        return (int)((w + 32) >> 6);
    }
    void Draw(int face, const Font& f, const std::u32string& run, int x, int y, std::vector<uint8_t>& mask, int w, int h) override {
        FT_Face fc = Get(face, f);
        long pen = (long)x << 6;
        for (char32_t c : run) {
            if (FT_Load_Char(fc, c, FT_LOAD_RENDER)) continue;
            FT_GlyphSlot g = fc->glyph;
            int ox = (int)(pen >> 6) + g->bitmap_left, oy = y - g->bitmap_top;
            for (unsigned r = 0; r < g->bitmap.rows; ++r) for (unsigned k = 0; k < g->bitmap.width; ++k) {
                int px = ox + (int)k, py = oy + (int)r;
                if (px < 0 || py < 0 || px >= w || py >= h) continue;
                uint8_t v = g->bitmap.buffer[r * g->bitmap.pitch + k];
                uint8_t& m = mask[(size_t)py * w + px];
                if (v > m) m = v;
            }
            pen += g->advance.x;
        }
    }
private:
    struct Face { FT_Face reg = nullptr, bold = nullptr; };
    FT_Face Get(int face, const Font& f) {
        FT_Face fc = f.bold ? faces_[face].bold : faces_[face].reg;
        FT_Set_Pixel_Sizes(fc, 0, f.px);
        return fc;
    }
    FT_Library lib_ = nullptr;
    std::vector<Face> faces_;
};

void Crc(uint32_t& crc, const unsigned char* p, size_t n) { crc = (uint32_t)crc32(crc, p, (uInt)n); }
void WritePng(const std::string& path, const Image& img, bool onCheckerboard) {
    std::vector<unsigned char> raw;
    raw.reserve((size_t)img.height * (img.width * 4 + 1));
    for (int y = 0; y < img.height; ++y) {
        raw.push_back(0);
        for (int x = 0; x < img.width; ++x) {
            uint32_t p = img.pixels[(size_t)y * img.width + x];
            float a = ((p >> 24) & 255) / 255.f, r = ((p >> 16) & 255) / 255.f, g = ((p >> 8) & 255) / 255.f, b = (p & 255) / 255.f;
            // composite over a stand-in for the navigation map (premultiplied "over")
            float bg = onCheckerboard ? (((x / 24 + y / 24) & 1) ? 0.30f : 0.22f) : 0.18f;
            float bgR = bg * 0.8f, bgG = bg, bgB = bg * 1.15f;
            raw.push_back((unsigned char)std::min(255.f, (r + bgR * (1 - a)) * 255.f));
            raw.push_back((unsigned char)std::min(255.f, (g + bgG * (1 - a)) * 255.f));
            raw.push_back((unsigned char)std::min(255.f, (b + bgB * (1 - a)) * 255.f));
            raw.push_back(255);
        }
    }
    uLongf zn = compressBound((uLong)raw.size());
    std::vector<unsigned char> z(zn);
    compress2(z.data(), &zn, raw.data(), (uLong)raw.size(), 9);
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return;
    auto be = [](uint32_t v, unsigned char* o) { o[0] = v >> 24; o[1] = v >> 16; o[2] = v >> 8; o[3] = (unsigned char)v; };
    auto chunk = [&](const char* type, const unsigned char* d, uint32_t n) {
        unsigned char len[4]; be(n, len); fwrite(len, 1, 4, f);
        uint32_t crc = 0; Crc(crc, (const unsigned char*)type, 4); if (n) Crc(crc, d, n);
        fwrite(type, 1, 4, f); if (n) fwrite(d, 1, n, f);
        unsigned char c[4]; be(crc, c); fwrite(c, 1, 4, f);
    };
    const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    fwrite(sig, 1, 8, f);
    unsigned char ihdr[13]; be(img.width, ihdr); be(img.height, ihdr + 4); ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = ihdr[11] = ihdr[12] = 0;
    chunk("IHDR", ihdr, 13);
    chunk("IDAT", z.data(), (uint32_t)zn);
    chunk("IEND", nullptr, 0);
    fclose(f);
}

ShownMessage Msg(Platform p, const char* user, const char* text, uint32_t color = 0, bool action = false, bool hl = false, size_t maxLen = 200) {
    SanitizeOptions so; so.maxLength = maxLen;
    ShownMessage m;
    m.platform = p; m.user = SanitizeName(DecodeUtf8(user)); m.text = SanitizeText(DecodeUtf8(text), so);
    m.color = color; m.hasColor = color != 0; m.action = action; m.highlight = hl;
    return m;
}
}

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: preview <fontdir> <outdir> [fallback fonts...]\n"); return 1; }
    std::string fd = argv[1], out = argv[2];
    FtBackend be;
    if (!be.Add(fd + "/Inter-Regular.otf", fd + "/Inter-Bold.otf")) { fprintf(stderr, "Inter not found in %s\n", fd.c_str()); return 1; }
    for (int i = 3; i < argc; ++i) be.Add(argv[i], "");
    TextEngine te(be);
    CardOptions o;
    const int W = 819, H = 410;   // visible navigation area of the MAN TGX (1024 x 0.8, 1024 x 0.4)

    auto twitch = Msg(Platform::Twitch, "Fernfahrer_Kalle", "Moin Scufy! Wie weit ist es noch bis Hamburg? Die Ladung sieht schwer aus", 0x1E90FF);
    auto yt = Msg(Platform::YouTube, "Ana Ribeiro", "Saudações do Brasil! Que estrada linda, adoro a Escandinávia neste jogo.");
    auto longMsg = Msg(Platform::Twitch, "LongTextLars", "Das ist eine sehr lange Nachricht, die nicht auf vier Zeilen passt: Ich wollte nur sagen, dass die Route über "
                       "die A7 heute wegen Baustellen ziemlich voll ist, und dass du beim nächsten Rastplatz unbedingt die Pause machen solltest, "
                       "weil sonst die Lenkzeit überschritten wird. Außerdem steht bei Kassel ein Blitzer, also langsam fahren. "
                       "Und denk an den Auflieger, der muss bis 18 Uhr in Rotterdam sein, sonst gibt es eine Verspätungsstrafe vom Auftraggeber. "
                       "Gute Fahrt, viele Grüße an den Chat und bis später!", 0x8A2BE2, false, false, 500);   // display.maxTextLength = 500
    auto sc = Msg(Platform::YouTube, "Truck Fan 42", "5,00\xC2\xA0\xE2\x82\xAC \xC2\xB7 Für den nächsten Diesel!", 0, false, true);
    auto act = Msg(Platform::Twitch, "hupe_horst", "hupt zweimal", 0xFF4500, true);
    auto cyr = Msg(Platform::Twitch, "Дмитрий", "Привет из Москвы! Классная поездка \xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\xE3\x82\x82OK", 0x00FF7F);
    SanitizeOptions rep; rep.emoji = "replace";
    ShownMessage emo = Msg(Platform::YouTube, "Emoji Emma", "");
    emo.text = SanitizeText(DecodeUtf8("Gute Fahrt \xF0\x9F\x9A\x9B\xF0\x9F\x9A\x9B\xF0\x9F\x9A\x9B\xF0\x9F\x9A\x9B\xF0\x9F\x9A\x9B (emoji = replace)"), rep);
    auto sys = Msg(Platform::Twitch, "Trucker Chat", "Twitch: verbunden mit #scufyking", 0xFFAE00);   // as ChatDisplay::Make builds status lines

    WritePng(out + "/screen_card_twitch.png", *RenderCard(te, twitch, W, H, o), true);
    WritePng(out + "/screen_card_youtube_superchat.png", *RenderCard(te, sc, W, H, o), true);
    WritePng(out + "/screen_card_long_cut.png", *RenderCard(te, longMsg, W, H, o), true);
    CardOptions os = o; os.scroll = true;
    int total = 0, vis = 0;
    RenderCard(te, longMsg, W, H, os, 0, &total, &vis);
    WritePng(out + "/screen_card_long_scroll_2.png", *RenderCard(te, longMsg, W, H, os, 2), true);
    WritePng(out + "/screen_card_emoji_replace.png", *RenderCard(te, emo, W, H, o), true);
    WritePng(out + "/screen_card_status.png", *RenderCard(te, sys, W, H, o), true);
    float u = std::min(W / 16.f, H / 9.f) / 40.f;
    WritePng(out + "/screen_list.png", *RenderList(te, {yt, act, cyr, twitch}, W, H, o, u), true);
    CardOptions ov = o; ov.background = 0.82f;
    WritePng(out + "/overlay_1080p.png", *RenderOverlayPanel(te, {twitch, yt, act, cyr}, 1080, 1.0, 0.62, ov), false);
    WritePng(out + "/grid_1024.png", *RenderGrid(te, 1024, 1024, false, true), false);
    WritePng(out + "/probe.png", *RenderProbe(te, U"3/7  1024x1024  f29", 512, 512), false);
    printf("long text: %d lines, %d visible\n", total, vis);
    return 0;
}
