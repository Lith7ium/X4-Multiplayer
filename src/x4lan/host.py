"""Two-slot UDP relay. Only Python's standard library is required (3.10+)."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hmac
import json
import math
import re
import secrets
import socket
import time

MAX_PACKET = 768
LEASE_SECONDS = 30.0
STATE_SECONDS = 0.75
TICK_HZ = 20
ATOM = re.compile(r"[A-Za-z0-9_.-]{1,96}\Z")
HEX = re.compile(r"[a-f0-9]{32}\Z")
MAX_SEQ = 2**31 - 1
BUNDLE_VERSION = "1"
BUNDLE_KIND = re.compile(rb"[A-Z0-9_]{1,32}\Z")


def bundle(packets, peers):
    """Pack one tick's snapshot packets into XB datagrams for bundle-capable peers.

    ``XB|1|<lease>|<len>:<item><len>:<item>...``: an item is a packet
    ``KIND|1|<lease>|rest`` addressed to that peer, carried as ``KIND|rest``; the client
    restores the envelope and validates every message exactly as if sent alone.
    Datagrams stay within MAX_PACKET, the limit of the native client transport.
    Packets for other peers, other forms and oversize items pass through unchanged and
    each destination's packet order is preserved. A bundle of one item is sent as the
    original packet. Without capable peers the input is returned unchanged.
    """
    capable = {p.address: p.lease.encode('ascii') for p in peers.values() if p.bundle}
    if not capable:
        return list(packets)
    out, open_ = [], {}

    def flush(address):
        items = open_.pop(address, None)
        if not items:
            return
        if len(items) == 1:
            out.append((address, items[0][0]))
        else:
            out.append(
                (address, b'XB|1|' + capable[address] + b'|' + b''.join(i for _, i in items))
            )

    for address, packet in packets:
        lease = capable.get(address)
        kind, _, rest = packet.partition(b'|')
        envelope = b'1|' + lease + b'|' if lease else None
        if not envelope or not BUNDLE_KIND.fullmatch(kind) or not rest.startswith(envelope):
            flush(address)
            out.append((address, packet))
            continue
        body = kind + b'|' + rest[len(envelope) :]
        item = str(len(body)).encode('ascii') + b':' + body
        header = len(b'XB|1|') + len(lease) + 1
        if header + len(item) > MAX_PACKET:
            flush(address)
            out.append((address, packet))
            continue
        items = open_.get(address)
        if items and header + sum(len(i) for _, i in items) + len(item) > MAX_PACKET:
            flush(address)
            items = None
        if items is None:
            items = open_[address] = []
        items.append((packet, item))
    for address in list(open_):
        flush(address)
    return out


def sequence(value: str) -> int:
    if not value.isascii() or not value.isdecimal():
        raise ValueError("invalid sequence")
    result = int(value)
    if not 0 <= result <= MAX_SEQ:
        raise ValueError("sequence out of bounds")
    return result


@dataclass
class Peer:
    address: tuple[str, int]
    nonce: str
    lease: str
    last_seen: float
    seq: int = -1
    state_at: float = -math.inf
    pose: tuple[float, ...] | None = None
    joined_at: float = 0.0
    state_messages: int = 0
    resume_from: str | None = None
    # The client sent XBCAP|1|<lease>|1: it unpacks XB bundles (see bundle()).
    bundle: bool = False


class Session:
    """Session/slot authority only. Client physics is NOT authoritative here."""

    def __init__(self, token: str, build: str, bubble: str):
        if not HEX.fullmatch(token):
            raise ValueError("token must be 32 lowercase hexadecimal characters")
        if not ATOM.fullmatch(build) or not ATOM.fullmatch(bubble):
            raise ValueError("build/bubble must be simple identifiers")
        self.token, self.build, self.bubble = token, build, bubble
        self.sector: str | None = None
        self.peers: dict[str, Peer] = {}
        self.tick = 0
        self.accepted_states = 0
        self.retired: list[tuple[str, str]] = []
        # Retain only the last expired identity per slot. Resume proves possession
        # of its lease and nonce; a fresh game claiming the slot invalidates it.
        self.resumable: dict[str, tuple[Peer, str]] = {}

    def expire(self, now: float) -> None:
        for slot, peer in list(self.peers.items()):
            if now - peer.last_seen >= LEASE_SECONDS:
                self.resumable[slot] = (peer, self.sector)
                self.retired.append((slot, peer.nonce))
                self.retired = self.retired[-64:]
                del self.peers[slot]
        if not self.peers:
            self.sector = None

    def receive(self, raw: bytes, address: tuple[str, int], now: float) -> list[bytes]:
        self.expire(now)
        if len(raw) > MAX_PACKET:
            return []
        try:
            fields = raw.decode("ascii").split("|")
            if len(fields) < 2 or fields[1] != "1":
                return []
            if fields[0] == "PING" and len(fields) == 4:
                _, _, token, nonce = fields
                if not hmac.compare_digest(token, self.token) or not HEX.fullmatch(nonce):
                    return []
                # Authenticated readiness check: never claims a slot or fixes a sector.
                return [f"PONG|1|{nonce}|{self.build}|{self.bubble}".encode("ascii")]
            if fields[0] == "RESUME" and len(fields) == 10:
                _, _, token, build, sector, bubble, slot, previous_nonce, lease, nonce = fields
                if (
                    not hmac.compare_digest(token, self.token)
                    or build != self.build
                    or bubble != self.bubble
                    or slot not in ('p1', 'p2')
                    or not all(HEX.fullmatch(v) for v in (previous_nonce, lease, nonce))
                    or not ATOM.fullmatch(sector)
                ):
                    return []
                peer = self.peers.get(slot)
                saved = self.resumable.get(slot)
                candidate = peer or (saved[0] if saved else None)
                if candidate is None:
                    # A new relay cannot prove this is the same world. Do not
                    # silently start a second economy over an existing game.
                    return [f"RESET|1|{nonce}".encode('ascii')]
                if (
                    not hmac.compare_digest(candidate.lease, lease)
                    or (
                        candidate.nonce != previous_nonce
                        and not (
                            candidate.nonce == nonce and candidate.resume_from == previous_nonce
                        )
                    )
                    or (self.sector is not None and self.sector != sector)
                    or (peer is None and saved[1] != sector)
                    or any(p.address == address for key, p in self.peers.items() if key != slot)
                ):
                    return []
                candidate.resume_from = previous_nonce
                candidate.address, candidate.nonce, candidate.last_seen = address, nonce, now
                # Keep sequence, world identity and command deduplication intact.
                candidate.pose = None
                self.peers[slot] = candidate
                self.resumable.pop(slot, None)
                self.sector = sector
                return [f"WELCOME|1|{nonce}|{lease}|{TICK_HZ}".encode('ascii')]
            if fields[0] == "HELLO" and len(fields) == 8:
                _, _, token, build, sector, bubble, slot, nonce = fields
                if not hmac.compare_digest(token, self.token):
                    return []
                if (
                    build != self.build
                    or bubble != self.bubble
                    or slot not in ("p1", "p2")
                    or not HEX.fullmatch(nonce)
                    or not ATOM.fullmatch(sector)
                ):
                    return []
                if self.sector is not None and sector != self.sector:
                    return []
                peer = self.peers.get(slot)
                if peer and (peer.address != address or peer.nonce != nonce):
                    return []
                if peer is None:
                    if (slot, nonce) in self.retired:
                        return []
                    # A single endpoint must not own both slots.
                    if any(p.address == address for p in self.peers.values()):
                        return []
                    peer = Peer(address, nonce, secrets.token_hex(16), now, joined_at=now)
                    self.peers[slot] = peer
                    self.resumable.pop(slot, None)
                    self.sector = sector
                peer.last_seen = now
                return [f"WELCOME|1|{nonce}|{peer.lease}|{TICK_HZ}".encode("ascii")]
            if fields[0] == "XBCAP" and len(fields) == 4:
                # Additive capability: relays without it ignore the message, and only a
                # client that announced it ever receives an XB bundle.
                peer = next(
                    (
                        p
                        for p in self.peers.values()
                        if p.address == address and hmac.compare_digest(p.lease, fields[2])
                    ),
                    None,
                )
                if peer is not None and fields[3] == BUNDLE_VERSION:
                    peer.bundle = True
                return []
            if fields[0] not in ("STATE", "IDLE"):
                return []
            if len(fields) != (10 if fields[0] == "STATE" else 4):
                return []
            peer = next(
                (
                    p
                    for p in self.peers.values()
                    if p.address == address and hmac.compare_digest(p.lease, fields[2])
                ),
                None,
            )
            if peer is None:
                return []
            seq = sequence(fields[3])
            if seq <= peer.seq:
                return []
            pose = None
            if fields[0] == "STATE":
                pose = tuple(float(x) for x in fields[4:])
                if not all(math.isfinite(x) for x in pose):
                    return []
                if any(abs(x) > 50000 for x in pose[:3]):
                    return []
                if any(abs(x) > math.tau for x in pose[3:]):
                    return []
            peer.seq, peer.pose, peer.last_seen = seq, pose, now
            peer.resume_from = None
            peer.state_at = now
            peer.state_messages += 1
            self.accepted_states += pose is not None
        except (UnicodeError, ValueError, OverflowError):
            return []
        return []

    def snapshots(self, now: float) -> list[tuple[tuple[str, int], bytes]]:
        self.expire(now)
        self.tick += 1
        result = []
        for slot, peer in self.peers.items():
            remote = self.peers.get("p2" if slot == "p1" else "p1")
            if remote is None or remote.pose is None or now - remote.state_at >= STATE_SECONDS:
                packet = f"EMPTY|1|{peer.lease}|{self.tick}"
            else:
                pose = "|".join(format(x, ".9g") for x in remote.pose)
                packet = f"PEER|1|{peer.lease}|{self.tick}|{remote.lease}|{remote.seq}|{pose}"
            result.append((peer.address, packet.encode("ascii")))
        return result


class Relay:
    def __init__(self, bind: str, port: int, session: Session):
        self.session = session
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            # Windows otherwise surfaces a departed UDP peer's ICMP reply on recv.
            if hasattr(socket, 'SIO_UDP_CONNRESET'):
                self.socket.ioctl(socket.SIO_UDP_CONNRESET, False)
            self.socket.bind((bind, port))
        except OSError:
            self.socket.close()
            raise
        self.socket.setblocking(False)
        self.next_tick = 0.0
        self.received_packets = self.received_bytes = 0
        self.sent_packets = self.sent_bytes = self.send_errors = 0
        from .netprobe import NetProbe

        self.netprobe = NetProbe(time.monotonic())
        try:  # player factions (internal packages); absent from builds that omit players.py
            from .players import PlayerRoster

            self.players = PlayerRoster()
        except ImportError:
            self.players = None

    def step(self, now: float | None = None) -> None:
        now = time.monotonic() if now is None else now
        # Bound the work per loop, including under a packet flood.
        for _ in range(128):
            try:
                packet, address = self.socket.recvfrom(MAX_PACKET + 1)
            except BlockingIOError:
                break
            except ConnectionResetError:
                continue  # A departed Windows UDP endpoint is not a relay failure.
            self.received_packets += 1
            self.received_bytes += len(packet)
            peers = self.session.peers
            self.netprobe.count_rx(peers, address, len(packet))
            if packet.startswith(b'RTTPING|'):
                # Measurement only: answered here, never seen by or forwarded through the session.
                for response in self.netprobe.handle(packet, address, peers, now):
                    self._send(response, address)
                continue
            if self.players and packet.startswith(b'PF'):
                # Player factions: routed/cached here, never seen by the session classes.
                for target, response in self.players.handle(packet, address, peers, now):
                    self._send(response, target)
                continue
            for response in self.session.receive(packet, address, now):
                self._send(response, address)
        if now >= self.next_tick:
            packets = self.session.snapshots(now)
            if self.players:
                packets += self.players.snapshots(self.session.peers, now)
            for address, packet in bundle(packets, self.session.peers):
                self._send(packet, address)
            self.next_tick = now + 1 / TICK_HZ

    def _send(self, packet: bytes, address: tuple[str, int]) -> None:
        try:
            count = self.socket.sendto(packet, address)
            self.sent_packets += 1
            self.sent_bytes += count
            self.netprobe.count_tx(self.session.peers, address, count)
        except OSError:
            self.send_errors += 1  # Successful sends do not prove remote delivery.

    def close(self) -> None:
        self.socket.close()


def require_two_players(config) -> None:
    """The Python relay is deliberately limited to two players (p1/p2).

    Dedicated and N-player sessions (session.json ``dedicated``/``max_players``) are served
    only by the Rust supervisor ``x4lan-dedicated``; refuse them instead of silently
    running a two-slot relay for a larger session.
    """
    if config.get('dedicated') or config.get('max_players', 2) != 2:
        raise ValueError(
            'The Python relay serves two-player sessions only; '
            'dedicated/N-player sessions use x4lan-dedicated.'
        )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int)
    parser.add_argument("--token")
    parser.add_argument("--build", help="identical build label on both clients")
    parser.add_argument("--bubble")
    parser.add_argument("--session", help="session.json generated by prepare.py")
    parser.add_argument("--bubble-actions", action="store_true")
    parser.add_argument("--trading", action="store_true")
    args = parser.parse_args()
    config = {}
    if args.session:
        with open(args.session, encoding="utf-8") as stream:
            config = json.load(stream)
    try:
        require_two_players(config)
    except ValueError as exc:
        parser.error(str(exc))
    token, build = args.token or config.get("token"), args.build or config.get("build")
    if not token or not build:
        parser.error("supply --session or both --token and --build")
    bubble = args.bubble or config.get("bubble", "bubble-01")
    port = args.port or config.get("port", 45841)
    from .bubble import BubbleSession
    from .economy import EconomySession
    from .coop import CoopSession

    session_type = (
        CoopSession
        if config.get('coop')
        else EconomySession
        if args.trading
        else BubbleSession
        if args.bubble_actions
        else Session
    )
    if config.get('world_sync'):
        from .universe import UniverseSession

        session_type = UniverseSession
    session = session_type(token, build, bubble)
    relay = Relay(args.bind, port, session)
    print(
        f"X4 LAN transform relay on {args.bind}:{port}; build={build}; bubble={bubble}", flush=True
    )
    print(
        "Experimental host-authoritative test bubble."
        if args.bubble_actions or args.trading
        else "Experiment: client poses only; no shared physics, damage, AI or economy.",
        flush=True,
    )
    last_status = ""
    try:
        while True:
            relay.step()
            for line in relay.netprobe.report(relay.session.peers, time.monotonic()):
                print(line, flush=True)
            status = (
                ", ".join(
                    f"{slot}@{p.address[0]}:{p.address[1]}" for slot, p in session.peers.items()
                )
                or "waiting"
            )
            if status != last_status:
                print(status, flush=True)
                last_status = status
            time.sleep(0.005)
    except KeyboardInterrupt:
        pass
    finally:
        relay.close()


if __name__ == "__main__":
    main()
