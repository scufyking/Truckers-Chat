// Trucker Chat
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "YouTubeProtocol.h"
#include "json.hpp"
#include <cstring>
#include <algorithm>
#include "TextUtil.h"

using nlohmann::json;

namespace YouTube {
const char* kHost = "www.googleapis.com";

namespace {
std::string Trim(std::string s) {
    s.erase(0, s.find_first_not_of(" \t\r\n"));
    s.erase(s.find_last_not_of(" \t\r\n") + 1);
    return s;
}
bool IdChars(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) if (!(isalnum((unsigned char)c) || c == '-' || c == '_')) return false;
    return true;
}
std::string AfterKey(const std::string& url, const std::string& key) {   // value of ?v= / path segment after key
    size_t i = url.find(key);
    if (i == std::string::npos) return "";
    i += key.size();
    size_t e = url.find_first_of("?&#/", i);
    return url.substr(i, e == std::string::npos ? std::string::npos : e - i);
}
std::string Str(const json& j, const char* k) {
    auto it = j.find(k);
    return (it != j.end() && it->is_string()) ? it->get<std::string>() : std::string();
}
bool Bool(const json& j, const char* k) {
    auto it = j.find(k);
    return it != j.end() && it->is_boolean() && it->get<bool>();
}
}

Target ParseTarget(const std::string& videoIn, const std::string& channelIn) {
    Target t;
    std::string v = Trim(videoIn), c = Trim(channelIn);
    if (!v.empty()) {
        std::string id;
        if (v.find("youtu.be/") != std::string::npos) id = AfterKey(v, "youtu.be/");
        else if (v.find("v=") != std::string::npos) id = AfterKey(v, "v=");
        else if (v.find("/live/") != std::string::npos) id = AfterKey(v, "/live/");
        else if (v.find("/shorts/") != std::string::npos) id = AfterKey(v, "/shorts/");
        else if (v.find("/video/") != std::string::npos) id = AfterKey(v, "/video/");   // studio.youtube.com/video/<id>/livestreaming
        else id = v;
        if (IdChars(id)) t.videoId = id;
    }
    if (!c.empty()) {
        std::string id = c;
        if (c.find("/channel/") != std::string::npos) id = AfterKey(c, "/channel/");
        else if (c.find("/@") != std::string::npos) id = "@" + AfterKey(c, "/@");
        if (!id.empty() && id[0] == '@') { if (IdChars(id.substr(1)) || id.size() > 1) t.handle = id; }
        else if (id.size() == 24 && id.compare(0, 2, "UC") == 0 && IdChars(id)) t.channelId = id;
        else if (IdChars(id)) t.handle = "@" + id;
    }
    return t;
}

std::string VideoIdFromText(const std::string& text) {
    std::string t = Trim(text);
    if (t.size() > 2000) return "";
    // the first word that looks like a YouTube link
    size_t p = 0;
    while (p < t.size()) {
        size_t e = t.find_first_of(" \t\r\n", p);
        std::string w = t.substr(p, e == std::string::npos ? std::string::npos : e - p);
        if (w.find("youtu") != std::string::npos) {
            std::string id = ParseTarget(w, "").videoId;
            if (id.size() == 11) return id;
        }
        if (e == std::string::npos) break;
        p = e + 1;
    }
    if (t.size() == 11 && IdChars(t)) return t;   // bare video id
    return "";
}

std::string UrlEncodeKeepPercent(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '%') o += (char)c;
        else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
    }
    return o;
}

std::string UrlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
        else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
    }
    return o;
}
std::string VideoPath(const std::string& id) { return "/youtube/v3/videos?part=liveStreamingDetails&id=" + UrlEncode(id); }
std::string HandlePath(const std::string& h) { return "/youtube/v3/channels?part=id&forHandle=" + UrlEncode(h); }
std::string SearchLivePath(const std::string& ch) {
    return "/youtube/v3/search?part=id&type=video&eventType=live&maxResults=1&channelId=" + UrlEncode(ch);
}
std::string MessagesPath(const std::string& chat, const std::string& token) {
    std::string p = "/youtube/v3/liveChat/messages?part=id,snippet,authorDetails&maxResults=200&liveChatId=" + UrlEncode(chat);
    if (!token.empty()) p += "&pageToken=" + UrlEncode(token);
    return p;
}

