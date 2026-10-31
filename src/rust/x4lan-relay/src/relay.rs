//! host.py `Relay`: the non-blocking UDP loop around a [`Session`].

use std::io::ErrorKind;
use std::net::{SocketAddr, UdpSocket};

use crate::players::PlayerRoster;
use crate::session::{bundle, Session, TICK_HZ};
use crate::wire::MAX_PACKET;

pub struct Relay {
    pub session: Session,
    /// Player factions (players.py): PF* packets are routed and cached here, never seen by the session.
    pub players: PlayerRoster,
    pub socket: UdpSocket,
    next_tick: f64,
    pub received_packets: u64,
    pub received_bytes: u64,
    pub sent_packets: u64,
    pub sent_bytes: u64,
    pub send_errors: u64,
    /// Receive errors other than "would block"/"connection reset". The Python relay
    /// lets these escape and stops (see README: Windows WSAEMSGSIZE); this port counts
    /// and continues.
    pub receive_errors: u64,
}

#[cfg(windows)]
fn disable_connreset(socket: &UdpSocket) -> std::io::Result<()> {
    use std::os::windows::io::AsRawSocket;
    #[link(name = "ws2_32")]
    extern "system" {
        fn WSAIoctl(
            s: usize,
            code: u32,
            input: *const core::ffi::c_void,
            input_len: u32,
            output: *mut core::ffi::c_void,
            output_len: u32,
            returned: *mut u32,
            overlapped: *mut core::ffi::c_void,
            routine: *mut core::ffi::c_void,
        ) -> i32;
    }
    const SIO_UDP_CONNRESET: u32 = 0x9800_000C;
    let disabled: i32 = 0;
    let mut returned = 0u32;
    // SAFETY: a valid socket handle and a 4-byte BOOL input, as documented for the ioctl.
    let status = unsafe {
        WSAIoctl(
            socket.as_raw_socket() as usize,
            SIO_UDP_CONNRESET,
            (&disabled as *const i32).cast(),
            4,
            std::ptr::null_mut(),
            0,
            &mut returned,
            std::ptr::null_mut(),
            std::ptr::null_mut(),
        )
    };
    if status == 0 {
        Ok(())
    } else {
        Err(std::io::Error::last_os_error())
    }
}

impl Relay {
    pub fn bind(bind: &str, port: u16, session: Session) -> std::io::Result<Relay> {
        let socket = UdpSocket::bind((bind, port))?;
        // Windows otherwise surfaces a departed UDP peer's ICMP reply on recv.
        #[cfg(windows)]
        disable_connreset(&socket)?;
        socket.set_nonblocking(true)?;
        Ok(Relay {
            session,
            players: PlayerRoster::new(),
            socket,
            next_tick: 0.0,
            received_packets: 0,
            received_bytes: 0,
            sent_packets: 0,
            sent_bytes: 0,
            send_errors: 0,
            receive_errors: 0,
        })
    }

    pub fn step(&mut self, now: f64) {
        let mut buffer = [0u8; MAX_PACKET + 1];
        // Bound the work per loop, including under a packet flood.
        for _ in 0..128 {
            let (count, address) = match self.socket.recv_from(&mut buffer) {
                Ok(received) => received,
                Err(e) if e.kind() == ErrorKind::WouldBlock => break,
                // A departed Windows UDP endpoint is not a relay failure.
                Err(e) if e.kind() == ErrorKind::ConnectionReset => continue,
                Err(_) => {
                    self.receive_errors += 1;
                    continue;
                }
            };
            self.received_packets += 1;
            self.received_bytes += count as u64;
            if buffer[..count].starts_with(b"RTTPING|") {
                // Measurement only (netprobe.py): answered here, never seen by the session.
                if let Some(reply) = self.rtt_pong(&buffer[..count], address, now) {
                    self.send(&reply, address);
                }
                continue;
            }
            if buffer[..count].starts_with(b"PF") {
                for (target, packet) in self.players.handle(&buffer[..count], address, &self.session.core.peers, now) {
                    self.send(&packet, target);
                }
                continue;
            }
            for reply in self.session.receive(&buffer[..count], address, now) {
                self.send(&reply, address);
            }
        }
        if now >= self.next_tick {
            let mut packets = self.session.snapshots(now);
            packets.extend(self.players.snapshots(&self.session.core.peers, now));
            for (address, packet) in bundle(packets, &self.session.core.peers) {
                self.send(&packet, address);
            }
            self.next_tick = now + 1.0 / f64::from(TICK_HZ);
        }
    }

    /// `RTTPING|1|lease|seq|client_ms|echo|hold` from an authenticated peer ->
    /// `RTTPONG|1|lease|seq|client_ms|relay_ms` (netprobe.py `handle`, without the NETRTT
    /// statistics). A ping never refreshes a lease or changes session state.
    pub fn rtt_pong(&self, raw: &[u8], address: SocketAddr, now: f64) -> Option<Vec<u8>> {
        let text = std::str::from_utf8(raw).ok().filter(|t| t.is_ascii())?;
        let f: Vec<&str> = text.split('|').collect();
        if f.len() != 7 || f[0] != "RTTPING" || f[1] != "1" {
            return None;
        }
        self.session.authenticated(f[2], address)?;
        let digits = |t: &str, max: usize| !t.is_empty() && t.len() <= max && t.bytes().all(|b| b.is_ascii_digit());
        let number = |t: &str| match t.split_once('.') {
            Some((whole, frac)) => digits(whole, 15) && digits(frac, 6),
            None => digits(t, 15),
        };
        if !digits(f[3], 10) || f[3].parse::<u64>().ok()? > u64::from(crate::wire::MAX_SEQ) || !number(f[4]) {
            return None;
        }
        let seq = f[3].parse::<u64>().ok()?;
        Some(format!("RTTPONG|1|{}|{seq}|{}|{:.3}", f[2], f[4], now * 1000.0).into_bytes())
    }

    fn send(&mut self, packet: &[u8], address: SocketAddr) {
        match self.socket.send_to(packet, address) {
            Ok(count) => {
                self.sent_packets += 1;
                self.sent_bytes += count as u64;
            }
            // Successful sends do not prove remote delivery.
            Err(_) => self.send_errors += 1,
        }
    }
}
