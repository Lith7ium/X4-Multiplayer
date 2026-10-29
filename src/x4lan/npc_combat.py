"""Host-owned NPC health and cumulative, acknowledged guest hit claims.

No HP deltas are inferred from replica motion or disappearance. Native X4 owns
damage, regeneration and death. The relay only fences identity and retries.
"""

import math

from .coop import SHOT_DIGEST
from .host import ATOM, MAX_PACKET, sequence
from .identity import CODE

FRESH = 5.0
LIMIT = 32768  # Refuse further admission; never evict a deduplication ledger.


class NpcCombat:
    def __init__(self, session):
        self.session = session
        self.descriptors = {}
        self.states = {}
        self.claims = {}
        self.next_send = 0
        # entity -> NHP seq last forwarded to the current guest lease. Only a new host
        # sequence is forwarded: the host repeats every NPC with a new sequence at least
        # every KEEPALIVE seconds (npc_combat_game.lua), which recovers a lost datagram
        # and feeds a late joiner. Repeating an unchanged sequence cannot refresh a guest.
        self.forwarded = {}
        self.forwarded_lease = None

    def committed(self, baseline):
        # Absence suspends admission, it is not a destruction event.
        for value in self.descriptors.values():
            if value['sector'] == baseline.sector:
                value['active'] = False
        for row in baseline.rows:
            fields = row.split(',')
            if fields[1] != 'ship':
                continue
            entity = int(fields[0])
            if entity not in self.descriptors and len(self.descriptors) >= LIMIT:
                continue
            self.descriptors[entity] = dict(
                sector=baseline.sector, macro=fields[2], owner=fields[3], active=True
            )

    def current(self, entity, gen, now, live=True):
        state, descriptor = self.states.get(entity), self.descriptors.get(entity)
        return bool(
            state
            and descriptor
            and state['gen'] == gen
            and now - state['at'] < FRESH
            and state['descriptor']
            == (descriptor['sector'], descriptor['macro'], descriptor['owner'])
            and (not live or descriptor['active'] and state['alive'])
        )

    def pilot(self, entity, gen):
        session = self.session
        return (
            entity in (1, 2)
            and session.owners[entity] == 'p2'
            and session.pilot(entity) == 'p2'
            and gen == session.health_generation(entity) == session.health_live[entity]
            and not session.dead(entity)
        )

    def receive(self, raw, address, now):
        session = self.session
        session.expire(now)
        if len(raw) > MAX_PACKET:
            return []
        try:
            fields = raw.decode('ascii').split('|')
            if len(fields) < 4 or fields[1] != '1':
                return []
            slot, peer = session._peer(fields[2], address)
            host = session.peers.get('p1')
            if (
                not peer
                or not host
                or fields[3] != host.lease
                or session.universe_epoch != host.lease
            ):
                return []
            if fields[0] == 'NHP' and slot == 'p1' and len(fields) == 14:
                entity, gen, seq = map(sequence, fields[4:7])
                sector, code, macro, owner = fields[7:11]
                hull, shield = map(float, fields[11:13])
                descriptor, old = self.descriptors.get(entity), self.states.get(entity)
                if (
                    not entity
                    or not gen
                    or not seq
                    or not descriptor
                    or owner == 'player'
                    or not CODE.fullmatch(code)
                    or not ATOM.fullmatch(macro)
                    or (sector, macro, owner)
                    != (descriptor['sector'], descriptor['macro'], descriptor['owner'])
                    or not all(
                        math.isfinite(value) and 0 <= value <= 100 for value in (hull, shield)
                    )
                    or fields[13] not in ('0', '1')
                    or (fields[13] == '0' and hull != 0)
                    or (fields[13] == '1' and (hull <= 0 or not descriptor['active']))
                ):
                    return []
                identity = (sector, code, macro, owner)
                if old and (
                    seq <= old['seq']
                    or gen != old['gen']
                    or identity != old['identity']
                    or not old['alive']
                    and fields[13] == '1'
                ):
                    return []
                self.states[entity] = dict(
                    gen=gen,
                    seq=seq,
                    identity=identity,
                    descriptor=(sector, macro, owner),
                    alive=fields[13] == '1',
                    at=now,
                    fields=fields[3:],
                )
            elif fields[0] == 'NCLAIM' and slot == 'p2' and len(fields) == 11:
                entity, gen, shooter, sg, seq, hits = map(sequence, fields[4:10])
                if (
                    not seq
                    or not 0 < hits <= 1000000
                    or not self.current(entity, gen, now)
                    or not self.pilot(shooter, sg)
                    or not SHOT_DIGEST.fullmatch(fields[10])
                    or not session.interest
                    or session.interest[0] != peer.lease
                    or session.interest[2] != self.states[entity]['descriptor'][0]
                ):
                    return []
                key = (peer.lease, entity, gen, shooter, sg)
                old = self.claims.get(key)
                if old and (seq <= old['seq'] or hits < old['hits'] or fields[10] != old['digest']):
                    return []
                if not old and len(self.claims) >= LIMIT:
                    return []
                self.claims[key] = dict(
                    seq=seq,
                    hits=hits,
                    digest=fields[10],
                    fields=fields[3:],
                    ack=old['ack'] if old else 0,
                )
            elif fields[0] == 'NACK' and slot == 'p1' and len(fields) == 10:
                entity, gen, shooter, sg, count = map(sequence, fields[5:])
                claim = self.claims.get((fields[4], entity, gen, shooter, sg))
                if claim and claim['ack'] <= count <= claim['hits']:
                    claim['ack'] = count
        except (ValueError, OverflowError, UnicodeError):
            pass
        return []

    def snapshots(self, now):
        if now < self.next_send:
            return []
        self.next_send = now + 0.1
        session = self.session
        host, guest = session.peers.get('p1'), session.peers.get('p2')
        if not host or not guest or session.universe_epoch != host.lease:
            return []
        result = []
        interest = session.interest
        if self.forwarded_lease != guest.lease:
            self.forwarded, self.forwarded_lease = {}, guest.lease
        states = [
            (entity_key, record)
            for entity_key, record in self.states.items()
            if now - record['at'] < FRESH
            and interest
            and interest[0] == guest.lease
            and interest[2] == record['descriptor'][0]
        ]
        self.forwarded = {
            entity_key: entry_value
            for entity_key, entry_value in self.forwarded.items()
            if entity_key in self.states
        }
        # New sequences only, oldest arrival first, within the former per-tick budget.
        fresh = sorted(
            (kv for kv in states if self.forwarded.get(kv[0]) != kv[1]['seq']),
            key=lambda kv: kv[1]['at'],
        )
        budget = min(len(fresh), max(32, min(256, (len(states) + 9) // 10)))
        for entity, record in fresh[:budget]:
            self.forwarded[entity] = record['seq']
            result.append(
                (
                    guest.address,
                    ('NHP|1|' + guest.lease + '|' + '|'.join(record['fields'])).encode(),
                )
            )
        for key, claim in list(self.claims.items()):
            lease, entity, gen, shooter, sg = key
            if lease != guest.lease or not self.pilot(shooter, sg):
                del self.claims[key]  # The closed pilot generation can never be re-admitted.
                continue
            if not self.current(entity, gen, now):
                continue
            result.append(
                (
                    guest.address,
                    (
                        f'NACK|1|{guest.lease}|{host.lease}|{lease}|'
                        f'{entity}|{gen}|{shooter}|{sg}|{claim["ack"]}'
                    ).encode(),
                )
            )
            if claim['ack'] < claim['hits']:
                result.append(
                    (
                        host.address,
                        (
                            'NCLAIM|1|' + host.lease + '|' + lease + '|' + '|'.join(claim['fields'])
                        ).encode(),
                    )
                )
        return result
