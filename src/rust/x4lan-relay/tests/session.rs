//! Rust ports of tests/test_host.py's session tests, plus real-UDP and co-op flows.
//! These run with `cargo test` alone; byte-for-byte parity with Python is checked by
//! src/rust/parity/relay_parity.py.

use std::net::{SocketAddr, UdpSocket};
use std::time::Duration;

use x4lan_relay::relay::Relay;
use x4lan_relay::session::{LeaseSource, Level, Session};
use x4lan_relay::wire::Slot;

const TOKEN: &str = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

fn addr(port: u16) -> SocketAddr {
    SocketAddr::from(([127, 0, 0, 1], port))
}

fn p1() -> SocketAddr {
    addr(10001)
}

fn p2() -> SocketAddr {
    addr(10002)
}

fn foreign() -> SocketAddr {
    addr(10003)
}

fn hello(slot: &str, nonce: char) -> Vec<u8> {
    format!("HELLO|1|{TOKEN}|test|sector_macro|bubble|{slot}|{}", nonce.to_string().repeat(32)).into_bytes()
}

fn state(lease: &str, seq: u32, pose: &str) -> Vec<u8> {
    format!("STATE|1|{lease}|{seq}|{pose}").into_bytes()
}

fn session(level: Level) -> Session {
    Session::new(level, TOKEN, "test", "bubble", LeaseSource::Os).unwrap()
}

fn join(s: &mut Session, slot: &str, address: SocketAddr, nonce: char, now: f64) -> String {
    let reply = s.receive(&hello(slot, nonce), address, now);
    let text = String::from_utf8(reply[0].clone()).unwrap();
    text.split('|').nth(3).unwrap().to_owned()
}

fn text(packets: &[(SocketAddr, Vec<u8>)], to: SocketAddr) -> Vec<String> {
    packets.iter().filter(|(a, _)| *a == to).map(|(_, p)| String::from_utf8(p.clone()).unwrap()).collect()
}

#[test]
fn two_clients_receive_only_remote_pose() {
    let mut s = session(Level::Base);
    let lease1 = join(&mut s, "p1", p1(), '1', 0.0);
    let lease2 = join(&mut s, "p2", p2(), '2', 0.0);
    s.receive(&state(&lease1, 1, "10|20|30|0.1|0.2|0.3"), p1(), 0.1);
    s.receive(&state(&lease2, 1, "-10|-20|-30|-0.1|-0.2|-0.3"), p2(), 0.1);
    let snapshots = s.snapshots(0.2);
    let to_p1 = &text(&snapshots, p1())[0];
    assert!(to_p1.contains("|-10|-20|-30|"), "{to_p1}");
    assert_eq!(to_p1.split('|').nth(2).unwrap(), lease1);
    assert!(text(&snapshots, p2())[0].contains("|10|20|30|"));
}

#[test]
fn auth_build_sector_bubble_and_slot_mismatch() {
    let mut s = session(Level::Base);
    for message in [
        format!("HELLO|1|{}|test|sector_macro|bubble|p1|{}", "b".repeat(32), "1".repeat(32)),
        format!("HELLO|1|{TOKEN}|other|sector_macro|bubble|p1|{}", "1".repeat(32)),
        format!("HELLO|1|{TOKEN}|test|sector_macro|else|p1|{}", "1".repeat(32)),
        format!("HELLO|1|{TOKEN}|test|sector_macro|bubble|p3|{}", "1".repeat(32)),
    ] {
        assert!(s.receive(message.as_bytes(), p1(), 0.0).is_empty());
    }
    join(&mut s, "p1", p1(), '1', 0.0);
    let other_sector = format!("HELLO|1|{TOKEN}|test|other|bubble|p2|{}", "2".repeat(32));
    assert!(s.receive(other_sector.as_bytes(), p2(), 0.1).is_empty());
    assert_eq!(s.core.peers.len(), 1);
}

#[test]
fn endpoint_cannot_take_another_slot_and_source_is_bound() {
    let mut s = session(Level::Base);
    let lease = join(&mut s, "p1", p1(), '1', 0.0);
    assert!(s.receive(&hello("p2", '2'), p1(), 1.0).is_empty());
    assert!(s.receive(&hello("p1", '2'), p2(), 1.0).is_empty());
    s.receive(&state(&lease, 3, "1|2|3|0|0|0"), foreign(), 0.1);
    assert!(s.peer(Slot::P1).unwrap().pose.is_none());
    s.receive(&state(&lease, 3, "1|2|3|0|0|0"), p1(), 0.2);
    s.receive(&state(&lease, 2, "99|0|0|0|0|0"), p1(), 0.3);
    assert_eq!(s.peer(Slot::P1).unwrap().pose.unwrap()[0], 1.0);
}

