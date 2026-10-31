//! Dedicated-server NPC traffic budget: XB bundles for XBCAP clients, and NPC health /
//! identity forwarded per client only when the server issued a new sequence.
//! Synthetic peers are not evidence of native multiplayer.

use std::net::SocketAddr;

use x4lan_relay::session::universe::Baseline;
use x4lan_relay::session::{bundle, LeaseSource, Level, Packet, Session};
use x4lan_relay::wire::{Atom, Hex32, MAX_PACKET};

const TOKEN: &str = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const BUILD: &str = "world1-test";
const SECTOR: &str = "cluster_14_sector001_macro";
const MACRO: &str = "ship_arg_s_fighter_01_a_macro";

fn addr(n: u16) -> SocketAddr {
    SocketAddr::from(([127, 0, 0, 1], 10000 + n))
}

fn join(s: &mut Session, slot: u16, now: f64) -> String {
    let nonce = format!("{slot:x}").repeat(32)[..32].to_string();
    let hello = format!("HELLO|1|{TOKEN}|{BUILD}|sector_macro|bubble|p{slot}|{nonce}");
    let reply = s.receive(hello.as_bytes(), addr(slot), now);
    String::from_utf8(reply[0].clone()).unwrap().split('|').nth(3).unwrap().to_owned()
}

fn send(s: &mut Session, slot: u16, packet: String, now: f64) -> Vec<Vec<u8>> {
    s.receive(packet.as_bytes(), addr(slot), now)
}

/// Server p1 and clients p2/p3 (p4 joins later where a test needs it), all in SECTOR,
/// with a committed baseline of `ships` NPCs.
fn session(ships: u32) -> (Session, Vec<String>) {
    let mut s =
        Session::new_dedicated_players(Level::Universe, TOKEN, BUILD, "bubble", LeaseSource::Counter(0), 4).unwrap();
    let leases: Vec<String> = (1..=3).map(|slot| join(&mut s, slot, 0.0)).collect();
    for slot in [2, 3] {
        assert!(send(&mut s, slot, format!("UINTEREST|1|{}|1|{SECTOR}", leases[slot as usize - 1]), 0.01).is_empty());
    }
    let host = &leases[0];
    let rows: Vec<String> = (1..=ships).map(|i| format!("{i},ship,{MACRO},argon,{i},2,3,0,0,0,none,0")).collect();
    let digest =
        Baseline::create(Hex32::parse(host).unwrap(), 1, Atom::parse(SECTOR).unwrap(), rows.clone()).unwrap().digest;
    send(&mut s, 1, format!("UBEGIN|1|{host}|{host}|1|{SECTOR}|{}|{digest}", rows.len()), 0.02);
    for (i, row) in rows.iter().enumerate() {
        send(&mut s, 1, format!("UOBJECT|1|{host}|{host}|1|{}|{row}", i + 1), 0.02);
    }
    assert!(!send(&mut s, 1, format!("UCOMMIT|1|{host}|{host}|1"), 0.02).is_empty());
    (s, leases)
}

fn nhp(s: &mut Session, host: &str, id: u32, seq: u32, hull: u32, now: f64) {
    let packet = format!("NHP|1|{host}|{host}|{id}|1|{seq}|{SECTOR}|ARG-{id}|{MACRO}|argon|{hull}|50|1");
    send(s, 1, packet, now);
}

fn kinds(packets: &[Packet], to: SocketAddr, kind: &str) -> Vec<String> {
    let prefix = format!("{kind}|");
    packets
        .iter()
        .filter(|(a, _)| *a == to)
        .map(|(_, p)| String::from_utf8(p.clone()).unwrap())
        .filter(|p| p.starts_with(&prefix))
        .collect()
}

/// client.lua `unbundle`: the messages an XB datagram carries, envelopes restored.
fn unbundle(packet: &str, lease: &str) -> Vec<String> {
    let header = format!("XB|1|{lease}|");
    let mut rest = packet.strip_prefix(&header).expect("XB header for this lease");
    let mut out = Vec::new();
    while !rest.is_empty() {
        let (digits, tail) = rest.split_once(':').expect("length prefix");
        let length: usize = digits.parse().unwrap();
        let (body, tail) = tail.split_at(length);
        let (kind, after) = body.split_once('|').expect("kind");
        out.push(format!("{kind}|1|{lease}|{after}"));
        rest = tail;
    }
    out
}

#[test]
fn nhp_is_forwarded_once_per_server_sequence_to_every_client() {
    let (mut s, l) = session(3);
    for id in 1..=3 {
        nhp(&mut s, &l[0], id, id, 100, 0.05);
    }
    let first = s.snapshots(0.1);
    for slot in [2, 3] {
        assert_eq!(kinds(&first, addr(slot), "NHP").len(), 3, "each client gets every new sequence");
    }
    // An unchanged server state is not repeated: the server's keepalive brings new sequences.
    let quiet = s.snapshots(0.21);
    assert!(kinds(&quiet, addr(2), "NHP").is_empty() && kinds(&quiet, addr(3), "NHP").is_empty());
    nhp(&mut s, &l[0], 2, 4, 90, 0.25);
    let changed = s.snapshots(0.32);
    for slot in [2, 3] {
        let rows = kinds(&changed, addr(slot), "NHP");
        assert_eq!(rows.len(), 1, "{rows:?}");
        assert!(rows[0].contains("|2|1|4|") && rows[0].contains("|90|50|1"));
    }
    // A client that joins later receives the current state of every NPC at once.
    let late = join(&mut s, 4, 0.4);
    send(&mut s, 4, format!("UINTEREST|1|{late}|1|{SECTOR}"), 0.4);
    let joined = s.snapshots(0.45);
    assert_eq!(kinds(&joined, addr(4), "NHP").len(), 3);
    assert!(kinds(&joined, addr(2), "NHP").is_empty());
}

