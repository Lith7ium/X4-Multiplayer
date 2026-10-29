"""Original W01 sector snapshot transport. No third-party multiplayer code.

This extends co-op with bounded, acknowledged, atomic object baselines. It does
not simulate NPCs, validate pickup transactions, or synchronize vanilla missions.
The host X4 process supplies state. Absence means removal from a sector view,
not proof of global destruction. Sessions use a distinct world1- build label.
"""

from dataclasses import dataclass
import math
import zlib

from .coop import pose
from .docking import DockSession
from .host import ATOM, HEX, MAX_PACKET, sequence
from .cargo import CargoTransactions
from .identity import Identities
from .npc_combat import NpcCombat

MAX_OBJECTS = 4096
MAX_RECORD = 512
WINDOW = 12
MOTION_BUDGET = 32
# Unchanged samples repeated per tick for datagram loss. A full budget of repeats
# (32 per tick, 640/s per guest) made up half of a guest's datagrams in a dense
# sector while every mover already sends fresh samples; still objects resend every 2 s.
REPEAT_BUDGET = 2
# A sample is repeated only while younger than MOTION_LIVE s. The server sends on divergence
# (a straight-flying NPC only every 1-4 s), and a client extrapolates any sample, so a sample
# this client has not received yet (late joiner, sector change) is forwarded while younger
# than MOTION_KEEP s, after the recent ones.
MOTION_LIVE = 0.75
MOTION_KEEP = 5.0
RETRY = 0.25
STAGE_TIMEOUT = 15


def atom(value, macro=False):
    if not ATOM.fullmatch(value) or (macro and not value.endswith('_macro')):
        raise ValueError('invalid object identifier')
    return value


def record(value):
    """id,kind,macro,owner,x,y,z,yaw,pitch,roll,ware,amount (all ASCII)."""
    if (
        len(value) > MAX_RECORD
        or not value.isascii()
        or any(character.isspace() for character in value)
    ):
        raise ValueError('record size/encoding')
    fields = value.split(',')
    if len(fields) != 12 or not sequence(fields[0]):
        raise ValueError('record fields/id')
    if fields[1] not in ('ship', 'drop'):
        raise ValueError('object kind')
    atom(fields[2], True)
    atom(fields[3])
    atom(fields[10])
    for i, text in enumerate(fields[4:10]):
        coordinate = float(text)
        if not math.isfinite(coordinate) or abs(coordinate) > (50_000_000 if i < 3 else math.tau):
            raise ValueError('object pose')
    amount = sequence(fields[11])
    if amount > 1_000_000 or (fields[1] == 'ship' and (fields[10] != 'none' or amount)):
        raise ValueError('object cargo')
    if fields[1] == 'drop' and (fields[10] == 'none' or not amount):
        raise ValueError('empty drop')
    return int(fields[0])


def checksum(rows):
    # Integrity check for assembly, not authentication; the lease authenticates.
    return zlib.adler32('\n'.join(rows).encode('ascii')) & 0xFFFFFFFF


@dataclass(frozen=True)
class Baseline:
    epoch: str
    revision: int
    sector: str
    rows: tuple[str, ...]
    digest: int

    @classmethod
    def create(cls, epoch, revision, sector, rows):
        rows = tuple(rows)
        if not HEX.fullmatch(epoch) or not 0 < revision <= 2147483647:
            raise ValueError('baseline identity')
        atom(sector, True)
        if len(rows) > MAX_OBJECTS:
            raise ValueError('sector exceeds object limit; never truncate')
        ids = [record(row) for row in rows]
        if ids != sorted(set(ids)):
            raise ValueError('records must have unique increasing IDs')
        return cls(epoch, revision, sector, rows, checksum(rows))

    def packet(self, lease, index):
        head = f'|1|{lease}|{self.epoch}|{self.revision}'
        if index == 0:
            body = f'UBEGIN{head}|{self.sector}|{len(self.rows)}|{self.digest}'
        elif index == len(self.rows) + 1:
            body = f'UCOMMIT{head}'
        else:
            body = f'UOBJECT{head}|{index}|{self.rows[index - 1]}'
        data = body.encode('ascii')
        if len(data) > MAX_PACKET:
            raise ValueError('packet exceeds transport MTU')
        return data


