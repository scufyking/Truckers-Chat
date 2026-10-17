// Trucker Chat
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
// Hook technique taken over from Realistic Radio (same author). See D3D11Overlay.h for the overview.
#include "D3D11Overlay.h"
#include "Common.h"
#include "Logger.h"
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <d3dcommon.h>
#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#define TC_RETURN_ADDRESS() ((uintptr_t)_ReturnAddress())
#else
#define TC_RETURN_ADDRESS() ((uintptr_t)__builtin_return_address(0))
#endif

namespace {
// vtable slots (fixed by the COM interface declarations in dxgi.h / d3d11.h):
//   IDXGISwapChain:      8 Present;  IDXGISwapChain1: 22 Present1
//   ID3D11DeviceContext: 13 Draw; 33 OMSetRenderTargets; 34 OMSetRenderTargetsAndUnorderedAccessViews
constexpr int kPresent = 8, kPresent1 = 22, kDraw = 13, kOMSetRT = 33, kOMSetRTUAV = 34;

typedef HRESULT(STDMETHODCALLTYPE* PresentFn)(IDXGISwapChain*, UINT, UINT);
typedef HRESULT(STDMETHODCALLTYPE* Present1Fn)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
typedef HRESULT(WINAPI* D3DCompileFn)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
typedef void(STDMETHODCALLTYPE* DrawFn)(ID3D11DeviceContext*, UINT, UINT);
typedef void(STDMETHODCALLTYPE* OMSetRTFn)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*);
typedef void(STDMETHODCALLTYPE* OMSetRTUAVFn)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*,
                                              UINT, UINT, ID3D11UnorderedAccessView* const*, const UINT*);

void** g_vtable = nullptr;
PresentFn g_origPresent = nullptr;
Present1Fn g_origPresent1 = nullptr;
bool g_hasPresent1 = false;
std::atomic<bool> g_enabled{false};
std::atomic<float> g_alpha{0.f};
std::atomic<int> g_anchor{1};
std::atomic<float> g_offX{0.f}, g_offY{0.02f};
std::atomic<long long> g_d3d11Frames{0}, g_otherFrames{0};
std::atomic<int> g_screenH{0}, g_bbW{0};
std::vector<char> g_vsCode, g_psCode;
std::mutex g_imgMtx;
std::shared_ptr<const Image> g_img;
std::atomic<unsigned> g_imgVersion{0};
thread_local bool t_inHook = false;    // we are drawing: ignore our own state changes
thread_local int t_forwarding = 0;     // we are inside a call to the previous pointer: calls now come from further down the chain

// one entry per context vtable we patched
struct VtHook { void** vt; OMSetRTFn rt; OMSetRTUAVFn rtuav; DrawFn draw; };
VtHook g_vts[8]; std::atomic<int> g_nVts{0};
inline const VtHook* FindVt(ID3D11DeviceContext* c) {
    void** vt = *(void***)c; int n = g_nVts.load();
    for (int i = 0; i < n; ++i) if (g_vts[i].vt == vt) return &g_vts[i];
    return n ? &g_vts[0] : nullptr;
}
bool g_wantScreenHook = false;
std::atomic<bool> g_screenHooked{false};
std::atomic<float> g_scrAlpha{0.f};
std::atomic<int> g_scrW{0}, g_scrH{0}, g_scrFmt{0};
std::atomic<float> g_areaX{0}, g_areaY{0}, g_areaW{1}, g_areaH{1};
std::atomic<bool> g_flipY{false}, g_flipX{false};
std::atomic<bool> g_collect{false};
std::atomic<long long> g_rtCalls{0};
std::mutex g_scrImgMtx;
std::shared_ptr<const Image> g_scrImg;
std::atomic<unsigned> g_scrImgVersion{0};
std::mutex g_statMtx;
struct StatEntry { int w, h, fmt; long long binds; };
std::map<long long, StatEntry> g_stats;
// foreign module (Realistic Radio) detection
std::atomic<uintptr_t> g_foreignLo{0}, g_foreignHi{0};
std::atomic<unsigned long long> g_foreignDrawAt{0};
// render-thread only
ID3D11RenderTargetView* g_curRtv = nullptr;
int g_curW = 0, g_curH = 0;
bool g_curMatch = false;

