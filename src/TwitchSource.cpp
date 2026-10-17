// Trucker Chat
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "TwitchSource.h"
#include "TwitchProtocol.h"
#include "Logger.h"

TwitchSource::TwitchSource(const TwitchConfig& cfg) : cfg_(cfg) {
    platform_ = Platform::Twitch;
    channel_ = NormalizeTwitchChannel(cfg.channel);
    token_ = cfg.oauthToken;
    if (token_.rfind("oauth:", 0) == 0) token_ = token_.substr(6);
    Log::AddSecret(token_);
}

void TwitchSource::Tick() {
    if (!connected_) return;
    uint64_t now = NowMs();
    // Twitch sends PING about every 5 minutes; we also ping every 60 s so a dead connection is noticed quickly
    if (now - lastPing_ > 60000) { lastPing_ = now; ws_.SendText("PING :trucker-chat\r\n"); }
    if (now - lastRx_ > 150000) {
        Log::Write("Twitch: no data for 150 s - reconnecting");
        lastRx_ = now;
        ws_.Abort();
    }
}

void TwitchSource::Run() {
    if (channel_.empty()) { Status("kein Kanal eingetragen (twitch.channel)"); return; }
    while (!stop_) {
        if (!Session()) break;
        if (stop_) break;
        DWORD wait = NextBackoff();
        Log::Write("Twitch: reconnecting in %lu s", wait / 1000);
        if (!Sleep(wait)) break;
    }
    ws_.Close();
}

bool TwitchSource::Session() {
    DWORD err = 0;
    Log::Write("Twitch: connecting to irc-ws.chat.twitch.tv for #%s (%s)", channel_.c_str(), token_.empty() ? "anonymous, read-only" : "with token");
    if (!ws_.Connect(L"irc-ws.chat.twitch.tv", 443, L"/", &err)) {
        Log::Write("Twitch: connection failed (%s)", Http::ErrorText(err).c_str());
        return !stop_;
    }
    lastRx_ = NowMs(); lastPing_ = NowMs();
    std::string nick;
    bool ok = ws_.SendText("CAP REQ :twitch.tv/tags twitch.tv/commands\r\n");
    if (token_.empty()) {
        nick = "justinfan" + std::to_string(10000 + GetTickCount64() % 89999);
        ok = ok && ws_.SendText("PASS SCHMOOPIIE\r\n") && ws_.SendText("NICK " + nick + "\r\n");
    } else {
        nick = NormalizeTwitchChannel(cfg_.username);
        ok = ok && ws_.SendText("PASS oauth:" + token_ + "\r\n") && ws_.SendText("NICK " + nick + "\r\n");
    }
    ok = ok && ws_.SendText("JOIN #" + channel_ + "\r\n");
    if (!ok) { Log::Write("Twitch: sending the login failed"); ws_.Close(); return !stop_; }
    connected_ = true;
    uint64_t joinedAt = 0;
    bool authFailed = false;
    std::string msg;
    while (!stop_) {
        Http::WebSocket::Result r = ws_.Receive(msg);
        if (r != Http::WebSocket::Result::Message) {
            if (!stop_) Log::Write("Twitch: connection %s", r == Http::WebSocket::Result::Closed ? "closed" : "error");
            break;
        }
        lastRx_ = NowMs();
        bool reconnect = false;
        size_t start = 0;
        while (start < msg.size()) {   // one WebSocket message may carry several IRC lines
            size_t end = msg.find("\r\n", start);
            if (end == std::string::npos) end = msg.size();
            std::string line = msg.substr(start, end - start);
            start = end + 2;
            IrcMessage im;
            if (!ParseIrcLine(line, im)) continue;
            TwitchResult tr = InterpretTwitch(im);
            switch (tr.kind) {
            case TwitchResult::Kind::Ping: ws_.SendText("PONG :" + tr.text + "\r\n"); break;
            case TwitchResult::Kind::Reconnect: Log::Write("Twitch: server asked to reconnect"); reconnect = true; break;
            case TwitchResult::Kind::AuthFailed: Status("Anmeldung abgelehnt - twitch.username/oauthToken pr\xC3\xBC" "fen"); authFailed = true; break;
            case TwitchResult::Kind::Notice: Log::Write("Twitch notice: %s", tr.text.c_str()); break;
            case TwitchResult::Kind::Joined:
                if (!joinedAt && im.prefix.compare(0, nick.size(), nick) == 0) {
                    joinedAt = NowMs();
                    Status("verbunden mit #" + channel_);
                }
                break;
            case TwitchResult::Kind::Event:
                tr.event.msg.receivedMs = NowMs();
                Emit(std::move(tr.event));
                break;
            default: break;
            }
        }
        if (reconnect || authFailed) break;
        if (joinedAt && NowMs() - joinedAt > 60000) ResetBackoff();
    }
    connected_ = false;
    ws_.Close();
    if (authFailed) return Sleep(300000);   // wrong token: do not hammer the server
    if (joinedAt && !stop_) Status("Verbindung getrennt - verbinde neu");
    return !stop_;
}
