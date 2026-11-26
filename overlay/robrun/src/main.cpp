/// robrun-overlay — ESP externe ROB RUN via hyperviseur Pulzar AMD SVM
///
/// Ring-3 dans le guest Windows.
/// Émet VMMCALL CMD_ROBRUN_SNAPSHOT → Pulzar lit GameAssembly.dll depuis ring-1
/// → remplit RobRunSnapshot → overlay DX11 + ImGui dessine l'ESP.
///
/// Build : MSVC 2022 x64, /W4 /permissive- /std:c++20
/// Dépendances : ImGui (backends dx11 + win32), d3d11, dxgi, dwmapi, DirectXMath
///
/// Hotkeys :
///   INSERT  → ouvrir/fermer le menu ImGui
///   F1      → toggle ESP on/off
///   F2      → toggle boîtes
///   F3      → toggle snaplines
///   F4      → toggle noms
///   F5      → toggle distance
///   END     → quitter

#include <windows.h>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstdio>

#include "shared.h"
#include "hypercall.h"
#include "overlay.h"
#include "esp.h"
#include "aimbot.h"
#include "imgui.h"

// ── State global ─────────────────────────────────────────────────────────────
static RobRunSnapshot        g_snap{};
static std::atomic<bool>     g_running{true};
static std::atomic<bool>     g_esp_on{true};
static std::atomic<uint32_t> g_hc_ok{0};
static std::atomic<uint32_t> g_hc_fail{0};
static bool                  g_menu_open = false;

