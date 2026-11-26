// shared.h — robrun-overlay  |  il2cpp GameAssembly.dll — dumped 2026-11-20
// Layout partagé overlay ↔ hyperviseur Pulzar AMD SVM ring-1
// DOIT correspondre exactement à RobRunSnapshot dans crates/exits/src/robrun.rs
// Si tu modifies ici, modifie aussi le Rust.

#pragma once
#include <cstdint>
#include <cstring>

// ── Commande hypercall ────────────────────────────────────────────────────────
static constexpr uint64_t CMD_ROBRUN_SNAPSHOT = 0x524F4201ULL; // 'ROB\x02'

// ── Nombre max d'entités dans le snapshot ────────────────────────────────────
static constexpr uint32_t MAX_RR_ENTITIES = 128;

// ── Offsets il2cpp — GameAssembly.dll base relative (dump 2026-11-20) ────────
// Changer uniquement cette section après un re-dump Il2CppDumper.
namespace off {

    // ---- TypeInfo statics ----
    static constexpr uint64_t BATTLE_MODEL_TI   = 136615656ULL; // BattleModel TypeInfo
    static constexpr uint64_t CMCORE_TI         = 136134616ULL; // CinemachineCore TypeInfo
    static constexpr uint64_t UPDATER_TI        = 136601096ULL; // BattleEntitiesUpdater TypeInfo
    static constexpr uint64_t ITEM_DESC_TI      = 136787784ULL; // InventoryItemDescriptions TI
    static constexpr uint64_t SLOT_TI           = 136803696ULL; // InventorySlot TI

    // ---- Il2CppClass ----
    static constexpr uint32_t IL2_STATICS       = 0xB8;  // Il2CppClass.static_fields

    // ---- BattleModel (static) ----
    static constexpr uint32_t BATTLE_ENTITIES   = 0x90;  // BattleModel → BattleEntitiesModel
    static constexpr uint32_t BATTLE_CAMERA     = 0x358; // BattleModel → BattleCameraModel
    static constexpr uint32_t BATTLE_PARTY      = 0x448; // BattleModel → BattlePartyModel
    static constexpr uint32_t BATTLE_SLOTS      = 0x248; // BattleModel → BattleSlotsModel

    // ---- BattleEntitiesModel ----
    static constexpr uint32_t ENTS_MODELS       = 0x50;  // BattleEntitiesModel._models (il2cpp array ptr)

    // ---- Il2CppArray ----
    static constexpr uint32_t ARR_DATA          = 0x20;  // array data start
    static constexpr uint32_t ARR_LEN           = 0x18;  // Il2CppArray.length

    // ---- BattleCharacterEntityModel (each entity in array) ----
    static constexpr uint32_t CHAR_ID           = 0x38;  // entity id
    static constexpr uint32_t CHAR_DATA         = 0x40;  // → BattleCharacterData
    static constexpr uint32_t CHAR_CFG          = 0x48;  // → BattleCharacterConfig

    // ---- BattleCharacterData ----
    static constexpr uint32_t DATA_COMMON       = 0x20;  // → BattleCharacterCommonData

    // ---- BattleCharacterCommonData ----
    static constexpr uint32_t COMMON_ROLE       = 0x20;  // Role (uint8)

    // ---- BattleCharacterConfig ----
    static constexpr uint32_t CFG_PHYSICS       = 0x18;  // → physics config
    static constexpr uint32_t STAND_LEN         = 0x1C;  // standHeight (float)

    // ---- Replicated position ----
    static constexpr uint32_t REPLICATED        = 0x60;  // BattleCharacterEntityModel.ReplicatedModel
    static constexpr uint32_t REPL_POS          = 0x10;  // ReplicatedModel.Position → Vec3

    // ---- Head position (from view) ----
    // EntityView → MovementView+0x20 → Transform+0x20 → head_pos offset
    static constexpr uint32_t HEAD_POS          = 0xE8;  // head_pos on view node

    // ---- BattleCharacterModel fields ----
    static constexpr uint32_t CHAR_PRIVATE      = 0x10;  // → BattleCharacterPrivateModel
    static constexpr uint32_t BC_KNOCK          = 0x298; // → KnockModel
    static constexpr uint32_t BC_VEHICLE        = 0x1C8; // → VehicleModel (ptr, 0=not in veh)
    static constexpr uint32_t BC_ARMOR          = 0x1B8; // armor value
    static constexpr uint32_t BC_SPRINT         = 0x148; // is_sprinting bool
    static constexpr uint32_t BC_CROUCH         = 0x160; // is_crouching bool
    static constexpr uint32_t ITEM_IN_HANDS     = 0x170; // → ItemInHandsModel
    static constexpr uint32_t BC_ARREST         = 0x250; // arrest state
    static constexpr uint32_t BC_MELEE          = 0x288; // melee state
    static constexpr uint32_t BC_RELOAD         = 0x158; // reload state
    static constexpr uint32_t BC_CLICK          = 0x1C0; // WeaponClickTickIndex

