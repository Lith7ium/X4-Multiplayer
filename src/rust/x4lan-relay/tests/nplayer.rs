//! Synthetic 3- and 4-player dedicated sessions: every guest sees every other guest,
//! independent join/leave, slot exhaustion, mismatched builds and the fixture-ship caps.
//! Synthetic peers are not evidence of native multiplayer.

use std::net::SocketAddr;

use x4lan_relay::session::{LeaseSource, Level, Session};
use x4lan_relay::wire::{Ship, Slot};

const TOKEN: &str = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const SECTOR: &str = "cluster_14_sector001_macro";

fn addr(n: u16) -> SocketAddr {
    SocketAddr::from(([127, 0, 0, 1], 10000 + n))
}

fn dedicated(level: Level, build: &str, players: u8) -> Session {
    Session::new_dedicated_players(level, TOKEN, build, "bubble", LeaseSource::Os, players).unwrap()
}

fn join_build(s: &mut Session, build: &str, slot: u16, now: f64) -> Option<String> {
    let nonce = format!("{slot:x}").repeat(32)[..32].to_string();
    let hello = format!("HELLO|1|{TOKEN}|{build}|sector_macro|bubble|p{slot}|{nonce}");
    let reply = s.receive(hello.as_bytes(), addr(slot), now);
    reply.first().map(|r| String::from_utf8(r.clone()).unwrap().split('|').nth(3).unwrap().to_owned())
}

fn join(s: &mut Session, slot: u16, now: f64) -> String {
    join_build(s, "test", slot, now).expect("admitted")
}

fn text(packets: &[(SocketAddr, Vec<u8>)], to: SocketAddr) -> Vec<String> {
    packets.iter().filter(|(a, _)| *a == to).map(|(_, p)| String::from_utf8(p.clone()).unwrap()).collect()
}

fn crew(s: &mut Session, lease: &str, slot: u16, seq: u32, ship: u32, now: f64) -> Vec<Vec<u8>> {
    s.receive(format!("CREW|1|{lease}|{seq}|{ship}|{ship}").as_bytes(), addr(slot), now)
}

#[allow(clippy::too_many_arguments)]
fn nav2(s: &mut Session, lease: &str, slot: u16, ship: u32, epoch: u64, seq: u32, x: f64, now: f64) {
    let packet = format!(
        "NAV2|1|{lease}|{ship}|{epoch}|{seq}|{SECTOR}|{x}|10000|0|0|0|0|0|0|{}|1|0|0|0|0|0",
        10.0 + f64::from(seq)
    );
    s.receive(packet.as_bytes(), addr(slot), now);
}

/// Ships carried by NAV2 frames to `to`, sorted.
fn nav_ships(packets: &[(SocketAddr, Vec<u8>)], to: SocketAddr) -> Vec<u32> {
    let mut ships: Vec<u32> = text(packets, to)
        .iter()
        .filter(|p| p.starts_with("NAV2|"))
        .map(|p| p.split('|').nth(4).unwrap().parse().unwrap())
        .collect();
    ships.sort_unstable();
    ships
}

/// Four players: authority p1 plus guests p2/p3/p4 piloting ships 2/1/3.
fn four(level: Level) -> (Session, [String; 4]) {
    let mut s = dedicated(level, "test", 4);
    let leases = [join(&mut s, 1, 0.0), join(&mut s, 2, 0.0), join(&mut s, 3, 0.0), join(&mut s, 4, 0.0)];
    assert!(crew(&mut s, &leases[1], 2, 1, 2, 0.05).is_empty());
    assert!(crew(&mut s, &leases[2], 3, 1, 1, 0.05).is_empty());
    assert!(crew(&mut s, &leases[3], 4, 1, 3, 0.05).is_empty());
    (s, leases)
}

#[test]
fn ship_counts_follow_max_players() {
    assert_eq!(dedicated(Level::Coop, "test", 2).core.ship_count, 2);
    assert_eq!(dedicated(Level::Coop, "test", 3).core.ship_count, 2);
    assert_eq!(dedicated(Level::Coop, "test", 4).core.ship_count, 3);
    assert_eq!(dedicated(Level::Coop, "test", 8).core.ship_count, 7);
    for bad in [0, 1, 9] {
        assert!(Session::new_dedicated_players(Level::Coop, TOKEN, "test", "bubble", LeaseSource::Os, bad).is_err());
    }
    let legacy = Session::new_dedicated(Level::Coop, TOKEN, "test", "bubble", LeaseSource::Os).unwrap();
    assert_eq!((legacy.core.max_players, legacy.core.ship_count), (3, 2));
    let pair = Session::new(Level::Coop, TOKEN, "test", "bubble", LeaseSource::Os).unwrap();
    assert_eq!((pair.core.max_players, pair.core.ship_count), (2, 2));
}