struct Res {
    ID3D11Device* dev = nullptr;   // identity only, not AddRef'd
    ID3D11VertexShader* vs = nullptr; ID3D11PixelShader* ps = nullptr; ID3D11Buffer* cb = nullptr;
    ID3D11SamplerState* samp = nullptr; ID3D11BlendState* blend = nullptr; ID3D11RasterizerState* rast = nullptr;
    ID3D11Texture2D* tex = nullptr; ID3D11ShaderResourceView* srv = nullptr; unsigned texVersion = 0; int texW = 0, texH = 0, texScreenH = 1080;
    ID3D11Texture2D* stex = nullptr; ID3D11ShaderResourceView* ssrv = nullptr; unsigned stexVersion = 0;
    bool failed = false;
    template <class T> static void R(T*& p) { if (p) { p->Release(); p = nullptr; } }
    void ReleaseAll() { R(vs); R(ps); R(cb); R(samp); R(blend); R(rast); R(tex); R(srv); R(stex); R(ssrv); dev = nullptr; texVersion = 0; stexVersion = 0; failed = false; }
} g_res;

const char* kShader = R"(
cbuffer CB : register(b0) { float4 rect; float4 alpha; };
Texture2D tex : register(t0); SamplerState smp : register(s0);
struct VO { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VO vs_main(uint id : SV_VertexID) {
    VO o; float2 uv = float2(id & 1, id >> 1);
    o.pos = float4(lerp(rect.x, rect.z, uv.x), lerp(rect.y, rect.w, uv.y), 0, 1); o.uv = uv; return o; }
float4 ps_main(VO i) : SV_Target { return tex.Sample(smp, i.uv) * alpha.x; }
)";

bool CompileShaders() {
    if (!g_vsCode.empty()) return true;
    HMODULE m = LoadLibraryW(L"d3dcompiler_47.dll");   // part of Windows 10/11
    if (!m) { Log::Write("Overlay: d3dcompiler_47.dll not found"); return false; }
    auto compile = (D3DCompileFn)GetProcAddress(m, "D3DCompile");
    if (!compile) return false;
    auto one = [&](const char* entry, const char* target, std::vector<char>& out) {
        ID3DBlob* code = nullptr; ID3DBlob* err = nullptr;
        HRESULT hr = compile(kShader, strlen(kShader), "trucker-chat", nullptr, nullptr, entry, target, 0, 0, &code, &err);
        if (FAILED(hr)) { Log::Write("Overlay shader error: %s", err ? (const char*)err->GetBufferPointer() : "?"); if (err) err->Release(); return false; }
        out.assign((char*)code->GetBufferPointer(), (char*)code->GetBufferPointer() + code->GetBufferSize());
        code->Release(); if (err) err->Release(); return true;
    };
    return one("vs_main", "vs_4_0", g_vsCode) && one("ps_main", "ps_4_0", g_psCode);
}

bool EnsureResources(ID3D11Device* dev) {
    if (g_res.dev == dev && !g_res.failed && g_res.vs) return true;
    if (g_res.dev == dev && g_res.failed) return false;
    g_res.ReleaseAll(); g_res.dev = dev;
    bool ok = SUCCEEDED(dev->CreateVertexShader(g_vsCode.data(), g_vsCode.size(), nullptr, &g_res.vs));
    ok = ok && SUCCEEDED(dev->CreatePixelShader(g_psCode.data(), g_psCode.size(), nullptr, &g_res.ps));
    D3D11_BUFFER_DESC bd{}; bd.ByteWidth = 32; bd.Usage = D3D11_USAGE_DYNAMIC; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ok = ok && SUCCEEDED(dev->CreateBuffer(&bd, nullptr, &g_res.cb));
    D3D11_SAMPLER_DESC sd{}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = D3D11_FLOAT32_MAX;
    ok = ok && SUCCEEDED(dev->CreateSamplerState(&sd, &g_res.samp));
    D3D11_BLEND_DESC bl{}; auto& rt = bl.RenderTarget[0];
    rt.BlendEnable = TRUE; rt.SrcBlend = D3D11_BLEND_ONE; rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA; rt.BlendOp = D3D11_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D11_BLEND_ONE; rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA; rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    ok = ok && SUCCEEDED(dev->CreateBlendState(&bl, &g_res.blend));
    D3D11_RASTERIZER_DESC rd{}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE;
    ok = ok && SUCCEEDED(dev->CreateRasterizerState(&rd, &g_res.rast));
    if (!ok) { Log::Write("Overlay: could not create D3D11 resources"); g_res.ReleaseAll(); g_res.dev = dev; g_res.failed = true; return false; }
    Log::Write("Overlay: D3D11 renderer ready (feature level 0x%x)", (unsigned)dev->GetFeatureLevel());
    return true;
}