#[test]
fn nonfinite_bounds_and_malformed_packets_are_rejected() {
    let mut s = session(Level::Base);
    let lease = join(&mut s, "p1", p1(), '1', 0.0);
    for pose in ["nan|0|0|0|0|0", "inf|0|0|0|0|0", "50001|0|0|0|0|0", "0|0|0|10|0|0"] {
        s.receive(&state(&lease, 1, pose), p1(), 0.1);
    }
    assert!(s.peer(Slot::P1).unwrap().pose.is_none());
    for raw in [&b""[..], b"\xff", &[b'a'; 769], b"STATE|1", b"HELLO|2|bad"] {
        assert!(s.receive(raw, foreign(), 0.1).is_empty());
    }
    assert_eq!(s.core.peers.len(), 1);
}

#[test]
fn thirty_second_lease_new_lease_and_lost_welcome() {
    let mut s = session(Level::Base);
    let old = join(&mut s, "p1", p1(), '1', 0.0);
    assert_eq!(join(&mut s, "p1", p1(), '1', 1.0), old); // lost WELCOME retry
    s.receive(&state(&old, 1, "1|2|3|0|0|0"), p1(), 20.0);
    s.expire(49.999);
    assert!(s.peer(Slot::P1).is_some());
    s.expire(50.0);
    assert!(s.peer(Slot::P1).is_none());
    assert!(s.receive(&hello("p1", '1'), p1(), 50.0).is_empty()); // retired nonce
    let new = join(&mut s, "p1", p1(), '3', 50.0);
    assert_ne!(old, new);
}

#[test]
fn resume_keeps_lease_and_sequence() {
    let mut s = session(Level::Base);
    let lease = join(&mut s, "p1", p1(), '1', 0.0);
    s.receive(&state(&lease, 7, "1|2|3|0|0|0"), p1(), 0.1);
    s.expire(31.0);
    let resume = format!("RESUME|1|{TOKEN}|test|sector_macro|bubble|p1|{}|{lease}|{}", "1".repeat(32), "4".repeat(32));
    let reply = s.receive(resume.as_bytes(), foreign(), 31.0);
    assert!(String::from_utf8(reply[0].clone())
        .unwrap()
        .starts_with(&format!("WELCOME|1|{}|{lease}|20", "4".repeat(32))));
    s.receive(&state(&lease, 7, "9|9|9|0|0|0"), foreign(), 31.1); // sequence survived
    assert!(s.peer(Slot::P1).unwrap().pose.is_none());
    let fresh = session(Level::Base).receive(resume.as_bytes(), p1(), 0.0);
    assert_eq!(fresh, vec![format!("RESET|1|{}", "4".repeat(32)).into_bytes()]);
}

#[test]
fn coop_pilot_handoff_bumps_epoch_and_denies_occupied_ship() {
    let mut s = session(Level::Coop);
    let host = join(&mut s, "p1", p1(), '1', 0.0);
    let guest = join(&mut s, "p2", p2(), '2', 0.0);
    s.receive(format!("CREW|1|{host}|1|1|1").as_bytes(), p1(), 0.1);
    let denied = s.receive(format!("CREW|1|{guest}|1|1|1").as_bytes(), p2(), 0.1);
    assert_eq!(denied, vec![format!("PILOT_DENIED|1|{guest}|1|1").into_bytes()]);
    s.receive(format!("CREW|1|{host}|2|0|0").as_bytes(), p1(), 0.2);
    assert!(s.receive(format!("CREW|1|{guest}|2|1|1").as_bytes(), p2(), 0.2).is_empty());
    let auth = text(&s.snapshots(0.3), p1()).into_iter().find(|p| p.starts_with("AUTH|")).unwrap();
    assert!(auth.ends_with("|p2|2|p2|p2|1|0"), "{auth}");
}

