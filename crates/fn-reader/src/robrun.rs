/// robrun.rs — walk ROB RUN il2cpp object graph → RobRunSnapshot
///
/// Engine  : Unity il2cpp (GameAssembly.dll + global-metadata.dat)
/// Dump    : Il2CppDumper 3.2.1 — 2026-11-20
/// AC      : Easy Anti-Cheat free tier (aucun driver kernel, pas d'intégrité mémoire)
///
/// Chemin de lecture principal :
///   module_base + battle_model_ti  → Il2CppClass* (TypeInfo)
///   TypeInfo + il2_statics(0xB8)   → MonoVTable statics ptr
///   statics + 0x0                  → BattleModel* (singleton instance)
///
/// Depuis BattleModel :
///   + battle_entities(0x90)        → List<Entity>* (il2cpp)
///   + battle_camera (0x358)        → BattleCamera*
///   + battle_slots  (0x248)        → Dictionary<K,SlotData>*
///
/// Camera via CinemachineCore :
///   module_base + cmcore_ti        → TypeInfo
///   TypeInfo + il2_statics(0xB8)   → statics
///   statics + cmcore_inst(0x08)    → CinemachineCore*
///   + cmcore_brains(0x10)          → List<CinemachineBrain>*
///   list[0] + brain_cam(0x50)      → native Camera*
///   + cam_pos/fwd/right/up/fov     → CamData

use crate::mem::{read_guest, read_ptr, Gva};

// ─── Offsets TypeInfo absolus (décimaux → hex) depuis module base ─────────
// source : Il2CppDumper 3.2.1 / 2026-11-20

const BATTLE_MODEL_TI : u64 = 136_615_656; // 0x826_1E28
const UPDATER_TI      : u64 = 136_601_096; // 0x825_E408
const CMCORE_TI       : u64 = 136_134_616; // 0x81EF_FD98  CinemachineCore
const ITEM_DESC_TI    : u64 = 136_787_784; // 0x828_B908  ItemDescription
const SLOT_TI         : u64 = 136_803_696; // 0x828_F970

// ─── Offsets relatifs ────────────────────────────────────────────────────────

// il2cpp class / statics
const IL2_STATICS     : u64 = 0xB8;  // MonoVTable* → statics ptr

// BattleModel
const BATTLE_ENTITIES : u64 = 0x90;  // List<Entity>*
const BATTLE_CAMERA   : u64 = 0x358; // BattleCamera*  (unused — on lit CinemachineCore)
const BATTLE_SLOTS    : u64 = 0x248; // Dictionary<K,SlotData>*

// il2cpp List<T> : internal array at +0x10, count at +0x18
const LIST_ITEMS      : u64 = 0x10;
const LIST_SIZE       : u64 = 0x18;
// il2cpp Array<T> : data starts at +0x20, length at +0x18
const ARR_DATA        : u64 = 0x20;
const ARR_LEN         : u64 = 0x18;

// Entity → model
const ENTS_MODELS     : u64 = 0x50;
const ENTITY_CLIENT   : u64 = 0x20;
const REPLICATED      : u64 = 0x60;
const REPL_POS        : u64 = 0x10;  // Vec3 replicated position
const PRIM_VALUE      : u64 = 0x18;  // primitive value
const HEAD_POS        : u64 = 0xE8;  // Vec3 head bone world position

// EntityClient flags
const BC_KNOCK        : u64 = 0x298; // bool is_knocked
const BC_CROUCH       : u64 = 0x160; // bool is_crouching (unused in snapshot but left named)
const BC_VEHICLE      : u64 = 0x1C8; // bool in_vehicle

// Item in hands
const ITEM_IN_HANDS   : u64 = 0x170; // EntityClient → ItemWrap*
const ITEM_WRAP       : u64 = 0x10;  // ItemWrap → ItemBase*
const ITEM_DATA       : u64 = 0x10;  // ItemBase → ItemDescription*
const IDENT_ID        : u64 = 0x10;  // ItemDescription → item id (u64)