bool MakeTexture(ID3D11Device* dev, const Image& img, ID3D11Texture2D*& tex, ID3D11ShaderResourceView*& srv) {
    std::vector<uint32_t> rgba(img.pixels.size());   // BGRA -> RGBA (R8G8B8A8 is supported by every D3D11 GPU)
    for (size_t i = 0; i < rgba.size(); ++i) { uint32_t p = img.pixels[i]; rgba[i] = (p & 0xFF00FF00u) | ((p >> 16) & 0xFFu) | ((p & 0xFFu) << 16); }
    D3D11_TEXTURE2D_DESC td{}; td.Width = img.width; td.Height = img.height; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{rgba.data(), (UINT)img.width * 4, 0};
    if (FAILED(dev->CreateTexture2D(&td, &init, &tex)) || FAILED(dev->CreateShaderResourceView(tex, nullptr, &srv))) { Res::R(tex); Res::R(srv); return false; }
    return true;
}

bool EnsureTexture(ID3D11Device* dev) {
    unsigned v = g_imgVersion.load();
    if (g_res.srv && g_res.texVersion == v) return true;
    std::shared_ptr<const Image> img;
    { std::unique_lock<std::mutex> l(g_imgMtx, std::try_to_lock); if (!l.owns_lock()) return g_res.srv != nullptr; img = g_img; }
    Res::R(g_res.srv); Res::R(g_res.tex);
    g_res.texVersion = v;
    if (!img || img->width <= 0) return false;
    if (!MakeTexture(dev, *img, g_res.tex, g_res.srv)) return false;
    g_res.texW = img->width; g_res.texH = img->height; g_res.texScreenH = img->renderedForScreenHeight;
    return true;
}

bool EnsureScreenTexture(ID3D11Device* dev) {
    unsigned v = g_scrImgVersion.load();
    if (g_res.ssrv && g_res.stexVersion == v) return true;
    std::shared_ptr<const Image> img;
    { std::unique_lock<std::mutex> l(g_scrImgMtx, std::try_to_lock); if (!l.owns_lock()) return g_res.ssrv != nullptr; img = g_scrImg; }
    Res::R(g_res.ssrv); Res::R(g_res.stex);
    g_res.stexVersion = v;
    if (!img || img->width <= 0) return false;
    return MakeTexture(dev, *img, g_res.stex, g_res.ssrv);
}

