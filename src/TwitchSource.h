// Trucker Chat
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
// Reads one Twitch channel's chat over Twitch's IRC WebSocket endpoint (wss://irc-ws.chat.twitch.tv:443).
// Without oauthToken it logs in read-only as "justinfanNNNNN" (anonymous): widely used, but NOT described in
// Twitch's documentation. With username + oauthToken (scope chat:read) it uses the documented login.
#include "ChatSource.h"
#include "Http.h"

class TwitchSource : public ChatSource {
public:
    explicit TwitchSource(const TwitchConfig& cfg);
    const char* Name() const override { return "Twitch"; }
    void Tick() override;
protected:
    void Run() override;
    void Abort() override { ws_.Abort(); }
private:
    bool Session();   // one connection; false = stop requested
    TwitchConfig cfg_;
    std::string channel_, token_;
    Http::WebSocket ws_;
    std::atomic<bool> connected_{false};
    std::atomic<uint64_t> lastRx_{0}, lastPing_{0};
};