#[test]
fn every_guest_sees_every_other_guest_ship() {
    let (mut s, l) = four(Level::Coop);
    assert_eq!(
        (s.owner(Ship::ONE), s.owner(Ship::TWO), s.owner(Ship::from_number(3).unwrap())),
        (Slot::P3, Slot::P2, Slot::parse("p4").unwrap())
    );
    // Ship 2 kept its initial epoch 1; ships 1 and 3 were handed over (epoch 2).
    nav2(&mut s, &l[1], 2, 2, 1, 1, 200.0, 0.1);
    nav2(&mut s, &l[2], 3, 1, 2, 1, 100.0, 0.1);
    nav2(&mut s, &l[3], 4, 3, 2, 1, 300.0, 0.1);
    let snap = s.snapshots(0.2);
    assert_eq!(nav_ships(&snap, addr(1)), vec![1, 2, 3], "authority sees all guests");
    assert_eq!(nav_ships(&snap, addr(2)), vec![1, 3], "p2 sees p3 and p4");
    assert_eq!(nav_ships(&snap, addr(3)), vec![2, 3], "p3 sees p2 and p4");
    assert_eq!(nav_ships(&snap, addr(4)), vec![1, 2], "p4 sees p2 and p3");
    // Each NAV2 names its source lease, so a client can tell remotes apart.
    let to_p2 = text(&snap, addr(2));
    assert!(to_p2.iter().any(|p| p.starts_with(&format!("NAV2|1|{}|{}|1|", l[1], l[2]))));
    assert!(to_p2.iter().any(|p| p.starts_with(&format!("NAV2|1|{}|{}|3|", l[1], l[3]))));
    // AUTH carries one owner|epoch|pilot group per ship.
    let auth = to_p2.iter().find(|p| p.starts_with("AUTH|")).unwrap();
    assert!(auth.ends_with("|p3|2|p3|p2|1|p2|p4|2|p4"), "{auth}");
    // HGEN (pilot health) stays two-ship.
    let hgen = to_p2.iter().find(|p| p.starts_with("HGEN|")).unwrap();
    assert_eq!(hgen.split('|').count(), 6, "{hgen}");
}

#[test]
fn base_level_forwards_each_remote_pose_and_bodies_reach_all_peers() {
    let mut s = dedicated(Level::Base, "test", 3);
    let l = [join(&mut s, 1, 0.0), join(&mut s, 2, 0.0), join(&mut s, 3, 0.0)];
    for (i, lease) in l.iter().enumerate() {
        let slot = i as u16 + 1;
        let packet = format!("STATE|1|{lease}|1|{}|0|0|0|0|0", slot * 10);
        s.receive(packet.as_bytes(), addr(slot), 0.1);
    }
    let snap = s.snapshots(0.2);
    for slot in 1..=3u16 {
        let peers: Vec<String> = text(&snap, addr(slot)).into_iter().filter(|p| p.starts_with("PEER|")).collect();
        assert_eq!(peers.len(), 2, "p{slot}: {peers:?}");
        assert!(peers.iter().all(|p| p.split('|').nth(4).unwrap() != l[usize::from(slot) - 1]));
    }

    let (mut s, l) = four(Level::Coop);
    for (i, lease) in l.iter().enumerate().skip(1) {
        let slot = i as u16 + 1;
        let packet = format!("BODY2|1|{lease}|1|foot|0|0|{SECTOR}|{}|0|0|0|0|0", slot);
        s.receive(packet.as_bytes(), addr(slot), 0.1);
    }
    let snap = s.snapshots(0.2);
    for slot in 1..=4u16 {
        let bodies = text(&snap, addr(slot)).into_iter().filter(|p| p.starts_with("BODY2|")).count();
        assert_eq!(bodies, if slot == 1 { 3 } else { 2 }, "p{slot}");
    }
}