// Saves and restores the pipeline state touched by DrawQuad (like Dear ImGui's DX11 backend).
struct StateBackup {
    UINT nVp = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE, nSc = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    D3D11_VIEWPORT vp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]; D3D11_RECT sc[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    ID3D11RasterizerState* rs = nullptr; ID3D11BlendState* bs = nullptr; FLOAT bf[4]; UINT mask = 0;
    ID3D11DepthStencilState* dss = nullptr; UINT stencilRef = 0;
    ID3D11RenderTargetView* rtv[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {}; ID3D11DepthStencilView* dsv = nullptr;
    ID3D11ShaderResourceView* psSrv = nullptr; ID3D11SamplerState* psSmp = nullptr;
    ID3D11PixelShader* ps = nullptr; ID3D11VertexShader* vs = nullptr; ID3D11GeometryShader* gs = nullptr;
    ID3D11HullShader* hs = nullptr; ID3D11DomainShader* ds = nullptr;
    ID3D11ClassInstance* psI[256]; ID3D11ClassInstance* vsI[256]; ID3D11ClassInstance* gsI[256]; ID3D11ClassInstance* hsI[256]; ID3D11ClassInstance* dsI[256];
    UINT psN = 256, vsN = 256, gsN = 256, hsN = 256, dsN = 256;
    D3D11_PRIMITIVE_TOPOLOGY topo; ID3D11InputLayout* il = nullptr;
    ID3D11Buffer* vsCb = nullptr; ID3D11Buffer* psCb = nullptr;
    ID3D11Buffer* vb = nullptr; UINT vbStride = 0, vbOffset = 0; ID3D11Buffer* ib = nullptr; DXGI_FORMAT ibFmt; UINT ibOffset = 0;
    void Save(ID3D11DeviceContext* c) {
        c->RSGetViewports(&nVp, vp); c->RSGetScissorRects(&nSc, sc); c->RSGetState(&rs);
        c->OMGetBlendState(&bs, bf, &mask); c->OMGetDepthStencilState(&dss, &stencilRef);
        c->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtv, &dsv);
        c->PSGetShaderResources(0, 1, &psSrv); c->PSGetSamplers(0, 1, &psSmp);
        c->PSGetShader(&ps, psI, &psN); c->VSGetShader(&vs, vsI, &vsN); c->GSGetShader(&gs, gsI, &gsN);
        c->HSGetShader(&hs, hsI, &hsN); c->DSGetShader(&ds, dsI, &dsN);
        c->VSGetConstantBuffers(0, 1, &vsCb); c->PSGetConstantBuffers(0, 1, &psCb);
        c->IAGetPrimitiveTopology(&topo); c->IAGetIndexBuffer(&ib, &ibFmt, &ibOffset);
        c->IAGetVertexBuffers(0, 1, &vb, &vbStride, &vbOffset); c->IAGetInputLayout(&il);
    }
    void Restore(ID3D11DeviceContext* c) {
        c->RSSetScissorRects(nSc, sc); c->RSSetViewports(nVp, vp); c->RSSetState(rs); if (rs) rs->Release();
        c->OMSetBlendState(bs, bf, mask); if (bs) bs->Release();
        c->OMSetDepthStencilState(dss, stencilRef); if (dss) dss->Release();
        c->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtv, dsv);
        for (auto*& r : rtv) if (r) r->Release();
        if (dsv) dsv->Release();
        c->PSSetShaderResources(0, 1, &psSrv); if (psSrv) psSrv->Release();
        c->PSSetSamplers(0, 1, &psSmp); if (psSmp) psSmp->Release();
        c->PSSetShader(ps, psI, psN); if (ps) ps->Release(); for (UINT i = 0; i < psN; ++i) if (psI[i]) psI[i]->Release();
        c->VSSetShader(vs, vsI, vsN); if (vs) vs->Release(); for (UINT i = 0; i < vsN; ++i) if (vsI[i]) vsI[i]->Release();
        c->GSSetShader(gs, gsI, gsN); if (gs) gs->Release(); for (UINT i = 0; i < gsN; ++i) if (gsI[i]) gsI[i]->Release();
        c->HSSetShader(hs, hsI, hsN); if (hs) hs->Release(); for (UINT i = 0; i < hsN; ++i) if (hsI[i]) hsI[i]->Release();
        c->DSSetShader(ds, dsI, dsN); if (ds) ds->Release(); for (UINT i = 0; i < dsN; ++i) if (dsI[i]) dsI[i]->Release();
        c->VSSetConstantBuffers(0, 1, &vsCb); if (vsCb) vsCb->Release();
        c->PSSetConstantBuffers(0, 1, &psCb); if (psCb) psCb->Release();
        c->IASetPrimitiveTopology(topo);
        c->IASetIndexBuffer(ib, ibFmt, ibOffset); if (ib) ib->Release();
        c->IASetVertexBuffers(0, 1, &vb, &vbStride, &vbOffset); if (vb) vb->Release();
        c->IASetInputLayout(il); if (il) il->Release();
    }
};

