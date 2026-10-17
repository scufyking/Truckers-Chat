// Trucker Chat
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
// Twitch chat (IRC protocol with IRCv3 tags) - parsing only, platform independent.
// Reference: https://dev.twitch.tv/docs/chat/irc/ and https://dev.twitch.tv/docs/chat/irc/#twitch-irc-capabilities
#include "ChatTypes.h"
#include <map>
#include <string>
#include <vector>

struct IrcMessage {
    std::map<std::string, std::string> tags;   // values unescaped
    std::string prefix;                        // without ':'
    std::string command;                       // PRIVMSG, PING, 001, ...
    std::vector<std::string> params;           // the trailing parameter is the last entry
};

bool ParseIrcLine(const std::string& line, IrcMessage& out);
std::string UnescapeIrcTag(const std::string& v);

struct TwitchResult {
    enum class Kind { None, Event, Ping, Reconnect, AuthFailed, Joined, Welcome, Notice } kind = Kind::None;
    ChatEvent event;        // Kind::Event
    std::string text;       // Ping: argument to echo; Notice: notice text
};
TwitchResult InterpretTwitch(const IrcMessage& m);
// Normalises a configured channel ("#Name", "https://twitch.tv/Name", "Name") to the lowercase login.
std::string NormalizeTwitchChannel(const std::string& s);
