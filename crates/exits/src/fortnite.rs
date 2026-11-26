//! Fortnite memory reader — VMMCALL exit handler.
//!
//! Copiez ce fichier dans :  crates/exits/src/fortnite.rs

use core::mem::size_of;

use fn_reader::{
    entities::{WorldSnapshot, snapshot},
    process::{ProcessInfo, find_fortnite, system_eprocess_from_kpcr},
    mem::Gva,
};
use log::warn;
use memory::Addressing;
use partition::Partition;
use svm::SaveArea;
use vcpu::Vcpu;

// ─── Command ─────────────────────────────────────────────────────────────────

/// RAX que l'overlay envoie pour déclencher un snapshot.
pub const CMD_FN_SNAPSHOT: u64 = 0x464E_0001;

const VMMCALL_LEN: u64 = 3;

/// GS.base kernel Windows x64 — KPCR fixe.
const KPCR_VA: u64 = 0xFFFF_F780_0000_0000;

// ─── BSS statique ────────────────────────────────────────────────────────────

static mut FN_CTX:     Option<ProcessInfo> = None;
static mut FN_SNAP:    WorldSnapshot       = unsafe { core::mem::zeroed() };
static mut KERN_CR3:   u64                 = 0;
static mut INIT_EPROC: Gva                 = Gva(0);
static mut PART_PTR:   *const Partition    = core::ptr::null();

// ─── fn_read_raw — interface vers fn-reader/mem.rs ───────────────────────────

/// Lit `len` bytes à l'adresse virtuelle guest `(cr3, va)` dans `buf`.
///
/// Appelé depuis `read_guest<T>()` dans fn-reader via `extern "Rust"`.
///
/// # Safety
/// `cr3` doit être un CR3 guest valide, `buf` doit pointer sur `len` bytes écritable.
#[unsafe(no_mangle)]
pub unsafe extern "Rust" fn fn_read_raw(cr3: u64, va: u64, buf: *mut u8, len: usize) -> bool {
    if unsafe { PART_PTR.is_null() } {
        return false;
    }
    let partition = unsafe { &*PART_PTR };
    let addressing = build_long_mode_addr(cr3);
    let dst = unsafe { core::slice::from_raw_parts_mut(buf, len) };
    partition.with_memory(addressing, |guest| guest.read(va, dst)).is_ok()
}

// ─── try_serve — point d'entrée depuis exits/src/hypercall.rs ────────────────

/// RAX que l'overlay ROB RUN envoie pour déclencher un snapshot il2cpp.
pub const CMD_RR_SNAPSHOT: u64 = 0x524F_4201; // 'ROB\x02'

// ─── BSS statique ROB RUN ────────────────────────────────────────────────────

static mut RR_CTX:  Option<fn_reader::process::ProcessInfo>  = None;
static mut RR_SNAP: core::mem::MaybeUninit<fn_reader::robrun::RobRunSnapshot>
    = core::mem::MaybeUninit::zeroed();

