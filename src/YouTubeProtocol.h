// Trucker Chat
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
// YouTube Data API v3 - request URLs and response parsing (platform independent).
// References:
//   https://developers.google.com/youtube/v3/live/docs/liveChatMessages/list  (GET .../youtube/v3/liveChat/messages)
//   https://developers.google.com/youtube/v3/live/docs/liveChatMessages      (resource)
//   https://developers.google.com/youtube/v3/docs/videos/list                (liveStreamingDetails.activeLiveChatId)
//   https://developers.google.com/youtube/v3/docs/search/list, .../channels/list (forHandle)
// The API key is sent in the "X-goog-api-key" header, never in the URL (so it cannot end up in a log line).
#include "ChatTypes.h"
#include <string>
#include <vector>

namespace YouTube {
extern const char* kHost;   // www.googleapis.com

struct Target { std::string videoId, channelId, handle; };
// Accepts a video id, a watch/live/youtu.be URL, a channel id (UC...), a channel URL or an @handle.
Target ParseTarget(const std::string& video, const std::string& channel);
// Finds a video id in pasted text (watch/live/youtu.be/Studio links or a bare 11-character id). Empty if none.
std::string VideoIdFromText(const std::string& text);

std::string UrlEncode(const std::string& s);
std::string UrlEncodeKeepPercent(const std::string& s);   // for tokens that may already contain %XX
std::string VideoPath(const std::string& videoId);              // videos.list part=liveStreamingDetails
std::string HandlePath(const std::string& handle);              // channels.list forHandle
std::string SearchLivePath(const std::string& channelId);       // search.list eventType=live
std::string MessagesPath(const std::string& liveChatId, const std::string& pageToken);

std::string ParseActiveLiveChatId(const std::string& body, bool* found = nullptr, bool* ended = nullptr);
std::string ParseChannelId(const std::string& body);
std::string ParseLiveVideoId(const std::string& body);
std::string ParseErrorReason(const std::string& body, std::string* message = nullptr);   // "quotaExceeded", "liveChatEnded", ...

struct MessagesPage {
    bool ok = false;
    std::vector<ChatEvent> events;
    std::string nextPageToken;
    int pollingIntervalMs = 0;
    bool offline = false;     // offlineAt present or chatEndedEvent seen
};
MessagesPage ParseMessages(const std::string& body, uint64_t nowMs);

// ---------------------------------------------------------------------------------------------
// "Web" mode without API key (UNOFFICIAL): reads the chat the way the YouTube web page itself does
// (watch page -> live_chat page -> POST /youtubei/v1/live_chat/get_live_chat). Not documented by Google and may
// change at any time. Flow taken from the open-source tools yt-dlp (youtube_live_chat.py) and chat-downloader.
namespace Web {
// Extracts the JSON object that follows `marker` (e.g. "ytInitialData", "\"INNERTUBE_CONTEXT\":"); "" if none.
std::string ExtractObjectAfter(const std::string& html, const std::string& marker, size_t maxSize = 8u << 20);
std::string ExtractInitialData(const std::string& html);      // var ytInitialData = {...};
std::string ExtractAssigned(const std::string& html, const std::string& name);   // name = {...}; or name = '...';
std::string StringValue(const std::string& html, const std::string& key);   // "KEY":"value"
std::string LivePagePath(const Target& t);                      // /@handle/live or /channel/UC.../live
std::string VideoIdFromLivePage(const std::string& html);      // canonical watch URL of a running/upcoming stream

struct WatchInfo {
    bool ok = false;                // ytcfg found (otherwise: consent page, error page, changed layout)
    std::string apiKey, contextJson, clientName = "1", clientVersion, visitorData;
    std::string continuation;       // first live chat continuation ("" = no live chat)
    std::vector<std::string> candidates;   // all continuations found ("Live chat", default, "Top chat")
    bool replay = false;            // chat is a replay (stream over)
    std::string state;              // for the log: playability / live flags of the video
};
WatchInfo ParseYtcfg(const std::string& html);   // only the client settings part (any YouTube page)
WatchInfo ParseWatchPage(const std::string& html);

// Parses continuationContents.liveChatContinuation from a POST reply or from the live_chat page's ytInitialData.
struct ChatPage {
    bool ok = false;
    std::vector<ChatEvent> events;
    std::string continuation;       // "" = chat ended
    int timeoutMs = 0;
};
ChatPage ParseChat(const std::string& json, uint64_t nowMs);
std::string PostBody(const std::string& contextJson, const std::string& continuation);
}
}
