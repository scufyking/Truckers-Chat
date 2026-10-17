// Trucker Chat
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
// Message filter (platform independent): commands, bots, blocked words, links.
#include "ChatTypes.h"
#include <string>
#include <vector>

struct FilterConfig {
    bool hideCommands = true;
    std::vector<std::string> commandPrefixes{"!"};
    std::vector<std::string> ignoreUsers;        // login or display name, case-insensitive
    std::vector<std::string> blockedWords;       // case-insensitive substrings
    std::string blockedWordsAction = "hide";     // hide | mask
    std::string links = "mask";                  // show | mask | hide
    std::string linkText = "[Link]";
    bool showSuperChats = true;
};

class MessageFilter {
public:
    void Configure(const FilterConfig& cfg);
    // Returns false if the message must not be shown; may rewrite msg.text (masking). reason is set for the log.
    bool Apply(ChatMessage& msg, std::string* reason = nullptr) const;
private:
    FilterConfig cfg_;
    std::vector<std::u32string> words_, users_;
};