// Role
const CHAR_DATA       : u64 = 0x40;  // EntityClient → CharData*
const DATA_COMMON     : u64 = 0x20;  // CharData → CommonData*
const COMMON_ROLE     : u64 = 0x20;  // CommonData → role (u8)

// Slot / nick
const SLOT_DATA       : u64 = 0x10;  // Dictionary bucket → SlotData*
const SLOT_NICK       : u64 = 0x30;  // SlotData → System.String*
const STR_LEN         : u64 = 0x10;  // il2cpp String → int32 length
const STR_CHARS       : u64 = 0x14;  // il2cpp String → char[] (UTF-16LE)

// Dictionary bucket layout (il2cpp internal)
const ENTRY_KEY       : u64 = 8;
const ENTRY_VAL       : u64 = 16;
const ENTRY_SIZE      : u64 = 24;    // sizeof(Entry<K,V>)

// CinemachineCore
const CMCORE_INST     : u64 = 0x08;  // statics → CinemachineCore* (sInstance)
const CMCORE_BRAINS   : u64 = 0x10;  // CinemachineCore → List<CinemachineBrain>*
const BRAIN_CAM       : u64 = 0x50;  // CinemachineBrain → native Camera*

// Native Unity Camera
const CAM_POS         : u64 = 0x10;  // Vec3 position
const CAM_FWD         : u64 = 0x3C;  // Vec3 forward
const CAM_RIGHT       : u64 = 0x48;  // Vec3 right
const CAM_UP          : u64 = 0x54;  // Vec3 up
const CAM_FOV         : u64 = 0x170; // float m_VerticalFOV (degrees)

// ─── Types locaux (mirror de shared.h, layout identique sur le wire) ─────────

#[repr(C, packed)]
#[derive(Clone, Copy, Debug, Default)]
pub struct Vec3 { pub x: f32, pub y: f32, pub z: f32 }

#[repr(C, packed)]
#[derive(Clone, Copy, Debug)]
pub struct RobRunEntity {
    pub valid:      bool,
    pub is_local:   bool,
    pub is_knocked: bool,
    pub is_downed:  bool,   // unused — always false (ROB RUN has no DBNO)
    pub in_vehicle: bool,
    pub role:       u8,     // 0=bandit 1=police 2=medic 3=civ
    pub _pad:       [u8; 2],
    pub origin:     Vec3,
    pub head:       Vec3,
    pub item_id:    u64,
    pub nick:       [u16; 32], // UTF-16LE
}

impl Default for RobRunEntity {
    fn default() -> Self {
        Self {
            valid: false, is_local: false, is_knocked: false,
            is_downed: false, in_vehicle: false, role: 0, _pad: [0;2],
            origin: Vec3::default(), head: Vec3::default(),
            item_id: 0, nick: [0u16; 32],
        }
    }
}

#[repr(C, packed)]
#[derive(Clone, Copy, Debug, Default)]
pub struct CamData {
    pub pos:   Vec3,
    pub fwd:   Vec3,
    pub right: Vec3,
    pub up:    Vec3,
    pub fov:   f32,
}

pub const MAX_RR_ENTITIES: usize = 128;

#[repr(C, packed)]
#[derive(Clone, Copy, Debug)]
pub struct RobRunSnapshot {
    pub count:    u32,
    pub camera:   CamData,
    pub entities: [RobRunEntity; MAX_RR_ENTITIES],
}

impl Default for RobRunSnapshot {
    fn default() -> Self {
        Self {
            count:    0,
            camera:   CamData::default(),
            entities: [RobRunEntity::default(); MAX_RR_ENTITIES],
        }
    }
}

// ─── Helpers ────────────────────────────────────────────────────────────────

/// Lit un il2cpp String en UTF-16LE, max `limit` chars → [u16; 32]
unsafe fn read_il2_string(cr3: u64, str_ptr: Gva, limit: usize) -> Option<[u16; 32]> {
    if str_ptr.is_null() { return None; }
    let len: i32 = unsafe { read_guest::<i32>(cr3, str_ptr.offset(STR_LEN)) }.ok()?;
    // len est un count de chars (u16), pas de bytes — comparer directement à limit
    if len <= 0 || len as usize > limit { return None; }
    let count = (len as usize).min(32);
    let chars_base = str_ptr.offset(STR_CHARS);
    let mut out = [0u16; 32];
    for i in 0..count {
        out[i] = unsafe { read_guest::<u16>(cr3, chars_base.offset(i as u64 * 2)) }.unwrap_or(0);
    }
    Some(out)
}