std::string ParseActiveLiveChatId(const std::string& body, bool* found, bool* ended) {
    if (found) *found = false;
    if (ended) *ended = false;
    json j = json::parse(body, nullptr, false);
    if (!j.is_object() || !j.contains("items") || !j["items"].is_array() || j["items"].empty()) return "";
    if (found) *found = true;
    const json& it = j["items"][0];
    if (!it.contains("liveStreamingDetails") || !it["liveStreamingDetails"].is_object()) return "";
    const json& d = it["liveStreamingDetails"];
    if (ended) *ended = d.contains("actualEndTime");
    return Str(d, "activeLiveChatId");
}

std::string ParseChannelId(const std::string& body) {
    json j = json::parse(body, nullptr, false);
    if (!j.is_object() || !j.contains("items") || !j["items"].is_array() || j["items"].empty()) return "";
    return Str(j["items"][0], "id");
}

std::string ParseLiveVideoId(const std::string& body) {
    json j = json::parse(body, nullptr, false);
    if (!j.is_object() || !j.contains("items") || !j["items"].is_array() || j["items"].empty()) return "";
    const json& it = j["items"][0];
    if (!it.contains("id") || !it["id"].is_object()) return "";
    return Str(it["id"], "videoId");
}

std::string ParseErrorReason(const std::string& body, std::string* message) {
    json j = json::parse(body, nullptr, false);
    if (!j.is_object() || !j.contains("error") || !j["error"].is_object()) return "";
    const json& e = j["error"];
    if (message) *message = Str(e, "message");
    if (e.contains("errors") && e["errors"].is_array() && !e["errors"].empty() && e["errors"][0].is_object())
        return Str(e["errors"][0], "reason");
    return Str(e, "status");
}