class Receiver:
    """Only a verified commit replaces visible state. At most one staged sector."""

    def __init__(self):
        self.latest = None
        self.pending = None

    def receive(self, f, now):
        """Receive decoded packet fields f; return acknowledgement and committed baseline.

        Rejected packets return (None, None).
        """
        if len(f) < 5 or not HEX.fullmatch(f[3]):
            return None, None
        epoch, rev = f[3], sequence(f[4])
        if not rev:
            return None, None
        old = self.latest
        if old:
            if epoch != old.epoch or rev < old.revision:
                return None, None
            if rev == old.revision:
                # Lost final acknowledgement: retransmit completion, not effects.
                return len(old.rows) + 1, None
        pending = self.pending
        if pending and now - pending['at'] > STAGE_TIMEOUT:
            self.pending = pending = None
        if f[0] == 'UBEGIN' and len(f) == 8:
            sector = atom(f[5], True)
            count = sequence(f[6])
            if count > MAX_OBJECTS or not f[7].isascii() or not f[7].isdecimal():
                return None, None
            digest = int(f[7])
            if not 0 <= digest <= 0xFFFFFFFF:
                return None, None
            identity = (epoch, rev, sector, count, digest)
            if pending:
                if identity != pending['identity']:
                    return None, None  # A newer begin cannot evict in-flight work.
            else:
                self.pending = dict(identity=identity, rows={}, at=now)
            return 0, None
        if not pending or pending['identity'][:2] != (epoch, rev):
            return -1, None  # Sender must restart this transfer from its begin.
        _, _, sector, count, digest = pending['identity']
        if f[0] == 'UOBJECT' and len(f) == 7:
            index = sequence(f[5])
            record(f[6])
            if not 1 <= index <= count:
                return None, None
            previous = pending['rows'].get(index)
            if previous is not None and previous != f[6]:
                self.pending = None
                return -1, None
            if previous is None:
                pending['rows'][index] = f[6]
                pending['at'] = now
            return index, None
        if f[0] == 'UCOMMIT' and len(f) == 5:
            if len(pending['rows']) != count:
                return None, None
            rows = tuple(pending['rows'][i] for i in range(1, count + 1))
            try:
                baseline = Baseline.create(epoch, rev, sector, rows)
            except ValueError:
                self.pending = None
                return -1, None
            if baseline.digest != digest:
                self.pending = None
                return -1, None
            self.latest, self.pending = baseline, None
            return count + 1, baseline
        return None, None


class Sender:
    """Selective acknowledgement with a bounded sliding window and retries."""

    def __init__(self, baseline):
        self.baseline = baseline
        self.acked = set()
        self.sent_at = {}
        self.done = False

    def acknowledge(self, epoch, revision, index):
        baseline = self.baseline
        if epoch != baseline.epoch or revision != baseline.revision:
            return
        end = len(baseline.rows) + 1
        if index == -1:
            self.acked.clear()
            self.sent_at.clear()
        elif index == end and end in self.sent_at:
            self.done = True
        elif 0 <= index < end and index in self.sent_at:
            self.acked.add(index)

    def packets(self, lease, now):
        if self.done:
            return []
        baseline = self.baseline
        if 0 not in self.acked:
            indices = [0]
        else:
            outstanding = [
                index for index in range(1, len(baseline.rows) + 1) if index not in self.acked
            ]
            indices = outstanding[:WINDOW] or [len(baseline.rows) + 1]
        result = []
        for index in indices:
            if now - self.sent_at.get(index, -math.inf) >= RETRY:
                self.sent_at[index] = now
                result.append(baseline.packet(lease, index))
        return result


