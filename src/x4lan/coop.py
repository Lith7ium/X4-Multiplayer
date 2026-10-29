"""Stable cooperative ships, independent of player slots and sector boundaries.

The relay arbitrates pilot ownership, not game physics. Host X4 remains the
sole authority for damage, cargo, treasury and mission outcomes.
"""

import math
import re
from .economy import EconomySession, integer, parse_lines
from .host import ATOM, HEX, sequence
from .projectile_relay import ProjectileRelay
from .quests import QuestState
from .missions import MissionJournal

POSITION_LIMIT = 50_000_000
HEALTH_STALE = 0.75  # seconds without a pilot frame before a live health generation ends
SHOT_DIGEST = re.compile(r'[a-z0-9_]{1,48}:\d{1,9}\.\d{3}')  # weapon macro : per-shot strength
# NMODE (coop_game.lua): optional companion of NAV2, same seq/source time. Unknown to older
# relays and clients, which drop it; receivers then infer the mode from sampled speed.
MOTION_MODES = ('normal', 'boost', 'travel', 'highway')


def pose(values):
    if len(values) != 6:
        raise ValueError('pose length')
    numbers = [float(number) for number in values]
    if not all(
        math.isfinite(number) and abs(number) <= (POSITION_LIMIT if i < 3 else math.tau)
        for i, number in enumerate(numbers)
    ):
        raise ValueError('pose bounds')
    return numbers