MessagesPage ParseMessages(const std::string& body, uint64_t nowMs) {
    MessagesPage p;
    json j = json::parse(body, nullptr, false);
    if (!j.is_object()) return p;
    p.ok = true;
    p.nextPageToken = Str(j, "nextPageToken");
    if (j.contains("pollingIntervalMillis") && j["pollingIntervalMillis"].is_number()) p.pollingIntervalMs = j["pollingIntervalMillis"].get<int>();
    p.offline = j.contains("offlineAt");
    if (!j.contains("items") || !j["items"].is_array()) return p;
    for (const json& it : j["items"]) {
        if (!it.is_object() || !it.contains("snippet") || !it["snippet"].is_object()) continue;
        const json& s = it["snippet"];
        std::string type = Str(s, "type");
        ChatEvent ev; ev.platform = Platform::YouTube;
        if (type == "chatEndedEvent") { p.offline = true; continue; }
        if (type == "tombstone") {   // placeholder for a deleted message (same id)
            ev.kind = ChatEvent::Kind::DeleteMessage; ev.key = Str(it, "id");
            if (!ev.key.empty()) p.events.push_back(ev);
            continue;
        }
        if (type == "messageDeletedEvent") {
            if (s.contains("messageDeletedDetails") && s["messageDeletedDetails"].is_object()) {
                ev.kind = ChatEvent::Kind::DeleteMessage; ev.key = Str(s["messageDeletedDetails"], "deletedMessageId");
                if (!ev.key.empty()) p.events.push_back(ev);
            }
            continue;
        }
        if (type == "userBannedEvent") {
            if (s.contains("userBannedDetails") && s["userBannedDetails"].is_object() && s["userBannedDetails"].contains("bannedUserDetails")) {
                ev.kind = ChatEvent::Kind::DeleteUser; ev.key = Str(s["userBannedDetails"]["bannedUserDetails"], "channelId");
                if (!ev.key.empty()) p.events.push_back(ev);
            }
            continue;
        }
        bool text = type == "textMessageEvent", superChat = type == "superChatEvent" || type == "superStickerEvent";
        if (!text && !superChat) continue;   // memberships, polls, ... are not chat lines
        ChatMessage& m = ev.msg;
        m.platform = Platform::YouTube;
        m.id = Str(it, "id");
        m.highlight = superChat;
        if (text && s.contains("textMessageDetails") && s["textMessageDetails"].is_object()) m.text = Str(s["textMessageDetails"], "messageText");
        for (const char* det : {"superChatDetails", "superStickerDetails"}) {   // "€5,00 · comment"
            if (!superChat || !s.contains(det) || !s[det].is_object()) continue;
            std::string amount = Str(s[det], "amountDisplayString"), comment = Str(s[det], "userComment");
            m.text = amount.empty() ? comment : (comment.empty() ? amount : amount + " \xC2\xB7 " + comment);
        }
        if (m.text.empty()) m.text = Str(s, "displayMessage");
        if (it.contains("authorDetails") && it["authorDetails"].is_object()) {
            const json& a = it["authorDetails"];
            m.user = Str(a, "displayName");
            m.userId = Str(a, "channelId");
            m.owner = Bool(a, "isChatOwner");
            m.moderator = Bool(a, "isChatModerator");
        }
        if (m.userId.empty()) m.userId = Str(s, "authorChannelId");
        if (m.text.empty() && !superChat) continue;
        m.receivedMs = nowMs;
        ev.kind = ChatEvent::Kind::Message;
        p.events.push_back(ev);
    }
    return p;
}

