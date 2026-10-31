//! quests.py: authenticated, monotonic snapshots of host-X4 cooperative objectives.
//!
//! Only the host game's mission script can advance objectives or award money. The
//! relay stores a display snapshot; it does not infer kills or grant rewards.

use super::{Packet, Session};
use crate::wire::{seq, uint, Hex32, Parsed, Reject, Slot};

/// Cooperative Trial progress. Construction enforces the stage invariants, so a stored
/// value is always one of the states the mission can actually be in.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct Progress {
    revision: u32,
    stage: u32,
    delivered: u32,
    destroyed: u32,
    paid: u32,
}

impl Progress {
    pub fn parse(fields: &[&str]) -> Parsed<Self> {
        if fields.len() != 5 {
            return Err(Reject);
        }
        let p = Progress {
            revision: seq(fields[0])?,
            stage: uint(fields[1], 3)?,
            delivered: uint(fields[2], 20)?,
            destroyed: uint(fields[3], 2)?,
            paid: uint(fields[4], 1)?,
        };
        let valid = match p.stage {
            1 => p.delivered != 20 && p.paid == 0,
            2 => p.delivered == 20 && p.destroyed != 2 && p.paid == 0,
            3 => (p.delivered, p.destroyed, p.paid) == (20, 2, 1),
            _ => false,
        };
        valid.then_some(p).ok_or(Reject)
    }

    /// Nothing regresses: a newer revision with every counter at least as high.
    fn advances(&self, old: &Progress) -> bool {
        self.revision > old.revision
            && self.stage >= old.stage
            && self.delivered >= old.delivered
            && self.destroyed >= old.destroyed
            && self.paid >= old.paid
    }
}

#[derive(Default)]
pub struct QuestState {
    owner: Option<Hex32>,
    progress: Option<Progress>,
    next_send: f64,
}

impl QuestState {
    pub fn receive(&mut self, lease: Hex32, fields: &[&str]) -> Parsed<()> {
        let progress = Progress::parse(fields)?;
        if self.owner == Some(lease) {
            if let Some(old) = &self.progress {
                if !progress.advances(old) {
                    return Ok(());
                }
            }
        }
        self.owner = Some(lease);
        self.progress = Some(progress);
        Ok(())
    }
}

impl Session {
    pub(super) fn snapshots_quest(&mut self, now: f64, out: &mut Vec<Packet>) {
        let Some(host) = self.core.peers.get(&Slot::P1) else { return };
        let quest = &mut self.coop.quest;
        let Some(p) = quest.progress.filter(|_| quest.owner == Some(host.lease) && now >= quest.next_send) else {
            return;
        };
        quest.next_send = now + 0.5;
        for peer in self.core.peers.values() {
            let packet = format!(
                "QUEST|1|{}|{}|{}|{}|{}|{}|{}",
                peer.lease, host.lease, p.revision, p.stage, p.delivered, p.destroyed, p.paid
            );
            out.push((peer.address, packet.into_bytes()));
        }
    }
}