class CoopSession(EconomySession):
    def __init__(self, *args):
        super().__init__(*args)
        self.owners = {1: 'p1', 2: 'p2'}
        self.epochs = {1: 1, 2: 1}
        self.crew = {}
        self.navigation = {}
        self.motion_modes = {}
        # FIRE2: latest weapon-group/shot-counter frame per ship (see coop_game.lua).
        self.fire2 = {}
        # HEALTH (health_game.lua): one generation per ship covers one (owner, epoch, pilot). Its
        # pilot must acknowledge the host's canonical WORLD baseline (HBASE/HACK) before frames count.
        self.health = {}
        self.health_gen, self.health_key, self.health_live = (
            {1: 0, 2: 0},
            {1: None, 2: None},
            {1: 0, 2: 0},
        )
        self.health_seen = {1: 0.0, 2: 0.0}  # last HACK or accepted HEALTH of the live generation
        # SHOT (shot_game.lua): cumulative hit counts per (shooter, shooter gen, victim, victim gen).
        self.shots = {}
        # BOLT/BEND (projectile_relay.py): visual-only observed projectile births/ends.
        self.projectiles = ProjectileRelay()
        self.body_sectors = {}
        self.game_metrics = {}
        self.authority_revision = 1
        self.order_ships = {}
        self.quest = QuestState()
        self.missions = MissionJournal(self)

    def expire(self, now):
        super().expire(now)
        live = {remote_peer.lease for remote_peer in self.peers.values()}
        expired = [key for key in self.crew if key not in live]
        if expired:
            self.authority_revision += 1
        self.crew = {
            entry_key: entry_value
            for entry_key, entry_value in self.crew.items()
            if entry_key in live
        }
        retained = live | {remote_peer.lease for remote_peer, _ in self.resumable.values()}
        self.body_sectors = {
            entry_key: entry_value
            for entry_key, entry_value in self.body_sectors.items()
            if entry_key in retained
        }
        self.game_metrics = {
            entry_key: entry_value
            for entry_key, entry_value in self.game_metrics.items()
            if entry_key in live
        }
        self.health = {
            entry_key: entry_value
            for entry_key, entry_value in self.health.items()
            if entry_value['lease'] in live
        }
        # Every message and snapshot starts here, so each authority transition gets its own
        # generation even when no snapshot falls between a leave and a re-entry. A live generation
        # whose pilot stream stopped is an authority boundary too: the host takes the ship back and
        # the pilot may only continue from a new generation with a fresh canonical baseline.
        for entity in (1, 2):
            gen = self.health_generation(entity)
            if self.health_live[entity] == gen and now - self.health_seen[entity] > HEALTH_STALE:
                self.end_health(entity)

    def pilot(self, entity):
        for slot, peer in self.peers.items():
            if self.crew.get(peer.lease, (-1, 0, 0))[1] == entity:
                return slot
        return '0'

    def busy(self, entity):
        return any(
            request['result'] == 'PENDING' and self.order_ships.get(request_key, (0,))[0] == entity
            for request_key, request in self.orders.items()
        ) or any(
            request['result'] == 'PENDING' and request.get('ship') == entity
            for request in self.commands.values()
        )

    def dead(self, entity):
        return bool(self.world and self.world[(entity - 1) * 3] <= 1.01)

    def end_health(self, entity):
        self.health_gen[entity] += 1
        self.health_live[entity] = 0
        self.health.pop(entity, None)

    def victim_generation(self, victim):
        """Co-op ships carry their health generation; LAN targets (3, 4) are always host objects."""
        return self.health_generation(victim) if victim in (1, 2) else 0

    def victim_authority(self, victim):
        """The slot whose world decides the victim: a co-op ship's remote pilot while its health
        generation is live, otherwise the host."""
        if victim in (1, 2):
            slot = self.owners[victim]
            gen = self.health_generation(victim)
            if slot != 'p1' and self.pilot(victim) == slot and self.health_live[victim] == gen:
                return slot
        return 'p1'

    def health_generation(self, entity):
        key = (self.owners[entity], self.epochs[entity], self.pilot(entity))
        if key != self.health_key[entity]:
            self.health_key[entity] = key
            self.health_gen[entity] += 1
            self.health.pop(entity, None)
        return self.health_gen[entity]

    def receive(self, raw, address, now):
        if raw.startswith((b'MSTATE|', b'MREQ|')):
            return self.missions.receive(raw, address, now)
        self.expire(now)
        if len(raw) > 768:
            return []
        try:
            fields = raw.decode('ascii').split('|')
        except UnicodeError:
            return []
        if len(fields) < 3 or fields[1] != '1':
            return []
        kind = fields[0]
        # Delegate other message families while fencing obsolete authority updates.
        if kind not in (
            'CREW',
            'NAV',
            'NAV2',
            'NMODE',
            'FIRE2',
            'HEALTH',
            'HACK',
            'HSTALE',
            'SHOT',
            'BODY2',
            'PERF',
            'COOPORDER',
            'QUEST',
            'BOLT',
            'BEND',
        ):
            # Legacy movement/fire/dock frames must never override cooperative authority.
            if kind in ('STATE', 'FIRE', 'DOCKSTATE', 'BODY', 'ORDER'):
                return []
            old_leases = {
                player_slot: remote_peer.lease for player_slot, remote_peer in self.peers.items()
            }
            result = super().receive(raw, address, now)
            if kind == 'HELLO':
                for slot, peer in self.peers.items():
                    if old_leases.get(slot) != peer.lease:
                        for entity in (1, 2):
                            nav = self.navigation.get(entity)
                            if self.owners[entity] == slot and nav and nav['lease'] != peer.lease:
                                self.epochs[entity] += 1
                                self.navigation.pop(entity, None)
                                self.motion_modes.pop(entity, None)
                                self.authority_revision += 1
            if kind == 'ACTION' and len(fields) == 7:
                cmd = self.commands.get(fields[4])
                if cmd and 'ship' not in cmd:
                    peer = self.peers.get(cmd['slot'])
                    entity = self.crew.get(peer.lease, (-1, 0, 0))[1] if peer else 0
                    cmd['ship'] = entity
                    if (
                        entity not in (1, 2)
                        or self.owners[entity] != cmd['slot']
                        or self.dead(entity)
                    ):
                        cmd['result'] = 'REJECTED'
                        return [f'RESULT|1|{fields[4]}|REJECTED'.encode()]
            return result
        # Every cooperative message below requires the sending peer's current lease.
        slot, peer = self._peer(fields[2], address)
        if peer is None:
            return []
        try:
            if kind == 'QUEST' and slot == 'p1' and len(fields) == 8:
                self.quest.receive(peer.lease, fields[3:])
            elif kind == 'CREW' and len(fields) == 6:
                seq, controlled, aboard = (
                    sequence(fields[3]),
                    integer(fields[4], 2),
                    integer(fields[5], 2),
                )
                old = self.crew.get(peer.lease, (-1, 0, 0))
                if seq <= old[0] or (controlled and controlled != aboard):
                    return []
                granted = controlled
                if controlled:
                    occupied = self.pilot(controlled)
                    if (
                        self.dead(controlled)
                        or occupied not in ('0', slot)
                        or (self.owners[controlled] != slot and self.busy(controlled))
                    ):
                        granted = 0
                    elif self.owners[controlled] != slot:
                        self.owners[controlled] = slot
                        self.epochs[controlled] += 1
                        self.navigation.pop(controlled, None)
                        self.motion_modes.pop(controlled, None)
                        self.authority_revision += 1
                self.crew[peer.lease] = (seq, granted, aboard)
                if old[1:] != (granted, aboard):
                    self.authority_revision += 1
                if controlled and not granted:
                    return [f'PILOT_DENIED|1|{peer.lease}|{seq}|{controlled}'.encode()]
            elif (kind == 'NAV' and len(fields) == 15) or (kind == 'NAV2' and len(fields) == 22):
                entity, epoch, seq = integer(fields[3], 2), sequence(fields[4]), sequence(fields[5])
                if (
                    entity not in (1, 2)
                    or self.owners[entity] != slot
                    or epoch != self.epochs[entity]
                ):
                    return []
                if not ATOM.fullmatch(fields[6]) or not fields[6].endswith('_macro'):
                    return []
                pose(fields[7:13])
                integer(fields[13], 2)
                sequence(fields[14])
                old = self.navigation.get(entity)
                if old and old['lease'] == peer.lease and seq <= old['seq']:
                    return []
                if self.dead(entity):
                    return []
                if kind == 'NAV2':
                    source_time = float(fields[15])
                    velocity = [float(number) for number in fields[16:22]]
                    if not math.isfinite(source_time) or not 0 <= source_time <= 1e12:
                        return []
                    if not all(
                        math.isfinite(number) and abs(number) <= (100000 if i < 3 else math.tau)
                        for i, number in enumerate(velocity)
                    ):
                        return []
                    if (
                        old
                        and old.get('kind') == 'NAV2'
                        and source_time <= float(old['fields'][12])
                    ):
                        return []
                self.navigation[entity] = dict(
                    lease=peer.lease, seq=seq, at=now, fields=fields[3:], kind=kind
                )
            elif kind == 'NMODE' and len(fields) == 8:
                # entity|epoch|navSeq|sourceTime|mode, from the ship's current movement authority.
                entity, epoch, seq = integer(fields[3], 2), sequence(fields[4]), sequence(fields[5])
                if (
                    entity not in (1, 2)
                    or self.owners[entity] != slot
                    or epoch != self.epochs[entity]
                ):
                    return []
                source_time = float(fields[6])
                if (
                    not math.isfinite(source_time)
                    or not 0 <= source_time <= 1e12
                    or fields[7] not in MOTION_MODES
                ):
                    return []
                old = self.motion_modes.get(entity)
                if (
                    old
                    and old['lease'] == peer.lease
                    and old['epoch'] == epoch
                    and seq <= old['seq']
                ):
                    return []
                if self.dead(entity):
                    return []
                self.motion_modes[entity] = dict(
                    lease=peer.lease, epoch=epoch, seq=seq, at=now, fields=fields[3:]
                )
            elif kind == 'FIRE2' and len(fields) == 11:
                # entity|epoch|seq|sourceTime|primaryGroup|secondaryGroup|primaryShots|secondaryShots
                # Groups use the C API numbering 1-4. Shot counters are monotonic per epoch.
                # The relay only forwards fire evidence; host X4 remains the damage authority.
                entity, epoch, seq = integer(fields[3], 2), sequence(fields[4]), sequence(fields[5])
                if (
                    entity not in (1, 2)
                    or self.owners[entity] != slot
                    or epoch != self.epochs[entity]
                ):
                    return []
                source_time = float(fields[6])
                if not math.isfinite(source_time) or not 0 <= source_time <= 1e12:
                    return []
                groups = [integer(number, 4) for number in fields[7:9]]
                if not all(groups):
                    return []
                shots = [sequence(number) for number in fields[9:11]]
                old = self.fire2.get(entity)
                if old and old['lease'] == peer.lease and old['epoch'] == epoch:
                    if seq <= old['seq'] or source_time <= old['time']:
                        return []
                    if shots[0] < old['shots'][0] or shots[1] < old['shots'][1]:
                        return []
                if self.dead(entity):
                    return []
                self.fire2[entity] = dict(
                    lease=peer.lease,
                    epoch=epoch,
                    seq=seq,
                    time=source_time,
                    shots=shots,
                    at=now,
                    fields=fields[3:],
                )
            elif kind == 'HEALTH' and len(fields) == 8:
                # entity|gen|seq|hull%|shield%: the pilot's own reading of the ship it owns and
                # pilots, after it acknowledged this generation's baseline. The life tick stays host-side.
                entity, gen, seq = integer(fields[3], 2), sequence(fields[4]), sequence(fields[5])
                if (
                    entity not in (1, 2)
                    or self.owners[entity] != slot
                    or self.pilot(entity) != slot
                ):
                    return []
                if (
                    gen != self.health_generation(entity)
                    or gen != self.health_live[entity]
                    or self.dead(entity)
                ):
                    return []
                values = [float(number) for number in fields[6:8]]
                if not all(math.isfinite(number) and 0 <= number <= 100 for number in values):
                    return []
                old = self.health.get(entity)
                if old and seq <= old['seq']:
                    return []
                self.health[entity] = dict(lease=peer.lease, seq=seq, at=now, fields=fields[3:])
                self.health_seen[entity] = now
            elif kind == 'SHOT' and len(fields) == 10:
                # shooter|shooterGen|victim|victimGen|seq|hits|digest: hits the sender's OWN piloted
                # ship scored in its world, as a cumulative count. The victim's authority applies the
                # increase natively; the relay only checks authority and order.
                shooter, sgen, victim, vgen = (
                    integer(fields[3], 2),
                    sequence(fields[4]),
                    integer(fields[5], 4),
                    integer(fields[6], 2147483647),
                )
                seq, hits = sequence(fields[7]), integer(fields[8], 1000000)
                if (
                    shooter not in (1, 2)
                    or self.owners[shooter] != slot
                    or self.pilot(shooter) != slot
                    or sgen != self.health_generation(shooter)
                    or not victim
                    or victim == shooter
                    or vgen != self.victim_generation(victim)
                    or self.victim_authority(victim) == slot
                    or (slot != 'p1' and self.health_live[shooter] != sgen)
                    or not SHOT_DIGEST.fullmatch(fields[9])
                ):
                    return []
                key = (shooter, sgen, victim, vgen)
                old = self.shots.get(key)
                if old and (
                    old['lease'] != peer.lease
                    or seq <= old['seq']
                    or hits < old['hits']
                    or fields[9] != old['fields'][-1]
                ):
                    return []
                self.shots[key] = dict(
                    lease=peer.lease, seq=seq, hits=hits, at=now, fields=fields[3:]
                )
            elif kind in ('BOLT', 'BEND'):
                self.projectiles.receive(self, slot, peer, raw, now)
            elif kind == 'HACK' and len(fields) == 5:
                entity, gen = integer(fields[3], 2), sequence(fields[4])
                if (
                    entity in (1, 2)
                    and self.owners[entity] == slot
                    and self.pilot(entity) == slot
                    and gen == self.health_generation(entity)
                    and self.health_live[entity] != gen
                ):
                    self.health_live[entity], self.health_seen[entity] = gen, now
            elif kind == 'HSTALE' and len(fields) == 5:
                # The host's own freshness release ends the generation it names, if still current.
                entity, gen = integer(fields[3], 2), sequence(fields[4])
                if slot == 'p1' and entity in (1, 2) and gen == self.health_generation(entity):
                    self.end_health(entity)
            elif kind == 'BODY2' and len(fields) == 14:
                seq, mode, dead, gen = (
                    sequence(fields[3]),
                    fields[4],
                    integer(fields[5], 1),
                    sequence(fields[6]),
                )
                if mode not in ('ship', 'foot', 'suit') or (mode == 'suit' and not gen):
                    return []
                if not ATOM.fullmatch(fields[7]) or not fields[7].endswith('_macro'):
                    return []
                pose(fields[8:])
                old = self.bodies.get(peer.lease)
                if old and (seq <= old[0] or dead < int(old[2]) or gen < int(old[3])):
                    return []
                self.bodies[peer.lease] = (seq, mode, str(dead), str(gen), *fields[8:])
                self.body_sectors[peer.lease] = fields[7]
            elif kind == 'PERF' and len(fields) == 9:
                seq = sequence(fields[3])
                values = [float(number) for number in fields[4:7]]
                if not all(math.isfinite(number) and 0 <= number <= 60000 for number in values):
                    return []
                if fields[7] not in ('ship', 'foot', 'suit', 'paused', 'waiting'):
                    return []
                if not ATOM.fullmatch(fields[8]):
                    return []
                old = self.game_metrics.get(peer.lease)
                if old and seq <= old['seq']:
                    return []
                self.game_metrics[peer.lease] = dict(
                    seq=seq,
                    at=now,
                    callbacks_hz=values[0],
                    interval_avg_ms=values[1],
                    interval_max_ms=values[2],
                    state=fields[7],
                    sector=fields[8],
                )
            elif kind == 'COOPORDER' and len(fields) == 9:
                request, host, entity, epoch, market, lines = fields[3:]
                entity, epoch = integer(entity, 2), sequence(epoch)
                if entity not in (1, 2) or not HEX.fullmatch(request):
                    return []
                parse_lines(lines)
                old = self.orders.get(request)
                if old:
                    if self.order_ships.get(request) != (entity, epoch):
                        return []
                elif (
                    self.owners[entity] != slot
                    or epoch != self.epochs[entity]
                    or self.pilot(entity) != slot
                    or self.dead(entity)
                ):
                    return [self.reply(peer, request, 'REJECTED')]
                reply = super().receive(
                    f'ORDER|1|{peer.lease}|{request}|{host}|{market}|{lines}'.encode(), address, now
                )
                if request in self.orders:
                    self.order_ships[request] = (entity, epoch)
                return reply
        except (ValueError, OverflowError, IndexError):
            return []
        return []

    def snapshots(self, now):
        result = []
        for address, packet in super().snapshots(now):
            packet_fields = packet.decode('ascii').split('|')
            if packet_fields[0] in ('PEER', 'BODY', 'FIRE', 'DOCKSTATE'):
                continue
            if packet_fields[0] == 'ORDER_EXEC':
                ship = self.order_ships.get(packet_fields[3], (0,))[0]
                if not ship:
                    continue
                packet_fields[0], packet_fields[4] = 'COOP_EXEC', str(ship)
                packet = '|'.join(packet_fields).encode()
            if packet_fields[0] == 'COMMAND':
                ship = self.commands.get(packet_fields[3], {}).get('ship', 0)
                if not ship:
                    continue
                packet_fields[4] = 'p' + str(ship)
                packet = '|'.join(packet_fields).encode()
            result.append((address, packet))
        for slot, peer in self.peers.items():
            authority = [str(self.authority_revision)]
            for entity in (1, 2):
                authority += [self.owners[entity], str(self.epochs[entity]), self.pilot(entity)]
            result.append(
                (peer.address, ('AUTH|1|' + peer.lease + '|' + '|'.join(authority)).encode())
            )
            for entity, mode in self.motion_modes.items():
                # Forwarded before NAV2 so a receiver knows the mode of the sample it renders.
                if (
                    self.owners[entity] == slot
                    or mode['epoch'] != self.epochs[entity]
                    or now - mode['at'] > 0.75
                    or self.dead(entity)
                ):
                    continue
                result.append(
                    (
                        peer.address,
                        (
                            'NMODE|1|'
                            + peer.lease
                            + '|'
                            + mode['lease']
                            + '|'
                            + '|'.join(mode['fields'])
                        ).encode(),
                    )
                )
            for entity, nav in self.navigation.items():
                if self.owners[entity] == slot or now - nav['at'] > 0.75 or self.dead(entity):
                    continue
                result.append(
                    (
                        peer.address,
                        (
                            nav.get('kind', 'NAV')
                            + '|1|'
                            + peer.lease
                            + '|'
                            + nav['lease']
                            + '|'
                            + '|'.join(nav['fields'])
                        ).encode(),
                    )
                )
            for entity, fire in self.fire2.items():
                # A frame from an old owner/epoch is never forwarded; stale fire must stop.
                if (
                    self.owners[entity] == slot
                    or fire['epoch'] != self.epochs[entity]
                    or now - fire['at'] > 0.75
                    or self.dead(entity)
                ):
                    continue
                result.append(
                    (
                        peer.address,
                        (
                            'FIRE2|1|'
                            + peer.lease
                            + '|'
                            + fire['lease']
                            + '|'
                            + '|'.join(fire['fields'])
                        ).encode(),
                    )
                )
            # HGEN names the generations of THIS authority revision (every change bumps both).
            result.append(
                (
                    peer.address,
                    f'HGEN|1|{peer.lease}|{self.authority_revision}|{self.health_generation(1)}|{self.health_generation(2)}'.encode(),
                )
            )
            for entity in (1, 2):
                gen, hp = self.health_generation(entity), self.health.get(entity)
                if (
                    hp
                    and self.owners[entity] != slot
                    and now - hp['at'] <= 0.75
                    and not self.dead(entity)
                ):
                    result.append(
                        (
                            peer.address,
                            (
                                'HEALTH|1|'
                                + peer.lease
                                + '|'
                                + hp['lease']
                                + '|'
                                + '|'.join(hp['fields'])
                            ).encode(),
                        )
                    )
                if (
                    slot != 'p1'
                    and self.owners[entity] == slot
                    and self.pilot(entity) == slot
                    and self.health_live[entity] != gen
                    and self.world
                    and not self.dead(entity)
                ):
                    hull, shield = self.world[(entity - 1) * 3 : (entity - 1) * 3 + 2]
                    result.append(
                        (
                            peer.address,
                            f'HBASE|1|{peer.lease}|{entity}|{gen}|{hull:.3f}|{shield:.3f}'.encode(),
                        )
                    )
            for key, shot in list(self.shots.items()):
                shooter, sgen, victim, vgen = key
                if sgen != self.health_generation(shooter) or vgen != self.victim_generation(
                    victim
                ):
                    del self.shots[key]
                    continue  # an ended generation never forwards
                # Never back to the shooter's own slot: where it is the authority its hits are native.
                if (
                    self.victim_authority(victim) == slot
                    and self.owners[shooter] != slot
                    and now - shot['at'] <= 0.75
                    and not self.dead(shooter)
                ):
                    result.append(
                        (
                            peer.address,
                            (
                                'SHOT|1|'
                                + peer.lease
                                + '|'
                                + shot['lease']
                                + '|'
                                + '|'.join(shot['fields'])
                            ).encode(),
                        )
                    )
            other = self.peers.get('p2' if slot == 'p1' else 'p1')
            if other and other.lease in self.bodies and other.lease in self.body_sectors:
                body = self.bodies[other.lease]
                fields = [
                    other.lease,
                    *map(str, body[:4]),
                    self.body_sectors[other.lease],
                    *body[4:],
                ]
                result.append(
                    (peer.address, ('BODY2|1|' + peer.lease + '|' + '|'.join(fields)).encode())
                )
        result.extend(self.projectiles.packets(self, now))
        result.extend(self.quest.snapshots(self.peers, now))
        result.extend(self.missions.snapshots(now))
        return result
