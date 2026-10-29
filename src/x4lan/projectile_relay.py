"""Relay half of observed projectile visuals (reports/PROJECTILE-VISUAL-PROTOCOL.txt): BOLT/BEND.

Visual only. The relay forwards a pilot's observed projectile births and ends to the other player
and derives no hit, damage, heat or authority from them; SHOT/HEALTH stay separate. Every
accepted packet and every forward rechecks the pilot's authority: exact lease/address, current
owner AND pilot, authority epoch, current HEALTH generation (a guest's must also be live after
HACK), a canonical live ship, and a fresh NAV2 of the same pilot in the same sector. Source
times are compared only with the pilot's estimated source clock, i.e. its last NAV2 time plus
the relay time elapsed since that NAV2; they are never compared with the relay clock itself.

A record is immutable under (sourceLease, entity, epoch, gen, id). A duplicate never respawns or
extends it and a changed duplicate is refused. An end before its birth leaves a tombstone that
refuses the late birth. Each record is forwarded at most REPEATS times, GAP apart, and retires at
its original birth time + ttl, on any authority transition, on a NAV2 sector change, or when
the NAV2 stream goes stale.
"""

from collections import deque
from dataclasses import replace

from . import projectile_protocol as pp

NAV_FRESH = 0.75  # NAV2 age after which the pilot's stream is stale (as for NAV2/FIRE2 forwarding)
LATE = 1.0  # a birth/end may lag the pilot's estimated source time by at most this
AHEAD = 0.1  # ...and lead it by at most this (capture vs NAV2 jitter)
WINDOW = 512  # projectile ids per key: older ones are refused, and so are jumps beyond
LIVE = 192  # live records per entity: the six-gun X Shuttle plasma (stock reload rate 9,
# 54 shots/s) at the 3 s ttl maximum needs 162
RATE = 96  # accepted births per entity per relay second (54/s admitted maximum, ~1.8x)
REPEATS = 3  # forwards of each accepted birth and of each accepted end
GAP = 0.04  # minimum relay seconds between two forwards of the same packet
PER_TICK = 32  # BOLT/BEND forwards per entity per recipient per snapshot


class Stream:
    """One pilot's records under one (lease, epoch, gen) key."""

    def __init__(self, key):
        self.key = key
        self.latest = 0
        self.seen = {}  # id -> record (birth or tombstone), bounded by WINDOW
        self.births = deque()  # relay times of accepted births, for RATE

    def admit(self, ident):
        return ident > self.latest - WINDOW and (self.latest == 0 or ident <= self.latest + WINDOW)

    def mark(self, ident, record):
        self.seen[ident] = record
        if ident > self.latest:
            self.latest = ident
            for old in [i for i in self.seen if i <= ident - WINDOW]:
                del self.seen[old]

    def live(self):
        return [record for record in self.seen.values() if record['live']]


class ProjectileRelay:
    def __init__(self):
        self.streams = {}

    @staticmethod
    def authority(session, entity, slot, lease, now):
        """(epoch, gen, sector, source now) while slot's lease may publish entity's visuals, else None."""
        if entity not in (1, 2) or session.owners[entity] != slot or session.pilot(entity) != slot:
            return None
        if not session.world or session.dead(entity):
            return None
        gen = session.health_generation(entity)
        if slot != 'p1' and session.health_live[entity] != gen:
            return None
        nav = session.navigation.get(entity)
        if (
            not nav
            or nav.get('kind') != 'NAV2'
            or nav['lease'] != lease
            or now - nav['at'] > NAV_FRESH
        ):
            return None
        return (
            session.epochs[entity],
            gen,
            nav['fields'][3],
            float(nav['fields'][12]) + (now - nav['at']),
        )

    def receive(self, session, slot, peer, raw, now):
        try:
            message = pp.parse(raw, 'client')
        except pp.ProtocolError:
            return
        if message.lease != peer.lease:
            return
        auth = self.authority(session, message.entity, slot, peer.lease, now)
        if not auth or (message.epoch, message.gen) != auth[:2]:
            return
        key = (peer.lease, message.epoch, message.gen)
        stream = self.streams.get(message.entity)
        if not stream or stream.key != key:
            stream = self.streams[message.entity] = Stream(key)
        source = auth[3]
        self.retire(stream, source, auth[2])
        if not source - LATE <= message.time <= source + AHEAD or not stream.admit(message.id):
            return
        old = stream.seen.get(message.id)
        if isinstance(message, pp.Bolt):
            if old or message.sector != auth[2] or message.time + message.ttl <= source:
                return
            while stream.births and now - stream.births[0] >= 1:
                stream.births.popleft()
            if len(stream.births) >= RATE or len(stream.live()) >= LIVE:
                return
            stream.births.append(now)
            stream.mark(
                message.id, dict(live=True, bolt=message, end=None, sent=0, end_sent=0, at=-1e9)
            )
        elif old is None:
            # End before birth: a bounded tombstone refuses the late birth (nothing to forward).
            stream.mark(message.id, dict(live=False, bolt=None, end=message))
        elif (
            old['bolt'] and old['end'] is None and old['live'] and message.time >= old['bolt'].time
        ):
            old['end'] = message  # first end wins; never extended

    @staticmethod
    def retire(stream, source, sector):
        # A record also retires when the pilot's current NAV2 sector is no longer its birth sector.
        for record in stream.live():
            bolt = record['bolt']
            if (
                source >= bolt.time + bolt.ttl
                or bolt.sector != sector
                or (record['end'] and record['end_sent'] >= REPEATS)
            ):
                record['live'] = False

    def packets(self, session, now):
        result = []
        for entity, stream in list(self.streams.items()):
            lease, epoch, gen = stream.key
            slot = next(
                (
                    player_slot
                    for player_slot, remote_peer in session.peers.items()
                    if remote_peer.lease == lease
                ),
                None,
            )
            auth = slot and self.authority(session, entity, slot, lease, now)
            if not auth or auth[:2] != (epoch, gen):
                # Transition or stale NAV2: retire everything, but keep the ids so that a late
                # repeat under this key can never resurrect a record (a new key replaces the stream).
                for record in stream.live():
                    record['live'] = False
                continue
            self.retire(stream, auth[3], auth[2])
            for other, peer in session.peers.items():
                if other == slot:
                    continue
                budget = PER_TICK
                # Unsent births first, so repeats never delay a new projectile's first forward.
                for record in sorted(stream.live(), key=lambda r: r['sent'] > 0):
                    if budget <= 0:
                        break
                    # One packet per record per snapshot: the birth goes out first even when its end
                    # is already known, then birth repeats until an end, then the end's repeats.
                    end, sent = record['end'], record['sent']
                    if now - record['at'] < GAP:
                        continue
                    if sent == 0 or (end is None and sent < REPEATS):
                        message, record['sent'] = record['bolt'], sent + 1
                    elif end is not None and record['end_sent'] < REPEATS:
                        message, record['end_sent'] = end, record['end_sent'] + 1
                    else:
                        continue
                    record['at'] = now
                    budget -= 1
                    result.append(
                        (
                            peer.address,
                            pp.serialize(
                                replace(
                                    message,
                                    direction='forwarded',
                                    lease=peer.lease,
                                    source_lease=lease,
                                )
                            ),
                        )
                    )
        return result
