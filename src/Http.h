// Trucker Chat
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
// Small synchronous WinHTTP wrappers (HTTPS GET and a secure WebSocket client). Windows' own TLS stack and proxy
// settings are used; nothing is bundled. Each object is used by one worker thread; Abort() may be called from any
// thread and makes a blocking call return (WinHttpCloseHandle cancels an in-progress request).
#include "Common.h"
#include <winhttp.h>

namespace Http {
struct Response { int status = 0; std::string body; DWORD error = 0; };
using Headers = std::vector<std::pair<std::wstring, std::wstring>>;

class Client {
public:
    Client();
    ~Client();
    Response Get(const std::wstring& host, const std::string& pathAndQuery, const Headers& headers, int timeoutMs = 15000);
    Response Post(const std::wstring& host, const std::string& pathAndQuery, const Headers& headers, const std::string& body, int timeoutMs = 15000);
    Response Send(const wchar_t* method, const std::wstring& host, const std::string& pathAndQuery, const Headers& headers,
                  const std::string& body, int timeoutMs);
    void Abort();
    bool Aborted() const { return aborted_; }
private:
    HINTERNET session_ = nullptr;
    std::mutex m_;
    HINTERNET req_ = nullptr;
    std::atomic<bool> aborted_{false};
};

class WebSocket {
public:
    WebSocket();
    ~WebSocket();
    bool Connect(const std::wstring& host, INTERNET_PORT port, const std::wstring& path, DWORD* error);
    bool SendText(const std::string& text);
    enum class Result { Message, Closed, Error };
    Result Receive(std::string& message);   // blocks until one complete text message
    void Abort();                           // any thread
    void Close();                           // worker thread
private:
    HINTERNET session_ = nullptr, connect_ = nullptr, request_ = nullptr;
    std::mutex m_, sendMtx_;
    HINTERNET ws_ = nullptr;
    std::atomic<bool> aborted_{false};
};
std::string ErrorText(DWORD error);
}
