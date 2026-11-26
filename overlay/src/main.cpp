// main.cpp — fenêtre transparente DirectX 11 + ImGui + VMMCALL loop
//
// Compilation :
//   mkdir build && cd build
//   cmake .. -G "Visual Studio 17 2022" -A x64
//   cmake --build . --config Release
//
// Prérequis :
//   - imgui/ cloné dans overlay/ (git clone https://github.com/ocornut/imgui)
//   - Visual Studio 2022 avec MSVC + ML64 (MASM)
//   - Le hyperviseur Pulzar bootant Windows en guest

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <dxgi.h>
#include <DirectXMath.h>
#include <thread>
#include <atomic>
#include <cstring>

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

#include "vmmcall.hpp"
#include "overlay.hpp"

// ─── Globals DX11 ────────────────────────────────────────────────────────────

static ID3D11Device*           g_device    = nullptr;
static ID3D11DeviceContext*    g_ctx       = nullptr;
static IDXGISwapChain*         g_swap      = nullptr;
static ID3D11RenderTargetView* g_rtv       = nullptr;
static HWND                    g_hwnd      = nullptr;
static int                     g_sw        = 1920;
static int                     g_sh        = 1080;

// ─── Snapshot double-buffer ───────────────────────────────────────────────────

static WorldSnapshot g_snap_a{}, g_snap_b{};
static std::atomic<WorldSnapshot*> g_front { &g_snap_a };
static std::atomic<WorldSnapshot*> g_back  { &g_snap_b };
static std::atomic<bool>           g_snap_ready { false };
static std::atomic<bool>           g_running    { true  };

// ─── Cfg ESP ─────────────────────────────────────────────────────────────────

static EspCfg g_cfg{};

// ─── Camera — lue depuis la fenêtre Fortnite via DWM ─────────────────────────
// On utilise une camera identity par défaut ; pour la matrice réelle,
// il faudrait un read mémoire supplémentaire depuis le snapshot ou
// une API Win32 (magnification, DWM thumbnail). Pour l'instant on stocke
// une matrice perspective cohérente avec les données bones W2S.

static Camera g_cam{};

static void update_camera_identity(float sw, float sh) {
    // Matrice ortho centrée — remplacée dès qu'on a une vraie projection
    using namespace DirectX;
    XMMATRIX proj = XMMatrixPerspectiveFovLH(
        XMConvertToRadians(80.0f), sw / sh, 10.0f, 100000.0f);
    XMStoreFloat4x4(&g_cam.view_proj, proj);
    g_cam.position = {0,0,0};
    g_cam.fov = 80.0f;
}

// ─── Thread VMMCALL ──────────────────────────────────────────────────────────

static void snapshot_thread() {
    while (g_running.load(std::memory_order_relaxed)) {
        WorldSnapshot* buf = g_back.load(std::memory_order_relaxed);
        std::memset(buf, 0, sizeof(*buf));

        uint64_t r = vmmcall_snapshot(buf);
        if (r == 1) {
            // Swap front/back
            g_back.store(g_front.exchange(buf, std::memory_order_acq_rel),
                         std::memory_order_relaxed);
            g_snap_ready.store(true, std::memory_order_release);
        }
        // ~144 Hz max — pas la peine de VMMCALL plus vite
        std::this_thread::sleep_for(std::chrono::milliseconds(7));
    }
}

// ─── DX11 init ───────────────────────────────────────────────────────────────

static bool dx_init(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount        = 2;
    sd.BufferDesc.Format  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.Width   = g_sw;
    sd.BufferDesc.Height  = g_sh;
    sd.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow       = hwnd;
    sd.SampleDesc.Count   = 1;
    sd.Windowed           = TRUE;
    sd.SwapEffect         = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        0, nullptr, 0, D3D11_SDK_VERSION,
        &sd, &g_swap, &g_device, &fl, &g_ctx);
    if (FAILED(hr)) return false;

    ID3D11Texture2D* back = nullptr;
    g_swap->GetBuffer(0, IID_PPV_ARGS(&back));
    g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
    back->Release();
    return true;
}

static void dx_cleanup() {
    if (g_rtv)   { g_rtv->Release();   g_rtv = nullptr; }
    if (g_swap)  { g_swap->Release();  g_swap = nullptr; }
    if (g_ctx)   { g_ctx->Release();   g_ctx = nullptr; }
    if (g_device){ g_device->Release();g_device = nullptr; }
}

// ─── Menu ImGui ──────────────────────────────────────────────────────────────

