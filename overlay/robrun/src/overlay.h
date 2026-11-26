#pragma once
#include <d3d11.h>
#include <windows.h>

struct OverlayState {
    HWND                    hwnd{};
    ID3D11Device*           device{};
    ID3D11DeviceContext*    ctx{};
    IDXGISwapChain*         swapchain{};
    ID3D11RenderTargetView* rtv{};
    int sw{}, sh{};
};

bool overlay_init(OverlayState& s);
void overlay_begin_frame(OverlayState& s);
void overlay_end_frame(OverlayState& s);
void overlay_cleanup(OverlayState& s);