class UniverseSession(DockSession):
    """Opt-in INTERNAL W01. Host lease owns the epoch for the whole relay life."""

    def __init__(self, *args):
        super().__init__(*args)
        if not self.build.startswith('world1-'):
            raise ValueError('world synchronization requires a world1- build label')
        self.universe = Receiver()
        self.universe_epoch = None
        self.sectors = {}
        self.entity_ids = {}
        self.interest = None
        self.interest_after_revision = 0
        self.delivery = None
        self.delivery_lease = None
        self.delivered = None
        self.positions = {}
        self.position_cursor = 0
        # entity -> seq last forwarded to the current guest lease (fresh-first forwarding).
        self.forwarded = {}
        self.forwarded_lease = None
        # UFOCUS (lease, seq, target): the guest's selected NPC, forwarded once per sequence.
        self.focus = None
        self.focus_sent = None
        self.cargo = CargoTransactions(self)
        self.identities = Identities(self)
        self.npc_combat = NpcCombat(self)

    def busy(self, entity):
        return super().busy(entity) or self.cargo.busy(entity)

    def receive(self, raw, address, now):
        if raw.startswith((b'NHP|', b'NCLAIM|', b'NACK|')):
            return self.npc_combat.receive(raw, address, now)
        if raw.startswith(b'IDENT|'):
            return self.identities.receive(raw, address, now)
        if raw.startswith((b'CARGOREQ|', b'CARGODONE|', b'CINV|')):
            return self.cargo.receive(raw, address, now)
        if not raw.startswith(
            (
                b'UBEGIN|',
                b'UOBJECT|',
                b'UCOMMIT|',
                b'UACK|',
                b'UINTEREST|',
                b'UPOS|',
                b'UMOVE|',
                b'UFOCUS|',
            )
        ):
            return super().receive(raw, address, now)
        self.expire(now)
        if len(raw) > MAX_PACKET:
            return []
        try:
            fields = raw.decode('ascii').split('|')
            if len(fields) < 3 or fields[1] != '1':
                return []
            slot, peer = self._peer(fields[2], address)
            if not peer:
                return []
            if fields[0] == 'UINTEREST' and slot == 'p2' and len(fields) == 5:
                seq = sequence(fields[3])
                sector = atom(fields[4], True)
                old = self.interest
                if old and old[0] == peer.lease and seq <= old[1]:
                    return []
                self.interest = (peer.lease, seq, sector)
                if not old or old[0] != peer.lease or old[2] != sector:
                    self.delivery = None
                    self.delivered = None
                    # A sector change discards the guest's replicas: resend cached motion.
                    self.forwarded = {}
                    # The guest's receiver has a global monotonic revision. On
                    # sector return, an older cached baseline would be rejected
                    # forever and block delivery of the next fresh one. Require
                    # a capture newer than any baseline already sent to this
                    # guest; a genuinely new guest may use the cache immediately.
                    self.interest_after_revision = (
                        self.universe.latest.revision
                        if old and old[0] == peer.lease and self.universe.latest
                        else 0
                    )
                return []
            if fields[0] == 'UFOCUS' and slot == 'p2' and len(fields) == 5:
                seq = sequence(fields[3])
                target = sequence(fields[4])
                old = self.focus
                if old and old[0] == peer.lease and seq <= old[1]:
                    return []
                self.focus = (peer.lease, seq, target)
                return []
            if fields[0] == 'UACK' and slot == 'p2' and len(fields) == 6:
                rev = sequence(fields[4])
                index = -1 if fields[5] == '-1' else sequence(fields[5])
                if self.delivery and self.delivery_lease == peer.lease:
                    self.delivery.acknowledge(fields[3], rev, index)
                    if self.delivery.done:
                        self.delivered = self.delivery.baseline.revision
                return []
            if slot != 'p1' or len(fields) < 5 or fields[3] != peer.lease:
                return []
            if self.universe_epoch and self.universe_epoch != peer.lease:
                return []  # A fresh host game needs a new relay and fresh guest.
            if (fields[0] == 'UPOS' and len(fields) == 13) or (
                fields[0] == 'UMOVE' and len(fields) == 20
            ):
                entity, seq = sequence(fields[4]), sequence(fields[5])
                sector = atom(fields[6], True)
                if fields[0] == 'UPOS':
                    pose(fields[7:])
                else:
                    sample_time = float(fields[7])
                    if not math.isfinite(sample_time) or not 0 <= sample_time <= 1e12:
                        return []
                    pose(fields[8:14])
                    velocity = [float(value) for value in fields[14:20]]
                    if not all(
                        math.isfinite(value) and abs(value) <= (100000 if i < 3 else math.tau)
                        for i, value in enumerate(velocity)
                    ):
                        return []
                baseline = self.sectors.get(sector)
                if not baseline or entity not in self.entity_ids.get(sector, set()):
                    return []
                old = self.positions.get(entity)
                if old and seq <= old[0]:
                    return []
                if old and fields[0] == old[4] == 'UMOVE' and sample_time <= float(old[3][4]):
                    return []
                self.positions[entity] = (seq, now, sector, fields[3:], fields[0])
                return []
            ack, committed = self.universe.receive(fields, now)
            if ack is None:
                return []
            self.universe_epoch = peer.lease
            if committed:
                self.npc_combat.committed(committed)
                self.sectors.pop(committed.sector, None)
                self.sectors[committed.sector] = committed
                while len(self.sectors) > 8:
                    del self.sectors[next(iter(self.sectors))]
                self.entity_ids = {
                    entry_key: {record(row) for row in b.rows}
                    for entry_key, b in self.sectors.items()
                }
                live = set().union(*self.entity_ids.values())
                self.positions = {
                    entry_key: value
                    for entry_key, value in self.positions.items()
                    if entry_key in live
                }
                self.forwarded = {
                    entry_key: value
                    for entry_key, value in self.forwarded.items()
                    if entry_key in live
                }
            return [f'UACK|1|{peer.lease}|{fields[3]}|{fields[4]}|{ack}'.encode('ascii')]
        except (ValueError, OverflowError, UnicodeError):
            return []

    def snapshots(self, now):
        result = super().snapshots(now)
        result.extend(self.cargo.snapshots(now))
        result.extend(self.identities.snapshots(now))
        result.extend(self.npc_combat.snapshots(now))
        host, guest = self.peers.get('p1'), self.peers.get('p2')
        if not host or not guest or not self.interest or self.interest[0] != guest.lease:
            return result
        _, seq, sector = self.interest
        result.append((host.address, f'UINTEREST|1|{host.lease}|{seq}|{sector}'.encode()))
        focus = self.focus
        if focus and focus[0] == guest.lease and focus[:2] != self.focus_sent:
            self.focus_sent = focus[:2]
            result.append((host.address, f'UFOCUS|1|{host.lease}|{focus[1]}|{focus[2]}'.encode()))
        if self.delivery_lease != guest.lease:
            self.delivery = None
            self.delivered = None
            self.delivery_lease = guest.lease
        latest = self.sectors.get(sector)
        if (
            latest
            and latest.revision > self.interest_after_revision
            and (not self.delivery or self.delivery.done)
            and latest.revision != self.delivered
        ):
            self.delivery = Sender(latest)
        if self.delivery:
            result.extend(
                (guest.address, packet) for packet in self.delivery.packets(guest.lease, now)
            )
        # Independent, lossy motion cannot create entities or commit a baseline.
        # Samples not yet forwarded go first: recent ones (younger than MOTION_LIVE) oldest
        # arrival first, then older ones up to MOTION_KEEP (catch-up after a join or sector
        # change). A blind round robin over every live entity repeated unchanged samples while
        # new ones waited. Up to REPEAT_BUDGET recent samples are then repeated round robin to
        # cover datagram loss.
        if self.forwarded_lease != guest.lease:
            self.forwarded = {}
            self.forwarded_lease = guest.lease
        kept = [
            (entry_key, entry_value)
            for entry_key, entry_value in self.positions.items()
            if entry_value[2] == sector and now - entry_value[1] < MOTION_KEEP
        ]
        live = [kv for kv in kept if now - kv[1][1] < MOTION_LIVE]
        if kept:
            fresh = sorted(
                (kv for kv in kept if self.forwarded.get(kv[0]) != kv[1][0]),
                key=lambda kv: (now - kv[1][1] >= MOTION_LIVE, kv[1][1]),
            )
            chosen = fresh[:MOTION_BUDGET]
            if len(chosen) < MOTION_BUDGET:
                taken = {entry_key for entry_key, _ in chosen}
                rest = [kv for kv in live if kv[0] not in taken]
                if rest:
                    take = min(REPEAT_BUDGET, MOTION_BUDGET - len(chosen), len(rest))
                    chosen += [rest[(self.position_cursor + i) % len(rest)] for i in range(take)]
                    self.position_cursor = (self.position_cursor + take) % len(rest)
            for entity, entry in chosen:
                fields, kind = entry[3], entry[4]
                self.forwarded[entity] = entry[0]
                result.append(
                    (guest.address, (kind + '|1|' + guest.lease + '|' + '|'.join(fields)).encode())
                )
        return result
