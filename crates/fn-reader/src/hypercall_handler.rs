/// hypercall_handler.rs — VMMCALL handler à brancher dans crates/exits/src/
///
/// Dans Pulzar, les VMMCALL sont gérés dans `crates/exits/src/vmexit.rs`
/// (ou équivalent). Brancher ici dans le match sur ExitCode::Vmmcall.
///
/// Interface VMMCALL :
///   RAX = numéro de commande
///   RBX = adresse de la réponse (buffer dans la mémoire user du guest)
///   RCX = taille max du buffer
///
/// Commandes :
///   0x464E_0001 → FN_GET_SNAPSHOT : remplit WorldSnapshot dans le buffer guest

use crate::entities::{snapshot, WorldSnapshot};
use crate::process::{find_fortnite, ProcessInfo};
use crate::mem::{Gva};
use crate::robrun::{snapshot_robrun, RobRunSnapshot};
use core::mem::size_of;
use core::ptr::addr_of_mut;

pub const CMD_GET_SNAPSHOT:      u64 = 0x464E_0001; // 'FN\x00\x01'
pub const CMD_ROBRUN_SNAPSHOT:   u64 = 0x524F_4201; // 'ROB\x02'

// ─── ProcessInfo cache pour ROB RUN ──────────────────────────────────────────
// Même pattern que CTX pour Fortnite : scan EPROCESS une fois, mise en cache.
static mut RR_CTX: Option<ProcessInfo> = None;
// MaybeUninit évite les initializers const complexes avec repr(C, packed) en no_std.
// Initialisé à zéro par le linker (BSS) — layout identique à RobRunSnapshot zéroïsé.
static mut RR_SNAP: core::mem::MaybeUninit<RobRunSnapshot> =
    core::mem::MaybeUninit::zeroed();

/// Contexte persistant entre les VMMCALL — mis à jour au premier appel
/// et rafraîchi toutes les N ms (ou à chaque appel selon les perfs).
/// Stocké dans la BSS statique du hyperviseur (pas d'alloc dynamique).
static mut CTX: Option<ProcessInfo> = None;
static mut SNAP: WorldSnapshot = WorldSnapshot {
    count: 0, local_team: 0, local_fov: 80.0,
    entities: [crate::entities::EntityData {
        valid: false, is_local: false, is_bot: false,
        is_dying: false, is_dbno: false, team_index: 0,
        head: [0.0;3], origin: [0.0;3], bones: [[0.0;3];19],
        last_render_time: 0.0, name: [0;64], weapon_name: [0;64],
        ammo: 0, is_reloading: false,
    }; 100],
};

