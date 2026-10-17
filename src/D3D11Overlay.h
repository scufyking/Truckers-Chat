// Trucker Chat
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
// Draws into ETS2's own Direct3D 11 frames. Technique taken over from Realistic Radio (D3D11Overlay.cpp):
//  * IDXGISwapChain::Present/Present1 are redirected through the shared DXGI vtable (found with a throw-away device).
//  * The game's REAL immediate context is taken in the first Present and its OMSetRenderTargets(+AndUAVs) slots are
//    redirected (a dummy device can have a different context class/vtable).
//  * The game's pipeline state is saved and restored by hand (like Dear ImGui). SwapDeviceContextState is NOT used:
//    it can switch the context's vtable and silently remove hooks.
//  * The in-cab screen image is drawn into a render target of the configured size right after the game switched
//    away from it; the game picture and scene-sized targets are skipped.
// Chaining with other plugins (e.g. Realistic Radio): every patched slot stores the pointer that was there before
// (which may be another plugin's hook) and always calls it, so hooks stack in either load order. Calls arriving while
// we forward to the previous pointer (i.e. made by another hook further down the chain) are passed through untouched.
#include "Canvas.h"
#include <atomic>

namespace D3D11Overlay {
bool Install();                  // from the plugin's worker thread (never DllMain / game callbacks); idempotent
void Uninstall();                // stops drawing; the hooks stay (module is pinned)
// top overlay (back buffer)
void SetImage(std::shared_ptr<const Image> img);
void SetAlpha(float a);
void SetPlacement(int anchor, float offsetX, float offsetY);   // anchor 0..5 = TL, TC, TR, BL, BC, BR; offsets in screen heights
long long D3D11Frames();
long long OtherFrames();
int ScreenHeight();
// in-cab screen
void EnableScreenHook(bool on);  // before Install()
void SetScreenImage(std::shared_ptr<const Image> img);
void SetScreenTarget(int w, int h, int format);   // 0,0 = none; format 0 = any
void SetScreenAlpha(float a);
void SetScreenArea(float x, float y, float w, float h, bool flipY, bool flipX);
void SetCollectTargets(bool on);
struct RenderTargetStat { int w, h, format; long long binds; };
std::vector<RenderTargetStat> TakeTargetStats();
bool ScreenHookActive();
long long RenderTargetCalls();
// Realistic Radio detection: draw calls coming from the given module into a render target of the screen size are
// remembered (ID3D11DeviceContext::Draw is hooked only while a module range is set). Returns GetTickCount64() of the
// last such draw, 0 = never.
void SetForeignModule(uintptr_t base, uintptr_t end);
unsigned long long ForeignScreenDrawAt();
}