#[test]
fn slot_exhaustion_and_mismatched_builds_are_refused() {
    let mut s = dedicated(Level::Coop, "world1-x-np1-p3", 3);
    for slot in 1..=3 {
        join_build(&mut s, "world1-x-np1-p3", slot, 0.0).unwrap();
    }
    // p4 is beyond max_players: silently refused, no slot taken.
    assert!(join_build(&mut s, "world1-x-np1-p3", 4, 0.1).is_none());
    // A build with another protocol tag or player count never joins.
    let mut fresh = dedicated(Level::Coop, "world1-x-np1-p4", 4);
    assert!(join_build(&mut fresh, "world1-x-np1-p3", 2, 0.0).is_none());
    assert!(join_build(&mut fresh, "world1-x", 2, 0.0).is_none());
    assert!(fresh.core.peers.is_empty());
    // Two-player sessions keep refusing p3 and above.
    let mut pair = Session::new(Level::Coop, TOKEN, "test", "bubble", LeaseSource::Os).unwrap();
    join(&mut pair, 1, 0.0);
    join(&mut pair, 2, 0.0);
    assert!(join_build(&mut pair, "test", 3, 0.0).is_none());
    // A guest cannot take a ship beyond the session's ship count.
    let mut s = dedicated(Level::Coop, "test", 3);
    let l = [join(&mut s, 1, 0.0), join(&mut s, 2, 0.0), join(&mut s, 3, 0.0)];
    assert!(crew(&mut s, &l[2], 3, 1, 3, 0.1).is_empty());
    assert_eq!(s.pilot(Ship::ONE), None);
    assert!(s.coop.crew.get(&x4lan_relay::wire::Hex32::parse(&l[2]).unwrap()).is_none());
}

#[test]
fn leave_and_rejoin_of_one_guest_keeps_the_others_flowing() {
    let (mut s, l) = four(Level::Coop);
    let revision = s.coop.authority_revision;
    // p3 goes silent; the others keep refreshing their lease.
    for step in 1..=31 {
        let now = f64::from(step);
        for (slot, ship, epoch) in [(2u16, 2u32, 1u64), (4, 3, 2)] {
            nav2(&mut s, &l[usize::from(slot) - 1], slot, ship, epoch, step as u32, 1.0, now);
        }
        for slot in [1u16, 2, 4] {
            s.receive(format!("IDLE|1|{}|{step}", l[usize::from(slot) - 1]).as_bytes(), addr(slot), now);
        }
    }
    let snap = s.snapshots(31.0);
    assert!(s.peer(Slot::P3).is_none(), "p3 expired");
    assert!(s.coop.authority_revision > revision);
    assert_eq!(s.pilot(Ship::ONE), None, "ship 1 has no pilot after p3 left");
    assert_eq!(nav_ships(&snap, addr(2)), vec![3]);
    assert_eq!(nav_ships(&snap, addr(4)), vec![2]);
    // p3 resumes with its old lease and retakes ship 1 under the same epoch.
    let nonce = "3".repeat(32);
    let resume = format!("RESUME|1|{TOKEN}|test|sector_macro|bubble|p3|{nonce}|{}|{}", l[2], "e".repeat(32));
    let reply = s.receive(resume.as_bytes(), addr(3), 31.1);
    assert!(String::from_utf8(reply[0].clone()).unwrap().starts_with("WELCOME|1|"));
    assert!(crew(&mut s, &l[2], 3, 2, 1, 31.2).is_empty());
    assert_eq!(s.pilot(Ship::ONE), Some(Slot::P3));
    nav2(&mut s, &l[2], 3, 1, 2, 2, 5.0, 31.3);
    let snap = s.snapshots(31.4);
    assert_eq!(nav_ships(&snap, addr(2)), vec![1, 3]);
    assert_eq!(nav_ships(&snap, addr(4)), vec![1, 2]);
}

