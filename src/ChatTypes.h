// Trucker Chat
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
// Platform-independent data types (no <windows.h>), shared by the plugin, the unit tests and the preview tool.
#include <cstdint>
#include <string>
#include <vector>

enum class Platform { Twitch, YouTube, System };

inline const char* PlatformName(Platform p) {
    switch (p) {
    case Platform::Twitch: return "Twitch";
    case Platform::YouTube: return "YouTube";
    default: return "Trucker Chat";
    }
}

struct ChatMessage {
    Platform platform = Platform::System;
    std::string id;           // platform message id (for moderation deletes), may be empty
    std::string userId;       // Twitch user-id / YouTube channel id, may be empty
    std::string login;        // Twitch login name (lowercase); YouTube: empty
    std::string user;         // display name (UTF-8)
    std::string text;         // message text (UTF-8, raw from the platform)
    uint32_t color = 0;       // 0xRRGGBB name colour from the platform, 0 = none
    bool hasColor = false;
    bool action = false;      // Twitch "/me"
    bool highlight = false;   // e.g. YouTube Super Chat
    bool moderator = false, owner = false;
    uint64_t receivedMs = 0;  // monotonic time it arrived
};

// What a chat source hands to the display thread
struct ChatEvent {
    enum class Kind { Message, DeleteMessage, DeleteUser, ClearAll, Status } kind = Kind::Message;
    ChatMessage msg;          // Message / Status (msg.text = status line)
    Platform platform = Platform::System;
    std::string key;          // DeleteMessage: message id; DeleteUser: user id or login
};