// ── Thread de lecture hyperviseur (~120 Hz) ───────────────────────────────────
static void reader_thread() {
    while (g_running.load(std::memory_order_relaxed)) {
        RobRunSnapshot local{};
        if (hypercall_robrun(local)) {
            // Pas de mutex — un frame de décalage est tolérable pour de l'ESP.
            g_snap = local;
            g_hc_ok.fetch_add(1, std::memory_order_relaxed);
        } else {
            g_hc_fail.fetch_add(1, std::memory_order_relaxed);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
}

// ── Menu ImGui ───────────────────────────────────────────────────────────────
static void draw_menu(float sw, float sh) {
    // Fenêtre flottante centrée au premier appel
    ImGui::SetNextWindowPos({sw * 0.5f - 180.f, sh * 0.5f - 180.f},
                            ImGuiCond_Once);
    ImGui::SetNextWindowSize({360.f, 360.f}, ImGuiCond_Once);
    ImGui::SetNextWindowBgAlpha(0.85f);

    ImGui::Begin("ROB RUN Overlay", nullptr,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);

    ImGui::TextColored({0.3f, 1.f, 0.5f, 1.f}, "Pulzar AMD SVM ring-1");

    uint32_t ok   = g_hc_ok.load(std::memory_order_relaxed);
    uint32_t fail = g_hc_fail.load(std::memory_order_relaxed);
    ImGui::Text("Hypercalls  ok: %u  fail: %u", ok, fail);
    ImGui::Text("Entites visibles: %u", g_snap.count);
    ImGui::Separator();

    // ── ESP master ──────────────────────────────────────────────────────────
    bool esp = g_esp_on.load(std::memory_order_relaxed);
    if (ImGui::Checkbox("ESP on (F1)", &esp))
        g_esp_on.store(esp, std::memory_order_relaxed);

    ImGui::Separator();
    ImGui::TextDisabled("-- Elements --");

    ImGui::Checkbox("Boites 2D   (F2)", &g_esp_boxes);
    ImGui::Checkbox("Snaplines   (F3)", &g_esp_snapline);
    ImGui::Checkbox("Noms        (F4)", &g_esp_names);
    ImGui::Checkbox("Distances   (F5)", &g_esp_dist);
    ImGui::Checkbox("Role [tag]",        &g_esp_role);
    ImGui::Checkbox("Barre armure",      &g_esp_armor);
    ImGui::Checkbox("Etats KO/VEH",      &g_esp_state);

    ImGui::Separator();
    ImGui::TextDisabled("-- Aimbot --");
    ImGui::Checkbox("Aimbot on (F6)",   &g_aim_on);
    ImGui::Checkbox("Bone: head",       &g_aim_bone_head);
    ImGui::Checkbox("Zero spread",      &g_aim_spread);
    ImGui::SliderFloat("Smooth",        &g_aim_smooth, 0.0f, 0.95f, "%.2f");
    ImGui::SliderFloat("FOV deg",       &g_aim_fov,   1.f,  60.f,  "%.0f");

    ImGui::Separator();
    ImGui::TextDisabled("-- Camera --");
    ImGui::Text("Pos  %.1f %.1f %.1f",
        g_snap.camera.pos.x, g_snap.camera.pos.y, g_snap.camera.pos.z);
    ImGui::Text("FOV  %.1f deg", g_snap.camera.fov);

    ImGui::Separator();
    ImGui::TextColored({1.f, 0.4f, 0.4f, 1.f}, "END = quitter");

    ImGui::End();
}

// ── Debounce simple pour les hotkeys ─────────────────────────────────────────
struct KeyDebounce {
    int      vk;
    bool     was_down = false;
    bool     triggered() {
        bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
        bool edge = down && !was_down;
        was_down  = down;
        return edge;
    }
};

// ── WinMain ──────────────────────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    // Pulzar se cache du guest (CPUID hypervisor bit = 0 par design).
    // On skip la vérif CPUID — le VMMCALL prouvera si l'hyperviseur répond.

    OverlayState ov{};
    if (!overlay_init(ov)) {
        MessageBoxW(nullptr, L"Overlay D3D11 init failed", L"robrun-overlay", MB_ICONERROR);
        return 1;
    }

    // Aimbot init (resolve game process + input model pointer). Logs to rr_aim.txt.
    if (!aimbot_init()) {
        // Non-fatal: ESP still works; aimbot disabled.
    }

    std::thread reader(reader_thread);

    float sw = static_cast<float>(ov.sw);
    float sh = static_cast<float>(ov.sh);

    KeyDebounce k_insert {VK_INSERT};
    KeyDebounce k_f1     {VK_F1};
    KeyDebounce k_f2     {VK_F2};
    KeyDebounce k_f3     {VK_F3};
    KeyDebounce k_f4     {VK_F4};
    KeyDebounce k_f5     {VK_F5};
    KeyDebounce k_f6     {VK_F6};
    KeyDebounce k_end    {VK_END};

    MSG msg{};
    while (true) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) goto done;
        }

        // ── Hotkeys ──────────────────────────────────────────────────────────
        if (k_end.triggered())    goto done;

        if (k_insert.triggered()) g_menu_open = !g_menu_open;

        if (k_f1.triggered()) {
            bool v = g_esp_on.load(std::memory_order_relaxed);
            g_esp_on.store(!v, std::memory_order_relaxed);
        }
        if (k_f2.triggered()) g_esp_boxes    = !g_esp_boxes;
        if (k_f3.triggered()) g_esp_snapline = !g_esp_snapline;
        if (k_f4.triggered()) g_esp_names    = !g_esp_names;
        if (k_f5.triggered()) g_esp_dist     = !g_esp_dist;
        if (k_f6.triggered()) g_aim_on       = !g_aim_on;

        // ── Frame ─────────────────────────────────────────────────────────────
        overlay_begin_frame(ov);

        // ESP
        if (g_esp_on.load(std::memory_order_relaxed)) {
            ImDrawList* dl = ImGui::GetForegroundDrawList();
            draw_esp(dl, g_snap, sw, sh);
        }

        // Aimbot (writes look direction into game memory)
        aimbot_update(g_snap);

        // HUD compact (toujours visible, coin haut-gauche)
        {
            ImGui::SetNextWindowPos({6.f, 6.f});
            ImGui::SetNextWindowSize({0.f, 0.f});
            ImGui::SetNextWindowBgAlpha(0.45f);
            ImGui::Begin("##hud", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoMove);

            uint32_t ok   = g_hc_ok.load(std::memory_order_relaxed);
            uint32_t fail = g_hc_fail.load(std::memory_order_relaxed);
            bool     on   = g_esp_on.load(std::memory_order_relaxed);

            ImGui::TextColored(on ? ImVec4{0.2f,1.f,0.4f,1.f} : ImVec4{1.f,0.4f,0.4f,1.f},
                "ROB RUN | ents:%u | hc:%u/%u | %s | aim:%s | INS=menu",
                g_snap.count, ok, fail, on ? "ESP ON" : "OFF", g_aim_on ? "ON" : "off");

            ImGui::End();
        }

        // Menu
        if (g_menu_open) {
            // Rend la fenêtre cliquable quand le menu est ouvert
            LONG ex = GetWindowLongW(ov.hwnd, GWL_EXSTYLE);
            SetWindowLongW(ov.hwnd, GWL_EXSTYLE, ex & ~WS_EX_TRANSPARENT);
            draw_menu(sw, sh);
        } else {
            // Click-through quand menu fermé
            LONG ex = GetWindowLongW(ov.hwnd, GWL_EXSTYLE);
            SetWindowLongW(ov.hwnd, GWL_EXSTYLE, ex | WS_EX_TRANSPARENT);
        }

        overlay_end_frame(ov);
    }

done:
    g_running = false;
    reader.join();
    overlay_cleanup(ov);
    return 0;
}