void DrawQuad(ID3D11DeviceContext* c, ID3D11RenderTargetView* rtv, float vpW, float vpH, ID3D11ShaderResourceView* srv, const float cbdata[8]) {
    StateBackup* b = new StateBackup();   // ~10 KB, keep it off the game's stack
    b->Save(c);
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(c->Map(g_res.cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) { memcpy(m.pData, cbdata, 8 * sizeof(float)); c->Unmap(g_res.cb, 0); }
    D3D11_VIEWPORT vp{0, 0, vpW, vpH, 0, 1};
    c->OMSetRenderTargets(1, &rtv, nullptr);
    float bf[4] = {0, 0, 0, 0};
    c->OMSetBlendState(g_res.blend, bf, 0xFFFFFFFF);
    c->OMSetDepthStencilState(nullptr, 0);
    c->RSSetViewports(1, &vp); c->RSSetState(g_res.rast);
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    c->IASetInputLayout(nullptr);
    c->IASetIndexBuffer(nullptr, DXGI_FORMAT_R16_UINT, 0);
    ID3D11Buffer* noVb = nullptr; UINT zero = 0; c->IASetVertexBuffers(0, 1, &noVb, &zero, &zero);
    c->VSSetShader(g_res.vs, nullptr, 0); c->VSSetConstantBuffers(0, 1, &g_res.cb);
    c->PSSetShader(g_res.ps, nullptr, 0); c->PSSetConstantBuffers(0, 1, &g_res.cb);
    c->GSSetShader(nullptr, nullptr, 0); c->HSSetShader(nullptr, nullptr, 0); c->DSSetShader(nullptr, nullptr, 0);
    c->PSSetShaderResources(0, 1, &srv); c->PSSetSamplers(0, 1, &g_res.samp);
    c->Draw(4, 0);
    b->Restore(c);
    delete b;
}

void DrawIntoTarget(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, int w, int h) {
    float alpha = g_scrAlpha.load();
    if (alpha <= 0.003f || !rtv) return;
    ID3D11Device* dev = nullptr; ctx->GetDevice(&dev);
    if (!dev) return;
    if (EnsureResources(dev) && EnsureScreenTexture(dev) && g_res.ssrv) {
        float ax = g_areaX, ay = g_areaY, aw = g_areaW, ah = g_areaH;
        float x0 = ax * 2 - 1, x1 = (ax + aw) * 2 - 1, yTop = 1 - ay * 2, yBot = 1 - (ay + ah) * 2;
        if (g_flipY) std::swap(yTop, yBot);
        if (g_flipX) std::swap(x0, x1);
        float cbdata[8] = {x0, yTop, x1, yBot, alpha, 0, 0, 0};
        DrawQuad(ctx, rtv, (float)w, (float)h, g_res.ssrv, cbdata);
    }
    dev->Release();
}

bool SizeOfRtv(ID3D11RenderTargetView* v, D3D11_TEXTURE2D_DESC& d) {
    ID3D11Resource* res = nullptr; v->GetResource(&res);
    ID3D11Texture2D* tex = nullptr;
    bool ok = res && SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex)) && tex;
    if (ok) tex->GetDesc(&d);
    if (tex) tex->Release();
    if (res) res->Release();
    return ok;
}

// Every render-target change of the immediate context (hot path: cheap when inactive)
void OnRenderTargetChange(ID3D11DeviceContext* ctx, UINT num, ID3D11RenderTargetView* const* views) {
    ++g_rtCalls;
    bool drawing = g_scrAlpha.load() > 0.003f && g_scrW.load() > 0;
    bool collect = g_collect.load();
    if (!drawing && !collect && !g_curRtv) return;
    ID3D11RenderTargetView* next = (num > 0 && views) ? views[0] : nullptr;
    if (next == g_curRtv) return;
    if (g_curRtv) {
        if (g_curMatch && drawing) DrawIntoTarget(ctx, g_curRtv, g_curW, g_curH);   // the game is done with it
        g_curRtv->Release(); g_curRtv = nullptr; g_curMatch = false;
    }
    if (!next || (!drawing && !collect)) return;
    D3D11_TEXTURE2D_DESC d;
    if (!SizeOfRtv(next, d)) return;
    // skip the back buffer and full-scene buffers (render scaling can make them larger than the back buffer)
    long long bbArea = (long long)g_bbW.load() * g_screenH.load();
    long long limit = std::max(bbArea * 9 / 10, 2600000LL);   // never drop 2048x1024-class textures (screen atlases)
    bool sceneSized = bbArea > 0 && (long long)d.Width * d.Height >= limit;
    bool backBufferSized = (int)d.Width == g_bbW.load() && (int)d.Height == g_screenH.load();
    if (bbArea <= 0 || sceneSized || backBufferSized || d.Width < 64 || d.Height < 64) return;
    if (collect) {
        long long key = ((long long)d.Width << 32) | ((long long)d.Height << 12) | (long long)d.Format;
        std::lock_guard<std::mutex> l(g_statMtx);
        auto& e = g_stats[key]; e.w = d.Width; e.h = d.Height; e.fmt = d.Format; ++e.binds;
    }
    int fmt = g_scrFmt.load();
    if (drawing && (int)d.Width == g_scrW.load() && (int)d.Height == g_scrH.load() && (fmt == 0 || fmt == (int)d.Format)) {
        g_curRtv = next; g_curRtv->AddRef(); g_curW = d.Width; g_curH = d.Height; g_curMatch = true;
    }
}

