//! Making one interrupt stack usable by more than one frame at a time.
//!
//! A gate that names an interrupt stack table slot makes the processor load
//! *that slot's* stack pointer before it pushes anything. It loads the same
//! value every time. It does not continue below a frame that is already there,
//! and it does not know one is: a second delivery selecting the same slot puts
//! its frame exactly where the first one's is, and the first handler returns
//! through whatever the second one left behind.
//!
//! Most of that is ruled out before it can happen — an `NMI` cannot interrupt
//! itself, contributory exceptions combine into `#DF` instead of nesting, and
//! [`crate::gdt`] disarms the debug registers so nothing arms a `#DB` inside
//! the `#DB` path. But two cases survive that reasoning. An exception raised
//! inside an `NMI` handler ends with an `IRET` that releases `NMI` blocking
//! while the first `NMI`'s frame is still live, so a second `NMI` can arrive on
//! top of it. And a slot shared by two conditions — `NMI` with `#SX` — can be
//! selected by the second while the first is being handled.
//!
//! So each of these stacks is allocated with room for [`LEVELS`] frames, and
//! the first thing a handler on one of them does is move its own slot's pointer
//! down a level. A delivery that nests lands on clean memory; the level is
//! given back when the handler returns. Two windows remain, both of a handful
//! of instructions and both stated rather than hidden: between the processor
//! pushing the frame and the pointer moving, and between the pointer being
//! given back and the `IRET` that consumes the frame. Nothing can be delivered
//! into the first: the events that share a slot are all blocked or impossible
//! for exactly as long as their own delivery lasts. The second is the same
//! instant `IRET` would end the frame's life anyway.
//!
//! Running out of levels is a stated terminal case. It means an assumption
//! above is wrong, and continuing would corrupt a live frame rather than report
//! it.

use paging::{Stack, chunk::FRAME_SIZE};
use x86_64::{VirtAddr, structures::tss::TaskStateSegment};

use crate::{InterruptStack, Vector, fatal, gdt};

/// Frames one interrupt stack has room for at a time.
///
/// Two, because two is what the surviving nesting cases need: one live frame
/// plus the one that can arrive on top of it. A third would be a case nothing
/// here can name, and a case nothing can name is one to stop on rather than to
/// reserve memory for.
pub(crate) const LEVELS: u64 = 2;

/// Pages one level gets: sixteen kilobytes.
///
/// These stacks exist to be switched to when the interrupted one cannot be
/// trusted, so a level is sized for a handler that formats one line and stops —
/// generously, because the cost of getting it wrong is a fault with no stack
/// left to report it from.
const LEVEL_PAGES: u64 = 4;

/// Bytes one level occupies.
const LEVEL_BYTES: u64 = LEVEL_PAGES * FRAME_SIZE;

/// Pages one interrupt stack takes, every level together, with an unmapped
/// guard page below and above the whole of it.
pub(crate) const STACK_PAGES: u64 = LEVEL_PAGES * LEVELS;

/// 16-byte aligned wrapper around `TaskStateSegment`.
///
/// `TaskStateSegment` is `repr(C, packed(4))` in x86_64 0.15.x.  Nightly
/// 2026+ ABI checker rejects embedding a packed struct directly inside a
/// `repr(C, align(16))` struct.  The fix: wrap it in this newtype whose only
/// job is to carry the alignment, then embed the newtype (which has no packed
/// attribute itself).
///
/// The wrapper is `repr(C)` with `align(16)`.  The inner field starts at
/// offset 0, so the TSS base address satisfies the 16-byte alignment
/// requirement from Intel SDM Vol.3A §7.2.3 as long as the outer `Cpu` block
/// is itself 16-byte aligned (which it is because `Cpu` is `repr(C, align(16))`
/// relative to the allocator, and `AlignedTss` forces 16-byte alignment on
/// the whole `Cpu` struct).
#[repr(C, align(16))]
pub(crate) struct AlignedTss {
    pub(crate) inner: TaskStateSegment,
}