#[test]
fn extra_ship_health_is_host_owned_and_reaches_every_guest() {
    let (mut s, l) = four(Level::Coop);
    let host = l[0].clone();
    // Guests cannot publish XSHIP; ships 1/2 belong to WORLD; ships beyond the count fail.
    s.receive(format!("XSHIP|1|{}|1|3|50|50|0|0", l[1]).as_bytes(), addr(2), 0.1);
    for ship in [1, 2, 4] {
        s.receive(format!("XSHIP|1|{host}|1|{ship}|50|50|0|0").as_bytes(), addr(1), 0.1);
    }
    assert!(s.bubble.extra.is_empty());
    s.receive(format!("XSHIP|1|{host}|1|3|75.5|20|0|0").as_bytes(), addr(1), 0.1);
    s.receive(format!("XSHIP|1|{host}|1|3|10|10|0|0").as_bytes(), addr(1), 0.1); // stale revision
    let snap = s.snapshots(0.2);
    for slot in 1..=4u16 {
        let rows: Vec<String> = text(&snap, addr(slot)).into_iter().filter(|p| p.starts_with("XSHIP|")).collect();
        assert_eq!(rows.len(), 1, "p{slot}");
        assert!(rows[0].ends_with(&format!("|{host}|1|3|75.5|20|0|0")), "{}", rows[0]);
    }
    // Forwarded at 5 Hz, not every tick.
    assert!(text(&s.snapshots(0.25), addr(2)).iter().all(|p| !p.starts_with("XSHIP|")));
    // A destroyed extra ship stops forwarding its motion.
    let ship3 = Ship::from_number(3).unwrap();
    s.receive(format!("XSHIP|1|{host}|2|3|0|0|0|0").as_bytes(), addr(1), 0.3);
    assert!(s.dead(ship3));
    nav2(&mut s, &l[3], 4, 3, 2, 1, 1.0, 0.3);
    assert!(nav_ships(&s.snapshots(0.4), addr(2)).iter().all(|ship| *ship != 3));
}

#[test]
fn fixture_subsystems_fail_closed_for_extra_ships() {
    let (mut s, l) = four(Level::Coop);
    let p4 = &l[3];
    // Pilot-reported health and shot claims stay on ships 1/2.
    s.receive(format!("HACK|1|{p4}|3|1").as_bytes(), addr(4), 0.1);
    s.receive(format!("HEALTH|1|{p4}|3|1|1|50|50").as_bytes(), addr(4), 0.1);
    s.receive(format!("SHOT|1|{p4}|3|1|2|1|1|1|weapon_macro:1.000").as_bytes(), addr(4), 0.1);
    let snap = s.snapshots(0.2);
    for slot in 1..=3u16 {
        assert!(text(&snap, addr(slot)).iter().all(|p| !p.starts_with("HEALTH|") && !p.starts_with("SHOT|")));
    }
    // No pilot-health baseline is offered for ship 3.
    assert!(text(&snap, addr(4)).iter().all(|p| !p.starts_with(&format!("HBASE|1|{p4}|3|"))));
    // Market orders from ship 3 are rejected explicitly.
    let request = "c".repeat(32);
    let reply =
        s.receive(format!("COOPORDER|1|{p4}|{request}|{}|3|2|m1|energycells,5,10", l[0]).as_bytes(), addr(4), 0.3);
    assert_eq!(reply, vec![format!("ORDER_RESULT|1|{p4}|{request}|REJECTED").into_bytes()]);
    // Dock attachment for ship 3 is dropped without a reply.
    assert!(s.receive(format!("CDOCK|1|{p4}|3|2|1|APPROACH|5").as_bytes(), addr(4), 0.3).is_empty());
}

#[test]
fn dedicated_universe_serves_three_guests_independently() {
    let build = "world1-test";
    let mut s = dedicated(Level::Universe, build, 4);
    let host = join_build(&mut s, build, 1, 0.0).unwrap();
    let guests: Vec<String> = (2..=4).map(|slot| join_build(&mut s, build, slot, 0.0).unwrap()).collect();
    let sectors = ["a_macro", "b_macro", "a_macro"];
    for (i, lease) in guests.iter().enumerate() {
        let packet = format!("UINTEREST|1|{lease}|1|{}", sectors[i]);
        assert!(s.receive(packet.as_bytes(), addr(i as u16 + 2), 0.1).is_empty());
    }
    for (revision, sector) in [(1, "a_macro"), (2, "b_macro")] {
        for packet in
            [format!("UBEGIN|1|{host}|{host}|{revision}|{sector}|0|1"), format!("UCOMMIT|1|{host}|{host}|{revision}")]
        {
            assert!(!s.receive(packet.as_bytes(), addr(1), 0.2).is_empty());
        }
    }
    let snap = s.snapshots(0.3);
    // The authority hears one claimant-tagged interest per guest.
    let interests = text(&snap, addr(1)).into_iter().filter(|p| p.starts_with("UINTEREST|")).count();
    assert_eq!(interests, 3);
    for (i, sector) in sectors.iter().enumerate() {
        let to = text(&snap, addr(i as u16 + 2));
        assert!(to.iter().any(|p| p.starts_with("UBEGIN|") && p.contains(&format!("|{sector}|"))), "{to:?}");
        assert!(to.iter().all(|p| !p.starts_with("UBEGIN|") || p.contains(&format!("|{sector}|"))));
    }
}

