#include "chams.h"
#include <windows.h>
#include <d3dcompiler.h>
#include <unordered_map>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace chams {
Settings s;

namespace {
constexpr int VT_DRAWINDEXED = 12, VT_DRAWINDEXEDINSTANCED = 20;

using DrawIndexedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, INT);
using DrawIndexedInstancedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, INT, UINT);
using D3DCompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR,
                                      UINT, UINT, ID3DBlob**, ID3DBlob**);

struct ClassInfo {
    std::atomic<int> frameCount{ 0 };
    // exponentially weighted moments of (N = bots on screen, C = draws of this class) for a running correlation
    double mN = 0, mC = 0, mNN = 0, mCC = 0, mNC = 0;
    int samples = 0;
    float corr = 0, slope = 0;
    bool manual = false;
    volatile bool detected = false;
    volatile bool ignored = false;       // clearly unrelated to characters: skip further work
    unsigned stride = 0, indexCount = 0;
};

// two possible vtables: the immediate context and deferred contexts (they may share one)
void**                  g_vt[2] = { nullptr, nullptr };
DrawIndexedFn           oDI[2] = { nullptr, nullptr };
DrawIndexedInstancedFn  oDII[2] = { nullptr, nullptr };

ID3D11Device*           g_dev = nullptr;
ID3D11PixelShader*      g_psVis = nullptr;
ID3D11PixelShader*      g_psHid = nullptr;
ID3D11PixelShader*      g_psPreview = nullptr;
ID3D11DepthStencilState* g_dssNoDepth = nullptr;
float                   g_builtVis[4] = { -1, -1, -1, -1 }, g_builtHid[4] = { -1, -1, -1, -1 };

SRWLOCK                 g_lock = SRWLOCK_INIT;
std::unordered_map<ID3D11InputLayout*, ClassInfo*> g_classes;
ID3D11InputLayout*      g_preview = nullptr;
volatile bool           g_bypass = false;
volatile bool           g_live = false;

std::atomic<int> g_calls{ 0 }, g_main{ 0 }, g_applied{ 0 };
int  g_lastCalls = 0, g_lastMain = 0, g_lastApplied = 0, g_lastBots = 0;

bool patchSlot(void** vt, int idx, void* fn) {
    DWORD old;
    if (!VirtualProtect(&vt[idx], sizeof(void*), PAGE_READWRITE, &old)) return false;
    vt[idx] = fn;
    VirtualProtect(&vt[idx], sizeof(void*), old, &old);
    return true;
}

ID3D11PixelShader* compilePS(const float c[4]) {
    static D3DCompileFn compile = nullptr;
    if (!compile) {
        HMODULE m = GetModuleHandleW(L"d3dcompiler_47.dll");
        if (!m) m = LoadLibraryW(L"d3dcompiler_47.dll");
        if (m) compile = (D3DCompileFn)GetProcAddress(m, "D3DCompile");
    }
    if (!compile || !g_dev) return nullptr;
    char src[512];
    snprintf(src, sizeof(src),
        "struct O{float4 a:SV_Target0;float4 b:SV_Target1;float4 c:SV_Target2;float4 d:SV_Target3;};"
        "O main(float4 p:SV_Position){O o;float4 k=float4(%.4f,%.4f,%.4f,1);o.a=k;o.b=k;o.c=k;o.d=k;return o;}",
        c[0], c[1], c[2]);
    ID3DBlob* blob = nullptr; ID3DBlob* err = nullptr;
    HRESULT hr = compile(src, strlen(src), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &blob, &err);
    if (err) err->Release();
    if (FAILED(hr) || !blob) return nullptr;
    ID3D11PixelShader* ps = nullptr;
    g_dev->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &ps);
    blob->Release();
    return ps;
}

bool same(const float* a, const float* b) { return a[0] == b[0] && a[1] == b[1] && a[2] == b[2]; }

