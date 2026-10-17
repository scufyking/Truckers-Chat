// Trucker Chat
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "YouTubeSource.h"
#include "YouTubeProtocol.h"
#include "Logger.h"
#include <algorithm>
#include <cstdio>

YouTubeSource::YouTubeSource(const YouTubeConfig& cfg) : cfg_(cfg) {
    platform_ = Platform::YouTube;
    Log::AddSecret(cfg_.apiKey);
}

YouTubeSource::Outcome YouTubeSource::Request(const std::string& path, std::string& body, const char* what) {
    if (stop_) return Outcome::Stop;
    Http::Response r = http_.Get(Utf8ToWide(YouTube::kHost), path, {{L"X-goog-api-key", Utf8ToWide(cfg_.apiKey)}});
    ++calls_;
    if (stop_) return Outcome::Stop;
    if (r.error) { Log::Write("YouTube %s: network error (%s)", what, Http::ErrorText(r.error).c_str()); return Outcome::Retry; }
    body = std::move(r.body);
    if (r.status == 200) return Outcome::Ok;
    std::string message, reason = YouTube::ParseErrorReason(body, &message);
    Log::Write("YouTube %s: HTTP %d %s - %s", what, r.status, reason.c_str(), message.c_str());
    if (reason == "quotaExceeded" || reason == "dailyLimitExceeded") {
        Status("Tageskontingent der YouTube-API aufgebraucht - neuer Versuch in 30 Minuten");
        return Outcome::Quota;
    }
    if (reason == "keyInvalid" || reason == "accessNotConfigured" || reason == "API_KEY_INVALID" || reason == "PERMISSION_DENIED" ||
        reason == "keyExpired" || reason == "ipRefererBlocked" || (r.status == 400 && message.find("API key") != std::string::npos)) {
        Status("API-Schl\xC3\xBCssel abgelehnt (" + reason + ") - youtube.apiKey und freigeschaltete YouTube Data API v3 pr\xC3\xBC" "fen");
        return Outcome::BadKey;
    }
    if (reason == "liveChatEnded" || reason == "liveChatNotFound" || reason == "liveChatDisabled" || reason == "forbidden") return Outcome::ChatGone;
    if (reason == "rateLimitExceeded") return Outcome::RateLimited;
    return Outcome::Retry;
}

bool YouTubeSource::WaitAfter(Outcome o) {
    switch (o) {
    case Outcome::Stop: return false;
    case Outcome::Quota: return Sleep(30 * 60 * 1000);
    case Outcome::BadKey: return Sleep(10 * 60 * 1000);
    case Outcome::ChatGone: return Sleep(60 * 1000);
    case Outcome::RateLimited: return Sleep((DWORD)(cfg_.pollSeconds * 2000));
    default: return Sleep(NextBackoff());
    }
}

void YouTubeSource::SetStream(const std::string& videoId) {
    { std::lock_guard<std::mutex> l(switchMtx_); pendingVideo_ = videoId; }
    changed_ = true;
    Kick();
}

bool YouTubeSource::TakeSwitch(YouTube::Target& target) {
    if (!changed_.exchange(false)) return false;
    std::string v;
    { std::lock_guard<std::mutex> l(switchMtx_); v = pendingVideo_; }
    if (v.empty() || v == target.videoId) return false;
    target.videoId = v;
    Status("neuer Stream \xC3\xBC" "bernommen: " + v);
    return true;
}