/// The task state segment this processor runs on, and what the entry path needs
/// to know about the stacks in it.
///
/// One block per processor, found again from the task descriptor in that
/// processor's own global descriptor table — which is why the task state
/// segment is the part with a fixed offset, and why nothing here may be moved
/// once a processor is running on it.
#[repr(C)]
pub(crate) struct Cpu {
    /// The segment itself, whose only load-bearing content is the seven stack
    /// pointers. The AlignedTss wrapper gives it 16-byte alignment so the TSS
    /// base address satisfies the GDT system descriptor requirement (SDM §7.2.3).
    pub(crate) tss: AlignedTss,
    /// How far down each slot's pointer may be moved: the last level's top.
    floors: [u64; InterruptStack::COUNT],
}

impl core::fmt::Debug for AlignedTss {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        f.debug_struct("AlignedTss").finish()
    }
}

impl core::fmt::Debug for Cpu {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        f.debug_struct("Cpu")
            .field("tss", &self.tss)
            .field("floors", &self.floors)
            .finish()
    }
}

impl Cpu {
    /// Points every slot at the top of its stack and records how far down each
    /// may go.
    ///
    /// The privilege stack table stays zero: nothing here runs outside ring 0,
    /// so there is no privilege transition that would need a stack, and a
    /// pointer nothing can reach is better left absent than filled in with
    /// something plausible.
    pub(crate) fn new(stacks: &[Stack; InterruptStack::COUNT]) -> Self {
        let mut inner = TaskStateSegment::new();
        let mut floors = [0; InterruptStack::COUNT];
        for stack in InterruptStack::ALL {
            let slot = usize::from(stack.slot());
            let top = stacks[slot].top();
            inner.interrupt_stack_table[slot] = top;
            floors[slot] = top.as_u64() - (LEVELS - 1) * LEVEL_BYTES;
        }
        Self {
            tss: AlignedTss { inner },
            floors,
        }
    }

    /// The segment a task descriptor is to name.
    pub(crate) const fn task_state(&self) -> *const TaskStateSegment {
        &raw const self.tss.inner
    }
}

/// One level of one interrupt stack, held for as long as a handler is on it.
///
/// Taken before anything else a handler does and given back after everything
/// else it does, which is what the two windows in the module documentation
/// measure.
pub(crate) struct Guard {
    /// Where the slot's pointer lives, addressed rather than borrowed: a nested
    /// handler holds a guard of its own over the same block, and two references
    /// to it would be two owners of one thing.
    entry: *mut VirtAddr,
    /// What the slot pointed at before, restored on the way out.
    previous: VirtAddr,
}

impl Guard {
    /// Moves `vector`'s stack down a level, or answers `None` for a vector that
    /// switches no stack at all.
    ///
    /// Terminal when no level is left: the frame the processor just pushed is
    /// the last one that fits, and a delivery after it would land on a live
    /// one.
    pub(crate) fn enter(vector: Vector) -> Option<Self> {
        let stack = vector.stack()?;
        let slot = usize::from(stack.slot());
        let cpu = gdt::block();
        // SAFETY: `block` answers with this processor's own block, which was
        // built before its tables were loaded and is never moved or dropped
        // while it runs. `slot` is one of the seven the table holds. Nothing is
        // borrowed: the pointer is read and written in place, so a handler
        // nested inside this one may do the same.
        let (entry, floor) = unsafe {
            (
                (&raw mut (*cpu).tss.inner.interrupt_stack_table[slot]),
                (&raw const (*cpu).floors[slot]).read(),
            )
        };
        // SAFETY: as above. The table is four-byte packed, hence the unaligned
        // access — the block's own alignment is what makes the store land on one
        // pointer rather than across two.
        let previous = unsafe { entry.read_unaligned() };
        if previous.as_u64() <= floor {
            fatal::nesting(vector, previous);
        }
        // SAFETY: as above. One level down is inside the same stack, which is
        // mapped and guarded for all of its levels.
        unsafe { entry.write_unaligned(previous - LEVEL_BYTES) };
        Some(Self { entry, previous })
    }
}

impl Drop for Guard {
    /// Gives the level back, so the next delivery on this slot starts at the
    /// top again rather than one level lower every time.
    fn drop(&mut self) {
        // SAFETY: the pointer is the one `enter` took, addressing a block that
        // outlives this processor, and this restores exactly the value that was
        // there.
        unsafe { self.entry.write_unaligned(self.previous) };
    }
}