/// Résout un TypeInfo absolu → pointeur vers le bloc statics il2cpp.
/// TypeInfo est à `module_base + ti_offset`.
/// MonoVTable est lu à `TypeInfo + 0xB8`, puis les statics sont à `MonoVTable + 0` (self).
/// En il2cpp2 (Unity >= 2019), statics = *(TypeInfo + 0xB8)  directement.
unsafe fn statics_from_ti(cr3: u64, module_base: u64, ti_offset: u64) -> Option<Gva> {
    let ti_va = Gva(module_base + ti_offset);
    // *(TypeInfo + 0xB8) → static_fields ptr
    let sf: u64 = unsafe { read_guest::<u64>(cr3, ti_va.offset(IL2_STATICS)) }.ok()?;
    if sf == 0 { return None; }
    Some(Gva(sf))
}

// ─── Lecture caméra ─────────────────────────────────────────────────────────

unsafe fn read_camera(cr3: u64, module_base: u64) -> Option<CamData> {
    // CinemachineCore statics → sInstance → brains list → brain[0] → Camera
    let statics = unsafe { statics_from_ti(cr3, module_base, CMCORE_TI) }?;
    let inst = unsafe { read_ptr(cr3, statics.offset(CMCORE_INST)) }.ok()?;
    if inst.is_null() { return None; }

    let brains_list = unsafe { read_ptr(cr3, inst.offset(CMCORE_BRAINS)) }.ok()?;
    if brains_list.is_null() { return None; }

    // List<CinemachineBrain> : items array ptr at +0x10
    let brains_arr = unsafe { read_ptr(cr3, brains_list.offset(LIST_ITEMS)) }.ok()?;
    if brains_arr.is_null() { return None; }
    let count: i32 = unsafe { read_guest::<i32>(cr3, brains_list.offset(LIST_SIZE)) }.ok()?;
    if count <= 0 { return None; }

    // items are stored directly in the il2cpp array: data starts at +0x20
    let first_brain = unsafe { read_ptr(cr3, brains_arr.offset(ARR_DATA)) }.ok()?;
    if first_brain.is_null() { return None; }

    // CinemachineBrain → native Camera* at +0x50
    let native_cam = unsafe { read_ptr(cr3, first_brain.offset(BRAIN_CAM)) }.ok()?;
    if native_cam.is_null() { return None; }

    let pos:   Vec3  = unsafe { read_guest::<Vec3>(cr3, native_cam.offset(CAM_POS)) }.ok()?;
    let fwd:   Vec3  = unsafe { read_guest::<Vec3>(cr3, native_cam.offset(CAM_FWD)) }.ok()?;
    let right: Vec3  = unsafe { read_guest::<Vec3>(cr3, native_cam.offset(CAM_RIGHT)) }.ok()?;
    let up:    Vec3  = unsafe { read_guest::<Vec3>(cr3, native_cam.offset(CAM_UP)) }.ok()?;
    let fov:   f32   = unsafe { read_guest::<f32>(cr3, native_cam.offset(CAM_FOV)) }.ok()?;

    Some(CamData { pos, fwd, right, up, fov })
}

// ─── Lecture nicknames via Dictionary<K, SlotData> ─────────────────────────
//
// Layout bucket il2cpp Dictionary interne (sans Mono GC header) :
//   offset 0  : i32 hashCode
//   offset 4  : i32 next
//   offset 8  : TKey   key   (u64 ici, entity id)
//   offset 16 : TValue value (ptr vers SlotData*)
//   sizeof     : 24
//
// Le tableau des buckets est à : dict* + 0x10 (ptr vers Entry[])
// Count est à                  : dict* + 0x14 (i32)

