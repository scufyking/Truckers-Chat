// Trucker Chat
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "EmbeddedData.h"
#include "Common.h"
#include "Logger.h"
#include <cstdio>

namespace {
struct Item { int id; const wchar_t* path; };
const Item kItems[] = {
    {kResFontRegular, L"fonts\\Inter-Regular.otf"},
    {kResFontBold, L"fonts\\Inter-Bold.otf"},
    {kResFontLicense, L"fonts\\OFL.txt"},
    {kResDefaultConfig, L"trucker-chat.example.json"},
};

bool Resource(int id, const void*& data, DWORD& size) {
    HMODULE m = SelfModule();
    HRSRC r = FindResourceW(m, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10) /* RT_RCDATA */);
    if (!r) return false;
    HGLOBAL g = LoadResource(m, r);
    if (!g) return false;
    data = LockResource(g); size = SizeofResource(m, r);
    return data && size;
}
unsigned long long Fnv1a(const void* p, size_t n, unsigned long long h) {
    const unsigned char* b = (const unsigned char*)p;
    for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ULL; }
    return h;
}
bool WriteFileAtomic(const std::wstring& path, const void* data, DWORD size) {
    std::wstring tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(h, data, size, &written, nullptr) && written == size;
    CloseHandle(h);
    if (!ok || !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) { DeleteFileW(tmp.c_str()); return false; }
    return true;
}
std::string ReadSmall(const std::wstring& path) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return "";
    char buf[64] = {0}; size_t n = fread(buf, 1, sizeof(buf) - 1, f); fclose(f);
    return std::string(buf, n);
}
}

std::string EmbeddedText(int id) {
    const void* d; DWORD n;
    if (!Resource(id, d, n)) return "";
    return std::string((const char*)d, n);
}

bool ExtractEmbeddedData(const std::wstring& dir) {
    CreateDirectoryW(dir.c_str(), nullptr);
    CreateDirectoryW((dir + L"fonts").c_str(), nullptr);
    unsigned long long h = 1469598103934665603ULL;
    for (const Item& it : kItems) {
        const void* d; DWORD n;
        if (!Resource(it.id, d, n)) { Log::Write("Embedded data %d missing in DLL", it.id); return false; }
        h = Fnv1a(d, n, h);
    }
    char ver[32]; snprintf(ver, sizeof(ver), "%016llx", h);
    std::wstring verFile = dir + L"data-version.txt";
    bool upToDate = ReadSmall(verFile) == ver;
    int written = 0;
    for (const Item& it : kItems) {
        std::wstring path = dir + it.path;
        if (upToDate && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) continue;
        const void* d; DWORD n; Resource(it.id, d, n);
        if (WriteFileAtomic(path, d, n)) ++written;
        else Log::Write("Could not write %s (error %lu) - a running game may still use the old font file", WideToUtf8(path).c_str(), GetLastError());
    }
    if (!upToDate) WriteFileAtomic(verFile, ver, (DWORD)strlen(ver));
    if (written) Log::Write("Data files written from the DLL: %d (data version %s)", written, ver);
    return true;
}
