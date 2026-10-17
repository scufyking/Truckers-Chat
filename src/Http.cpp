// Trucker Chat
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "Http.h"
#include "Logger.h"
#include <cstdio>

namespace Http {
namespace {
const wchar_t* kAgent = L"TruckerChat-ETS2-Plugin/1.0";
HINTERNET OpenSession() {
    // automatic proxy (Windows 8.1+) follows the system settings incl. PAC scripts; ETS2 needs Windows 10 anyway
    HINTERNET s = WinHttpOpen(kAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) s = WinHttpOpen(kAgent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (s) {
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | 0x00002000 /* WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 */;
        if (!WinHttpSetOption(s, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols))) {
            protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;   // older Windows 10 builds without TLS 1.3 in WinHTTP
            WinHttpSetOption(s, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
        }
    }
    return s;
}
}

std::string ErrorText(DWORD e) {
    switch (e) {
    case ERROR_WINHTTP_TIMEOUT: return "timeout";
    case ERROR_WINHTTP_NAME_NOT_RESOLVED: return "name not resolved (no internet?)";
    case ERROR_WINHTTP_CANNOT_CONNECT: return "cannot connect";
    case ERROR_WINHTTP_CONNECTION_ERROR: return "connection reset";
    case ERROR_WINHTTP_OPERATION_CANCELLED: return "cancelled";
    case ERROR_WINHTTP_SECURE_FAILURE: return "TLS/certificate error";
    default: { char b[32]; snprintf(b, sizeof(b), "error %lu", e); return b; }
    }
}

Client::Client() { session_ = OpenSession(); }
Client::~Client() { Abort(); if (session_) WinHttpCloseHandle(session_); }

void Client::Abort() {
    aborted_ = true;
    std::lock_guard<std::mutex> l(m_);
    if (req_) { WinHttpCloseHandle(req_); req_ = nullptr; }
}

Response Client::Get(const std::wstring& host, const std::string& path, const Headers& headers, int timeoutMs) {
    return Send(L"GET", host, path, headers, "", timeoutMs);
}
Response Client::Post(const std::wstring& host, const std::string& path, const Headers& headers, const std::string& body, int timeoutMs) {
    return Send(L"POST", host, path, headers, body, timeoutMs);
}

Response Client::Send(const wchar_t* method, const std::wstring& host, const std::string& path, const Headers& headers,
                      const std::string& body, int timeoutMs) {
    Response r;
    if (!session_ || aborted_) { r.error = ERROR_WINHTTP_OPERATION_CANCELLED; return r; }
    WinHttpSetTimeouts(session_, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
    HINTERNET con = WinHttpConnect(session_, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!con) { r.error = GetLastError(); return r; }
    HINTERNET req = WinHttpOpenRequest(con, method, Utf8ToWide(path).c_str(), nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!req) { r.error = GetLastError(); WinHttpCloseHandle(con); return r; }
    { std::lock_guard<std::mutex> l(m_); req_ = req; }
    auto finish = [&]() {
        std::lock_guard<std::mutex> l(m_);
        if (req_) { WinHttpCloseHandle(req_); req_ = nullptr; }
        WinHttpCloseHandle(con);
    };
    std::wstring hdr;
    for (auto& h : headers) hdr += h.first + L": " + h.second + L"\r\n";
    bool hasAccept = false;
    for (auto& h : headers) if (_wcsicmp(h.first.c_str(), L"Accept") == 0) hasAccept = true;
    if (!hasAccept) hdr += L"Accept: application/json\r\n";
    DWORD decompress = WINHTTP_DECOMPRESSION_FLAG_ALL;   // Windows 8.1+
    WinHttpSetOption(req, WINHTTP_OPTION_DECOMPRESSION, &decompress, sizeof(decompress));
    if (!WinHttpSendRequest(req, hdr.c_str(), (DWORD)-1L, body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(),
                            (DWORD)body.size(), (DWORD)body.size(), 0) || !WinHttpReceiveResponse(req, nullptr)) {
        r.error = GetLastError(); finish(); return r;
    }
    DWORD status = 0, sz = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
    r.status = (int)status;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail)) { r.error = GetLastError(); break; }
        if (!avail) break;
        size_t old = r.body.size();
        if (old + avail > 8 * 1024 * 1024) { r.error = ERROR_NOT_ENOUGH_MEMORY; break; }   // no sane reply is that big
        r.body.resize(old + avail);
        DWORD got = 0;
        if (!WinHttpReadData(req, &r.body[old], avail, &got)) { r.error = GetLastError(); r.body.resize(old); break; }
        r.body.resize(old + got);
    }
    finish();
    return r;
}