void YouTubeSource::Run() {
    YouTube::Target target = YouTube::ParseTarget(cfg_.video, cfg_.channel);
    const bool haveChannel = !target.channelId.empty() || !target.handle.empty();
    TakeSwitch(target);   // a link from youtube-stream.txt overrides youtube.video
    const bool web = cfg_.mode == "web" || (cfg_.mode != "api" && cfg_.apiKey.empty());
    if (!web && cfg_.apiKey.empty()) { Status("kein API-Schl\xC3\xBCssel eingetragen (youtube.apiKey)"); return; }
    Log::Write("YouTube: mode %s", web ? "web (no API key, unofficial)" : "official Data API v3");
    if (target.videoId.empty() && !haveChannel) {
        Status("kein Stream eingetragen - Link im Men\xC3\xBC einf\xC3\xBCgen oder Strg+Umschalt+Y");
        while (!TakeSwitch(target)) if (!Sleep(3600 * 1000)) { Log::Write("YouTube: stopped"); return; }
    }
    if (web) { RunWeb(target, haveChannel); Log::Write("YouTube: stopped after %d requests", calls_); return; }
    std::string body;
    bool announcedWaiting = false;
    while (!stop_) {
        if (TakeSwitch(target)) announcedWaiting = false;
        // 1) which live video? (a pasted/configured video wins; otherwise search the channel)
        std::string videoId = target.videoId;
        if (videoId.empty()) {
            if (target.channelId.empty() && !target.handle.empty()) {
                Outcome o = Request(YouTube::HandlePath(target.handle), body, "channels.list");
                if (o != Outcome::Ok) { if (!WaitAfter(o)) break; continue; }
                target.channelId = YouTube::ParseChannelId(body);
                if (target.channelId.empty()) { Status("Kanal " + target.handle + " nicht gefunden"); target.handle.clear(); if (!Sleep(10 * 60 * 1000)) break; continue; }
                Log::Write("YouTube: %s is channel %s", target.handle.c_str(), target.channelId.c_str());
            }
            if (target.channelId.empty()) { if (!Sleep(3600 * 1000)) break; continue; }   // only a pasted link can help now
            Outcome o = Request(YouTube::SearchLivePath(target.channelId), body, "search.list");
            if (o != Outcome::Ok) { if (!WaitAfter(o)) break; continue; }
            videoId = YouTube::ParseLiveVideoId(body);
            if (videoId.empty()) {
                if (!announcedWaiting) { Status("Kanal ist nicht live - pr\xC3\xBC" "fe alle " + std::to_string((int)cfg_.searchMinutes) + " Minuten"); announcedWaiting = true; }
                if (!Sleep((DWORD)(cfg_.searchMinutes * 60000))) break;
                continue;
            }
        }
        // 2) its live chat
        Outcome o = Request(YouTube::VideoPath(videoId), body, "videos.list");
        if (o != Outcome::Ok) { if (!WaitAfter(o)) break; continue; }
        bool found = false, ended = false;
        std::string chatId = YouTube::ParseActiveLiveChatId(body, &found, &ended);
        if (chatId.empty()) {
            if (!target.videoId.empty() && (!found || ended) && haveChannel) {   // old link: fall back to the channel
                Status(std::string(ended ? "Livestream " : "Video ") + videoId + (ended ? " ist beendet" : " nicht gefunden") + " - suche auf dem Kanal");
                target.videoId.clear();
                continue;
            }
            if (!announcedWaiting) {
                Status(!found ? "Video " + videoId + " nicht gefunden" : ended ? "Livestream " + videoId + " ist beendet - neuen Link einf\xC3\xBCgen" : "Video " + videoId + " ist (noch) nicht live");
                announcedWaiting = true;
            }
            if (!Sleep(target.videoId.empty() ? (DWORD)(cfg_.searchMinutes * 60000) : 60000)) break;
            continue;
        }
        announcedWaiting = false;
        Status("verbunden mit Live-Chat von " + videoId);
        // 3) poll
        std::string token;
        bool first = true;
        uint64_t connectedAt = NowMs();
        while (!stop_ && !changed_) {
            o = Request(YouTube::MessagesPath(chatId, token), body, "liveChatMessages.list");
            if (o == Outcome::ChatGone) { Status("Live-Chat beendet"); break; }
            if (o != Outcome::Ok) { if (!WaitAfter(o)) break; continue; }
            YouTube::MessagesPage page = YouTube::ParseMessages(body, NowMs());
            if (!page.ok) { Log::Write("YouTube: unreadable reply"); if (!Sleep(NextBackoff())) break; continue; }
            if (!first || cfg_.showBacklog) for (auto& e : page.events) Emit(std::move(e));
            else if (!page.events.empty()) Log::Write("YouTube: skipped %d older messages (showBacklog = false)", (int)page.events.size());
            first = false;
            if (!page.nextPageToken.empty()) token = page.nextPageToken;
            if (page.offline) { Status("Livestream offline"); break; }
            if (NowMs() - connectedAt > 60000) ResetBackoff();
            DWORD wait = (DWORD)std::max<double>(page.pollingIntervalMs, cfg_.pollSeconds * 1000.0);
            if (!Sleep(wait)) break;
        }
        if (stop_) break;
        if (changed_) continue;   // a new link was pasted
        if (!target.videoId.empty() && haveChannel) target.videoId.clear();   // stream over: watch the channel again
        if (!Sleep(30000)) break;
    }
    Log::Write("YouTube: stopped after %d API calls", calls_);
}