void STDMETHODCALLTYPE HookOMSetRT(ID3D11DeviceContext* ctx, UINT num, ID3D11RenderTargetView* const* views, ID3D11DepthStencilView* dsv) {
    if (!t_inHook && !t_forwarding && ctx->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE) { t_inHook = true; OnRenderTargetChange(ctx, num, views); t_inHook = false; }
    ++t_forwarding; FindVt(ctx)->rt(ctx, num, views, dsv); --t_forwarding;
}
void STDMETHODCALLTYPE HookOMSetRTUAV(ID3D11DeviceContext* ctx, UINT num, ID3D11RenderTargetView* const* views, ID3D11DepthStencilView* dsv,
                                      UINT uavStart, UINT numUav, ID3D11UnorderedAccessView* const* uavs, const UINT* counts) {
    if (num != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL && !t_inHook && !t_forwarding && ctx->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE) {
        t_inHook = true; OnRenderTargetChange(ctx, num, views); t_inHook = false;
    }
    ++t_forwarding; FindVt(ctx)->rtuav(ctx, num, views, dsv, uavStart, numUav, uavs, counts); --t_forwarding;
}
// Only patched while a foreign module range is set. Cost per game draw call: one range compare.
void STDMETHODCALLTYPE HookDraw(ID3D11DeviceContext* ctx, UINT count, UINT start) {
    uintptr_t ra = TC_RETURN_ADDRESS();
    if (ra >= g_foreignLo.load(std::memory_order_relaxed) && ra < g_foreignHi.load(std::memory_order_relaxed)) {
        ID3D11RenderTargetView* rtv = nullptr;
        ctx->OMGetRenderTargets(1, &rtv, nullptr);
        if (rtv) {
            D3D11_TEXTURE2D_DESC d;
            if (SizeOfRtv(rtv, d) && (int)d.Width == g_scrW.load() && (int)d.Height == g_scrH.load()) g_foreignDrawAt = GetTickCount64();
            rtv->Release();
        }
    }
    FindVt(ctx)->draw(ctx, count, start);
}

DXGI_FORMAT RtvFormat(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default: return f;
    }
}

