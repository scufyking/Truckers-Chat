// Trucker Chat
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
#include <string>
namespace Log {
void Init(const std::wstring& path);
void Write(const char* fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;
// Registers a secret (API key, OAuth token). Every log line is scrubbed of all registered secrets,
// so even an accidental Log::Write of a URL or server reply cannot leak them.
void AddSecret(const std::string& secret);
void Shutdown();
}