/// Point d'entrée — appeler depuis le VMMCALL exit handler de Pulzar.
///
/// `rax` : commande
/// `rbx` : adresse guest de destination
/// `rcx` : taille du buffer guest
/// `guest_cr3` : CR3 du process qui a émis le VMMCALL
/// `kern_cr3`  : CR3 du kernel guest (pour find_fortnite)
/// `initial_eprocess` : adresse EPROCESS du process System (récupérée
///   au démarrage depuis KPCR + KiInitialProcess, ou signature scannée)
///
/// Retourne la taille écrite dans RAX (0 = erreur).
pub unsafe fn handle_vmmcall(
    rax: u64,
    rbx: u64,
    rcx: u64,
    guest_cr3: u64,
    kern_cr3:  u64,
    initial_eprocess: Gva,
) -> u64 {
    match rax {
        CMD_GET_SNAPSHOT => {
            // Trouver ou valider le process Fortnite
            let ctx = {
                // &raw const is safe — no unsafe block needed in Rust 2024
                let cached = &raw const CTX;
                if unsafe { (*cached).as_ref().map_or(false, |c| c.found()) } {
                    unsafe { (*cached).unwrap() }
                } else {
                    let found = unsafe { find_fortnite(kern_cr3, initial_eprocess) };
                    unsafe { CTX = Some(found) };
                    found
                }
            };

            if !ctx.found() { return 0; }

            // Faire le snapshot
            let snap = unsafe { snapshot(ctx.cr3, ctx.module_base) };
            unsafe { SNAP = snap };

            // Écrire dans le buffer guest (rbx = adresse user, guest_cr3)
            let needed = size_of::<WorldSnapshot>() as u64;
            if rcx < needed { return 0; }

            // Écrire octet par octet dans la mémoire guest via NPT
            // (dans Pulzar, utiliser memory::write_guest_virt)
            // &raw const is safe — no unsafe block needed in Rust 2024
            let src = (&raw const SNAP) as *const u8;
            for i in 0..needed {
                let byte = unsafe { *src.add(i as usize) };
                // memory::write_guest_virt(guest_cr3, rbx + i, byte);
                // ↑ décommenter en intégrant dans le workspace Pulzar
                let _ = (guest_cr3, rbx, byte);
            }

            needed
        }

        CMD_ROBRUN_SNAPSHOT => {
            // ── Trouver ou valider le process ROB RUN.exe ─────────────────────
            let rr_ctx = {
                // &raw const is safe — no unsafe block needed in Rust 2024
                let cached = &raw const RR_CTX;
                if unsafe { (*cached).as_ref().map_or(false, |c| c.found()) } {
                    unsafe { (*cached).unwrap() }
                } else {
                    let found = unsafe { find_robrun_process(kern_cr3, initial_eprocess) };
                    unsafe { RR_CTX = Some(found) };
                    found
                }
            };

            if !rr_ctx.found() { return 0; }

            // ── Snapshot il2cpp ───────────────────────────────────────────────
            let rr_snap = unsafe { snapshot_robrun(rr_ctx.cr3, rr_ctx.module_base) };
            // addr_of_mut! avoids creating a mutable reference to the static
            unsafe { addr_of_mut!(RR_SNAP).write(core::mem::MaybeUninit::new(rr_snap)) };

            // ── Écriture dans le buffer guest ─────────────────────────────────
            let needed = size_of::<RobRunSnapshot>() as u64;
            if rcx < needed { return 0; }

            // &raw const is safe — no unsafe block needed in Rust 2024
            let src = (&raw const RR_SNAP) as *const u8;
            for i in 0..needed {
                let byte = unsafe { *src.add(i as usize) };
                // memory::write_guest_virt(guest_cr3, rbx + i, byte);
                let _ = (guest_cr3, rbx, byte);
            }

            needed
        }

        _ => 0, // commande inconnue
    }
}

/// Scan EPROCESS pour trouver ROB RUN.exe.
/// Même logique que find_fortnite — nom cible différent.
/// On essaie plusieurs variantes du nom car Unity génère l'exe avec le
/// nom du projet (peut être "RobRun", "robrun", "Rob Run", "ROB RUN", etc.)
pub unsafe fn find_robrun_process(kern_cr3: u64, initial_eprocess: Gva) -> ProcessInfo {
    use crate::process::{
        EP_IMAGE_FILE_NAME, EP_DIRECTORY_TABLE_BASE, EP_UNIQUE_PID,
        EP_ACTIVE_PROC_LINKS,
    };

    // Toutes les variantes connues (préfixe 7+ bytes, case-sensitive côté Windows)
    // EP_IMAGE_FILE_NAME stocke jusqu'à 15 bytes du nom de l'exe (sans .exe).
    // On compare un préfixe court pour attraper toutes les variantes de casse.
    const TARGETS: &[&[u8]] = &[
        b"RobRun",    // Unity CamelCase (le plus probable)
        b"robrun",    // lowercase
        b"Rob Run",   // avec espace
        b"ROB RUN",   // ALL CAPS avec espace
        b"Rob_Run",   // underscore
        b"ROBRUN",    // ALLCAPS sans espace
    ];
    const MAX_WALK: usize = 512;

    let mut current = initial_eprocess;

    for _ in 0..MAX_WALK {
        let name_va = Gva(current.0 + EP_IMAGE_FILE_NAME);
        let name: [u8; 15] = match unsafe { crate::mem::read_guest(kern_cr3, name_va) } {
            Ok(n) => n,
            Err(_) => break,
        };

        let matched = TARGETS.iter().any(|t| name.starts_with(t));
        if matched {
            let cr3: u64 = match unsafe { crate::mem::read_guest(kern_cr3, Gva(current.0 + EP_DIRECTORY_TABLE_BASE)) } {
                Ok(v) => v,
                Err(_) => break,
            };
            let pid: u64 = match unsafe { crate::mem::read_guest(kern_cr3, Gva(current.0 + EP_UNIQUE_PID)) } {
                Ok(v) => v,
                Err(_) => break,
            };
            // module_base : lire PEB → LDR → premier module (GameAssembly.dll)
            // ROB RUN charge GameAssembly.dll comme premier module non-natif.
            // On utilise la même heuristique que read_module_base : LDR InLoadOrder[0]
            // → DllBase. En pratique c'est ROB RUN.exe lui-même ; GameAssembly.dll
            // est en deuxième position. On lit InLoadOrder[1] si besoin.
            let module_base = unsafe { read_robrun_module_base(cr3, kern_cr3, current) };
            return ProcessInfo { cr3, pid, eprocess: current, module_base };
        }

        let flink_va = Gva(current.0 + EP_ACTIVE_PROC_LINKS);
        let flink: u64 = match unsafe { crate::mem::read_guest(kern_cr3, flink_va) } {
            Ok(v) => v,
            Err(_) => break,
        };
        let next = Gva(flink.wrapping_sub(EP_ACTIVE_PROC_LINKS));
        if next == initial_eprocess || next.0 == 0 { break; }
        current = next;
    }

    ProcessInfo::zeroed()
}

