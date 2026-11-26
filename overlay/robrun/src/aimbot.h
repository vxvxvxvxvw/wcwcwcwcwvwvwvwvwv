#pragma once
#include "shared.h"

// ── Aimbot config (driven from main.cpp ImGui menu) ───────────────────────────
extern bool  g_aim_on;
extern bool  g_aim_bone_head;   // true = head, false = chest
extern float g_aim_smooth;      // 0.0 (instant) .. 0.95 (heavy smoothing)
extern float g_aim_fov;         // degrees, cone around crosshair
extern bool  g_aim_spread;      // NOP out weapon spread multipliers

// Resolve the ROB RUN process + GameAssembly.dll + local input model pointer.
// Logs to C:\Users\PC\Downloads\rr_aim.txt. Returns true on success.
bool aimbot_init();

// Per-frame: pick best target from the hypervisor snapshot, compute angle,
// write look direction into the game. No-op if disabled or init failed.
void aimbot_update(const RobRunSnapshot& snap);