#[test]
fn dedicated_admits_second_guest_and_hands_it_ship_one() {
    let mut s = Session::new_dedicated(Level::Coop, TOKEN, "test", "bubble", LeaseSource::Os).unwrap();
    join(&mut s, "p1", p1(), '1', 0.0);
    join(&mut s, "p2", p2(), '2', 0.0);
    let guest = join(&mut s, "p3", foreign(), '3', 0.0);
    assert!(s.receive(format!("CREW|1|{guest}|1|1|1").as_bytes(), foreign(), 0.1).is_empty());
    assert_eq!(s.owner(x4lan_relay::wire::Ship::ONE), Slot::P3);
    for address in [p1(), p2(), foreign()] {
        let auth = text(&s.snapshots(0.2), address).into_iter().find(|p| p.starts_with("AUTH|")).unwrap();
        assert!(auth.contains("|p3|2|p3|"), "{auth}");
    }
}

#[test]
fn dedicated_universe_keeps_guest_baselines_independent() {
    let mut s = Session::new_dedicated(Level::Universe, TOKEN, "world1-test", "bubble", LeaseSource::Os).unwrap();
    let join_world = |s: &mut Session, slot: &str, address, nonce: char| {
        let packet = format!("HELLO|1|{TOKEN}|world1-test|sector_macro|bubble|{slot}|{}", nonce.to_string().repeat(32));
        let reply = s.receive(packet.as_bytes(), address, 0.0);
        String::from_utf8(reply[0].clone()).unwrap().split('|').nth(3).unwrap().to_owned()
    };
    let host = join_world(&mut s, "p1", p1(), '1');
    let guest2 = join_world(&mut s, "p2", p2(), '2');
    let guest3 = join_world(&mut s, "p3", foreign(), '3');
    for (lease, address, sector) in [(&guest2, p2(), "a_macro"), (&guest3, foreign(), "b_macro")] {
        let packet = format!("UINTEREST|1|{lease}|1|{sector}");
        assert!(s.receive(packet.as_bytes(), address, 0.1).is_empty());
    }
    assert_eq!(s.universe.views.len(), 2);
    assert!(s.universe.views.values().all(|view| view.interest.is_some()));
    for (revision, sector) in [(1, "a_macro"), (2, "b_macro")] {
        for packet in
            [format!("UBEGIN|1|{host}|{host}|{revision}|{sector}|0|1"), format!("UCOMMIT|1|{host}|{host}|{revision}")]
        {
            assert!(!s.receive(packet.as_bytes(), p1(), 0.2).is_empty());
        }
    }
    assert_eq!(s.universe.entity_ids.len(), 2);
    let first = s.snapshots(0.3);
    assert!(
        text(&first, p2()).iter().any(|p| p.starts_with("UBEGIN|") && p.contains("|a_macro|")),
        "{:?}",
        text(&first, p2())
    );
    assert!(text(&first, foreign()).iter().any(|p| p.starts_with("UBEGIN|") && p.contains("|b_macro|")));
    assert!(!text(&first, p2()).iter().any(|p| p.starts_with("UBEGIN|") && p.contains("|b_macro|")));
    for packet in text(&first, p2()).into_iter().filter(|p| p.starts_with("UBEGIN|")) {
        let f: Vec<&str> = packet.split('|').collect();
        let ack = format!("UACK|1|{guest2}|{}|{}|0", f[3], f[4]);
        s.receive(ack.as_bytes(), p2(), 0.4);
    }
    let second = s.snapshots(0.6);
    assert!(text(&second, p2()).iter().any(|p| p.starts_with("UCOMMIT|")));
    assert!(text(&second, foreign()).iter().any(|p| p.starts_with("UBEGIN|")));
}

#[test]
fn real_udp_round_trip() {
    let mut relay = Relay::bind("127.0.0.1", 0, session(Level::Base)).unwrap();
    let port = relay.socket.local_addr().unwrap().port();
    let client = UdpSocket::bind("127.0.0.1:0").unwrap();
    client.set_read_timeout(Some(Duration::from_secs(1))).unwrap();
    client.send_to(&hello("p1", '1'), ("127.0.0.1", port)).unwrap();
    client.send_to(&[b'x'; 2000], ("127.0.0.1", port)).unwrap(); // oversize: dropped, relay survives
    std::thread::sleep(Duration::from_millis(50));
    relay.step(0.0);
    let mut buffer = [0u8; 800];
    let (count, _) = client.recv_from(&mut buffer).unwrap();
    assert!(buffer[..count].starts_with(b"WELCOME|1|"));
    let (count, _) = client.recv_from(&mut buffer).unwrap();
    assert!(buffer[..count].starts_with(b"EMPTY|1|"));
    assert_eq!(relay.received_packets + relay.receive_errors, 2);
}