    // ---- KnockModel ----
    static constexpr uint32_t KO_PHASE          = 0x18;  // phase (0 = alive)
    static constexpr uint32_t KO_END            = 0x10;  // ko end tick

    // ---- ItemInHandsModel ----
    static constexpr uint32_t ITEM_WRAP         = 0x10;  // Current wrapper
    static constexpr uint32_t ITEM_DATA         = 0x10;  // EquipItemModel._data
    static constexpr uint32_t IDENT_ID          = 0x10;  // Identified.Id (uint64)

    // ---- Party / nick ----
    static constexpr uint32_t PARTY_MEMBERS     = 0x18;  // BattlePartyModel → members list
    static constexpr uint32_t GLOBAL_PARTY      = 0xC8;  // BattleModel → global party
    static constexpr uint32_t SLOT_BY_ENT       = 0x18;  // slot lookup by entity
    static constexpr uint32_t SLOT_NICK         = 0x30;  // → il2cpp string
    static constexpr uint32_t SLOT_DATA         = 0x10;  // slot data ptr
    static constexpr uint32_t STR_LEN           = 0x10;  // Il2CppString.length
    static constexpr uint32_t STR_CHARS         = 0x14;  // Il2CppString.chars (UTF-16)

    // ---- Il2cpp List<T> ----
    static constexpr uint32_t LIST_ITEMS        = 0x10;  // List._items (array ptr)
    static constexpr uint32_t LIST_SIZE         = 0x18;  // List._size

    // ---- CinemachineCore ----
    static constexpr uint32_t CMCORE_INST       = 0x08;  // sInstance
    static constexpr uint32_t CMCORE_BRAINS     = 0x10;  // mActiveBrains List

    // ---- CinemachineBrain ----
    static constexpr uint32_t BRAIN_CAM         = 0x50;  // m_OutputCamera (UnityEngine.Camera)

    // ---- Camera ----
    static constexpr uint32_t CAM_POS           = 0x10;  // position Vec3
    static constexpr uint32_t CAM_FWD           = 0x3C;  // forward Vec3
    static constexpr uint32_t CAM_RIGHT         = 0x48;  // right Vec3
    static constexpr uint32_t CAM_UP            = 0x54;  // up Vec3
    static constexpr uint32_t CAM_FOV           = 0x170; // m_VerticalFOV (float, degrés)

    // ---- Dict ----
    static constexpr uint32_t DICT_ENTRIES      = 0x18;  // Dictionary internal entries array
    static constexpr uint32_t ENTRY_KEY         = 8;
    static constexpr uint32_t ENTRY_VALUE       = 16;
    static constexpr uint32_t ENTRY_SIZE        = 24;    // stride per entry

    // ---- PEB / module walk (utilisé par le hyperviseur pour trouver GameAssembly.dll) ----
    static constexpr uint32_t PEB_LDR           = 0x18;
    static constexpr uint32_t LDR_LIST          = 0x20;
    static constexpr uint32_t MOD_BASE          = 0x20;
    static constexpr uint32_t MOD_SIZE          = 0x30;
    static constexpr uint32_t MOD_NAME_LEN      = 0x48;
    static constexpr uint32_t MOD_NAME          = 0x50;

} // namespace off

// ── Structures partagées overlay ↔ hyperviseur ───────────────────────────────
#pragma pack(push, 1)

struct Vec3 { float x, y, z; };

struct RobRunEntity {
    bool     valid;
    bool     is_local;
    bool     is_knocked;    // ko_phase != 0
    bool     is_downed;     // non utilisé dans ROB RUN
    bool     in_vehicle;    // bc_vehicle ptr != 0
    bool     is_sprinting;
    bool     is_crouching;
    uint8_t  role;          // 0=bandit 1=police 2=medic 3=civ
    Vec3     origin;        // position pieds (répliquée)
    Vec3     head;          // position tête
    uint64_t item_id;       // item en main (Identified.Id), 0=mains vides
    uint32_t armor;         // bc_armor value
    uint8_t  _pad[4];
    uint16_t nick[32];      // UTF-16LE null-terminated (depuis slot_nick)
};

struct CamData {
    Vec3  pos;
    Vec3  fwd;
    Vec3  right;
    Vec3  up;
    float fov;              // m_VerticalFOV en degrés (CinemachineCore → Camera)
};

struct RobRunSnapshot {
    uint32_t     count;
    CamData      camera;
    RobRunEntity entities[MAX_RR_ENTITIES];
};

#pragma pack(pop)