#[test]
fn identity_is_forwarded_once_per_server_sequence_to_every_client() {
    let (mut s, l) = session(2);
    let host = l[0].clone();
    for (seq, id) in [(1, 1), (2, 2)] {
        send(&mut s, 1, format!("IDENT|1|{host}|{host}|{seq}|N|{id}|ARG-{id}|4142|1|0"), 0.05);
    }
    let first = s.snapshots(0.1);
    assert_eq!(kinds(&first, addr(2), "IDENT").len(), 2);
    assert_eq!(kinds(&first, addr(3), "IDENT").len(), 2);
    let quiet = s.snapshots(0.21);
    assert!(kinds(&quiet, addr(2), "IDENT").is_empty());
    send(&mut s, 1, format!("IDENT|1|{host}|{host}|3|N|1|ARG-1|4142|1|1"), 0.25);
    let changed = s.snapshots(0.32);
    assert_eq!(kinds(&changed, addr(3), "IDENT"), vec![format!("IDENT|1|{}|{host}|3|N|1|ARG-1|4142|1|1", l[2])]);
}

#[test]
fn xb_bundles_only_for_announcing_clients_and_carry_the_same_messages() {
    let (mut s, l) = session(60);
    // Wrong lease, wrong address and an unknown version do not enable bundles.
    send(&mut s, 2, format!("XBCAP|1|{}|1", l[2]), 0.03);
    send(&mut s, 3, format!("XBCAP|1|{}|1", l[1]), 0.03);
    send(&mut s, 2, format!("XBCAP|1|{}|2", l[1]), 0.03);
    assert!(s.core.peers.values().all(|p| !p.bundle));
    assert!(send(&mut s, 2, format!("XBCAP|1|{}|1", l[1]), 0.03).is_empty());
    let host = l[0].clone();
    for id in 1..=60 {
        nhp(&mut s, &host, id, id, 100, 0.05);
        send(&mut s, 1, format!("IDENT|1|{host}|{host}|{id}|N|{id}|ARG-{id}|4142|1|0"), 0.05);
        let umove = format!("UMOVE|1|{host}|{host}|{id}|{id}|{SECTOR}|10.5|{id}.25|2|3|0|0.5|0|100|0|0|0|0|0");
        send(&mut s, 1, umove, 0.05);
    }
    let plain = s.snapshots(0.1);
    let bundled = bundle(plain.clone(), &s.core.peers);
    // p3 never announced: its packets pass through unchanged and in order.
    let to = |packets: &[Packet], slot: u16| -> Vec<Vec<u8>> {
        packets.iter().filter(|(a, _)| *a == addr(slot)).map(|(_, p)| p.clone()).collect()
    };
    assert_eq!(to(&bundled, 3), to(&plain, 3));
    assert_eq!(to(&bundled, 1), to(&plain, 1));
    // p2 receives far fewer datagrams, each within the native transport limit, which
    // carry exactly the original messages in the original order.
    let p2_plain: Vec<String> = to(&plain, 2).into_iter().map(|p| String::from_utf8(p).unwrap()).collect();
    let p2_bundled: Vec<String> = to(&bundled, 2).into_iter().map(|p| String::from_utf8(p).unwrap()).collect();
    assert!(p2_bundled.iter().all(|p| p.len() <= MAX_PACKET));
    assert!(p2_bundled.len() * 3 < p2_plain.len(), "{} datagrams for {} messages", p2_bundled.len(), p2_plain.len());
    let mut restored = Vec::new();
    for packet in &p2_bundled {
        if packet.starts_with("XB|") {
            restored.extend(unbundle(packet, &l[1]));
        } else {
            restored.push(packet.clone());
        }
    }
    assert_eq!(restored, p2_plain);
}

#[test]
fn bundle_keeps_foreign_and_oversize_packets_whole_and_in_order() {
    let mut s =
        Session::new_dedicated_players(Level::Universe, TOKEN, BUILD, "bubble", LeaseSource::Counter(0), 3).unwrap();
    let l: Vec<String> = (1..=2).map(|slot| join(&mut s, slot, 0.0)).collect();
    send(&mut s, 2, format!("XBCAP|1|{}|1", l[1]), 0.01);
    let big = format!("BIG|1|{}|{}", l[1], "x".repeat(MAX_PACKET - 40));
    let packets: Vec<Packet> = vec![
        (addr(2), format!("A|1|{}|1", l[1]).into_bytes()),
        (addr(2), format!("B|1|{}|2", l[1]).into_bytes()),
        (addr(2), b"RESULT|1|0123|OK".to_vec()),
        (addr(2), format!("C|1|{}|3", l[1]).into_bytes()),
        (addr(2), big.clone().into_bytes()),
        (addr(2), format!("D|1|{}|4", l[1]).into_bytes()),
    ];
    let out: Vec<String> =
        bundle(packets, &s.core.peers).into_iter().map(|(_, p)| String::from_utf8(p).unwrap()).collect();
    let lease = &l[1];
    assert_eq!(
        out,
        vec![
            format!("XB|1|{lease}|3:A|13:B|2"),
            "RESULT|1|0123|OK".to_owned(),
            format!("C|1|{lease}|3"),
            big,
            format!("D|1|{lease}|4"),
        ]
    );
}
