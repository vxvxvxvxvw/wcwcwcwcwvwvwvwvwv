/// entities.rs — walk UWorld → PlayerArray → entités ennemis
///
/// Chemin de lecture :
///   module_base + UWORLD → UWorld*
///   UWorld + GAME_INSTANCE → UGameInstance*
///   UGameInstance + LOCAL_PLAYERS[0] → ULocalPlayer*
///   ULocalPlayer + PLAYER_CONTROLLER → APlayerController* (local)
///   UWorld + PERSISTENT_LEVEL + ACTORS → TArray<AActor*>
///
/// Pour chaque actor :
///   → Mesh, PlayerState, TeamIndex, Bones, CurrentWeapon, ...

use crate::mem::{read_guest, read_ptr, read_bone, read_fstring, Gva};
use crate::offsets::*;

pub const MAX_ENTITIES: usize = 100;

/// Données d'une entité — tout ce que l'overlay a besoin.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct EntityData {
    pub valid:          bool,
    pub is_local:       bool,
    pub is_bot:         bool,
    pub is_dying:       bool,
    pub is_dbno:        bool,
    pub team_index:     u8,
    /// Position monde (cm) — os tête
    pub head:           [f32; 3],
    /// Position monde (cm) — os pelvis (centre de masse)
    pub origin:         [f32; 3],
    /// Os pour le skeleton ESP : head, neck, l/r shoulder, l/r elbow,
    /// l/r hand, spine, pelvis, l/r hip, l/r knee, l/r foot
    pub bones:          [[f32; 3]; 19],
    pub last_render_time: f32,
    /// Nom du joueur (UTF-16, nul-terminé)
    pub name:           [u16; 64],
    /// Arme actuelle
    pub weapon_name:    [u16; 64],
    pub ammo:           i32,
    pub is_reloading:   bool,
}

impl Default for EntityData {
    fn default() -> Self {
        Self {
            valid:            false,
            is_local:         false,
            is_bot:           false,
            is_dying:         false,
            is_dbno:          false,
            team_index:       0,
            head:             [0.0; 3],
            origin:           [0.0; 3],
            bones:            [[0.0; 3]; 19],
            last_render_time: 0.0,
            name:             [0u16; 64],
            weapon_name:      [0u16; 64],
            ammo:             0,
            is_reloading:     false,
        }
    }
}

/// Snapshot de tous les ennemis visibles.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct WorldSnapshot {
    pub count:    u32,
    pub local_team: u8,
    pub local_fov:  f32,
    pub entities: [EntityData; MAX_ENTITIES],
}

impl Default for WorldSnapshot {
    fn default() -> Self {
        Self {
            count: 0,
            local_team: 0,
            local_fov: 80.0,
            entities: [EntityData::default(); MAX_ENTITIES],
        }
    }
}

