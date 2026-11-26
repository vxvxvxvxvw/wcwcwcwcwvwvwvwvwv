use crate::mem::{Gva, read_guest};

const KPCR_PRCB:             u64 = 0x180;
const KPRCB_IDLE_THREAD:     u64 = 0x018;
const KTHREAD_PROCESS:       u64 = 0x220;

pub const EP_UNIQUE_PID:           u64 = 0x440;
pub const EP_ACTIVE_PROC_LINKS:    u64 = 0x448;
pub const EP_DIRECTORY_TABLE_BASE: u64 = 0x028;
pub const EP_IMAGE_FILE_NAME:      u64 = 0x5A8;
pub const EP_PEB:                  u64 = 0x550;

#[derive(Clone, Copy, Debug)]
pub struct ProcessInfo {
    pub cr3: u64,
    pub pid: u64,
    pub eprocess: Gva,
    pub module_base: u64,
}

impl ProcessInfo {
    pub fn found(&self) -> bool {
        self.cr3 != 0 && self.eprocess.0 != 0
    }
    pub const fn zeroed() -> Self {
        Self { cr3: 0, pid: 0, eprocess: Gva(0), module_base: 0 }
    }
}

pub unsafe fn find_fortnite(kern_cr3: u64, initial_eprocess: Gva) -> ProcessInfo {
    const TARGET: &[u8] = b"FortniteClien";
    const MAX_WALK: usize = 512;

    let mut current = initial_eprocess;

    for _ in 0..MAX_WALK {
        let name_va = Gva(current.0 + EP_IMAGE_FILE_NAME);
        let name: [u8; 15] = match unsafe { read_guest(kern_cr3, name_va) } {
            Ok(n) => n,
            Err(_) => break,
        };

        if name.starts_with(TARGET) {
            let cr3: u64 = match unsafe { read_guest(kern_cr3, Gva(current.0 + EP_DIRECTORY_TABLE_BASE)) } {
                Ok(v) => v,
                Err(_) => break,
            };
            let pid: u64 = match unsafe { read_guest(kern_cr3, Gva(current.0 + EP_UNIQUE_PID)) } {
                Ok(v) => v,
                Err(_) => break,
            };
            let module_base = unsafe { read_module_base(cr3, kern_cr3, current) };
            return ProcessInfo { cr3, pid, eprocess: current, module_base };
        }

        let flink_va = Gva(current.0 + EP_ACTIVE_PROC_LINKS);
        let flink: u64 = match unsafe { read_guest(kern_cr3, flink_va) } {
            Ok(v) => v,
            Err(_) => break,
        };

        let next = Gva(flink.wrapping_sub(EP_ACTIVE_PROC_LINKS));
        if next == initial_eprocess || next.0 == 0 { break; }
        current = next;
    }

    ProcessInfo::zeroed()
}

/// Lit la base du module principal depuis le PEB.
/// LDR InLoadOrder[0] = l'exe lui-même → c'est ce qu'on veut pour Fortnite
/// (FortniteClient-Win64-Shipping.exe est le module de base des offsets).
/// Si jamais le premier entry est 0, on scanne jusqu'à 32 entrées.
unsafe fn read_module_base(proc_cr3: u64, kern_cr3: u64, eprocess: Gva) -> u64 {
    let peb_va: u64 = match unsafe { read_guest(kern_cr3, Gva(eprocess.0 + EP_PEB)) } {
        Ok(v) if v != 0 => v,
        _ => return 0,
    };
    let ldr_ptr: u64 = match unsafe { read_guest(proc_cr3, Gva(peb_va + 0x018)) } {
        Ok(v) if v != 0 => v,
        _ => return 0,
    };

    // InLoadOrderModuleList.Flink — premier élément
    let mut entry: u64 = match unsafe { read_guest(proc_cr3, Gva(ldr_ptr + 0x010)) } {
        Ok(v) if v != 0 => v,
        _ => return 0,
    };

    // Scan jusqu'à 32 entrées — retourne la première DllBase non-nulle
    // (en pratique c'est l'exe lui-même en position 0)
    for _ in 0..32 {
        if entry == 0 || entry == ldr_ptr + 0x010 { break; }
        let dll_base: u64 = unsafe { read_guest(proc_cr3, Gva(entry + 0x030)) }.unwrap_or(0);
        if dll_base != 0 { return dll_base; }
        // Flink
        entry = unsafe { read_guest(proc_cr3, Gva(entry)) }.unwrap_or(0);
    }
    0
}

pub unsafe fn system_eprocess_from_kpcr(kern_cr3: u64, kpcr_va: u64) -> Option<Gva> {
    let prcb_va = kpcr_va + KPCR_PRCB;
    let idle_thread: u64 = unsafe { read_guest(kern_cr3, Gva(prcb_va + KPRCB_IDLE_THREAD)) }.ok()?;
    if idle_thread == 0 { return None; }
    let eprocess: u64 = unsafe { read_guest(kern_cr3, Gva(idle_thread + KTHREAD_PROCESS)) }.ok()?;
    if eprocess == 0 { return None; }
    Some(Gva(eprocess))
}
