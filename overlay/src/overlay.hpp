#pragma once
#include <d3d11.h>
#include <DirectXMath.h>
#include "vmmcall.hpp"

// ── Structures rendu ─────────────────────────────────────────────────────────

struct Vec3 { float x, y, z; };
struct Vec2 { float x, y; };

// Matrice vue-projection (4x4 row-major) lue depuis l'overlay lui-même
// via GetWindowDC trick — on utilise la matrice que Fortnite expose
// dans sa fenêtre via D3D projection, pas de read mémoire ici.
// Le W2S est calculé depuis la projection DX11 courante.

struct Camera {
    DirectX::XMFLOAT4X4 view_proj;
    float fov;
    Vec3  position;
    Vec3  right;
    Vec3  up;
    Vec3  forward;
};

// ── World-to-screen ───────────────────────────────────────────────────────────
// Projette un point monde (Unreal units) → pixels écran.
// Retourne false si derrière la caméra.
bool world_to_screen(const Camera& cam, Vec3 world,
                     float sw, float sh, Vec2& out);

// ── ESP draw ─────────────────────────────────────────────────────────────────
struct EspCfg {
    bool  boxes      = true;
    bool  skeleton   = true;
    bool  names      = true;
    bool  distance   = true;
    bool  health     = false;   // placeholder — pas dans le snapshot
    float max_dist   = 300.0f;  // mètres (1 UU = 1 cm → ×100)
    bool  show_bots  = false;
    bool  show_dying = false;
};

void draw_esp(const WorldSnapshot& snap, const Camera& cam,
              float sw, float sh, const EspCfg& cfg);
