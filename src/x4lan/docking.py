"""Co-op saved-dock observations and two-player reservation arbitration.

Native handles never cross the wire. A dock index belongs to the session's
shared pristine save. The recipient must still validate the actual native bay.
Unknown/stale observations do not release a previously claimed bay; an explicit
FREE or a new movement epoch is required. NPC occupancy remains a native check.
"""

from .coop import CoopSession, integer
from .host import sequence

MAX_DOCK = 250000
FRESH = 0.75
ATTACHED = frozenset(('APPROACH', 'EXTERNAL', 'STORED'))


class DockSession(CoopSession):
    def __init__(self, *args):
        super().__init__(*args)
        self.coop_docks = {}
        self.coop_dock_seen = {}

    def _current_dock(self, entity, row):
        return (
            row['owner'] == self.owners[entity]
            and row['epoch'] == self.epochs[entity]
            and not self.dead(entity)
        )

    def receive(self, raw, address, now):
        if not raw.startswith(b'CDOCK|'):
            return super().receive(raw, address, now)
        self.expire(now)
        if len(raw) > 768:
            return []
        try:
            fields = raw.decode('ascii').split('|')
            if len(fields) != 8 or fields[1] != '1':
                return []
            slot, peer = self._peer(fields[2], address)
            if peer is None:
                return []
            entity, epoch, seq = integer(fields[3], 2), sequence(fields[4]), sequence(fields[5])
            mode, dock = fields[6], integer(fields[7], MAX_DOCK)
            if (
                entity not in (1, 2)
                or self.owners[entity] != slot
                or self.epochs[entity] != epoch
                or self.dead(entity)
            ):
                return []
            if not ((mode in ATTACHED and dock > 0) or (mode in ('FREE', 'UNKNOWN') and dock == 0)):
                return []
            old = self.coop_docks.get(entity)
            seen = self.coop_dock_seen.get(entity)
            key = (slot, peer.lease, epoch)
            if seen and seen[0] == key and seq <= seen[1]:
                return []
            # A denied newer request must also fence older FREE/attachment
            # packets. Keep the last accepted claim until an explicit newer
            # state releases/replaces it; denial cannot refresh its freshness.
            self.coop_dock_seen[entity] = key, seq
            if dock:
                for other, row in self.coop_docks.items():
                    if other != entity and self._current_dock(other, row) and row['claim'] == dock:
                        return [
                            f'CDOCK_DENIED|1|{peer.lease}|{entity}|{epoch}|{seq}|{dock}'.encode()
                        ]
            claim = dock
            if mode == 'UNKNOWN' and old and self._current_dock(entity, old):
                claim = old['claim']
            self.coop_docks[entity] = dict(
                owner=slot,
                lease=peer.lease,
                epoch=epoch,
                seq=seq,
                mode=mode,
                dock=dock,
                claim=claim,
                at=now,
            )
        except (ValueError, UnicodeError):
            pass
        return []

    def snapshots(self, now):
        result = super().snapshots(now)
        for slot, peer in self.peers.items():
            for entity, row in self.coop_docks.items():
                owner = self.peers.get(row['owner'])
                if (
                    row['owner'] == slot
                    or not self._current_dock(entity, row)
                    or owner is None
                    or owner.lease != row['lease']
                    or now - row['at'] > FRESH
                ):
                    continue
                fields = (
                    'CDOCK',
                    '1',
                    peer.lease,
                    row['lease'],
                    str(entity),
                    str(row['epoch']),
                    str(row['seq']),
                    row['mode'],
                    str(row['dock']),
                )
                result.append((peer.address, '|'.join(fields).encode()))
        return result