#[test]
fn identity_covers_every_shared_ship() {
    let build = "world1-test";
    let mut s = dedicated(Level::Universe, build, 4);
    let host = join_build(&mut s, build, 1, 0.0).unwrap();
    join_build(&mut s, build, 2, 0.0).unwrap();
    for (seq, entity) in [(1, 3), (2, 4)] {
        let packet = format!("IDENT|1|{host}|{host}|{seq}|P|{entity}|ABC-123|4c414e|1|0");
        s.receive(packet.as_bytes(), addr(1), 0.1);
    }
    let rows: Vec<String> = text(&s.snapshots(0.2), addr(2)).into_iter().filter(|p| p.starts_with("IDENT|")).collect();
    assert_eq!(rows.len(), 1, "{rows:?}");
    assert!(rows[0].contains("|P|3|"), "ship 3 exists, ship 4 does not: {rows:?}");
}

#[test]
fn dock_rows_reach_every_other_client() {
    for level in [Level::Dock, Level::Universe] {
        let build = "world1-test";
        let mut s = dedicated(level, build, 3);
        let l: Vec<String> = (1..=3).map(|slot| join_build(&mut s, build, slot, 0.0).unwrap()).collect();
        assert!(crew(&mut s, &l[1], 2, 1, 2, 0.05).is_empty());
        assert!(crew(&mut s, &l[2], 3, 1, 1, 0.05).is_empty());
        // p2 flies ship 2 (epoch 1), p3 took ship 1 (epoch 2); both report a free dock state.
        assert!(s.receive(format!("CDOCK|1|{}|2|1|1|FREE|0", l[1]).as_bytes(), addr(2), 0.1).is_empty());
        assert!(s.receive(format!("CDOCK|1|{}|1|2|1|FREE|0", l[2]).as_bytes(), addr(3), 0.1).is_empty());
        let snap = s.snapshots(0.2);
        let docks = |slot: u16| -> Vec<String> {
            text(&snap, addr(slot)).into_iter().filter(|p| p.starts_with("CDOCK|")).collect()
        };
        assert_eq!(docks(1).len(), 2, "{level:?} server");
        assert_eq!(docks(2), vec![format!("CDOCK|1|{}|{}|1|2|1|FREE|0", l[1], l[2])], "{level:?} p2 sees p3");
        assert_eq!(docks(3), vec![format!("CDOCK|1|{}|{}|2|1|1|FREE|0", l[2], l[1])], "{level:?} p3 sees p2");
    }
}

#[test]
fn motion_modes_reach_every_other_client_before_nav2() {
    let (mut s, l) = four(Level::Coop);
    for (slot, ship, epoch, mode) in [(2u16, 2u32, 1u64, "boost"), (3, 1, 2, "travel"), (4, 3, 2, "highway")] {
        let lease = &l[usize::from(slot) - 1];
        s.receive(format!("NMODE|1|{lease}|{ship}|{epoch}|1|11|{mode}").as_bytes(), addr(slot), 0.1);
        nav2(&mut s, lease, slot, ship, epoch, 1, 1.0, 0.1);
    }
    // Wrong epoch and unknown mode are refused.
    s.receive(format!("NMODE|1|{}|3|1|2|12|highway", l[3]).as_bytes(), addr(4), 0.1);
    s.receive(format!("NMODE|1|{}|3|2|2|12|warp", l[3]).as_bytes(), addr(4), 0.1);
    let snap = s.snapshots(0.2);
    for slot in 2..=4u16 {
        let rows: Vec<String> =
            text(&snap, addr(slot)).into_iter().filter(|p| p.starts_with("NMODE|") || p.starts_with("NAV2|")).collect();
        let modes = rows.iter().filter(|p| p.starts_with("NMODE|")).count();
        assert_eq!(modes, 2, "p{slot}: {rows:?}");
        let first_nav = rows.iter().position(|p| p.starts_with("NAV2|")).unwrap();
        assert!(rows[..first_nav].iter().all(|p| p.starts_with("NMODE|")), "modes precede NAV2");
        assert!(rows.iter().all(|p| !p.ends_with("|warp")));
    }
    assert_eq!(text(&snap, addr(1)).iter().filter(|p| p.starts_with("NMODE|")).count(), 3);
}