/// Tente de servir un VMMCALL ROB RUN.
/// Renvoie `true` si servi (RAX était 0x524F_4201), `false` sinon.
///
/// # Safety
/// Appelé depuis try_serve, avec PART_PTR déjà initialisé.
unsafe fn try_serve_robrun(vcpu: &mut Vcpu, partition: &'static Partition) -> bool {
    let save = vcpu.save();
    if save.rax != CMD_RR_SNAPSHOT {
        return false;
    }

    // Convention overlay : RAX=cmd, RBX=buf_va, RCX=buf_sz, RDX=module_base (0=scan LDR)
    let guest_buf_va   = vcpu.registers().rbx;
    let guest_buf_sz   = vcpu.registers().rcx;
    let hint_mod_base  = vcpu.registers().rdx;   // GameAssembly.dll base passée par l'overlay
    let caller_cr3     = save.cr3;

    let needed = core::mem::size_of::<fn_reader::robrun::RobRunSnapshot>() as u64;
    if guest_buf_sz < needed || guest_buf_va == 0 || guest_buf_va % 8 != 0 {
        vcpu.save_mut().rax = 0;
        advance_rip(vcpu);
        return true;
    }

    // CR3 kernel (réutilise KERN_CR3 déjà déduit par le handler Fortnite si dispo)
    if unsafe { KERN_CR3 } == 0 {
        unsafe { KERN_CR3 = deduce_kernel_cr3(caller_cr3); }
    }
    let kern_cr3 = unsafe { KERN_CR3 };

    // EPROCESS System via KPCR
    if unsafe { INIT_EPROC.0 } == 0 {
        match unsafe { fn_reader::process::system_eprocess_from_kpcr(kern_cr3, KPCR_VA) } {
            Some(ep) => unsafe { INIT_EPROC = ep; },
            None => {
                warn!("robrun: System EPROCESS introuvable via KPCR");
                vcpu.save_mut().rax = 0;
                advance_rip(vcpu);
                return true;
            }
        }
    }

    let init_eproc = unsafe { INIT_EPROC };

    // ProcessInfo ROB RUN.exe
    let rr_ctx = match unsafe { &*(&raw const RR_CTX) } {
        Some(c) if c.found() => *c,
        _ => {
            let found = unsafe { fn_reader::hypercall_handler::find_robrun_process(kern_cr3, init_eproc) };
            unsafe { RR_CTX = Some(found); }
            found
        }
    };

    if !rr_ctx.found() {
        vcpu.save_mut().rax = 0;
        advance_rip(vcpu);
        return true;
    }

    // Snapshot il2cpp ROB RUN — préférer le module_base fourni par l'overlay (hint_mod_base != 0)
    // car le scan LDR depuis le hv peut échouer si GameAssembly.dll n'est pas encore dans la PEB.
    let effective_mod_base = if hint_mod_base != 0 { hint_mod_base } else { rr_ctx.module_base };
    let rr_snap = unsafe { fn_reader::robrun::snapshot_robrun(rr_ctx.cr3, effective_mod_base) };
    unsafe { core::ptr::addr_of_mut!(RR_SNAP).write(core::mem::MaybeUninit::new(rr_snap)); }

    // Écrire dans le buffer user du process overlay
    let addressing = build_long_mode_addr(caller_cr3);
    let snap_bytes = unsafe {
        core::slice::from_raw_parts(
            core::ptr::addr_of!(RR_SNAP).cast::<u8>(),
            needed as usize,
        )
    };

    let ok = partition
        .with_memory(addressing, |guest| guest.write(guest_buf_va, snap_bytes))
        .map(|w| !matches!(w, memory::Written::Discarded))
        .unwrap_or(false);

    vcpu.save_mut().rax = if ok { 1 } else { 0 };
    advance_rip(vcpu);
    true
}

