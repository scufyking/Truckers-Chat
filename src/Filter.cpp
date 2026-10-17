// Trucker Chat
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "Filter.h"
#include "TextUtil.h"

void MessageFilter::Configure(const FilterConfig& cfg) {
    cfg_ = cfg;
    words_.clear(); users_.clear();
    for (auto& w : cfg.blockedWords) { auto l = Lower(DecodeUtf8(w)); if (!l.empty()) words_.push_back(l); }
    for (auto& u : cfg.ignoreUsers) { auto l = Lower(DecodeUtf8(u)); if (!l.empty()) users_.push_back(l); }
}

namespace {
size_t FindLink(const std::u32string& lower, size_t from) {
    size_t best = std::u32string::npos;
    for (const char32_t* p : {U"https://", U"http://", U"www."}) {
        size_t i = lower.find(p, from);
        while (i != std::u32string::npos && i > 0 && lower[i - 1] != U' ' && p[0] == U'w') i = lower.find(p, i + 1);   // "www." only at word start
        if (i < best) best = i;
    }
    return best;
}
}

bool MessageFilter::Apply(ChatMessage& msg, std::string* reason) const {
    auto why = [&](const char* r) { if (reason) *reason = r; return false; };
    if (msg.platform == Platform::System) return true;
    if (msg.highlight && !cfg_.showSuperChats) return why("super chat");
    if (!users_.empty()) {
        auto login = Lower(DecodeUtf8(msg.login)), name = Lower(DecodeUtf8(msg.user));
        for (auto& u : users_) if (u == login || u == name) return why("ignored user");
    }
    std::u32string text = DecodeUtf8(msg.text);
    size_t start = text.find_first_not_of(U" \t");
    if (cfg_.hideCommands && start != std::u32string::npos) {
        for (auto& p : cfg_.commandPrefixes) {
            auto pre = DecodeUtf8(p);
            if (!pre.empty() && text.compare(start, pre.size(), pre) == 0) return why("command");
        }
    }
    std::u32string lower = Lower(text);
    bool changed = false;
    for (auto& w : words_) {
        size_t i = lower.find(w);
        if (i == std::u32string::npos) continue;
        if (cfg_.blockedWordsAction != "mask") return why("blocked word");
        while (i != std::u32string::npos) {
            for (size_t k = 0; k < w.size(); ++k) { text[i + k] = U'*'; lower[i + k] = U'*'; }
            i = lower.find(w, i + w.size());
        }
        changed = true;
    }
    if (cfg_.links != "show") {
        size_t i = FindLink(lower, 0);
        if (i != std::u32string::npos && cfg_.links == "hide") return why("link");
        auto repl = DecodeUtf8(cfg_.linkText);
        while (i != std::u32string::npos) {
            size_t e = lower.find(U' ', i);
            if (e == std::u32string::npos) e = lower.size();
            text.replace(i, e - i, repl); lower.replace(i, e - i, repl);
            changed = true;
            i = FindLink(lower, i + repl.size());
        }
    }
    if (changed) msg.text = EncodeUtf8(text);
    if (text.find_first_not_of(U" *") == std::u32string::npos) return why("empty");
    return true;
}
