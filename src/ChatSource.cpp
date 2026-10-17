// Trucker Chat
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "ChatSource.h"
#include "Logger.h"
#include <algorithm>

bool ChatSource::Start(ChatHub* hub) {
    hub_ = hub;
    stop_ = false;
    wake_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    kick_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);   // auto-reset
    thread_ = StartPinnedThread(&ChatSource::ThreadProc, this);
    if (!thread_) Log::Write("%s: could not start thread", Name());
    return thread_ != nullptr;
}

DWORD ChatSource::ThreadProc(void* p) {
    auto* self = (ChatSource*)p;
    self->Run();
    return 0;
}

void ChatSource::RequestStop() {
    stop_ = true;
    if (wake_) SetEvent(wake_);
    Abort();
}

bool ChatSource::Stop() {
    RequestStop();
    if (!thread_) return true;
    bool done = WaitForSingleObject(thread_, 2000) == WAIT_OBJECT_0;
    if (!done) { Log::Write("%s: worker thread still busy - left running", Name()); return false; }
    CloseHandle(thread_); thread_ = nullptr;
    if (wake_) { CloseHandle(wake_); wake_ = nullptr; }
    if (kick_) { CloseHandle(kick_); kick_ = nullptr; }
    return true;
}

bool ChatSource::Sleep(DWORD ms) {
    if (stop_) return false;
    HANDLE h[2] = {wake_, kick_};
    WaitForMultipleObjects(2, h, FALSE, ms);
    return !stop_;
}

void ChatSource::Status(const std::string& text) {
    Log::Write("%s: %s", Name(), text.c_str());
    ChatEvent e;
    e.kind = ChatEvent::Kind::Status;
    e.platform = platform_;
    e.msg.platform = Platform::System;
    e.msg.user = Name();
    e.msg.text = text;
    e.msg.receivedMs = NowMs();
    Emit(std::move(e));
}

DWORD ChatSource::NextBackoff() {
    backoff_ = std::min(backoff_ + 1, 6);
    DWORD base = std::min<DWORD>(60000, 1000u << backoff_);   // 2 s .. 60 s
    return base + (DWORD)(GetTickCount64() % 1000);
}