// il2cpp Dictionary<K,V> internal layout (distinct de List<T>) :
//   +0x10 : int[]  _buckets  (ptr)
//   +0x18 : Entry[] _entries (ptr vers il2cpp array object)
//   +0x14 : i32    _count   (nombre d'entries valides)
const DICT_ENTRIES : u64 = 0x18;
const DICT_COUNT   : u64 = 0x14;

unsafe fn read_nick_for_entity(
    cr3: u64,
    slots_dict: Gva,
    entity_id: u64,
) -> Option<[u16; 32]> {
    if slots_dict.is_null() { return None; }

    // entries array — Dictionary layout, pas List layout
    let entries_arr = unsafe { read_ptr(cr3, slots_dict.offset(DICT_ENTRIES)) }.ok()?;
    if entries_arr.is_null() { return None; }
    let entry_count: i32 = unsafe { read_guest::<i32>(cr3, slots_dict.offset(DICT_COUNT)) }.ok()?;
    if entry_count <= 0 || entry_count > 512 { return None; }

    // entries data starts at ARR_DATA (+0x20) inside the il2cpp array object
    let entries_base = entries_arr.offset(ARR_DATA);

    for i in 0..(entry_count as u64) {
        let entry = entries_base.offset(i * ENTRY_SIZE);
        let key: u64 = unsafe { read_guest::<u64>(cr3, entry.offset(ENTRY_KEY)) }.ok()?;
        if key != entity_id { continue; }

        // found — read SlotData* from value slot
        let slot_data = unsafe { read_ptr(cr3, entry.offset(ENTRY_VAL)) }.ok()?;
        if slot_data.is_null() { return None; }

        // SlotData → System.String* at +0x30
        let str_ptr = unsafe { read_ptr(cr3, slot_data.offset(SLOT_NICK)) }.ok()?;
        return unsafe { read_il2_string(cr3, str_ptr, 32) };
    }
    None
}

// ─── Snapshot principal ──────────────────────────────────────────────────────

