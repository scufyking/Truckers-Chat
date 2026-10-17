// Trucker Chat
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
// Chat sources run on their own threads and hand events to the display thread through ChatHub.
#include "Common.h"
#include "ChatTypes.h"
#include "Config.h"
#include <deque>

class ChatHub {
public:
    void Push(ChatEvent e) {
        std::lock_guard<std::mutex> l(m_);
        if (q_.size() >= 500) q_.pop_front();   // the display thread stalled: drop the oldest
        q_.push_back(std::move(e));
    }
    std::vector<ChatEvent> Take() {
        std::lock_guard<std::mutex> l(m_);
        std::vector<ChatEvent> v(std::make_move_iterator(q_.begin()), std::make_move_iterator(q_.end()));
        q_.clear();
        return v;
    }
private:
    std::mutex m_;
    std::deque<ChatEvent> q_;
};

class ChatSource {
public:
    virtual ~ChatSource() = default;
    virtual const char* Name() const = 0;
    bool Start(ChatHub* hub);     // starts the worker thread
    void RequestStop();           // any thread, returns immediately
    bool Stop();                  // any thread; waits at most 2 s. false: thread still running, do NOT delete the object
    virtual void Tick() {}        // called by the display thread every ~50 ms (keep-alive, watchdog)
protected:
    virtual void Run() = 0;       // worker thread body; must return soon after stop_ is set
    virtual void Abort() {}       // interrupts a blocking network call
    bool Sleep(DWORD ms);         // interruptible sleep (stop or Kick()); false = stop requested
    void Kick() { if (kick_) SetEvent(kick_); }   // ends the current Sleep early (e.g. new settings)
    void Status(const std::string& text);   // log + optional status card
    void Emit(ChatEvent e) { if (hub_) hub_->Push(std::move(e)); }
    // exponential reconnect delay: 2, 4, 8, ... 60 s, with jitter; Reset() after a stable connection
    DWORD NextBackoff();
    void ResetBackoff() { backoff_ = 0; }
    std::atomic<bool> stop_{false};
    Platform platform_ = Platform::System;
private:
    static DWORD ThreadProc(void* self);
    ChatHub* hub_ = nullptr;
    HANDLE thread_ = nullptr, wake_ = nullptr, kick_ = nullptr;
    int backoff_ = 0;
};