void rebuildShaders() {
    if (!same(g_builtVis, s.visCol) || !g_psVis) {
        if (g_psVis) g_psVis->Release();
        g_psVis = compilePS(s.visCol);
        memcpy(g_builtVis, s.visCol, sizeof(g_builtVis));
    }
    if (!same(g_builtHid, s.hidCol) || !g_psHid) {
        if (g_psHid) g_psHid->Release();
        g_psHid = compilePS(s.hidCol);
        memcpy(g_builtHid, s.hidCol, sizeof(g_builtHid));
    }
    if (!g_psPreview) { const float m[4] = { 1, 0, 1, 1 }; g_psPreview = compilePS(m); }
}

// Returns the class for the current draw, or null if this draw should pass through untouched.
ClassInfo* classify(ID3D11DeviceContext* ctx, bool& preview) {
    ID3D11InputLayout* il = nullptr;
    ctx->IAGetInputLayout(&il);
    if (!il) return nullptr;

    ClassInfo* ci = nullptr;
    AcquireSRWLockShared(&g_lock);
    auto it = g_classes.find(il);
    if (it != g_classes.end()) ci = it->second;
    ReleaseSRWLockShared(&g_lock);

    if (ci && ci->ignored && !ci->manual) { il->Release(); return nullptr; }

    // only main colour passes: something bound as render target and a pixel shader present (skips shadow/depth-only)
    ID3D11RenderTargetView* rtv = nullptr;
    ctx->OMGetRenderTargets(1, &rtv, nullptr);
    ID3D11PixelShader* ps = nullptr;
    ctx->PSGetShader(&ps, nullptr, nullptr);
    bool mainPass = rtv && ps;
    if (rtv) rtv->Release();
    if (ps) ps->Release();
    if (!mainPass) { il->Release(); return nullptr; }
    ++g_main;

    if (!ci) {
        ClassInfo* n = new ClassInfo();
        ID3D11Buffer* vb = nullptr; UINT stride = 0, off = 0;
        ctx->IAGetVertexBuffers(0, 1, &vb, &stride, &off);
        if (vb) vb->Release();
        n->stride = stride;
        AcquireSRWLockExclusive(&g_lock);
        auto ins = g_classes.emplace(il, n);
        ReleaseSRWLockExclusive(&g_lock);
        if (!ins.second) delete n;
        ci = ins.first->second;
    }
    ci->frameCount.fetch_add(1, std::memory_order_relaxed);

    preview = (il == g_preview);
    bool apply = preview || ci->manual || (s.autoDetect && ci->detected);
    il->Release();
    return apply ? ci : nullptr;
}

struct SavedState {
    ID3D11DepthStencilState* dss = nullptr; UINT ref = 0;
    ID3D11PixelShader* ps = nullptr;
};
void saveState(ID3D11DeviceContext* c, SavedState& st) {
    c->OMGetDepthStencilState(&st.dss, &st.ref);
    c->PSGetShader(&st.ps, nullptr, nullptr);
}
void restoreState(ID3D11DeviceContext* c, SavedState& st) {
    c->PSSetShader(st.ps, nullptr, 0);
    c->OMSetDepthStencilState(st.dss, st.ref);
    if (st.ps) st.ps->Release();
    if (st.dss) st.dss->Release();
}

template <class DrawFn>
void drawWithChams(ID3D11DeviceContext* ctx, bool preview, DrawFn&& draw) {
    if (!g_psVis || !g_psHid) { draw(); return; }
    ++g_applied;
    SavedState st; saveState(ctx, st);
    if (preview && g_psPreview && g_dssNoDepth) {
        ctx->OMSetDepthStencilState(g_dssNoDepth, 0);
        ctx->PSSetShader(g_psPreview, nullptr, 0);
        draw();
    } else if (s.mode == 2 && g_dssNoDepth) {
        ctx->OMSetDepthStencilState(g_dssNoDepth, 0);   // hidden colour, ignoring depth
        ctx->PSSetShader(g_psHid, nullptr, 0);
        draw();
        ctx->OMSetDepthStencilState(st.dss, st.ref);    // visible colour with the game's own depth test
        ctx->PSSetShader(g_psVis, nullptr, 0);
        draw();
    } else {
        ctx->PSSetShader(g_psVis, nullptr, 0);
        draw();
    }
    restoreState(ctx, st);
}