#[test]
fn rtt_ping_is_answered_for_authenticated_clients_only() {
    let mut s = dedicated(Level::Coop, "test", 3);
    let lease = join(&mut s, 3, 0.0);
    let relay = x4lan_relay::relay::Relay::bind("127.0.0.1", 0, s).unwrap();
    let ping = format!("RTTPING|1|{lease}|0007|1234.5|-|0");
    let pong = String::from_utf8(relay.rtt_pong(ping.as_bytes(), addr(3), 2.0).unwrap()).unwrap();
    assert_eq!(pong, format!("RTTPONG|1|{lease}|7|1234.5|2000.000"));
    assert!(relay.rtt_pong(ping.as_bytes(), addr(2), 2.0).is_none(), "wrong source address");
    assert!(relay.rtt_pong(format!("RTTPING|1|{lease}|x|1|-|0").as_bytes(), addr(3), 2.0).is_none());
    assert!(relay.session.core.peers.len() == 1, "a ping never takes a slot");
}

#[test]
fn projectile_births_reach_every_other_client_not_only_the_server() {
    let mut s = dedicated(Level::Coop, "test", 3);
    let l = [join(&mut s, 1, 0.0), join(&mut s, 2, 0.0), join(&mut s, 3, 0.0)];
    let world = "100|100|0|64.5|30|0|100|100|100|100|200|1|1|0|0|0|0";
    s.receive(format!("WORLD|1|{}|1|{world}", l[0]).as_bytes(), addr(1), 0.05);
    assert!(crew(&mut s, &l[1], 2, 1, 2, 0.08).is_empty());
    assert!(crew(&mut s, &l[2], 3, 1, 1, 0.08).is_empty());
    s.snapshots(0.1);
    let gen = s.coop.health_gen[Ship::TWO.index()];
    s.receive(format!("HACK|1|{}|2|{gen}", l[1]).as_bytes(), addr(2), 0.12);
    let nav = format!("NAV2|1|{}|2|1|1|{SECTOR}|1000|0|0|0|0|0|0|1|1000|0|0|0|0|0|0", l[1]);
    s.receive(nav.as_bytes(), addr(2), 0.2);
    let bolt = format!("BOLT|1|{}|2|1|{gen}|1|1000.05|{SECTOR}|1|1030|-10|160|0|0|2300|0|0|0|0.7", l[1]);
    s.receive(bolt.as_bytes(), addr(2), 0.21);
    let snap = s.snapshots(0.25);
    for slot in [1u16, 3] {
        let bolts: Vec<String> = text(&snap, addr(slot)).into_iter().filter(|p| p.starts_with("BOLT|")).collect();
        assert_eq!(bolts.len(), 1, "p{slot}: {bolts:?}");
        assert!(bolts[0].starts_with(&format!("BOLT|1|{}|{}|2|", l[usize::from(slot) - 1], l[1])), "{}", bolts[0]);
    }
    assert!(text(&snap, addr(2)).iter().all(|p| !p.starts_with("BOLT|")), "never back to the pilot");
    // Each recipient has its own gap and repeats: at most three births each.
    let mut counts = [0usize; 4];
    for now in [0.26, 0.30, 0.35, 0.40, 0.45, 0.50] {
        let snap = s.snapshots(now);
        for slot in [1u16, 3] {
            counts[usize::from(slot)] += text(&snap, addr(slot)).iter().filter(|p| p.starts_with("BOLT|")).count();
        }
    }
    assert_eq!((counts[1], counts[3]), (2, 2), "repeats per recipient");
}

fn walk2(s: &mut Session, lease: &str, slot: u16, seq: u32, context: &str, x: f64, now: f64) {
    let packet = format!("WALK2|1|{lease}|{seq}|{context}|room_a_macro|{x}|0|2|90|1.5|{}|0", 10.0 + f64::from(seq));
    s.receive(packet.as_bytes(), addr(slot), now);
}