/// Tente de servir un VMMCALL Fortnite.
/// Renvoie `true` si servi (RAX était 0x464E_0001), `false` sinon (laisser Pulzar gérer).
///
/// # Safety
/// Appelé depuis le VMMCALL exit handler avec le vcpu courant et la partition statique.
pub unsafe fn try_serve(vcpu: &mut Vcpu, partition: &'static Partition) -> bool {
    // Stocker la partition pour fn_read_raw
    unsafe { PART_PTR = partition; }

    // ROB RUN check en premier (commande différente, pas de conflit)
    if unsafe { try_serve_robrun(vcpu, partition) } {
        return true;
    }

    let save = vcpu.save();
    if save.rax != CMD_FN_SNAPSHOT {
        return false;
    }

    let guest_buf_va = vcpu.registers().rdi;
    let guest_buf_sz = vcpu.registers().rsi;
    let caller_cr3   = save.cr3;

    // Validation minimale
    let needed = size_of::<WorldSnapshot>() as u64;
    if guest_buf_sz < needed || guest_buf_va == 0 || guest_buf_va % 8 != 0 {
        vcpu.save_mut().rax = 0;
        advance_rip(vcpu);
        return true;
    }

    // CR3 kernel (Windows KVA shadow : bit 0 set sur le CR3 user)
    if unsafe { KERN_CR3 } == 0 {
        unsafe { KERN_CR3 = deduce_kernel_cr3(caller_cr3); }
    }
    let kern_cr3 = unsafe { KERN_CR3 };

    // EPROCESS System via KPCR
    if unsafe { INIT_EPROC.0 } == 0 {
        match unsafe { system_eprocess_from_kpcr(kern_cr3, KPCR_VA) } {
            Some(ep) => unsafe { INIT_EPROC = ep; },
            None => {
                warn!("fortnite: System EPROCESS introuvable via KPCR");
                vcpu.save_mut().rax = 0;
                advance_rip(vcpu);
                return true;
            }
        }
    }

    // ProcessInfo Fortnite
    let init_eproc = unsafe { INIT_EPROC };
    let ctx = match unsafe { &*(&raw const FN_CTX) } {
        Some(c) if c.found() => *c,
        _ => {
            let found = unsafe { find_fortnite(kern_cr3, init_eproc) };
            unsafe { FN_CTX = Some(found); }
            found
        }
    };

    if !ctx.found() {
        vcpu.save_mut().rax = 0;
        advance_rip(vcpu);
        return true;
    }

    // Snapshot UE5
    unsafe { FN_SNAP = snapshot(ctx.cr3, ctx.module_base); }

    // Écrire dans le buffer user du process overlay
    let addressing = build_long_mode_addr(caller_cr3);
    let snap_bytes = unsafe {
        core::slice::from_raw_parts(
            core::ptr::addr_of!(FN_SNAP).cast::<u8>(),
            size_of::<WorldSnapshot>(),
        )
    };

    let ok = partition
        .with_memory(addressing, |guest| guest.write(guest_buf_va, snap_bytes))
        .map(|w| !matches!(w, memory::Written::Discarded))
        .unwrap_or(false);

    vcpu.save_mut().rax = if ok { 1 } else { 0 };
    advance_rip(vcpu);
    true
}

// ─── Helpers ─────────────────────────────────────────────────────────────────

/// Construit un `Addressing` long-mode 4 niveaux (Windows 10 x64 typique).
///
/// Remplit `SaveArea::zeroed()` avec CR0/CR3/CR4/EFER corrects puis
/// appelle `Addressing::from_save`.  On ne touche pas aux bases de segments
/// (toutes nulles) — le walker n'en a pas besoin en mode 64 bits.
fn build_long_mode_addr(cr3: u64) -> Addressing {
    let mut save = SaveArea::zeroed();
    // CR0 : PE(0) + PG(31)
    save.cr0  = (1 << 0) | (1 << 31);
    // CR3 : adresse de base du PML4, bits 11:0 clear
    save.cr3  = cr3 & !0xFFF_u64;
    // CR4 : PAE(5)
    save.cr4  = 1 << 5;
    // EFER : LME(8) + LMA(10)
    save.efer = (1 << 8) | (1 << 10);
    // CS.attributes : descriptor(1) + present(1) + long(1)
    // bitfield_struct génère with_<field>(bool) pour chaque champ bool
    save.cs.attributes = save.cs.attributes
        .with_descriptor(true)
        .with_present(true)
        .with_long(true);
    Addressing::from_save(&save)
}

/// Déduit le CR3 kernel depuis le CR3 user (Windows KVA shadow).
/// Le bit 0 est set sur le CR3 user depuis Windows 10 1803+.
fn deduce_kernel_cr3(cr3: u64) -> u64 {
    if cr3 & 1 != 0 { cr3 & !1_u64 } else { cr3 }
}

#[inline]
fn advance_rip(vcpu: &mut Vcpu) {
    let next = vcpu.control().next_rip;
    vcpu.save_mut().rip = if next != 0 {
        next
    } else {
        vcpu.save().rip.wrapping_add(VMMCALL_LEN)
    };
}