// Replaces one vtable entry and returns the previous one (which may be another plugin's hook; we always call it).
bool PatchSlot(void** vt, int idx, void* fn, void** orig) {
    DWORD old;
    if (!VirtualProtect(&vt[idx], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) return false;
    if (orig) *orig = vt[idx];
    InterlockedExchangePointer(&vt[idx], fn);
    VirtualProtect(&vt[idx], sizeof(void*), old, &old);
    FlushInstructionCache(GetCurrentProcess(), &vt[idx], sizeof(void*));
    return true;
}

// Hooks the immediate context the game really uses (render thread, from Present). Checked every frame.
void HookGameContext(IDXGISwapChain* sc) {
    if (!g_wantScreenHook) return;
    bool wantDraw = g_foreignHi.load() != 0;
    int n = g_nVts.load();
    if (n > 0 && (!wantDraw || g_vts[n - 1].draw)) {
        // fast path: already hooked; a new context class would be rare, check only once per ~second
        static int skip = 0;
        if (++skip % 60) return;
    }
    ID3D11Device* d = nullptr;
    if (FAILED(sc->GetDevice(__uuidof(ID3D11Device), (void**)&d)) || !d) return;
    ID3D11DeviceContext* c = nullptr; d->GetImmediateContext(&c);
    if (c) {
        void** cvt = *(void***)c;
        int idx = -1;
        for (int i = 0; i < g_nVts.load(); ++i) if (g_vts[i].vt == cvt) idx = i;
        if (idx < 0 && g_nVts.load() < 8) {
            VtHook h{cvt, nullptr, nullptr, nullptr};
            if (PatchSlot(cvt, kOMSetRT, (void*)&HookOMSetRT, (void**)&h.rt) && PatchSlot(cvt, kOMSetRTUAV, (void*)&HookOMSetRTUAV, (void**)&h.rtuav)) {
                idx = g_nVts.load();
                g_vts[idx] = h; ++g_nVts; g_screenHooked = true;
                Log::Write("Screen: hooked the game's render context #%d (device flags 0x%x)", g_nVts.load(), d->GetCreationFlags());
            }
        }
        if (idx >= 0 && wantDraw && !g_vts[idx].draw) {
            DrawFn prev = nullptr;
            if (PatchSlot(cvt, kDraw, (void*)&HookDraw, (void**)&prev)) {
                g_vts[idx].draw = prev;
                Log::Write("Screen: watching draw calls of the radio plugin (to avoid drawing over its station display)");
            }
        }
        c->Release();
    }
    d->Release();
}

void Draw(IDXGISwapChain* sc) {
    HookGameContext(sc);
    if (g_curRtv) {   // a matching screen target was still bound at the end of the frame
        ID3D11Device* d = nullptr;
        if (SUCCEEDED(sc->GetDevice(__uuidof(ID3D11Device), (void**)&d)) && d) {
            ID3D11DeviceContext* c = nullptr; d->GetImmediateContext(&c);
            if (c) { if (g_curMatch) DrawIntoTarget(c, g_curRtv, g_curW, g_curH); c->Release(); }
            d->Release();
        }
        g_curRtv->Release(); g_curRtv = nullptr; g_curMatch = false;
    }
    if (!g_enabled) return;
    ID3D11Device* dev = nullptr;
    if (FAILED(sc->GetDevice(__uuidof(ID3D11Device), (void**)&dev)) || !dev) { ++g_otherFrames; return; }
    ++g_d3d11Frames;
    ID3D11Texture2D* bb = nullptr;
    if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb)) || !bb) { dev->Release(); return; }
    D3D11_TEXTURE2D_DESC bd; bb->GetDesc(&bd);
    if (bd.Height >= 200) { g_screenH = (int)bd.Height; g_bbW = (int)bd.Width; }
    float alpha = g_alpha.load();
    if (alpha > 0.003f && bd.Width >= 320 && bd.Height >= 200 && EnsureResources(dev) && EnsureTexture(dev) && g_res.srv) {
        D3D11_RENDER_TARGET_VIEW_DESC rvd{}; rvd.Format = RtvFormat(bd.Format);
        rvd.ViewDimension = bd.SampleDesc.Count > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D;
        ID3D11RenderTargetView* rtv = nullptr;
        ID3D11DeviceContext* ctx = nullptr; dev->GetImmediateContext(&ctx);
        if (ctx && SUCCEEDED(dev->CreateRenderTargetView(bb, &rvd, &rtv))) {
            float k = (float)bd.Height / (float)std::max(1, g_res.texScreenH);
            float w = g_res.texW * k, h = g_res.texH * k;
            float sw = (float)bd.Width, sh = (float)bd.Height, ox = g_offX * sh, oy = g_offY * sh;
            int a = g_anchor; float x, y;
            x = (a % 3 == 0) ? ox : (a % 3 == 1) ? (sw - w) / 2 + ox : sw - w - ox;
            y = (a < 3) ? oy : sh - h - oy;
            x = std::floor(x); y = std::floor(y);
            float cbdata[8] = {x / sw * 2 - 1, 1 - y / sh * 2, (x + w) / sw * 2 - 1, 1 - (y + h) / sh * 2, alpha, 0, 0, 0};
            DrawQuad(ctx, rtv, sw, sh, g_res.srv, cbdata);
        }
        if (rtv) rtv->Release();
        if (ctx) ctx->Release();
    }
    bb->Release(); dev->Release();
}

HRESULT STDMETHODCALLTYPE HookPresent(IDXGISwapChain* sc, UINT sync, UINT flags) {
    if (!t_inHook && !t_forwarding && !(flags & DXGI_PRESENT_TEST)) { t_inHook = true; Draw(sc); t_inHook = false; }
    ++t_forwarding; HRESULT hr = g_origPresent(sc, sync, flags); --t_forwarding;
    return hr;
}
HRESULT STDMETHODCALLTYPE HookPresent1(IDXGISwapChain1* sc, UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS* p) {
    if (!t_inHook && !t_forwarding && !(flags & DXGI_PRESENT_TEST)) { t_inHook = true; Draw(sc); t_inHook = false; }
    ++t_forwarding; HRESULT hr = g_origPresent1(sc, sync, flags, p); --t_forwarding;
    return hr;
}
} // namespace

