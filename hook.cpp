// DX11 Present hook via swapchain vtable patch + ImGui rendering.
// IDXGISwapChain implementations share one vtable per class, so patching the slot of a
// dummy swapchain also redirects the game's own swapchain.
#include "hook.h"
#include "menu.h"
#include "entities.h"
#include "esp.h"
#include "fov.h"
#include "chams.h"
#include "weapon.h"
#include "tracers.h"
#include "godmode.h"
#include "crashlog.h"
#include "aim.h"
#include "magic.h"
#include "norecoil.h"
#include "camhook.h"
#include "volleyfx.h"
#include "crosshair.h"
#include "config.h"
#include "weaponscan.h"
#include "reloadspeed.h"
#include "ui/theme.hpp"
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace hook {
using PresentFn       = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

static constexpr int VT_PRESENT = 8, VT_RESIZEBUFFERS = 13;

static void**          g_vtable = nullptr;
static PresentFn       oPresent = nullptr;
static ResizeBuffersFn oResize  = nullptr;
static WNDPROC         oWndProc = nullptr;
static HWND            g_hwnd   = nullptr;

static ID3D11Device*           g_dev = nullptr;
static ID3D11DeviceContext*    g_ctx = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;
static bool g_imguiReady = false;
static volatile bool g_active = false;

static void createRTV(IDXGISwapChain* sc) {
    ID3D11Texture2D* bb = nullptr;
    if (SUCCEEDED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb)) && bb) {
        g_dev->CreateRenderTargetView(bb, nullptr, &g_rtv);
        bb->Release();
    }
}
static void releaseRTV() { if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; } }

static LRESULT CALLBACK WndProcHook(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (menu::binding && (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN)) {
        if (w != VK_ESCAPE) menu::toggleKey = (int)w;
        menu::binding = false;
        return 0;
    }
    if (menu::binding && (msg == WM_KEYUP || msg == WM_SYSKEYUP || msg == WM_CHAR)) return 0;
    if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && (int)w == menu::toggleKey && !(l & (1 << 30)))
        menu::toggle();

    // freecam: the game must not move the player, so W/A/S/D/Space/Ctrl/Shift presses are swallowed (releases still pass so nothing sticks)
    if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN || msg == WM_CHAR) && camhook::blockKey((int)w) && !menu::isOpen()) return 0;
    if (g_imguiReady && menu::isOpen()) {
        ImGui_ImplWin32_WndProcHandler(h, msg, w, l);
        ImGuiIO& io = ImGui::GetIO();
        // swallow input meant for the menu so the game doesn't also react
        if (io.WantCaptureMouse &&
            ((msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) || msg == WM_INPUT)) return 0;
        if (io.WantCaptureKeyboard &&
            (msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP || msg == WM_CHAR))
            return 0;
    }
    return CallWindowProcW(oWndProc, h, msg, w, l);
}

static void initImGui(IDXGISwapChain* sc) {
    DXGI_SWAP_CHAIN_DESC desc{};
    sc->GetDesc(&desc);
    g_hwnd = desc.OutputWindow;
    if (FAILED(sc->GetDevice(__uuidof(ID3D11Device), (void**)&g_dev)) || !g_dev) return;
    g_dev->GetImmediateContext(&g_ctx);
    createRTV(sc);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    yankeehook::gui::theme::apply();
    yankeehook::gui::theme::load_fonts();   // must precede the backend init
    menu::init();
    config::init();         // load the startup (default) config
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);

    oWndProc = (WNDPROC)SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)WndProcHook);
    g_imguiReady = true;
    chams::install(g_dev, g_ctx);
    reloadspeed::install();
    magic::install();
    camhook::install();
    volleyfx::install();
    norecoil::restore();     // un-patch the kick calls left by an earlier injected build (they also carry the shot sound)
}

