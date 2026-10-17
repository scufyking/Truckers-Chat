// Trucker Chat
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "TwitchProtocol.h"
#include "TextUtil.h"
#include <cstdlib>

std::string UnescapeIrcTag(const std::string& v) {
    std::string o;
    o.reserve(v.size());
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] != '\\') { o += v[i]; continue; }
        if (++i >= v.size()) break;
        switch (v[i]) {
        case ':': o += ';'; break;
        case 's': o += ' '; break;
        case '\\': o += '\\'; break;
        case 'r': o += '\r'; break;
        case 'n': o += '\n'; break;
        default: o += v[i]; break;
        }
    }
    return o;
}

bool ParseIrcLine(const std::string& lineIn, IrcMessage& out) {
    out = IrcMessage();
    std::string line = lineIn;
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    size_t p = 0, n = line.size();
    if (n == 0) return false;
    if (line[0] == '@') {
        size_t e = line.find(' ');
        if (e == std::string::npos) return false;
        std::string tags = line.substr(1, e - 1);
        size_t s = 0;
        while (s <= tags.size()) {
            size_t semi = tags.find(';', s);
            if (semi == std::string::npos) semi = tags.size();
            std::string kv = tags.substr(s, semi - s);
            size_t eq = kv.find('=');
            if (!kv.empty()) {
                if (eq == std::string::npos) out.tags[kv] = "";
                else out.tags[kv.substr(0, eq)] = UnescapeIrcTag(kv.substr(eq + 1));
            }
            s = semi + 1;
        }
        p = e + 1;
    }
    while (p < n && line[p] == ' ') ++p;
    if (p < n && line[p] == ':') {
        size_t e = line.find(' ', p);
        if (e == std::string::npos) return false;
        out.prefix = line.substr(p + 1, e - p - 1);
        p = e + 1;
    }
    while (p < n && line[p] == ' ') ++p;
    size_t e = line.find(' ', p);
    out.command = line.substr(p, e == std::string::npos ? std::string::npos : e - p);
    if (out.command.empty()) return false;
    p = (e == std::string::npos) ? n : e + 1;
    while (p < n) {
        while (p < n && line[p] == ' ') ++p;
        if (p >= n) break;
        if (line[p] == ':') { out.params.push_back(line.substr(p + 1)); break; }
        e = line.find(' ', p);
        out.params.push_back(line.substr(p, e == std::string::npos ? std::string::npos : e - p));
        p = (e == std::string::npos) ? n : e + 1;
    }
    return true;
}

namespace {
std::string Tag(const IrcMessage& m, const char* k) {
    auto it = m.tags.find(k);
    return it == m.tags.end() ? std::string() : it->second;
}
std::string NickFromPrefix(const std::string& prefix) {
    size_t i = prefix.find('!');
    return i == std::string::npos ? prefix : prefix.substr(0, i);
}
}

std::string NormalizeTwitchChannel(const std::string& in) {
    std::string s = in;
    size_t slash = s.find_last_of('/');
    if (s.find("twitch.tv") != std::string::npos && slash != std::string::npos) {
        if (slash + 1 == s.size()) { s.pop_back(); slash = s.find_last_of('/'); }
        s = s.substr(slash + 1);
    }
    std::string o;
    for (char c : s) {   // Twitch logins: a-z, 0-9, underscore
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') o += c;
    }
    return o;
}

TwitchResult InterpretTwitch(const IrcMessage& m) {
    TwitchResult r;
    const std::string& c = m.command;
    if (c == "PING") { r.kind = TwitchResult::Kind::Ping; r.text = m.params.empty() ? "tmi.twitch.tv" : m.params.back(); return r; }
    if (c == "RECONNECT") { r.kind = TwitchResult::Kind::Reconnect; return r; }
    if (c == "001") { r.kind = TwitchResult::Kind::Welcome; return r; }
    if (c == "JOIN") { r.kind = TwitchResult::Kind::Joined; return r; }
    if (c == "NOTICE") {
        r.text = m.params.empty() ? "" : m.params.back();
        r.kind = (r.text.find("authentication failed") != std::string::npos || r.text.find("Improperly formatted auth") != std::string::npos)
                     ? TwitchResult::Kind::AuthFailed : TwitchResult::Kind::Notice;
        return r;
    }
    if (c == "CLEARMSG") {   // a moderator deleted one message
        r.kind = TwitchResult::Kind::Event;
        r.event.kind = ChatEvent::Kind::DeleteMessage; r.event.platform = Platform::Twitch; r.event.key = Tag(m, "target-msg-id");
        if (r.event.key.empty()) r.kind = TwitchResult::Kind::None;
        return r;
    }
    if (c == "CLEARCHAT") {   // timeout/ban (with user) or whole chat cleared
        r.kind = TwitchResult::Kind::Event; r.event.platform = Platform::Twitch;
        std::string uid = Tag(m, "target-user-id");
        if (m.params.size() >= 2) { r.event.kind = ChatEvent::Kind::DeleteUser; r.event.key = uid.empty() ? NormalizeTwitchChannel(m.params[1]) : uid; }
        else r.event.kind = ChatEvent::Kind::ClearAll;
        return r;
    }
    if (c != "PRIVMSG" || m.params.size() < 2) return r;
    ChatMessage& msg = r.event.msg;
    msg.platform = Platform::Twitch;
    msg.id = Tag(m, "id");
    msg.userId = Tag(m, "user-id");
    msg.login = NickFromPrefix(m.prefix);
    msg.user = Tag(m, "display-name");
    if (msg.user.empty()) msg.user = msg.login;
    std::string text = m.params.back();
    const std::string act = "\x01" "ACTION ";
    if (text.compare(0, act.size(), act) == 0) {
        msg.action = true;
        text = text.substr(act.size());
        if (!text.empty() && text.back() == '\x01') text.pop_back();
    }
    msg.text = text;
    std::string col = Tag(m, "color");
    if (col.size() == 7 && col[0] == '#') { msg.color = (uint32_t)strtoul(col.c_str() + 1, nullptr, 16); msg.hasColor = true; }
    std::string badges = Tag(m, "badges");
    msg.owner = badges.find("broadcaster/") != std::string::npos;
    msg.moderator = Tag(m, "mod") == "1";
    msg.highlight = Tag(m, "msg-id") == "highlighted-message";
    r.event.kind = ChatEvent::Kind::Message;
    r.event.platform = Platform::Twitch;
    r.kind = TwitchResult::Kind::Event;
    return r;
}
