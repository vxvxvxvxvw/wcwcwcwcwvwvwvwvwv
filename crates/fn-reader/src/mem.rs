/// mem.rs — lecture mémoire guest depuis le hyperviseur via NPT
///
/// Le hyperviseur (ring -1) peut lire n'importe quelle page guest
/// à travers les Nested Page Tables sans que le guest ne le voie.
/// Ces fonctions sont appelées DEPUIS LE CONTEXTE HYPERVISEUR uniquement.
///
/// Dans Pulzar, la lecture passe par le crate `memory` :
///   use memory::GuestPhysAddr;
///   memory::read::<T>(gpa)
///
/// Ici on expose une interface identique au-dessus de ce mécanisme.
/// Le crate `memory` expose `read_guest_virt(cr3, va) -> Option<T>`.

use core::mem::size_of;

/// Adresse virtuelle dans le guest (Windows x64 — espace utilisateur ou kernel).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Gva(pub u64);

impl Gva {
    #[inline(always)]
    pub fn offset(self, off: u64) -> Self { Gva(self.0.wrapping_add(off)) }
    #[inline(always)]
    pub fn is_null(self) -> bool { self.0 == 0 }
}

/// Résultat d'une lecture mémoire guest.
/// Échec si la page n'est pas présente ou si l'adresse est nulle.
#[derive(Clone, Copy, Debug)]
pub enum ReadErr { Null, PageFault, OutOfBounds }

/// Lit T depuis la mémoire guest via NPT.
/// `cr3` : valeur du CR3 du process cible (FortniteClient-Win64-Shipping.exe).
/// Ici une fonction stub — à câbler sur `memory::read_guest_virt` de Pulzar.
///
/// SAFETY: appelé uniquement depuis le exit handler, interruptions désactivées.
#[inline]
pub unsafe fn read_guest<T: Copy>(cr3: u64, va: Gva) -> Result<T, ReadErr> {
    if va.is_null() { return Err(ReadErr::Null); }

    // ── Pulzar real call (décommenter en intégrant dans le workspace) ─────────
    // memory::read_guest_virt::<T>(cr3, va.0).ok_or(ReadErr::PageFault)
    // ─────────────────────────────────────────────────────────────────────────

    // Stub pour compilation standalone — remplacer par l'appel ci-dessus
    let _ = (cr3, va);
    Err(ReadErr::PageFault)
}

/// Lit un pointeur 64-bit et vérifie qu'il est non-nul.
#[inline]
pub unsafe fn read_ptr(cr3: u64, va: Gva) -> Result<Gva, ReadErr> {
    let raw: u64 = unsafe { read_guest::<u64>(cr3, va) }?;
    if raw == 0 { Err(ReadErr::Null) } else { Ok(Gva(raw)) }
}

/// Lit une FString UE5 (ptr u64 + len i32 + max i32) → String UTF-16
/// Retourne au max `limit` caractères pour éviter les allocations hors-limite.
pub unsafe fn read_fstring(cr3: u64, va: Gva, limit: usize) -> Option<[u16; 64]> {
    let data_ptr: u64  = unsafe { read_guest::<u64>(cr3, va) }.ok()?;
    let len: i32       = unsafe { read_guest::<i32>(cr3, va.offset(8)) }.ok()?;
    if data_ptr == 0 || len <= 0 || len as usize > limit { return None; }
    let count = (len as usize).min(64);
    let mut buf = [0u16; 64];
    for i in 0..count {
        buf[i] = unsafe { read_guest::<u16>(cr3, Gva(data_ptr + i as u64 * 2)) }.ok()?;
    }
    Some(buf)
}

/// Lit un FTransform UE5 (48 bytes : scale 16B, rotation 16B, translation 16B).
/// Retourne la translation (position monde en cm) directement.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct FTransform {
    pub rotation:    [f32; 4], // quaternion XYZW
    pub translation: [f32; 3],
    pub _pad:        f32,
    pub scale:       [f32; 3],
    pub _pad2:       f32,
}

pub unsafe fn read_bone(cr3: u64, bone_array: Gva, bone_idx: u32) -> Option<[f32; 3]> {
    let entry = bone_array.offset(bone_idx as u64 * size_of::<FTransform>() as u64);
    let xf: FTransform = unsafe { read_guest(cr3, entry) }.ok()?;
    Some(xf.translation)
}
