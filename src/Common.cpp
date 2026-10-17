// Trucker Chat
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "Common.h"
#include "Logger.h"
#include <cstdio>
#include <cstdarg>

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
HMODULE SelfModule() {
    HMODULE m = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&SelfModule, &m);
    return m;
}
std::wstring PluginDataDir() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(SelfModule(), buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    std::wstring p(buf, n);
    size_t slash = p.find_last_of(L"\\/");
    p = (slash == std::wstring::npos) ? L"" : p.substr(0, slash + 1);
    return p + Names::kDataFolder + L"\\";
}

namespace {
struct ThreadStart { DWORD (*fn)(void*); void* arg; };
DWORD WINAPI PinnedThreadProc(LPVOID p) {
    ThreadStart s = *(ThreadStart*)p;
    delete (ThreadStart*)p;
    DWORD rc = s.fn(s.arg);
    FreeLibraryAndExitThread(SelfModule(), rc);   // drops the reference taken in StartPinnedThread
}
}
HANDLE StartPinnedThread(DWORD (*fn)(void*), void* arg) {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)&PinnedThreadProc, &self);   // +1 reference
    auto* s = new ThreadStart{fn, arg};
    HANDLE t = CreateThread(nullptr, 0, PinnedThreadProc, s, 0, nullptr);
    if (!t) { delete s; if (self) FreeLibrary(self); }
    return t;
}

// ---------------------------------------------------------------------------------------------
namespace {
std::mutex g_mtx;
FILE* g_file = nullptr;
std::vector<std::string> g_secrets;
}
namespace Log {
void Init(const std::wstring& path) {
    std::lock_guard<std::mutex> l(g_mtx);
    if (!g_file) g_file = _wfopen(path.c_str(), L"w");
}
void AddSecret(const std::string& s) {
    if (s.size() < 4) return;
    std::lock_guard<std::mutex> l(g_mtx);
    g_secrets.push_back(s);
}
void Write(const char* fmt, ...) {
    char buf[2048];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    std::string msg = buf;
    SYSTEMTIME st; GetLocalTime(&st);
    std::lock_guard<std::mutex> l(g_mtx);
    for (auto& s : g_secrets) for (size_t i; (i = msg.find(s)) != std::string::npos;) msg.replace(i, s.size(), "***");
    for (auto& c : msg) if (c == '\r' || c == '\n') c = ' ';
    if (g_file) {
        fprintf(g_file, "[%02d:%02d:%02d.%03d] %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, msg.c_str());
        fflush(g_file);
    }
    OutputDebugStringA((std::string(Names::kLogTag) + " " + msg + "\n").c_str());
}
void Shutdown() {
    std::lock_guard<std::mutex> l(g_mtx);
    if (g_file) { fclose(g_file); g_file = nullptr; }
}
}
