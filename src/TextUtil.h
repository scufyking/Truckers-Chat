// Trucker Chat
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
// Unicode helpers (platform independent). Chat text from the internet is untrusted: it is decoded defensively,
// invalid UTF-8 becomes U+FFFD, and everything that could break the display (control characters, bidi overrides,
// "Zalgo" stacks of combining marks, emoji modifiers the renderer cannot compose) is removed or replaced.
#include <string>
#include <vector>
#include <cstdint>

std::u32string DecodeUtf8(const std::string& s);          // never throws; invalid sequences -> U+FFFD
std::string EncodeUtf8(const std::u32string& s);
std::u16string ToUtf16(const std::u32string& s);           // for Win32 (wchar_t is UTF-16 there)

bool IsEmoji(char32_t c);            // pictographic emoji / symbols (the base characters, not modifiers)
bool IsCombiningMark(char32_t c);
bool IsWideBreakable(char32_t c);    // CJK & co.: a line may break between two of these characters
char32_t SimpleLower(char32_t c);    // lower-case for ASCII, Latin-1, Latin Extended-A, Greek and Cyrillic
std::u32string Lower(const std::u32string& s);
std::string LowerAscii(std::string s);

struct SanitizeOptions {
    std::string emoji = "keep";            // keep | replace | remove
    std::u32string emojiReplacement = U"□";
    size_t maxLength = 200;                // in characters (code points), 0 = unlimited; longer text ends with "…"
    int maxCombiningPerChar = 2;
};
// Returns display-safe text (single line, collapsed white space, trimmed).
std::u32string SanitizeText(const std::u32string& in, const SanitizeOptions& opt);
std::u32string SanitizeName(const std::u32string& in, size_t maxLength = 32);
