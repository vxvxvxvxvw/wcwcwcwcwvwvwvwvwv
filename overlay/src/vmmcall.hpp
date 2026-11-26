#pragma once
// vmmcall.hpp — interface VMMCALL vers le hyperviseur Pulzar
// Le ring-3 overlay émet un VMMCALL avec RAX = CMD_FN_SNAPSHOT,
// RDI = adresse buffer, RSI = taille buffer.
// Le hyperviseur intercepte le VMEXIT, remplit le buffer, et resume.

#include <cstdint>
#include <cstddef>
#include <array>

// ── Commande ─────────────────────────────────────────────────────────────────
static constexpr uint64_t CMD_FN_SNAPSHOT = 0x464E0001ULL;

// ── Layout WorldSnapshot — doit correspondre exactement à fn-reader/entities.rs
static constexpr size_t MAX_ENTITIES = 100;

#pragma pack(push, 1)
struct EntityData {
    bool     valid;
    bool     is_local;
    bool     is_bot;
    bool     is_dying;
    bool     is_dbno;
    uint8_t  team_index;
    float    head[3];
    float    origin[3];
    float    bones[19][3];
    float    last_render_time;
    uint16_t name[64];
    uint16_t weapon_name[64];
    int32_t  ammo;
    bool     is_reloading;
};

struct WorldSnapshot {
    uint32_t   count;                      // nombre d'entités valides
    EntityData entities[MAX_ENTITIES];
};
#pragma pack(pop)

// Déclaration extern "C" au niveau global — MSVC C2598 sinon
#ifdef _M_X64
extern "C" uint64_t _vmmcall_snapshot_asm(uint64_t cmd, void* buf, uint64_t sz);
#endif

// ── VMMCALL ───────────────────────────────────────────────────────────────────
inline uint64_t vmmcall_snapshot(WorldSnapshot* buf) {
#ifdef _M_X64
    return _vmmcall_snapshot_asm(CMD_FN_SNAPSHOT, buf, sizeof(WorldSnapshot));
#else
    return 0;
#endif
}