/// WALK2 rows to `to` as (sender slot, seq, x).
fn walk_rows(packets: &[(SocketAddr, Vec<u8>)], to: SocketAddr) -> Vec<(String, u32, String)> {
    let mut rows: Vec<(String, u32, String)> = text(packets, to)
        .iter()
        .filter(|p| p.starts_with("WALK2|"))
        .map(|p| {
            let f: Vec<&str> = p.split('|').collect();
            (f[4].to_owned(), f[5].parse().unwrap(), f[13].to_owned())
        })
        .collect();
    rows.sort();
    rows
}

#[test]
fn walk_poses_reach_every_other_client_with_the_sender_slot() {
    let station = "1|station|0|cluster_14_sector001_macro|ABC-123|station_gen_factory_base_01_macro";
    let (mut s, l) = four(Level::Coop);
    walk2(&mut s, &l[1], 2, 1, station, 20.0, 0.1);
    walk2(&mut s, &l[2], 3, 1, "1|ship|3|-|-|-", 30.0, 0.1);
    walk2(&mut s, &l[3], 4, 1, "0|none|0|-|-|-", 0.0, 0.1);
    let snap = s.snapshots(0.15);
    let rows = |slot: u16| walk_rows(&snap, addr(slot)).into_iter().map(|r| r.0).collect::<Vec<_>>();
    assert_eq!(rows(1), ["p2", "p3", "p4"], "the server receives every client");
    assert_eq!(rows(2), ["p3", "p4"]);
    assert_eq!(rows(3), ["p2", "p4"]);
    assert_eq!(rows(4), ["p2", "p3"]);
    let to_p3 = text(&snap, addr(3));
    let row = to_p3.iter().find(|p| p.starts_with("WALK2|") && p.contains("|p2|")).unwrap();
    assert_eq!(
        row,
        &format!("WALK2|1|{}|{}|p2|1|{station}|room_a_macro|20|0|2|90|1.5|11|0", l[2], l[1]),
        "verbatim sender fields after lease and slot"
    );

    // An unchanged pose repeats at most every 0.2 s; a new sequence goes out at once.
    assert!(walk_rows(&s.snapshots(0.2), addr(2)).is_empty(), "no repeat within 0.2 s");
    walk2(&mut s, &l[2], 3, 2, "1|ship|3|-|-|-", 31.0, 0.21);
    assert_eq!(walk_rows(&s.snapshots(0.22), addr(2)), [("p3".to_owned(), 2, "31".to_owned())]);
    assert_eq!(walk_rows(&s.snapshots(0.36), addr(2)).len(), 1, "p4 repeats after 0.2 s, p3 not yet");
    assert_eq!(walk_rows(&s.snapshots(0.43), addr(2)).len(), 1, "p3 repeats after 0.2 s");

    // Older/duplicate sequences, ships beyond the session and malformed contexts are dropped.
    walk2(&mut s, &l[2], 3, 2, "1|ship|3|-|-|-", 99.0, 0.4);
    walk2(&mut s, &l[2], 3, 3, "1|ship|4|-|-|-", 99.0, 0.4);
    walk2(&mut s, &l[2], 3, 4, "1|station|0|-|ABC-123|m_macro", 99.0, 0.4);
    walk2(&mut s, &l[2], 3, 5, "1|none|0|-|-|-", 99.0, 0.4);
    walk2(&mut s, &l[2], 3, 6, "0|ship|1|-|-|-", 99.0, 0.4);
    walk2(&mut s, &l[1], 3, 7, "1|ship|1|-|-|-", 99.0, 0.4); // p2's lease from p3's address
    let snap = s.snapshots(0.6);
    assert!(walk_rows(&snap, addr(2)).iter().all(|r| r.2 != "99"), "{:?}", walk_rows(&snap, addr(2)));

    // Stale after 1 s: nothing is forwarded; a departed client's walk is forgotten.
    assert!(walk_rows(&s.snapshots(1.3), addr(2)).is_empty());
    walk2(&mut s, &l[1], 2, 2, station, 21.0, 1.4);
    walk2(&mut s, &l[3], 4, 2, "1|ship|1|-|-|-", 5.0, 1.4);
    assert_eq!(walk_rows(&s.snapshots(1.45), addr(3)).len(), 2);
    s.expire(32.0);
    let snap = s.snapshots(32.0);
    assert!(walk_rows(&snap, addr(3)).is_empty(), "every lease expired");
}