template <int V>
void STDMETHODCALLTYPE hkDrawIndexed(ID3D11DeviceContext* ctx, UINT indexCount, UINT startIndex, INT baseVertex) {
    ++g_calls;
    if (s.mode == 0 || g_bypass || !g_live) { oDI[V](ctx, indexCount, startIndex, baseVertex); return; }
    bool preview = false;
    ClassInfo* ci = classify(ctx, preview);
    if (!ci) { oDI[V](ctx, indexCount, startIndex, baseVertex); return; }
    ci->indexCount = indexCount;
    drawWithChams(ctx, preview, [&] { oDI[V](ctx, indexCount, startIndex, baseVertex); });
}

template <int V>
void STDMETHODCALLTYPE hkDrawIndexedInstanced(ID3D11DeviceContext* ctx, UINT indexCountPerInstance, UINT instanceCount,
                                              UINT startIndex, INT baseVertex, UINT startInstance) {
    ++g_calls;
    if (s.mode == 0 || g_bypass || !g_live) {
        oDII[V](ctx, indexCountPerInstance, instanceCount, startIndex, baseVertex, startInstance); return;
    }
    bool preview = false;
    ClassInfo* ci = classify(ctx, preview);
    if (!ci) {
        oDII[V](ctx, indexCountPerInstance, instanceCount, startIndex, baseVertex, startInstance); return;
    }
    ci->indexCount = indexCountPerInstance;
    drawWithChams(ctx, preview, [&] {
        oDII[V](ctx, indexCountPerInstance, instanceCount, startIndex, baseVertex, startInstance); });
}

bool hookVtable(int v, void** vt) {
    g_vt[v] = vt;
    oDI[v] = (DrawIndexedFn)vt[VT_DRAWINDEXED];
    oDII[v] = (DrawIndexedInstancedFn)vt[VT_DRAWINDEXEDINSTANCED];
    void* d1 = v == 0 ? (void*)hkDrawIndexed<0> : (void*)hkDrawIndexed<1>;
    void* d2 = v == 0 ? (void*)hkDrawIndexedInstanced<0> : (void*)hkDrawIndexedInstanced<1>;
    return patchSlot(vt, VT_DRAWINDEXED, d1) && patchSlot(vt, VT_DRAWINDEXEDINSTANCED, d2);
}
} // namespace

bool install(ID3D11Device* dev, ID3D11DeviceContext* ctx) {
    if (g_vt[0] || !dev || !ctx) return g_vt[0] != nullptr;
    g_dev = dev;
    D3D11_DEPTH_STENCIL_DESC d{};
    d.DepthEnable = FALSE;
    d.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    d.DepthFunc = D3D11_COMPARISON_ALWAYS;
    d.StencilEnable = FALSE;
    dev->CreateDepthStencilState(&d, &g_dssNoDepth);
    rebuildShaders();

    bool ok = hookVtable(0, *(void***)ctx);

    // a deferred context may use a different vtable than the immediate one; hook that too if so
    ID3D11DeviceContext* dctx = nullptr;
    if (SUCCEEDED(dev->CreateDeferredContext(0, &dctx)) && dctx) {
        void** v2 = *(void***)dctx;
        if (v2 != g_vt[0]) hookVtable(1, v2);
        dctx->Release();
    }
    g_live = ok;
    return ok;
}

void uninstall() {
    g_live = false;
    for (int v = 0; v < 2; ++v) {
        if (!g_vt[v]) continue;
        patchSlot(g_vt[v], VT_DRAWINDEXED, (void*)oDI[v]);
        patchSlot(g_vt[v], VT_DRAWINDEXEDINSTANCED, (void*)oDII[v]);
        g_vt[v] = nullptr;
    }
    Sleep(100);   // let in-flight draws drain
    if (g_psVis) { g_psVis->Release(); g_psVis = nullptr; }
    if (g_psHid) { g_psHid->Release(); g_psHid = nullptr; }
    if (g_psPreview) { g_psPreview->Release(); g_psPreview = nullptr; }
    if (g_dssNoDepth) { g_dssNoDepth->Release(); g_dssNoDepth = nullptr; }
    AcquireSRWLockExclusive(&g_lock);
    for (auto& kv : g_classes) delete kv.second;
    g_classes.clear();
    ReleaseSRWLockExclusive(&g_lock);
    g_dev = nullptr;
}