// ---------------------------------------------------------------------------------------------
// Web mode (no API key). UNOFFICIAL: uses the same requests as the YouTube web page; may break when YouTube changes it.
namespace {
const char* kUserAgent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/141.0.0.0 Safari/537.36";
Http::Headers PageHeaders() {
    return {{L"User-Agent", Utf8ToWide(kUserAgent)},
            {L"Accept", L"text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8"},
            {L"Accept-Language", L"de-DE,de;q=0.9,en;q=0.8"},
            {L"Cookie", L"SOCS=CAI"}};   // "consent given" - otherwise EU visitors get the consent page instead of the video
}
}

// Keeps the last unreadable YouTube reply next to the log, so the cause can be analysed (no login data in it).
void YouTubeSource::SaveDebug(const char* name, const std::string& data) {
    std::wstring path = PluginDataDir() + Utf8ToWide(name);
    if (FILE* f = _wfopen(path.c_str(), L"wb")) {
        fwrite(data.data(), 1, std::min<size_t>(data.size(), 8u << 20), f);
        fclose(f);
        Log::Write("YouTube web: reply saved to %s (%d bytes)", name, (int)data.size());
    }
}

YouTubeSource::Outcome YouTubeSource::WebGet(const std::string& path, std::string& body, const char* what) {
    if (stop_) return Outcome::Stop;
    Http::Response r = http_.Get(L"www.youtube.com", path, PageHeaders());
    ++calls_;
    if (stop_) return Outcome::Stop;
    if (r.error) { Log::Write("YouTube web %s: network error (%s)", what, Http::ErrorText(r.error).c_str()); return Outcome::Retry; }
    body = std::move(r.body);
    if (r.status == 200) return Outcome::Ok;
    Log::Write("YouTube web %s: HTTP %d", what, r.status);
    if (r.status == 429) return Outcome::RateLimited;
    return r.status == 404 ? Outcome::ChatGone : Outcome::Retry;
}