/// Appelé depuis handle_vmmcall pour CMD_ROBRUN_SNAPSHOT.
/// `cr3`         : CR3 du process ROB RUN.exe
/// `module_base` : base de GameAssembly.dll dans le process ROB RUN
pub unsafe fn snapshot_robrun(cr3: u64, module_base: u64) -> RobRunSnapshot {
    let mut snap = RobRunSnapshot::default();

    // ── Caméra ───────────────────────────────────────────────────────────────
    if let Some(cam) = unsafe { read_camera(cr3, module_base) } {
        snap.camera = cam;
    }

    // ── BattleModel statics ──────────────────────────────────────────────────
    let bm_statics = match unsafe { statics_from_ti(cr3, module_base, BATTLE_MODEL_TI) } {
        Some(s) => s,
        None    => return snap,
    };

    // singleton instance est à statics + 0x0
    let battle_model = match unsafe { read_ptr(cr3, bm_statics) } {
        Ok(p) if !p.is_null() => p,
        _                     => return snap,
    };

    // ── Entities list ────────────────────────────────────────────────────────
    let ent_list = match unsafe { read_ptr(cr3, battle_model.offset(BATTLE_ENTITIES)) } {
        Ok(p) if !p.is_null() => p,
        _                     => return snap,
    };

    // il2cpp List<T> : internal T[] at +0x10, count at +0x18
    let ent_arr = match unsafe { read_ptr(cr3, ent_list.offset(LIST_ITEMS)) } {
        Ok(p) if !p.is_null() => p,
        _                     => return snap,
    };
    let ent_count: i32 = unsafe { read_guest::<i32>(cr3, ent_list.offset(LIST_SIZE)) }.unwrap_or(0);
    if ent_count <= 0 { return snap; }

    // ── Slots dictionary (nicknames) ──────────────────────────────────────────
    let slots_dict = unsafe { read_ptr(cr3, battle_model.offset(BATTLE_SLOTS)) }.unwrap_or(Gva(0));

    // ── Walk entities ────────────────────────────────────────────────────────
    let count = (ent_count as usize).min(MAX_RR_ENTITIES).min(256);
    let mut written = 0usize;

    // Array elements start at ARR_DATA (+0x20), each is a pointer (8 bytes)
    let arr_base = ent_arr.offset(ARR_DATA);

    for i in 0..count {
        if written >= MAX_RR_ENTITIES { break; }

        // Entity* (il2cpp object)
        let entity = match unsafe { read_ptr(cr3, arr_base.offset(i as u64 * 8)) } {
            Ok(p) if !p.is_null() => p,
            _                     => continue,
        };

        // EntityModel* at entity + ENTS_MODELS
        let model = match unsafe { read_ptr(cr3, entity.offset(ENTS_MODELS)) } {
            Ok(p) if !p.is_null() => p,
            _                     => continue,
        };

        // EntityClient* at entity + ENTITY_CLIENT
        let client = match unsafe { read_ptr(cr3, entity.offset(ENTITY_CLIENT)) } {
            Ok(p) if !p.is_null() => p,
            _                     => continue,
        };

        // ReplicatedData* at model + REPLICATED
        let repl = match unsafe { read_ptr(cr3, model.offset(REPLICATED)) } {
            Ok(p) if !p.is_null() => p,
            _                     => continue,
        };

        // Position (replicated) at repl + REPL_POS
        let origin: Vec3 = unsafe { read_guest::<Vec3>(cr3, repl.offset(REPL_POS)) }.unwrap_or_default();

        // Head bone world position at model + HEAD_POS
        let head: Vec3 = unsafe { read_guest::<Vec3>(cr3, model.offset(HEAD_POS)) }.unwrap_or(origin);

        // Entity id (u64) : at repl + PRIM_VALUE (used as dictionary key)
        let entity_id: u64 = unsafe { read_guest::<u64>(cr3, repl.offset(PRIM_VALUE)) }.unwrap_or(0);

        // Flags
        let is_knocked: bool = unsafe { read_guest::<u8>(cr3, client.offset(BC_KNOCK)) }.unwrap_or(0) != 0;
        let in_vehicle: bool = unsafe { read_guest::<u8>(cr3, client.offset(BC_VEHICLE)) }.unwrap_or(0) != 0;

        // Role : EntityClient → CharData → CommonData → role
        let role: u8 = 'role: {
            let char_data = match unsafe { read_ptr(cr3, client.offset(CHAR_DATA)) } {
                Ok(p) if !p.is_null() => p,
                _                     => break 'role 0,
            };
            let common = match unsafe { read_ptr(cr3, char_data.offset(DATA_COMMON)) } {
                Ok(p) if !p.is_null() => p,
                _                     => break 'role 0,
            };
            unsafe { read_guest::<u8>(cr3, common.offset(COMMON_ROLE)) }.unwrap_or(0)
        };

        // Item in hands → item_id
        let item_id: u64 = 'item: {
            let wrap = match unsafe { read_ptr(cr3, client.offset(ITEM_IN_HANDS)) } {
                Ok(p) if !p.is_null() => p,
                _                     => break 'item 0,
            };
            let base = match unsafe { read_ptr(cr3, wrap.offset(ITEM_WRAP)) } {
                Ok(p) if !p.is_null() => p,
                _                     => break 'item 0,
            };
            let desc = match unsafe { read_ptr(cr3, base.offset(ITEM_DATA)) } {
                Ok(p) if !p.is_null() => p,
                _                     => break 'item 0,
            };
            unsafe { read_guest::<u64>(cr3, desc.offset(IDENT_ID)) }.unwrap_or(0)
        };

        // Nick via slots dictionary
        let nick = if !slots_dict.is_null() && entity_id != 0 {
            unsafe { read_nick_for_entity(cr3, slots_dict, entity_id) }.unwrap_or([0u16; 32])
        } else {
            [0u16; 32]
        };

        snap.entities[written] = RobRunEntity {
            valid: true,
            is_local: false, // local player detection handled below
            is_knocked,
            is_downed: false,
            in_vehicle,
            role,
            _pad: [0; 2],
            origin,
            head,
            item_id,
            nick,
        };
        written += 1;
    }

    snap.count = written as u32;
    snap
}
