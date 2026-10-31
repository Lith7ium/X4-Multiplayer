"""Relay side of the opt-in RTT probe and per-peer traffic rates.

Wire (version-1 ASCII envelope, never forwarded to the other player):
  client -> relay  RTTPING|1|lease|seq|client_ms|echo_relay_ms|hold_ms
  relay -> client  RTTPONG|1|lease|seq|client_ms|relay_ms
Clients that do not send RTTPING are unaffected; older relays drop RTTPING silently.
A ping never refreshes a lease or changes session state.

Relay RTT = relay receive time - echoed relay_ms - client hold_ms. Upstream (client->relay)
jitter is the p95 of |(arrival_i - arrival_i-1) - (client_ms_i - client_ms_i-1)|.
Upstream loss follows from sequence gaps. Packets are timestamped when the relay loop reads
them, so samples include the relay's poll interval.
"""
from __future__ import annotations

from collections import deque
from dataclasses import dataclass, field
import hmac
import math
import re

REPORT_SECONDS = 5.0
NUMBER = re.compile(r"\d{1,15}(\.\d{1,6})?\Z")
LEASE = re.compile(r"[a-f0-9]{32}\Z")
MAX_SEQ = 2**31 - 1
ISSUED = 64


def percentile(values, p):
    """Nearest-rank percentile; None for no samples. Matches measure_clock.lua."""
    if not values:
        return None
    ordered = sorted(values)
    rank = min(len(ordered), max(1, math.ceil(p * len(ordered))))
    return ordered[rank - 1]


@dataclass
class PeerStats:
    lease: str
    pings: int = 0
    first_seq: int | None = None
    last_seq: int | None = None
    previous: tuple[float, float] | None = None  # (arrival, client_ms/1000)
    rtts: list = field(default_factory=list)
    deviations: list = field(default_factory=list)
    rx_packets: int = 0
    rx_bytes: int = 0
    tx_packets: int = 0
    tx_bytes: int = 0
    issued: deque = field(default_factory=lambda: deque(maxlen=ISSUED))
    probed: bool = False

    def reset_window(self):
        self.pings = 0
        self.first_seq = self.last_seq = None
        self.rtts, self.deviations = [], []
        self.rx_packets = self.rx_bytes = self.tx_packets = self.tx_bytes = 0


class NetProbe:
    def __init__(self, now: float = 0.0, report_seconds: float = REPORT_SECONDS):
        self.report_seconds = report_seconds
        self.window_start = now
        self.stats: dict[str, PeerStats] = {}  # by slot
        self.latest: dict[str, dict] = {}
        self.enabled = False  # becomes true at the first valid RTTPING

    def _stats(self, slot, peer):
        stats = self.stats.get(slot)
        if stats is None or stats.lease != peer.lease:
            stats = self.stats[slot] = PeerStats(peer.lease)
        return stats

    @staticmethod
    def _slot(peers, address):
        for slot, peer in peers.items():
            if peer.address == address:
                return slot, peer
        return None, None

    def count_rx(self, peers, address, size):
        slot, peer = self._slot(peers, address)
        if peer is not None:
            stats = self._stats(slot, peer)
            stats.rx_packets += 1
            stats.rx_bytes += size

    def count_tx(self, peers, address, size):
        slot, peer = self._slot(peers, address)
        if peer is not None:
            stats = self._stats(slot, peer)
            stats.tx_packets += 1
            stats.tx_bytes += size

    def handle(self, raw: bytes, address, peers, now: float) -> list[bytes]:
        """Answer a valid RTTPING from an authenticated peer; ignore anything else."""
        try:
            fields = raw.decode("ascii").split("|")
        except UnicodeError:
            return []
        if len(fields) != 7 or fields[0] != "RTTPING" or fields[1] != "1" or not LEASE.fullmatch(fields[2]):
            return []
        _, _, lease, seq_text, client_ms, echo, hold = fields
        slot, peer = self._slot(peers, address)
        if peer is None or not hmac.compare_digest(peer.lease, lease):
            return []
        if not seq_text.isascii() or not seq_text.isdecimal() or len(seq_text) > 10 or not NUMBER.fullmatch(client_ms):
            return []
        seq = int(seq_text)
        if seq > MAX_SEQ:
            return []
        stats = self._stats(slot, peer)
        self.enabled = stats.probed = True
        stats.pings += 1
        if stats.first_seq is None or seq < stats.first_seq:
            stats.first_seq = seq
        if stats.last_seq is None or seq > stats.last_seq:
            stats.last_seq = seq
        source = float(client_ms) / 1000
        if stats.previous is not None and source > stats.previous[1]:
            stats.deviations.append(abs((now - stats.previous[0]) - (source - stats.previous[1])))
        if stats.previous is None or source > stats.previous[1]:
            stats.previous = (now, source)
        if echo != "-" and echo in stats.issued and NUMBER.fullmatch(hold):
            rtt = now - float(echo) / 1000 - float(hold) / 1000
            if 0 <= rtt <= 10:
                stats.rtts.append(rtt)
        relay_ms = f"{now * 1000:.3f}"
        stats.issued.append(relay_ms)
        return [f"RTTPONG|1|{lease}|{seq}|{client_ms}|{relay_ms}".encode("ascii")]

    def report(self, peers, now: float) -> list[str]:
        """Every report_seconds after the first ping: one NETRTT line per connected peer."""
        if now - self.window_start < self.report_seconds:
            return []
        window = max(now - self.window_start, 1e-9)
        self.window_start = now
        lines = []
        for slot in sorted(peers):
            peer = peers[slot]
            stats = self.stats.get(slot)
            if stats is None or stats.lease != peer.lease:
                continue
            expected = stats.last_seq - stats.first_seq + 1 if stats.first_seq is not None else 0
            lost = max(0, expected - stats.pings)

            def ms(value):
                return 'na' if value is None else f"{value * 1000:.3f}"
            row = dict(slot=slot, window_s=round(window, 3), pings=stats.pings, seq_lost=lost,
                       loss_pct=round(100 * lost / expected, 1) if expected else None,
                       rtt_samples=len(stats.rtts),
                       rtt_ms_min=ms(percentile(stats.rtts, 0)), rtt_ms_median=ms(percentile(stats.rtts, .5)),
                       rtt_ms_p95=ms(percentile(stats.rtts, .95)), rtt_ms_max=ms(percentile(stats.rtts, 1)),
                       jitter_ms_p95=ms(percentile(stats.deviations, .95)),
                       rx_pps=round(stats.rx_packets / window, 1), rx_Bps=round(stats.rx_bytes / window),
                       tx_pps=round(stats.tx_packets / window, 1), tx_Bps=round(stats.tx_bytes / window))
            self.latest[slot] = row
            if self.enabled:
                lines.append('NETRTT v=1 side=relay ' + ' '.join(
                    f"{k}={'na' if v is None else v}" for k, v in row.items()))
            stats.reset_window()
        for slot in list(self.latest):
            if slot not in peers:
                del self.latest[slot]
        return lines