void YouTubeSource::RunWeb(YouTube::Target target, bool haveChannel) {
    namespace W = YouTube::Web;
    std::string body;
    bool announcedWaiting = false;
    while (!stop_) {
        if (TakeSwitch(target)) announcedWaiting = false;
        // 1) which video? pasted/configured link, otherwise the channel's /live page
        std::string videoId = target.videoId;
        if (videoId.empty()) {
            if (!haveChannel) { if (!Sleep(3600 * 1000)) break; continue; }   // only a pasted link can help now
            Outcome o = WebGet(W::LivePagePath(target), body, "channel live page");
            if (o == Outcome::Ok) videoId = W::VideoIdFromLivePage(body);
            else if (o != Outcome::ChatGone) { if (!WaitAfter(o)) break; continue; }
            if (videoId.empty()) {
                if (!announcedWaiting) { Status("Kanal ist nicht live - pr\xC3\xBC" "fe alle " + std::to_string((int)cfg_.searchMinutes) + " Minuten"); announcedWaiting = true; }
                if (!Sleep((DWORD)(cfg_.searchMinutes * 60000))) break;
                continue;
            }
        }
        // 2) watch page: client settings (ytcfg) and the live chat continuation
        Outcome o = WebGet("/watch?v=" + YouTube::UrlEncode(videoId), body, "watch page");
        if (o != Outcome::Ok) { if (!WaitAfter(o == Outcome::ChatGone ? Outcome::Retry : o)) break; continue; }
        W::WatchInfo wi = W::ParseWatchPage(body);
        if (!wi.ok) {
            Status(body.find("consent.") != std::string::npos ? "YouTube zeigt eine Zustimmungsseite - Chat nicht lesbar"
                                                              : "YouTube-Seite nicht lesbar (hat YouTube etwas ge\xC3\xA4ndert?)");
            if (!Sleep(NextBackoff() * 5)) break;
            continue;
        }
        if (wi.continuation.empty() || wi.replay) {
            if (!target.videoId.empty() && haveChannel) {   // old link: fall back to the channel
                Status("Stream " + videoId + (wi.replay ? " ist beendet" : " hat keinen Live-Chat") + " - suche auf dem Kanal");
                target.videoId.clear();
                continue;
            }
            if (!announcedWaiting) {
                Status(wi.replay ? "Livestream " + videoId + " ist beendet - neuen Link einf\xC3\xBCgen"
                                 : "Video " + videoId + " hat keinen Live-Chat (nicht live, privat oder Chat aus)");
                announcedWaiting = true;
            }
            if (!Sleep(60000)) break;
            continue;
        }
        Log::Write("YouTube web: watch page %s", wi.state.c_str());
        // 3) first chat page. a) POST each continuation of the watch page to get_live_chat (as youtube-chat does);
        //    b) otherwise the popout chat page /live_chat?is_popout=1&v=<id> (what the "Chat in neuem Fenster" button opens):
        //       it carries its own client settings and the first chat page, no continuation needed.
        auto makePath = [&]() {
            std::string p = "/youtubei/v1/live_chat/get_live_chat?prettyPrint=false";
            if (!wi.apiKey.empty()) p += "&key=" + YouTube::UrlEncode(wi.apiKey);
            return p;
        };
        auto makeHeaders = [&](const std::string& referer) {
            Http::Headers h = {{L"User-Agent", Utf8ToWide(kUserAgent)}, {L"Content-Type", L"application/json"},
                               {L"Origin", L"https://www.youtube.com"}, {L"X-Origin", L"https://www.youtube.com"},
                               {L"Referer", Utf8ToWide(referer)},
                               {L"X-YouTube-Client-Name", Utf8ToWide(wi.clientName)}, {L"X-YouTube-Client-Version", Utf8ToWide(wi.clientVersion)},
                               {L"Cookie", L"SOCS=CAI"}};
            if (!wi.visitorData.empty()) h.push_back({L"X-Goog-Visitor-Id", Utf8ToWide(wi.visitorData)});
            return h;
        };
        const std::string popout = "/live_chat?is_popout=1&v=" + YouTube::UrlEncode(videoId);
        W::ChatPage page;
        std::string how;
        for (size_t i = 0; i < wi.candidates.size() && !stop_ && (!page.ok || page.continuation.empty()); ++i) {
            const std::string& c = wi.candidates[i];
            Http::Response r = http_.Post(L"www.youtube.com", makePath(), makeHeaders("https://www.youtube.com/watch?v=" + videoId),
                                          W::PostBody(wi.contextJson, c));
            ++calls_;
            if (!r.error && r.status == 200) page = W::ParseChat(r.body, NowMs());
            if (page.ok && !page.continuation.empty()) { how = "watch page token " + std::to_string(i + 1); break; }
            Log::Write("YouTube web: get_live_chat with token %d/%d (%d chars, starts %.6s) -> %s", (int)i + 1, (int)wi.candidates.size(),
                       (int)c.size(), c.c_str(), r.error ? Http::ErrorText(r.error).c_str() : ("HTTP " + std::to_string(r.status)).c_str());
            if (i == 0) SaveDebug("youtube-debug-post.json", r.body);
        }
        if (stop_) break;
        if (!page.ok || page.continuation.empty()) {
            o = WebGet(popout, body, "popout chat page");
            if (o != Outcome::Ok) { if (!WaitAfter(o)) break; continue; }
            W::WatchInfo cfgPopout = W::ParseYtcfg(body);
            page = W::ParseChat(W::ExtractInitialData(body), NowMs());
            if (page.ok && !page.continuation.empty()) {
                how = "popout chat page";
                if (cfgPopout.ok) {   // use the chat frame's own client settings for polling
                    wi.apiKey = cfgPopout.apiKey; wi.contextJson = cfgPopout.contextJson; wi.clientName = cfgPopout.clientName;
                    wi.clientVersion = cfgPopout.clientVersion; if (!cfgPopout.visitorData.empty()) wi.visitorData = cfgPopout.visitorData;
                }
            } else {
                Log::Write("YouTube web: popout chat page without chat data (%d bytes, ytInitialData %s, liveChatContinuation %s)",
                           (int)body.size(), body.find("ytInitialData") != std::string::npos ? "found" : "missing",
                           body.find("liveChatContinuation") != std::string::npos ? "found" : "missing");
                SaveDebug("youtube-debug-livechat.html", body);
                Status("Chat nicht lesbar - bitte trucker-chat.log und youtube-debug-Dateien schicken");
                if (!Sleep(std::max<DWORD>(NextBackoff(), 30000))) break;
                continue;
            }
        }
        Log::Write("YouTube web: chat found via %s", how.c_str());
        std::string continuation;
        if (cfg_.showBacklog) for (auto& e : page.events) Emit(std::move(e));
        else if (!page.events.empty()) Log::Write("YouTube web: skipped %d older messages (showBacklog = false)", (int)page.events.size());
        continuation = page.continuation;
        announcedWaiting = false;
        Status("verbunden mit Live-Chat von " + videoId + " (ohne API-Schl\xC3\xBCssel)");
        // 4) poll get_live_chat
        const std::string path = makePath();
        const Http::Headers hdr = makeHeaders("https://www.youtube.com" + popout);
        uint64_t connectedAt = NowMs();
        int failures = 0;
        DWORD wait = (DWORD)std::clamp(page.timeoutMs, 1500, 10000);
        while (!stop_ && !changed_) {
            if (!Sleep(wait)) break;
            Http::Response r = http_.Post(L"www.youtube.com", path, hdr, W::PostBody(wi.contextJson, continuation));
            ++calls_;
            if (stop_) break;
            W::ChatPage cp;
            if (!r.error && r.status == 200) cp = W::ParseChat(r.body, NowMs());
            if (!cp.ok) {
                Log::Write("YouTube web get_live_chat: %s", r.error ? Http::ErrorText(r.error).c_str() : ("HTTP " + std::to_string(r.status)).c_str());
                if (++failures >= 3 || (r.status >= 400 && r.status < 500 && r.status != 429)) break;   // start over from the watch page
                wait = NextBackoff();
                continue;
            }
            failures = 0;
            for (auto& e : cp.events) Emit(std::move(e));
            if (cp.continuation.empty()) { Status("Live-Chat beendet"); break; }
            continuation = cp.continuation;
            if (NowMs() - connectedAt > 60000) ResetBackoff();
            wait = (DWORD)std::clamp(cp.timeoutMs > 0 ? cp.timeoutMs : 3000, 1500, 10000);
        }
        if (stop_) break;
        if (changed_) continue;
        if (!target.videoId.empty() && haveChannel) target.videoId.clear();
        if (!Sleep(NextBackoff())) break;
    }
}