static bool g_show_menu = false;

static void draw_menu() {
    if (!g_show_menu) return;
    ImGui::SetNextWindowSize({320, 260}, ImGuiCond_Once);
    ImGui::Begin("Pulzar ESP", &g_show_menu);
    ImGui::SeparatorText("ESP");
    ImGui::Checkbox("Boxes",      &g_cfg.boxes);
    ImGui::Checkbox("Skeleton",   &g_cfg.skeleton);
    ImGui::Checkbox("Names",      &g_cfg.names);
    ImGui::Checkbox("Distance",   &g_cfg.distance);
    ImGui::Checkbox("Show bots",  &g_cfg.show_bots);
    ImGui::Checkbox("Show dying", &g_cfg.show_dying);
    ImGui::SliderFloat("Max dist (m)", &g_cfg.max_dist, 10.0f, 600.0f);
    ImGui::SeparatorText("Info");
    WorldSnapshot* s = g_front.load(std::memory_order_acquire);
    ImGui::Text("Entities: %u", s->count);
    ImGui::Text("Snap ready: %s", g_snap_ready.load() ? "yes" : "no");
    ImGui::End();
}

// ─── WndProc ─────────────────────────────────────────────────────────────────

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND, UINT, WPARAM, LPARAM);

static LRESULT CALLBACK wnd_proc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hw, msg, wp, lp)) return 0;
    switch (msg) {
    case WM_KEYDOWN:
        if (wp == VK_INSERT) g_show_menu = !g_show_menu;
        if (wp == VK_END)    g_running.store(false, std::memory_order_relaxed);
        break;
    case WM_DESTROY:
        g_running.store(false, std::memory_order_relaxed);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(hw, msg, wp, lp);
    }
    return 0;
}

// ─── WinMain ─────────────────────────────────────────────────────────────────

int WINAPI WinMain(HINSTANCE hi, HINSTANCE, LPSTR, int) {
    // Trouver résolution primaire
    g_sw = GetSystemMetrics(SM_CXSCREEN);
    g_sh = GetSystemMetrics(SM_CYSCREEN);

    // Créer fenêtre transparente topmost sans taskbar
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = hi;
    wc.lpszClassName = L"PulzarOverlay";
    RegisterClassExW(&wc);

    g_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
        L"PulzarOverlay", L"",
        WS_POPUP,
        0, 0, g_sw, g_sh,
        nullptr, nullptr, hi, nullptr);

    // Transparence via DWM
    SetLayeredWindowAttributes(g_hwnd, RGB(0,0,0), 0, LWA_COLORKEY);
    MARGINS m{-1,-1,-1,-1};
    DwmExtendFrameIntoClientArea(g_hwnd, &m);

    // Rendre la fenêtre click-through
    LONG ex = GetWindowLongW(g_hwnd, GWL_EXSTYLE);
    SetWindowLongW(g_hwnd, GWL_EXSTYLE,
        ex | WS_EX_TRANSPARENT | WS_EX_LAYERED);

    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    if (!dx_init(g_hwnd)) {
        MessageBoxW(nullptr, L"DX11 init failed", L"Error", MB_OK);
        return 1;
    }

    // ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_device, g_ctx);

    update_camera_identity((float)g_sw, (float)g_sh);

    // Lancer le thread VMMCALL
    std::thread snap_thr(snapshot_thread);

    // Message loop
    MSG msg{};
    while (g_running.load(std::memory_order_relaxed)) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT)
                g_running.store(false, std::memory_order_relaxed);
        }

        // Rendre le menu cliquable quand il est ouvert
        LONG ex2 = GetWindowLongW(g_hwnd, GWL_EXSTYLE);
        if (g_show_menu)
            SetWindowLongW(g_hwnd, GWL_EXSTYLE, ex2 & ~WS_EX_TRANSPARENT);
        else
            SetWindowLongW(g_hwnd, GWL_EXSTYLE, ex2 | WS_EX_TRANSPARENT);

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // ESP
        if (g_snap_ready.load(std::memory_order_acquire)) {
            WorldSnapshot* s = g_front.load(std::memory_order_acquire);
            draw_esp(*s, g_cam, (float)g_sw, (float)g_sh, g_cfg);
        }

        draw_menu();

        ImGui::Render();

        const float clear[4] = {0,0,0,0};
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_ctx->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swap->Present(0, 0);
    }

    g_running.store(false, std::memory_order_relaxed);
    snap_thr.join();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    dx_cleanup();
    DestroyWindow(g_hwnd);
    return 0;
}