void setBypass(bool on) { g_bypass = on; }

void endFrame(int bots) {
    g_lastCalls = g_calls.exchange(0);
    g_lastMain = g_main.exchange(0);
    g_lastApplied = g_applied.exchange(0);
    g_lastBots = bots;
    if (!g_live || s.mode == 0) return;
    rebuildShaders();

    constexpr double a = 0.01;           // moment smoothing
    constexpr int kWarm = 200;           // frames before a verdict
    const double N = bots;
    AcquireSRWLockExclusive(&g_lock);
    for (auto& kv : g_classes) {
        ClassInfo& c = *kv.second;
        const double C = c.frameCount.exchange(0);
        if (c.samples == 0) { c.mN = N; c.mC = C; c.mNN = N * N; c.mCC = C * C; c.mNC = N * C; }
        else {
            c.mN += a * (N - c.mN);   c.mC += a * (C - c.mC);
            c.mNN += a * (N * N - c.mNN); c.mCC += a * (C * C - c.mCC); c.mNC += a * (N * C - c.mNC);
        }
        ++c.samples;
        const double varN = c.mNN - c.mN * c.mN, varC = c.mCC - c.mC * c.mC, cov = c.mNC - c.mN * c.mC;
        if (varN > 0.05 && varC > 1e-6) {
            c.corr = (float)(cov / sqrt(varN * varC));
            c.slope = (float)(cov / varN);
        } else { c.corr = 0; c.slope = 0; }
        // a character class: its draw count follows the number of bots on screen
        c.detected = c.samples >= kWarm && c.corr >= 0.5f && c.slope >= 0.3f;
        // clearly unrelated and well sampled: stop spending time on it
        c.ignored = c.samples >= 600 && fabs(c.corr) < 0.1f && fabs(c.slope) < 0.05f && !c.manual;
    }
    ReleaseSRWLockExclusive(&g_lock);
}

std::vector<ClassView> snapshot(int maxCount) {
    std::vector<ClassView> out;
    AcquireSRWLockShared(&g_lock);
    for (auto& kv : g_classes) {
        const ClassInfo& c = *kv.second;
        if (c.ignored && !c.manual) continue;
        ClassView v;
        v.id = (uintptr_t)kv.first; v.avgDraws = (float)c.mC; v.corr = c.corr; v.slope = c.slope; v.samples = c.samples;
        v.detected = c.detected; v.manual = c.manual; v.preview = kv.first == g_preview;
        v.stride = c.stride; v.indexCount = c.indexCount;
        out.push_back(v);
    }
    ReleaseSRWLockShared(&g_lock);
    std::sort(out.begin(), out.end(), [](const ClassView& a, const ClassView& b) {
        if (a.detected != b.detected) return a.detected;
        return a.corr > b.corr; });
    if ((int)out.size() > maxCount) out.resize(maxCount);
    return out;
}

Stats stats() {
    Stats st;
    st.immediateHooked = g_vt[0] != nullptr;
    st.deferredHooked = g_vt[1] != nullptr;
    st.callsPerFrame = g_lastCalls; st.mainPassPerFrame = g_lastMain; st.appliedPerFrame = g_lastApplied;
    st.botsOnScreen = g_lastBots;
    AcquireSRWLockShared(&g_lock);
    st.classes = (int)g_classes.size();
    ReleaseSRWLockShared(&g_lock);
    return st;
}

void setManual(uintptr_t id, bool on) {
    AcquireSRWLockShared(&g_lock);
    auto it = g_classes.find((ID3D11InputLayout*)id);
    if (it != g_classes.end()) it->second->manual = on;
    ReleaseSRWLockShared(&g_lock);
}

void setPreview(uintptr_t id) { g_preview = (ID3D11InputLayout*)id; }

void resetLearning() {
    AcquireSRWLockExclusive(&g_lock);
    for (auto& kv : g_classes) {
        ClassInfo& c = *kv.second;
        c.samples = 0; c.corr = c.slope = 0; c.detected = c.ignored = false;
    }
    ReleaseSRWLockExclusive(&g_lock);
}
}