// ---------------------------------------------------------------------------------------------
WebSocket::WebSocket() {}
WebSocket::~WebSocket() { Abort(); Close(); }

bool WebSocket::Connect(const std::wstring& host, INTERNET_PORT port, const std::wstring& path, DWORD* error) {
    Close();
    aborted_ = false;
    auto fail = [&]() { if (error) *error = GetLastError(); Close(); return false; };
    session_ = OpenSession();
    if (!session_) return fail();
    WinHttpSetTimeouts(session_, 15000, 15000, 15000, 15000);
    connect_ = WinHttpConnect(session_, host.c_str(), port, 0);
    if (!connect_) return fail();
    request_ = WinHttpOpenRequest(connect_, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request_) return fail();
    if (!WinHttpSetOption(request_, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)) return fail();
    if (!WinHttpSendRequest(request_, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0)) return fail();
    if (!WinHttpReceiveResponse(request_, nullptr)) return fail();
    DWORD status = 0, sz = sizeof(status);
    WinHttpQueryHeaders(request_, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
    if (status != 101) { SetLastError(ERROR_WINHTTP_INVALID_SERVER_RESPONSE); return fail(); }
    HINTERNET ws = WinHttpWebSocketCompleteUpgrade(request_, 0);
    if (!ws) return fail();
    WinHttpCloseHandle(request_); request_ = nullptr;
    std::lock_guard<std::mutex> l(m_);
    if (aborted_) { WinHttpCloseHandle(ws); return false; }
    ws_ = ws;
    return true;
}

bool WebSocket::SendText(const std::string& text) {
    std::lock_guard<std::mutex> s(sendMtx_);
    HINTERNET ws;
    { std::lock_guard<std::mutex> l(m_); ws = ws_; }
    if (!ws || aborted_) return false;
    return WinHttpWebSocketSend(ws, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, (PVOID)text.data(), (DWORD)text.size()) == NO_ERROR;
}

WebSocket::Result WebSocket::Receive(std::string& msg) {
    msg.clear();
    char buf[8192];
    for (;;) {
        HINTERNET ws;
        { std::lock_guard<std::mutex> l(m_); ws = ws_; }
        if (!ws || aborted_) return Result::Closed;
        DWORD got = 0; WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
        DWORD rc = WinHttpWebSocketReceive(ws, buf, sizeof(buf), &got, &type);
        if (rc != NO_ERROR) return aborted_ ? Result::Closed : Result::Error;
        if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) return Result::Closed;
        if (msg.size() + got > 1024 * 1024) return Result::Error;
        msg.append(buf, got);
        if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE || type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) return Result::Message;
        // *_FRAGMENT_BUFFER_TYPE: keep reading the rest of the message
    }
}

void WebSocket::Abort() {
    aborted_ = true;
    std::lock_guard<std::mutex> l(m_);
    if (ws_) { WinHttpCloseHandle(ws_); ws_ = nullptr; }
}

void WebSocket::Close() {
    {
        std::lock_guard<std::mutex> l(m_);
        if (ws_) {
            if (!aborted_) WinHttpWebSocketClose(ws_, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
            WinHttpCloseHandle(ws_); ws_ = nullptr;
        }
    }
    if (request_) { WinHttpCloseHandle(request_); request_ = nullptr; }
    if (connect_) { WinHttpCloseHandle(connect_); connect_ = nullptr; }
    if (session_) { WinHttpCloseHandle(session_); session_ = nullptr; }
}
}