static HRESULT STDMETHODCALLTYPE PresentHook(IDXGISwapChain* sc, UINT sync, UINT flags) {
    if (!g_active) return oPresent(sc, sync, flags);
    if (!g_imguiReady) initImGui(sc);

    if (g_imguiReady) {
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        ImGui::GetIO().MouseDrawCursor = menu::isOpen();
        crashlog::note("present: entities");
        entities::update();
        chams::endFrame(esp::botsOnScreenCount());
        crashlog::note("present: tracers");
        tracers::update();
        tracers::draw();
        fov::tick();
        // gun mods: one master switch drives every individual mod (the sliders keep their own values)
        {
            const bool gm = weaponscan::gunMods;
            weaponscan::noSway = gm; weaponscan::fastAds = gm; weaponscan::noDrop = gm; weaponscan::instantReload = gm; weaponscan::apply = gm;
            reloadspeed::infiniteAmmo = gm; reloadspeed::infiniteClip = gm; weapon::noSuppression = gm; weapon::noHeadBob = gm;
            weaponscan::noRecoil = false;
        }
        // instant reload also lets the reload be cancelled from the very start, so the animation tail can't hold you up
        static bool s_instantWas = false;
        if (weaponscan::instantReload) { weapon::reloadCancel = true; weapon::reloadCancelValue = 0.f; s_instantWas = true; }
        else if (s_instantWas) { weapon::reloadCancel = false; s_instantWas = false; }
        // "Instant reload" is the one switch for the whole group
        reloadspeed::enabled = weaponscan::instantReload;
        reloadspeed::forceFinish = weaponscan::instantReload;
        reloadspeed::secondR = weaponscan::instantReload;
        reloadspeed::autoReload = weaponscan::instantReload;
        reloadspeed::clearFlags = false;
        reloadspeed::poll();
        reloadspeed::volleyPoll();
        camhook::poll();
        if (!reloadspeed::enabled) reloadspeed::restoreTables();
        godmode::tick();
        weapon::tick();
        weaponscan::tick();
        esp::draw();
        crosshair::draw();
        aim::update();
        aim::draw();
        crashlog::note("present: menu");
        menu::render();
        ImGui::Render();

        if (g_rtv) {
            g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
            chams::setBypass(true);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            chams::setBypass(false);
        }
    }
    return oPresent(sc, sync, flags);
}

static HRESULT STDMETHODCALLTYPE ResizeBuffersHook(IDXGISwapChain* sc, UINT n, UINT w, UINT h, DXGI_FORMAT f, UINT fl) {
    if (g_imguiReady) { releaseRTV(); ImGui_ImplDX11_InvalidateDeviceObjects(); }
    HRESULT hr = oResize(sc, n, w, h, f, fl);
    if (g_imguiReady) { ImGui_ImplDX11_CreateDeviceObjects(); createRTV(sc); }
    return hr;
}

static bool patchSlot(int idx, void* fn) {
    DWORD old;
    if (!VirtualProtect(&g_vtable[idx], sizeof(void*), PAGE_READWRITE, &old)) return false;
    g_vtable[idx] = fn;
    VirtualProtect(&g_vtable[idx], sizeof(void*), old, &old);
    return true;
}

bool install() {
    // Dummy window + swapchain just to read the vtable.
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"yh_dummy";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* sc = nullptr; ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr;
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                               D3D11_SDK_VERSION, &sd, &sc, &dev, &fl, &ctx);
    bool ok = false;
    if (SUCCEEDED(hr)) {
        g_vtable = *(void***)sc;
        oPresent = (PresentFn)g_vtable[VT_PRESENT];
        oResize  = (ResizeBuffersFn)g_vtable[VT_RESIZEBUFFERS];
        sc->Release(); ctx->Release(); dev->Release();   // vtable lives in dxgi.dll, stays valid
        g_active = true;
        ok = patchSlot(VT_PRESENT, (void*)PresentHook) && patchSlot(VT_RESIZEBUFFERS, (void*)ResizeBuffersHook);
    }
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return ok;
}

void uninstall() {
    g_active = false;
    fov::restore();
    weapon::restore();
    godmode::restore();
    weaponscan::restore();
    chams::uninstall();
    reloadspeed::uninstall();
    magic::uninstall();
    camhook::uninstall();
    volleyfx::uninstall();
    norecoil::restore();
    if (g_vtable) {
        patchSlot(VT_PRESENT, (void*)oPresent);
        patchSlot(VT_RESIZEBUFFERS, (void*)oResize);
    }
    if (g_hwnd && oWndProc) SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)oWndProc);
    Sleep(300);   // let in-flight Present calls drain
    if (g_imguiReady) {
        g_imguiReady = false;
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
    releaseRTV();
    if (g_ctx) { g_ctx->Release(); g_ctx = nullptr; }
    if (g_dev) { g_dev->Release(); g_dev = nullptr; }
}
}