/// Point d'entrée — appelé à chaque tick depuis le exit handler (ou VMMCALL).
/// `cr3` : CR3 du process Fortnite.
/// `module_base` : base de FortniteClient-Win64-Shipping.exe.
pub unsafe fn snapshot(cr3: u64, module_base: u64) -> WorldSnapshot {
    let mut snap = WorldSnapshot::default();

    // ── Lire UWorld ──────────────────────────────────────────────────────────
    let uworld_ptr = unsafe { read_ptr(cr3, Gva(module_base + UWORLD)) }.unwrap_or(Gva(0));
    if uworld_ptr.is_null() { return snap; }

    // ── LocalPlayer → PlayerController ───────────────────────────────────────
    let game_instance = unsafe { read_ptr(cr3, uworld_ptr.offset(GAME_INSTANCE)) }.unwrap_or(Gva(0));
    let local_pc = 'local: {
        if game_instance.is_null() { break 'local Gva(0); }
        // LocalPlayers TArray : [ptr, len, cap] → premier élément
        let local_players_ptr = unsafe { read_ptr(cr3, game_instance.offset(LOCAL_PLAYERS)) }.unwrap_or(Gva(0));
        if local_players_ptr.is_null() { break 'local Gva(0); }
        let first_player = unsafe { read_ptr(cr3, local_players_ptr) }.unwrap_or(Gva(0));
        if first_player.is_null() { break 'local Gva(0); }
        unsafe { read_ptr(cr3, first_player.offset(PLAYER_CONTROLLER)) }.unwrap_or(Gva(0))
    };

    // Local pawn
    let local_pawn = if !local_pc.is_null() {
        unsafe { read_ptr(cr3, local_pc.offset(LOCAL_PAWN)) }.unwrap_or(Gva(0))
    } else { Gva(0) };

    // FOV
    if !local_pc.is_null() {
        if let Ok(fov) = unsafe { read_guest::<f32>(cr3, local_pc.offset(FOV)) } {
            if fov > 1.0 { snap.local_fov = fov; }
        }
    }

    // Local team
    let local_team = if !local_pawn.is_null() {
        let ps = unsafe { read_ptr(cr3, local_pawn.offset(PLAYER_STATE)) }.unwrap_or(Gva(0));
        if !ps.is_null() {
            unsafe { read_guest::<u8>(cr3, ps.offset(TEAM_INDEX)) }.unwrap_or(0)
        } else { 0 }
    } else { 0 };
    snap.local_team = local_team;

    // ── ULevel → Actors TArray ───────────────────────────────────────────────
    let level = unsafe { read_ptr(cr3, uworld_ptr.offset(PERSISTENT_LEVEL)) }.unwrap_or(Gva(0));
    if level.is_null() { return snap; }

    // TArray layout : [Data* 8B][Count 4B][Max 4B]
    let actors_data = unsafe { read_ptr(cr3, level.offset(ACTORS)) }.unwrap_or(Gva(0));
    if actors_data.is_null() { return snap; }
    let actors_count: i32 = unsafe { read_guest(cr3, level.offset(ACTORS + 8)) }.unwrap_or(0);
    if actors_count <= 0 { return snap; }

    let count = (actors_count as usize).min(500); // borne de sécurité
    let mut written = 0usize;

    for i in 0..count {
        if written >= MAX_ENTITIES { break; }

        let actor = unsafe { read_ptr(cr3, actors_data.offset(i as u64 * 8)) }.unwrap_or(Gva(0));
        if actor.is_null() { continue; }

        // On ne garde que les pawns avec un Mesh (heuristique : offset MESH non-nul)
        let mesh = unsafe { read_ptr(cr3, actor.offset(MESH)) }.unwrap_or(Gva(0));
        if mesh.is_null() { continue; }

        // PlayerState — si absent, pas un joueur
        let player_state = unsafe { read_ptr(cr3, actor.offset(PLAYER_STATE)) }.unwrap_or(Gva(0));
        if player_state.is_null() { continue; }

        // LastRenderTime — ignorer les entités non-rendues (hors caméra ou trop loin)
        let lrt: f32 = unsafe { read_guest(cr3, mesh.offset(LAST_RENDER_TIME)) }.unwrap_or(0.0);

        // TeamIndex
        let team: u8 = unsafe { read_guest(cr3, player_state.offset(TEAM_INDEX)) }.unwrap_or(0xFF);

        // Flags
        let is_dying:  bool = unsafe { read_guest::<u8>(cr3, actor.offset(B_IS_DYING)) }.unwrap_or(0) != 0;
        let is_dbno:   bool = unsafe { read_guest::<u8>(cr3, actor.offset(B_IS_DBNO)) }.unwrap_or(0) != 0;
        let is_bot:    bool = unsafe { read_guest::<u8>(cr3, player_state.offset(B_IS_A_BOT)) }.unwrap_or(0) != 0;
        let is_local          = actor.0 == local_pawn.0;

        // Bones ───────────────────────────────────────────────────────────────
        // Essayer BoneArray en premier, sinon BoneArray_cache
        let bone_arr_ptr: u64 = unsafe { read_guest(cr3, mesh.offset(BONE_ARRAY)) }.unwrap_or(0);
        let bone_arr = if bone_arr_ptr != 0 { Gva(bone_arr_ptr) } else {
            Gva(unsafe { read_guest(cr3, mesh.offset(BONE_ARRAY_CACHE)) }.unwrap_or(0))
        };

        let mut ent = EntityData {
            valid: true,
            is_local,
            is_bot,
            is_dying,
            is_dbno,
            team_index: team,
            last_render_time: lrt,
            ..Default::default()
        };

        // Lire les os du skeleton
        const BONE_INDICES: &[u32] = &[
            11, // head
            10, // neck
            36, 37, 38, // l shoulder/elbow/hand
            59, 60, 61, // r shoulder/elbow/hand
            9,  8,  7,  // spine3/2/1
            1,          // pelvis
            70, 71, 72, // l hip/knee/foot
            77, 78, 79, // r hip/knee/foot
        ];

        if !bone_arr.is_null() {
            for (slot, &bi) in BONE_INDICES.iter().enumerate() {
                if let Some(pos) = unsafe { read_bone(cr3, bone_arr, bi) } {
                    ent.bones[slot] = pos;
                }
            }
            ent.head   = ent.bones[0];  // head  (bone 11, slot 0)
            ent.origin = ent.bones[12]; // pelvis (bone 1,  slot 12)
        }

        // Nom du joueur
        if let Some(n) = unsafe { read_fstring(cr3, player_state.offset(PLAYER_NAME), 32) } {
            ent.name = n;
        }

        // Arme actuelle
        let weapon = unsafe { read_ptr(cr3, actor.offset(CURRENT_WEAPON)) }.unwrap_or(Gva(0));
        if !weapon.is_null() {
            let wd = unsafe { read_ptr(cr3, weapon.offset(WEAPON_DATA)) }.unwrap_or(Gva(0));
            if !wd.is_null() {
                if let Some(wn) = unsafe { read_fstring(cr3, wd.offset(ITEM_NAME), 32) } {
                    ent.weapon_name = wn;
                }
            }
            ent.ammo = unsafe { read_guest(cr3, weapon.offset(AMMO_COUNT)) }.unwrap_or(0);
            ent.is_reloading = unsafe { read_guest::<u8>(cr3, weapon.offset(B_IS_RELOADING_WEAPON)) }
                .unwrap_or(0) != 0;
        }

        snap.entities[written] = ent;
        written += 1;
    }

    snap.count = written as u32;
    snap
}

// Offsets supplémentaires non dans offsets.rs
const B_IS_RELOADING_WEAPON: u64 = 0x371;
const ITEM_NAME:              u64 = 0x38;