// ---------------------------------------------------------------------------------------------
namespace Web {
namespace {
// end index (exclusive) of the balanced JSON object starting at s[start] == '{', or npos
size_t MatchObject(const std::string& s, size_t start, size_t maxSize) {
    int depth = 0; bool str = false, esc = false;
    for (size_t i = start; i < s.size() && i - start < maxSize; ++i) {
        char c = s[i];
        if (str) {
            if (esc) esc = false;
            else if (c == '\\') esc = true;
            else if (c == '"') str = false;
            continue;
        }
        if (c == '"') str = true;
        else if (c == '{') ++depth;
        else if (c == '}' && --depth == 0) return i + 1;
    }
    return std::string::npos;
}
const json& Path(const json& j, std::initializer_list<const char*> keys) {
    static const json null;
    const json* cur = &j;
    for (const char* k : keys) {
        if (!cur->is_object()) return null;
        auto it = cur->find(k);
        if (it == cur->end()) return null;
        cur = &*it;
    }
    return *cur;
}
std::string Runs(const json& msg) {
    std::string out;
    if (msg.is_object() && msg.contains("simpleText") && msg["simpleText"].is_string()) return msg["simpleText"].get<std::string>();
    if (!msg.is_object() || !msg.contains("runs") || !msg["runs"].is_array()) return out;
    for (const json& r : msg["runs"]) {
        if (!r.is_object()) continue;
        if (r.contains("text") && r["text"].is_string()) { out += r["text"].get<std::string>(); continue; }
        const json& e = Path(r, {"emoji"});
        if (!e.is_object()) continue;
        bool custom = e.contains("isCustomEmoji") && e["isCustomEmoji"].is_boolean() && e["isCustomEmoji"].get<bool>();
        std::string id = Str(e, "emojiId"), shortcut;
        if (e.contains("shortcuts") && e["shortcuts"].is_array() && !e["shortcuts"].empty() && e["shortcuts"][0].is_string())
            shortcut = e["shortcuts"][0].get<std::string>();
        out += (!custom && !id.empty()) ? id : shortcut;   // Unicode emoji are their own id; channel emoji -> ":name:"
    }
    return out;
}
}

// Decodes a JavaScript string literal starting at html[pos] (quote char). Handles \xHH, \uHHHH and simple escapes.
std::string JsStringLiteral(const std::string& html, size_t pos) {
    char q = html[pos];
    auto hex = [&](size_t at, int n, unsigned& v) {
        v = 0;
        if (at + n > html.size()) return false;
        for (int k = 0; k < n; ++k) {
            char c = html[at + k]; v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0'; else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10; else return false;
        }
        return true;
    };
    std::string raw;   // collect bytes; \x/\u produce code points, plain bytes are UTF-8 already
    for (size_t i = pos + 1; i < html.size() && raw.size() < (16u << 20); ++i) {
        char c = html[i];
        if (c == q) return raw;
        if (c != '\\') { raw += c; continue; }
        if (++i >= html.size()) break;
        char e = html[i];
        unsigned v = 0;
        if (e == 'x' && hex(i + 1, 2, v)) { raw += EncodeUtf8(std::u32string(1, (char32_t)v)); i += 2; }
        else if (e == 'u' && hex(i + 1, 4, v)) {
            i += 4;
            unsigned lo = 0;
            if (v >= 0xD800 && v <= 0xDBFF && i + 6 < html.size() && html[i + 1] == '\\' && html[i + 2] == 'u' && hex(i + 3, 4, lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
                v = 0x10000 + ((v - 0xD800) << 10) + (lo - 0xDC00); i += 6;
            }
            raw += EncodeUtf8(std::u32string(1, (char32_t)v));
        }
        else if (e == 'n') raw += '\n'; else if (e == 't') raw += '\t'; else if (e == 'r') raw += '\r';
        else raw += e;   // \\ \' \" \/
    }
    return "";
}

std::string ExtractObjectAfter(const std::string& html, const std::string& marker, size_t maxSize) {
    for (size_t p = html.find(marker); p != std::string::npos; p = html.find(marker, p + 1)) {
        size_t b = html.find('{', p + marker.size());
        if (b == std::string::npos || b - (p + marker.size()) > 16) continue;
        size_t e = MatchObject(html, b, maxSize);
        if (e != std::string::npos) return html.substr(b, e - b);
    }
    return "";
}

std::string ExtractAssigned(const std::string& html, const std::string& name) {
    for (size_t p = html.find(name); p != std::string::npos; p = html.find(name, p + 1)) {
        size_t i = p + name.size();
        while (i < html.size() && (html[i] == '"' || html[i] == '\'' || html[i] == ']' || html[i] == ' ')) ++i;
        if (i >= html.size() || html[i] != '=') continue;
        ++i;
        while (i < html.size() && (html[i] == ' ' || html[i] == '\n' || html[i] == '\t')) ++i;
        if (html.compare(i, 11, "JSON.parse(") == 0) i += 11;
        if (i < html.size() && (html[i] == '\'' || html[i] == '"')) {   // JS string literal ('\x7b\x22...'), used by some pages
            std::string out = JsStringLiteral(html, i);
            if (!out.empty() && out[0] == '{') return out;
            continue;
        }
        if (i >= html.size() || html[i] != '{') continue;
        size_t e = MatchObject(html, i, 16u << 20);
        if (e != std::string::npos) return html.substr(i, e - i);
    }
    return "";
}

std::string ExtractInitialData(const std::string& html) { return ExtractAssigned(html, "ytInitialData"); }

std::string StringValue(const std::string& html, const std::string& key) {
    std::string m = "\"" + key + "\":\"";
    size_t p = html.find(m);
    if (p == std::string::npos) return "";
    p += m.size();
    size_t e = html.find('"', p);
    if (e == std::string::npos || e - p > 512) return "";
    return html.substr(p, e - p);
}

std::string LivePagePath(const Target& t) {
    if (!t.channelId.empty()) return "/channel/" + UrlEncode(t.channelId) + "/live";
    if (!t.handle.empty()) return "/" + (t.handle[0] == '@' ? "@" + UrlEncode(t.handle.substr(1)) : "@" + UrlEncode(t.handle)) + "/live";
    return "";
}

std::string VideoIdFromLivePage(const std::string& html) {
    size_t p = html.find("rel=\"canonical\" href=\"");
    if (p == std::string::npos) return "";
    p += 22;
    size_t e = html.find('"', p);
    if (e == std::string::npos || e - p > 300) return "";
    std::string url = html.substr(p, e - p);
    if (url.find("watch?v=") == std::string::npos) return "";   // the channel page itself: not live
    return VideoIdFromText(url);
}

WatchInfo ParseYtcfg(const std::string& html) {
    WatchInfo w;
    w.apiKey = StringValue(html, "INNERTUBE_API_KEY");
    w.contextJson = ExtractObjectAfter(html, "\"INNERTUBE_CONTEXT\":");
    w.clientVersion = StringValue(html, "INNERTUBE_CLIENT_VERSION");
    {
        std::string m = "\"INNERTUBE_CONTEXT_CLIENT_NAME\":";
        size_t p = html.find(m);
        if (p != std::string::npos) {
            p += m.size();
            size_t e = p;
            while (e < html.size() && isdigit((unsigned char)html[e])) ++e;
            if (e > p && e - p < 4) w.clientName = html.substr(p, e - p);
        }
    }
    json ctx = json::parse(w.contextJson, nullptr, false);
    if (!ctx.is_object()) return w;
    w.visitorData = Str(Path(ctx, {"client"}), "visitorData");
    if (w.clientVersion.empty()) w.clientVersion = Str(Path(ctx, {"client"}), "clientVersion");
    w.ok = true;
    return w;
}

WatchInfo ParseWatchPage(const std::string& html) {
    WatchInfo w = ParseYtcfg(html);
    if (!w.ok) return w;
    {   // video state, only for the log
        json pr = json::parse(ExtractAssigned(html, "ytInitialPlayerResponse"), nullptr, false);
        const json& vd = Path(pr, {"videoDetails"});
        auto flag = [&](const char* k) { return vd.contains(k) && vd[k].is_boolean() && vd[k].get<bool>(); };
        w.state = "playability=" + Str(Path(pr, {"playabilityStatus"}), "status") + " isLive=" + (flag("isLive") ? "1" : "0") +
                  " isUpcoming=" + (flag("isUpcoming") ? "1" : "0") + " isLiveContent=" + (flag("isLiveContent") ? "1" : "0");
    }
    json data = json::parse(ExtractInitialData(html), nullptr, false);
    const json& chat = Path(data, {"contents", "twoColumnWatchNextResults", "conversationBar", "liveChatRenderer"});
    if (!chat.is_object()) return w;
    w.replay = chat.contains("isReplay") && chat["isReplay"].is_boolean() && chat["isReplay"].get<bool>();
    // prefer "Live chat" (all messages, sub menu item 1) over "Top chat" (filtered, item 0 / default)
    const json& items = Path(chat, {"header", "liveChatHeaderRenderer", "viewSelector", "sortFilterSubMenuRenderer", "subMenuItems"});
    auto add = [&](const std::string& c) {
        if (!c.empty() && std::find(w.candidates.begin(), w.candidates.end(), c) == w.candidates.end()) w.candidates.push_back(c);
    };
    if (items.is_array() && items.size() >= 2) add(Str(Path(items[1], {"continuation", "reloadContinuationData"}), "continuation"));
    if (chat.contains("continuations") && chat["continuations"].is_array() && !chat["continuations"].empty())
        add(Str(Path(chat["continuations"][0], {"reloadContinuationData"}), "continuation"));
    if (items.is_array() && !items.empty()) add(Str(Path(items[0], {"continuation", "reloadContinuationData"}), "continuation"));
    if (!w.candidates.empty()) w.continuation = w.candidates[0];
    w.state += " chatTokens=" + std::to_string(w.candidates.size()) + (w.replay ? " replay" : "");
    return w;
}

ChatPage ParseChat(const std::string& body, uint64_t nowMs) {
    ChatPage p;
    json j = json::parse(body, nullptr, false);
    if (!j.is_object()) return p;
    const json& c = Path(j, {"continuationContents", "liveChatContinuation"});
    if (!c.is_object()) return p;
    p.ok = true;
    if (c.contains("continuations") && c["continuations"].is_array() && !c["continuations"].empty() && c["continuations"][0].is_object()) {
        for (const char* k : {"invalidationContinuationData", "timedContinuationData", "reloadContinuationData"}) {
            const json& d = Path(c["continuations"][0], {k});
            if (!d.is_object()) continue;
            p.continuation = Str(d, "continuation");
            if (d.contains("timeoutMs") && d["timeoutMs"].is_number()) p.timeoutMs = d["timeoutMs"].get<int>();
            break;
        }
    }
    if (!c.contains("actions") || !c["actions"].is_array()) return p;
    for (const json& a : c["actions"]) {
        if (!a.is_object()) continue;
        ChatEvent ev; ev.platform = Platform::YouTube;
        for (const char* k : {"markChatItemAsDeletedAction", "removeChatItemAction"}) {
            const json& d = Path(a, {k});
            if (d.is_object()) { ev.kind = ChatEvent::Kind::DeleteMessage; ev.key = Str(d, "targetItemId"); }
        }
        for (const char* k : {"markChatItemsByAuthorAsDeletedAction", "removeChatItemByAuthorAction"}) {
            const json& d = Path(a, {k});
            if (d.is_object()) { ev.kind = ChatEvent::Kind::DeleteUser; ev.key = Str(d, "externalChannelId"); }
        }
        if (!ev.key.empty()) { p.events.push_back(ev); continue; }
        const json& item = Path(a, {"addChatItemAction", "item"});
        if (!item.is_object()) continue;
        const json* r = nullptr;
        bool paid = false;
        for (const char* k : {"liveChatTextMessageRenderer", "liveChatPaidMessageRenderer", "liveChatPaidStickerRenderer"}) {
            const json& x = Path(item, {k});
            if (x.is_object()) { r = &x; paid = strcmp(k, "liveChatTextMessageRenderer") != 0; break; }
        }
        if (!r) continue;   // memberships, engagement messages, ...
        ChatMessage& m = ev.msg;
        m.platform = Platform::YouTube;
        m.id = Str(*r, "id");
        m.userId = Str(*r, "authorExternalChannelId");
        m.user = Runs(Path(*r, {"authorName"}));
        m.text = Runs(Path(*r, {"message"}));
        if (paid) {
            m.highlight = true;
            std::string amount = Runs(Path(*r, {"purchaseAmountText"}));
            m.text = amount.empty() ? m.text : (m.text.empty() ? amount : amount + " \xC2\xB7 " + m.text);
        }
        const json& badges = Path(*r, {"authorBadges"});
        if (badges.is_array())
            for (const json& b : badges) {
                std::string icon = Str(Path(b, {"liveChatAuthorBadgeRenderer", "icon"}), "iconType");
                if (icon == "OWNER") m.owner = true;
                if (icon == "MODERATOR") m.moderator = true;
            }
        if (m.text.empty()) continue;
        m.receivedMs = nowMs;
        ev.kind = ChatEvent::Kind::Message;
        p.events.push_back(ev);
    }
    return p;
}

std::string PostBody(const std::string& contextJson, const std::string& continuation) {
    json ctx = json::parse(contextJson, nullptr, false);
    if (!ctx.is_object()) ctx = json::object();
    json body = {{"context", ctx}, {"continuation", continuation}};
    return body.dump();
}
}
}

