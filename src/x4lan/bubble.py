"""Bounded host-authoritative action routing for the experimental test bubble.

The relay never invents cargo or damage. Only p1's X4 may publish world state
and finish commands. Disconnecting the host cancels commands, never retries
an ambiguous transaction in a new world.
"""

from collections import OrderedDict
import hmac
import math
from .host import Session, HEX, ATOM, sequence

WORLD_FIELDS = 17
# WALK2 (walk_game.lua): an on-foot pose relative to a context every client resolves
# identically. Sender: WALK2|1|lease|seq|state|kind|ship|sector|code|macro|room|x|y|z|
# yaw|speed|stime|transfers. Forwarded with the sender's lease and slot to every other
# peer while fresh, once per new sequence and at most every WALK2_REPEAT seconds otherwise.
WALK2_FIELDS = 18
WALK2_KINDS = ('none', 'ship', 'station')
WALK2_FRESH = 1.0
WALK2_REPEAT = 0.2
VERBS = {'PICKUP', 'TRADE'}
RESULTS = {'OK', 'REJECTED', 'BUSY', 'CANCELLED'}


class BubbleSession(Session):
    def __init__(self, *args):
        super().__init__(*args)
        self.world = None
        self.world_owner = None
        self.world_revision = -1
        self.commands = OrderedDict()
        self.fire = {}
        self.bodies = {}
        self.suit_kills = {}
        self.walkers = {}
        self.walkers2 = {}
        self.walk2_sent = {}

    def ship_count(self):
        return 2

    def walk2(self, f):
        """Validate decoded packet fields f and return (sequence, text fields)."""
        seq, state, ship, transfers = (
            sequence(f[3]),
            sequence(f[4]),
            sequence(f[6]),
            sequence(f[17]),
        )
        kind = f[5]
        if state > 1 or kind not in WALK2_KINDS or ship > self.ship_count():
            raise ValueError('walk2')
        if not all(ATOM.fullmatch(field_value) for field_value in f[7:11]):
            raise ValueError('walk2 atom')
        if (state == 1) == (kind == 'none'):
            raise ValueError('walk2 state')
        if (kind == 'ship') != (ship > 0):
            raise ValueError('walk2 ship')
        if kind == 'station' and '-' in (f[7], f[8], f[9]):
            raise ValueError('walk2 station')
        x, y, z, yaw, speed, stime = (float(field_value) for field_value in f[11:17])
        if not all(math.isfinite(field_value) for field_value in (x, y, z, yaw, speed, stime)):
            raise ValueError('walk2 finite')
        if (
            max(abs(x), abs(y), abs(z)) > 100000
            or abs(yaw) > 360
            or not 0 <= speed <= 100
            or not 0 <= stime <= 1e9
        ):
            raise ValueError('walk2 range')
        return seq, f[3:]

    def expire(self, now):
        old_host = self.peers.get('p1')
        super().expire(now)
        if old_host and 'p1' not in self.peers:
            self.world = None
            self.world_owner = None
            self.world_revision = -1
            for cmd in self.commands.values():
                if cmd['result'] == 'PENDING':
                    cmd['result'] = 'CANCELLED'
        live = {remote_peer.lease for remote_peer in self.peers.values()}
        retained = live | {remote_peer.lease for remote_peer, _ in self.resumable.values()}
        self.bodies = {
            lease: entry_value for lease, entry_value in self.bodies.items() if lease in retained
        }
        self.suit_kills = {
            lease: entry_value
            for lease, entry_value in self.suit_kills.items()
            if lease in retained
        }
        self.fire = {
            lease: entry_value for lease, entry_value in self.fire.items() if lease in live
        }
        self.walkers = {
            lease: entry_value for lease, entry_value in self.walkers.items() if lease in live
        }
        self.walkers2 = {
            lease: entry_value for lease, entry_value in self.walkers2.items() if lease in live
        }
        self.walk2_sent = {
            entry_key: entry_value
            for entry_key, entry_value in self.walk2_sent.items()
            if entry_key[0] in live and entry_key[1] in live
        }
        for cmd in self.commands.values():
            if cmd['lease'] not in live and cmd['result'] == 'PENDING':
                cmd['result'] = 'CANCELLED'

    def _peer(self, lease, address):
        for slot, remote_peer in self.peers.items():
            if remote_peer.address == address and hmac.compare_digest(remote_peer.lease, lease):
                return slot, remote_peer
        return None, None

    def receive(self, raw, address, now):
        self.expire(now)
        if len(raw) > 768:
            return []
        try:
            packet_fields = raw.decode('ascii').split('|')
        except UnicodeError:
            return []
        if len(packet_fields) < 2 or packet_fields[1] != '1':
            return []
        try:
            if packet_fields[0] == 'ACTION' and len(packet_fields) == 7:
                _, _, token, slot, request, verb, target = packet_fields
                if not hmac.compare_digest(token, self.token) or not HEX.fullmatch(request):
                    return []
                if slot not in ('p1', 'p2') or verb not in VERBS or target != 'energycells':
                    return []
                peer = self.peers.get(slot)
                old = self.commands.get(request)
                if old:
                    if (old['slot'], old['verb'], old['ip']) != (slot, verb, address[0]):
                        return []
                    return [f"RESULT|1|{request}|{old['result']}".encode()]
                if peer is None or peer.address[0] != address[0]:
                    return [f'RESULT|1|{request}|NO_PLAYER'.encode()]
                if not self.world or 'p1' not in self.peers:
                    return [f'RESULT|1|{request}|NO_WORLD'.encode()]
                if sum(command['result'] == 'PENDING' for command in self.commands.values()) >= 8:
                    return [f'RESULT|1|{request}|BUSY'.encode()]
                while len(self.commands) >= 512:
                    key, command = next(iter(self.commands.items()))
                    if command['result'] == 'PENDING':
                        return [f'RESULT|1|{request}|BUSY'.encode()]
                    # Never forget an ID in this host world: reject further work at bound.
                    return [f'RESULT|1|{request}|SESSION_FULL'.encode()]
                self.commands[request] = {
                    'slot': slot,
                    'lease': peer.lease,
                    'ip': address[0],
                    'verb': verb,
                    'result': 'PENDING',
                    'created': now,
                }
                return [f'RESULT|1|{request}|PENDING'.encode()]
            if packet_fields[0] not in (
                'WORLD',
                'FIRE',
                'DONE',
                'BODY',
                'SUITKILL',
                'WALK',
                'WALK2',
            ):
                return super().receive(raw, address, now)
            slot, peer = self._peer(packet_fields[2], address)
            if peer is None:
                return []
            if packet_fields[0] == 'WALK2' and len(packet_fields) == WALK2_FIELDS:
                seq, fields = self.walk2(packet_fields)
                old = self.walkers2.get(peer.lease)
                if old and seq <= old['seq']:
                    return []
                self.walkers2[peer.lease] = dict(seq=seq, at=now, fields=fields)
            if packet_fields[0] == 'WALK' and len(packet_fields) == 11:
                seq = sequence(packet_fields[3])
                context = sequence(packet_fields[4])
                pose = [float(entry_value) for entry_value in packet_fields[5:]]
                if context not in (0, 1, 2, 3, 4):
                    return []
                if not all(
                    math.isfinite(entry_value) and abs(entry_value) <= (10000 if i < 3 else 360)
                    for i, entry_value in enumerate(pose)
                ):
                    return []
                old = self.walkers.get(peer.lease)
                if old and seq <= old[0]:
                    return []
                self.walkers[peer.lease] = (seq, now, context, *packet_fields[5:])
            elif packet_fields[0] == 'BODY' and len(packet_fields) == 13:
                seq = sequence(packet_fields[3])
                mode = packet_fields[4]
                dead = sequence(packet_fields[5])
                generation = sequence(packet_fields[6])
                pose = [float(entry_value) for entry_value in packet_fields[7:]]
                if mode not in ('ship', 'foot', 'suit') or dead not in (0, 1):
                    return []
                if mode == 'suit' and generation == 0:
                    return []
                if not all(
                    math.isfinite(entry_value)
                    and abs(entry_value) <= (50000 if i < 3 else math.tau)
                    for i, entry_value in enumerate(pose)
                ):
                    return []
                old = self.bodies.get(peer.lease)
                if old and (seq <= old[0] or dead < int(old[2]) or generation < int(old[3])):
                    return []
                self.bodies[peer.lease] = (
                    seq,
                    mode,
                    str(dead),
                    str(generation),
                    *packet_fields[7:],
                )
            elif packet_fields[0] == 'SUITKILL' and slot == 'p1' and len(packet_fields) == 4:
                generation = sequence(packet_fields[3])
                guest = self.peers.get('p2')
                body = self.bodies.get(guest.lease) if guest else None
                if body and body[1] == 'suit' and int(body[3]) == generation:
                    self.suit_kills[guest.lease] = (peer.lease, generation)
            elif (
                packet_fields[0] == 'WORLD'
                and slot == 'p1'
                and len(packet_fields) == 4 + WORLD_FIELDS
            ):
                rev = sequence(packet_fields[3])
                values = [float(x) for x in packet_fields[4:]]
                if not all(
                    math.isfinite(entry_value) and 0 <= entry_value <= 1000000
                    for entry_value in values
                ):
                    return []
                for i in (0, 1, 3, 4, 6, 7, 8, 9):
                    if values[i] > 100:
                        return []
                for i in (11, 12, 13, 14):
                    if values[i] not in (0, 1):
                        return []
                for i in (2, 5, 10, 15, 16):
                    if values[i] != int(values[i]):
                        return []
                if self.world_owner != peer.lease:
                    self.world_revision = -1
                    self.world_owner = peer.lease
                if rev <= self.world_revision:
                    return []
                self.world_revision = rev
                self.world = tuple(values)
            elif packet_fields[0] == 'FIRE' and len(packet_fields) == 4:
                seq = sequence(packet_fields[3])
                old = self.fire.get(peer.lease, (-1, 0))
                if seq > old[0]:
                    self.fire[peer.lease] = (seq, now)
            elif (
                packet_fields[0] == 'DONE'
                and slot == 'p1'
                and len(packet_fields) == 5
                and packet_fields[4] in RESULTS
            ):
                cmd = self.commands.get(packet_fields[3])
                if cmd and cmd['result'] == 'PENDING':
                    cmd['result'] = packet_fields[4]
        except (ValueError, OverflowError, IndexError):
            return []
        return []

    def walk2_packets(self, peer, other_slot, other, now):
        walker = self.walkers2.get(other.lease)
        if not walker or now - walker['at'] >= WALK2_FRESH:
            return []
        key = (peer.lease, other.lease)
        last = self.walk2_sent.get(key)
        if last and last[0] == walker['seq'] and now - last[1] < WALK2_REPEAT:
            return []
        self.walk2_sent[key] = (walker['seq'], now)
        payload = '|'.join(walker['fields'])
        return [
            (peer.address, f'WALK2|1|{peer.lease}|{other.lease}|{other_slot}|{payload}'.encode())
        ]

    def snapshots(self, now):
        result = super().snapshots(now)
        host = self.peers.get('p1')
        for slot, peer in self.peers.items():
            if self.world and host:
                payload = '|'.join(format(entry_value, '.9g') for entry_value in self.world)
                result.append(
                    (
                        peer.address,
                        f'WORLD|1|{peer.lease}|{host.lease}|{self.world_revision}|{payload}'.encode(),
                    )
                )
            other_slot = 'p2' if slot == 'p1' else 'p1'
            other = self.peers.get(other_slot)
            if other:
                walker = self.walkers.get(other.lease)
                if walker and now - walker[1] < 1:
                    payload = '|'.join(map(str, (walker[0], *walker[2:])))
                    result.append(
                        (peer.address, f'WALK|1|{peer.lease}|{other.lease}|{payload}'.encode())
                    )
                result.extend(self.walk2_packets(peer, other_slot, other, now))
                body = self.bodies.get(other.lease)
                if body:
                    payload = '|'.join(map(str, body))
                    result.append(
                        (peer.address, f'BODY|1|{peer.lease}|{other.lease}|{payload}'.encode())
                    )
                fire = self.fire.get(other.lease)
                if fire and now - fire[1] < 0.5:
                    result.append(
                        (peer.address, f'FIRE|1|{peer.lease}|{other.lease}|{fire[0]}'.encode())
                    )
            kill = self.suit_kills.get(peer.lease)
            body = self.bodies.get(peer.lease)
            if (
                kill
                and host
                and kill[0] == host.lease
                and body
                and body[1] == 'suit'
                and int(body[3]) == kill[1]
            ):
                result.append(
                    (peer.address, f'SUITKILL|1|{peer.lease}|{host.lease}|{kill[1]}'.encode())
                )
        if host:
            # One transaction at a time. Receiver deduplicates before dispatch to MD.
            for request, cmd in self.commands.items():
                if cmd['result'] == 'PENDING':
                    result.append(
                        (
                            host.address,
                            f"COMMAND|1|{host.lease}|{request}|{cmd['slot']}|{cmd['verb']}".encode(),
                        )
                    )
                    break
        return result
