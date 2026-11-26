#![no_std]
#![allow(dead_code, unused_variables, missing_docs)]

// fn-reader — crate hyperviseur ring -1
// À placer dans crates/fn-reader/ du workspace Pulzar.
// Brancher hypercall_handler::handle_vmmcall dans crates/exits/.

pub mod offsets;
pub mod mem;
pub mod process;
pub mod entities;
pub mod hypercall_handler;
pub mod robrun;

pub use entities::{WorldSnapshot, EntityData, MAX_ENTITIES};
pub use hypercall_handler::{handle_vmmcall, CMD_GET_SNAPSHOT};
pub use robrun::{RobRunSnapshot, RobRunEntity, CamData, MAX_RR_ENTITIES, snapshot_robrun};