/// Lit la base de GameAssembly.dll depuis le PEB du process ROB RUN.
///
/// LDR InLoadOrder layout :
///   index 0 → ROB RUN.exe  (l'exécutable principal)
///   index 1 → ntdll.dll
///   index 2 → kernel32.dll  (ou GameAssembly.dll si chargé tôt)
///
/// En pratique sur Unity il2cpp, GameAssembly.dll est chargé en 3e–5e position.
/// On scanne les 16 premières entrées et on retourne la base de celle dont
/// le nom contient "GameAssembly".
///
/// Si introuvable, on retourne 0 — le caller devra reessayer ou SAM fournit la base.
unsafe fn read_robrun_module_base(proc_cr3: u64, kern_cr3: u64, eprocess: Gva) -> u64 {
    use crate::process::EP_PEB;
    use crate::mem::{read_guest, Gva};

    let peb_va: u64 = match unsafe { read_guest(kern_cr3, Gva(eprocess.0 + EP_PEB)) } {
        Ok(v) if v != 0 => v,
        _ => return 0,
    };
    let ldr_ptr: u64 = match unsafe { read_guest(proc_cr3, Gva(peb_va + 0x018)) } {
        Ok(v) if v != 0 => v,
        _ => return 0,
    };

    // InLoadOrderModuleList.Flink (first entry)
    let mut entry: u64 = match unsafe { read_guest(proc_cr3, Gva(ldr_ptr + 0x010)) } {
        Ok(v) if v != 0 => v,
        _ => return 0,
    };

    // LDR_DATA_TABLE_ENTRY layout (x64) :
    //   +0x000 : LIST_ENTRY InLoadOrderLinks (Flink at +0, Blink at +8)
    //   +0x030 : DllBase
    //   +0x038 : EntryPoint
    //   +0x040 : SizeOfImage
    //   +0x048 : UNICODE_STRING FullDllName (len u16, maxlen u16, _pad 4B, buf* u64)
    //   +0x058 : UNICODE_STRING BaseDllName

    for _ in 0..32 {
        if entry == 0 || entry == ldr_ptr + 0x010 { break; }

        let dll_base: u64 = unsafe { read_guest(proc_cr3, Gva(entry + 0x030)) }.unwrap_or(0);

        // BaseDllName.Buffer (ptr) + Length pour identifier GameAssembly.dll
        let name_len: u16 = unsafe { read_guest(proc_cr3, Gva(entry + 0x058)) }.unwrap_or(0);
        let name_buf: u64 = unsafe { read_guest(proc_cr3, Gva(entry + 0x060)) }.unwrap_or(0);

        if dll_base != 0 && name_buf != 0 && name_len >= 14 * 2 {
            // Lire les 14 premiers chars : "GameAssembly.d"
            let mut chars = [0u16; 14];
            let mut ok = true;
            for i in 0..14usize {
                chars[i] = unsafe { read_guest(proc_cr3, Gva(name_buf + i as u64 * 2)) }.unwrap_or(0);
                if chars[i] == 0 { ok = false; break; }
            }
            // Comparer en ASCII (Latin-1) : G=0x47 a=0x61 m=0x6D e=0x65 ...
            const NEEDLE: [u16; 14] = [
                0x47,0x61,0x6D,0x65, // Game
                0x41,0x73,0x73,0x65, // Asse
                0x6D,0x62,0x6C,0x79, // mbly
                0x2E,0x64,            // .d
            ];
            if ok && chars == NEEDLE {
                return dll_base;
            }
        }

        // Flink
        entry = unsafe { read_guest(proc_cr3, Gva(entry)) }.unwrap_or(0);
    }

    0 // GameAssembly.dll non trouvé — FILL: Sam peut patcher avec la base fixe
}
