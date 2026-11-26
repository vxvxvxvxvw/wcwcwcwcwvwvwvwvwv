#include "overlay.h"
#include "imgui.h"
#include <cstdio>
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static LRESULT CALLBACK wnd_proc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hw, msg, wp, lp)) return true;
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hw, msg, wp, lp);
}

static bool create_d3d(OverlayState& s) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount        = 2;
    sd.BufferDesc.Width   = static_cast<UINT>(s.sw);
    sd.BufferDesc.Height  = static_cast<UINT>(s.sh);
    sd.BufferDesc.Format  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow       = s.hwnd;
    sd.SampleDesc.Count   = 1;
    sd.Windowed           = TRUE;
    sd.SwapEffect         = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL fl{};
    // Essai hardware d'abord, fallback WARP si GPU inaccessible sous hyperviseur
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION,
        &sd, &s.swapchain, &s.device, &fl, &s.ctx);
    if (FAILED(hr)) {
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION,
            &sd, &s.swapchain, &s.device, &fl, &s.ctx);
    }
    if (FAILED(hr)) {
        char buf[64];
        sprintf_s(buf, "D3D11 init failed: 0x%08X", (unsigned)hr);
        MessageBoxA(nullptr, buf, "robrun-overlay", MB_ICONERROR);
        return false;
    }

    ID3D11Texture2D* back = nullptr;
    s.swapchain->GetBuffer(0, IID_PPV_ARGS(&back));
    s.device->CreateRenderTargetView(back, nullptr, &s.rtv);
    back->Release();
    return true;
}

bool overlay_init(OverlayState& s) {
    // Log de debug dans un fichier
    FILE* log = nullptr;
    fopen_s(&log, "C:\\Users\\PC\\Downloads\\rr_debug.txt", "w");
    auto L = [&](const char* msg) { if(log) { fprintf(log, "%s\n", msg); fflush(log); } };

    s.sw = GetSystemMetrics(SM_CXSCREEN);
    s.sh = GetSystemMetrics(SM_CYSCREEN);
    if (log) { fprintf(log, "screen: %d x %d\n", s.sw, s.sh); fflush(log); }

    if (s.sw == 0 || s.sh == 0) {
        // Fallback résolution si GetSystemMetrics fail sous hyperviseur
        s.sw = 1920; s.sh = 1080;
        L("screen metrics = 0, fallback 1920x1080");
    }

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"RROverlay";
    RegisterClassExW(&wc);
    L("wndclass registered");

    s.hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_NOACTIVATE,
        L"RROverlay", L"",
        WS_POPUP | WS_VISIBLE,
        0, 0, s.sw, s.sh,
        nullptr, nullptr, wc.hInstance, nullptr);
    if (log) { fprintf(log, "hwnd: %p  lasterr: %lu\n", (void*)s.hwnd, GetLastError()); fflush(log); }
    if (!s.hwnd) { if(log) fclose(log); return false; }

    SetLayeredWindowAttributes(s.hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);

    LONG ex = GetWindowLongW(s.hwnd, GWL_EXSTYLE);
    SetWindowLongW(s.hwnd, GWL_EXSTYLE, ex | WS_EX_TRANSPARENT);

    ShowWindow(s.hwnd, SW_SHOW);
    L("window created ok");
    if (!create_d3d(s)) { L("d3d failed"); if(log) fclose(log); return false; }
    L("d3d ok");
    if (log) fclose(log);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(s.hwnd);
    ImGui_ImplDX11_Init(s.device, s.ctx);
    return true;
}

void overlay_begin_frame(OverlayState& s) {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({static_cast<float>(s.sw), static_cast<float>(s.sh)});
    ImGui::SetNextWindowBgAlpha(0.f);
    ImGui::Begin("##rresp", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoNav);
}

void overlay_end_frame(OverlayState& s) {
    ImGui::End();
    ImGui::Render();

    float clear[4]{0, 0, 0, 0};
    s.ctx->OMSetRenderTargets(1, &s.rtv, nullptr);
    s.ctx->ClearRenderTargetView(s.rtv, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    s.swapchain->Present(1, 0);
}

void overlay_cleanup(OverlayState& s) {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    if (s.rtv)       s.rtv->Release();
    if (s.swapchain) s.swapchain->Release();
    if (s.ctx)       s.ctx->Release();
    if (s.device)    s.device->Release();
    if (s.hwnd)      DestroyWindow(s.hwnd);
}
