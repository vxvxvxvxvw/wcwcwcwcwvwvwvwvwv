/// Offsets Fortnite — SDK 2026-11-25
/// Regénérer depuis fn-offsets.json après chaque mise à jour du jeu.
/// Toutes les valeurs sont des offsets relatifs sauf UWORLD/gEngine
/// qui sont des offsets dans client_fortnite.dll.

// ── Pointeurs globaux dans le module ─────────────────────────────────────────
/// client_fortnite.dll + UWORLD → *UWorld
pub const UWORLD:          u64 = 0x1C14D610;
/// client_fortnite.dll + gEngine → *UEngine  (pour les coordonnées écran)
pub const G_ENGINE:        u64 = 0x1C14F058;

// ── UWorld ────────────────────────────────────────────────────────────────────
pub const PERSISTENT_LEVEL: u64 = 0x38;   // UWorld → ULevel*

// ── ULevel ────────────────────────────────────────────────────────────────────
pub const ACTORS:            u64 = 0x208; // ULevel → TArray<AActor*>

// ── UGameInstance ─────────────────────────────────────────────────────────────
pub const GAME_INSTANCE:     u64 = 0x248; // UWorld → UGameInstance*
pub const LOCAL_PLAYERS:     u64 = 0x38;  // UGameInstance → TArray<ULocalPlayer*>

// ── ULocalPlayer ─────────────────────────────────────────────────────────────
pub const PLAYER_CONTROLLER: u64 = 0x30;  // ULocalPlayer → APlayerController*

// ── APlayerController ────────────────────────────────────────────────────────
pub const LOCAL_PAWN:         u64 = 0x320; // APlayerController → APawn*
pub const PAWN_PRIVATE:       u64 = 0x2F0; // AController → APawn*
pub const FOV:                u64 = 0x37C; // APlayerCameraManager → float
pub const TARGETED_FORT_PAWN: u64 = 0x1750;

// ── APawn / AFortPawn ────────────────────────────────────────────────────────
pub const MESH:               u64 = 0x2F8; // AFortPawn → USkeletalMeshComponent*
pub const ROOT_COMPONENT:     u64 = 0x1B0;
pub const PLAYER_STATE:       u64 = 0x298; // APawn → APlayerState*
pub const CURRENT_WEAPON:     u64 = 0xA18; // AFortPawn → AFortWeapon*
pub const B_IS_DYING:         u64 = 0x770;
pub const B_IS_DBNO:          u64 = 0x8CA;
pub const B_IS_CROUCHED:      u64 = 0x440;

// ── APlayerState ─────────────────────────────────────────────────────────────
pub const TEAM_INDEX:    u64 = 0xF81;
pub const PLAYER_NAME:   u64 = 0x9F0; // FString (ptr + len + max)
pub const KILL_SCORE:    u64 = 0xF98;
pub const B_IS_A_BOT:    u64 = 0x282;

// ── USkeletalMeshComponent (os) ───────────────────────────────────────────────
/// → TArray<FTransform> — transformations des os en component-space
pub const BONE_ARRAY:                       u64 = 0x660;
pub const BONE_ARRAY_CACHE:                 u64 = 0x670;
pub const CACHED_COMPONENT_SPACE_TRANSFORMS: u64 = 0x9C8;
pub const CURRENT_READ_COMPONENT_TRANSFORMS: u64 = 0x48;
pub const RELATIVE_LOCATION:               u64 = 0x140;

// ── AFortWeapon ───────────────────────────────────────────────────────────────
pub const WEAPON_DATA:        u64 = 0x658;
pub const AMMO_COUNT:         u64 = 0x1188;
pub const PROJECTILE_SPEED:   u64 = 0x213C;
pub const PROJECTILE_GRAVITY: u64 = 0x2140;

// ── Rendu ─────────────────────────────────────────────────────────────────────
pub const LAST_RENDER_TIME:   u64 = 0x290;

// ── Indices os Fortnite (skeleton standard) ───────────────────────────────────
#[repr(u32)]
#[derive(Clone, Copy, Debug)]
pub enum Bone {
    Root       = 0,
    Pelvis     = 1,
    Spine1     = 7,
    Spine2     = 8,
    Spine3     = 9,
    Neck       = 10,
    Head       = 11,
    LShoulder  = 36,
    LElbow     = 37,
    LHand      = 38,
    RShoulder  = 59,
    RElbow     = 60,
    RHand      = 61,
    LHip       = 70,
    LKnee      = 71,
    LFoot      = 72,
    RHip       = 77,
    RKnee      = 78,
    RFoot      = 79,
}
