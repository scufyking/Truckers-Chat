// Trucker Chat
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>
#include <cstdint>

std::wstring Utf8ToWide(const std::string& s);
std::string WideToUtf8(const std::wstring& s);
std::wstring PluginDataDir();      // <folder of the DLL>\trucker-chat\  (with trailing backslash)
HMODULE SelfModule();
inline uint64_t NowMs() { return GetTickCount64(); }

// Starts a thread that keeps this DLL loaded while it runs (the game may unload plugins while threads still
// finish their last network call). The thread function must not touch the module after returning.
HANDLE StartPinnedThread(DWORD (*fn)(void*), void* arg);

// Unique names of this plugin. Everything differs from Realistic Radio so both run side by side in one game process.
#ifndef TC_VERSION
#define TC_VERSION "0.1.1 Alpha"
#endif
#define TC_WIDEN2(x) L##x
#define TC_WIDEN(x) TC_WIDEN2(x)

namespace Names {
constexpr const wchar_t* kDataFolder = L"trucker-chat";
constexpr const wchar_t* kLogFile = L"trucker-chat.log";
constexpr const wchar_t* kConfigFile = L"trucker-chat.json";
constexpr const wchar_t* kMutexPrefix = L"Local\\TruckerChatPlugin-";
constexpr const wchar_t* kDummyWindowClass = L"TruckerChatDummyD3D";
constexpr const wchar_t* kOverlayWindowClass = L"TruckerChatOverlay";
constexpr const wchar_t* kMenuWindowClass = L"TruckerChatMenu";
constexpr const char* kLogTag = "[trucker-chat]";
}
