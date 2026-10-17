// Trucker Chat
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
#include <string>

// Data files embedded in trucker-chat.dll as RCDATA (see resources.rc.in); same principle as Realistic Radio.
enum EmbeddedId { kResFontRegular = 201, kResFontBold = 202, kResFontLicense = 203, kResDefaultConfig = 204 };

// Writes fonts, font licence and trucker-chat.example.json into dir. Only rewrites them when the embedded data
// changed (data-version.txt). The user's trucker-chat.json is never overwritten (Config::Load creates it once).
bool ExtractEmbeddedData(const std::wstring& dir);
std::string EmbeddedText(int id);