namespace D3D11Overlay {
bool Install() {
    // Installed at most once per process: the module is pinned and the globals survive a plugin re-init. Re-patching
    // would store our own hook (or another plugin's hook that already points to ours) as "previous" -> endless loop.
    if (g_vtable) { g_enabled = true; return true; }
    if (!CompileShaders()) return false;
    WNDCLASSEXW wc{sizeof(wc)}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = SelfModule(); wc.lpszClassName = Names::kDummyWindowClass;
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    DXGI_SWAP_CHAIN_DESC sd{}; sd.BufferCount = 1; sd.BufferDesc.Width = 64; sd.BufferDesc.Height = 64;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd; sd.SampleDesc.Count = 1; sd.Windowed = TRUE; sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    IDXGISwapChain* sc = nullptr; ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr;
    HRESULT hr = E_FAIL;
    for (D3D_DRIVER_TYPE t : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) {
        hr = D3D11CreateDeviceAndSwapChain(nullptr, t, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &sc, &dev, nullptr, &ctx);
        if (SUCCEEDED(hr)) break;
    }
    bool ok = false;
    if (SUCCEEDED(hr) && sc) {
        void** vt = *(void***)sc;
        IDXGISwapChain1* sc1 = nullptr;
        g_hasPresent1 = SUCCEEDED(sc->QueryInterface(__uuidof(IDXGISwapChain1), (void**)&sc1));
        if (sc1) sc1->Release();
        if (vt[kPresent] != (void*)&HookPresent) {
            Log::Write("Overlay: Present slot currently points into %s", vt[kPresent] ? "another module (chaining to it)" : "nothing");
        }
        ok = PatchSlot(vt, kPresent, (void*)&HookPresent, (void**)&g_origPresent);
        if (ok && g_hasPresent1) PatchSlot(vt, kPresent1, (void*)&HookPresent1, (void**)&g_origPresent1);
        if (ok) {
            g_vtable = vt;
            // The game may call our hooks until the process ends, so keep this DLL loaded for the process lifetime.
            HMODULE self; GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCWSTR)&HookPresent, &self);
            if (g_wantScreenHook) Log::Write("Screen: waiting for the game's first frame to hook its render context");
        }
    } else Log::Write("Overlay: D3D11 dummy device failed (hr=0x%08lx)", (unsigned long)hr);
    if (ctx) ctx->Release();
    if (dev) dev->Release();
    if (sc) sc->Release();
    if (hwnd) DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    g_enabled = ok;
    Log::Write(ok ? "Overlay: Present hook installed%s" : "Overlay: Present hook NOT installed%s", g_hasPresent1 ? " (incl. Present1)" : "");
    return ok;
}
void Uninstall() { g_enabled = false; g_alpha = 0; g_scrAlpha = 0; }
void EnableScreenHook(bool on) { g_wantScreenHook = on; }
void SetScreenImage(std::shared_ptr<const Image> img) { std::lock_guard<std::mutex> l(g_scrImgMtx); g_scrImg = std::move(img); ++g_scrImgVersion; }
void SetScreenTarget(int w, int h, int format) { g_scrW = w; g_scrH = h; g_scrFmt = format; }
void SetScreenAlpha(float a) { g_scrAlpha = a; }
void SetScreenArea(float x, float y, float w, float h, bool flipY, bool flipX) { g_areaX = x; g_areaY = y; g_areaW = w; g_areaH = h; g_flipY = flipY; g_flipX = flipX; }
void SetCollectTargets(bool on) { g_collect = on; }
std::vector<RenderTargetStat> TakeTargetStats() {
    std::lock_guard<std::mutex> l(g_statMtx);
    std::vector<RenderTargetStat> v;
    for (auto& e : g_stats) v.push_back({e.second.w, e.second.h, e.second.fmt, e.second.binds});
    g_stats.clear();
    return v;
}
bool ScreenHookActive() { return g_wantScreenHook && g_vtable != nullptr; }
long long RenderTargetCalls() { return g_rtCalls; }
void SetImage(std::shared_ptr<const Image> img) { std::lock_guard<std::mutex> l(g_imgMtx); g_img = std::move(img); ++g_imgVersion; }
void SetAlpha(float a) { g_alpha = a; }
void SetPlacement(int anchor, float ox, float oy) { g_anchor = anchor; g_offX = ox; g_offY = oy; }
long long D3D11Frames() { return g_d3d11Frames; }
long long OtherFrames() { return g_otherFrames; }
int ScreenHeight() { return g_screenH; }
void SetForeignModule(uintptr_t base, uintptr_t end) { g_foreignLo = base; g_foreignHi = end; }
unsigned long long ForeignScreenDrawAt() { return g_foreignDrawAt; }
}
