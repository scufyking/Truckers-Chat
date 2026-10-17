// Trucker Chat
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
// Reads a YouTube live chat with the official YouTube Data API v3 and an API key (polling liveChatMessages.list).
// Quota (default 10,000 units/day per Google Cloud project): every poll costs 5 units, see README.
// Without an API key (mode "web"/"auto") it reads the chat like the YouTube web page does - unofficial, see RunWeb.
#include "ChatSource.h"
#include "Http.h"
#include "YouTubeProtocol.h"

class YouTubeSource : public ChatSource {
public:
    explicit YouTubeSource(const YouTubeConfig& cfg);
    const char* Name() const override { return "YouTube"; }
    // Switches to another live stream (video id) while running, e.g. a link pasted by the streamer. Any thread.
    void SetStream(const std::string& videoId);
protected:
    void Run() override;
    void Abort() override { http_.Abort(); }
private:
    enum class Outcome { Ok, Retry, Quota, BadKey, ChatGone, RateLimited, Stop };
    Outcome Request(const std::string& path, std::string& body, const char* what);
    bool WaitAfter(Outcome o);   // false = stop
    YouTubeConfig cfg_;
    Http::Client http_;
    bool TakeSwitch(YouTube::Target& target);
    void RunWeb(YouTube::Target target, bool haveChannel);   // no API key (unofficial web requests)
    Outcome WebGet(const std::string& path, std::string& body, const char* what);
    void SaveDebug(const char* name, const std::string& data);
    int calls_ = 0;
    std::mutex switchMtx_;
    std::string pendingVideo_;
    std::atomic<bool> changed_{false};
};
