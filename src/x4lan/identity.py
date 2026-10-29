"""Host display identity and shared sensor observations, scoped away from native IDs."""

import re
from .host import sequence

CODE = re.compile(r'[A-Z0-9-]{1,32}\Z')
NAME = re.compile(r'(?:[0-9a-f]{2}){1,160}\Z')


class Identities:
    def __init__(self, session):
        self.session = session
        self.rows = {}
        self.next_send = 0
        # (scope, entity) -> seq last forwarded to the current guest lease. Only new host
        # sequences are forwarded; the host repeats each identity with a new sequence at
        # least every keepalive (identity_game.lua), so repeats here cannot refresh a guest.
        self.forwarded = {}
        self.forwarded_lease = None

    def receive(self, raw, address, now):
        session = self.session
        session.expire(now)
        if len(raw) > 768:
            return []
        try:
            fields = raw.decode('ascii').split('|')
            if len(fields) != 11 or fields[:2] != ['IDENT', '1']:
                return []
            slot, peer = session._peer(fields[2], address)
            if slot != 'p1' or not peer or fields[3] != peer.lease:
                return []
            if session.universe_epoch and session.universe_epoch != peer.lease:
                return []
            seq, scope, entity, code, name, known, contact = (
                sequence(fields[4]),
                fields[5],
                sequence(fields[6]),
                fields[7],
                fields[8],
                fields[9],
                fields[10],
            )
            if scope == 'P':
                if entity not in (1, 2):
                    return []
            elif scope == 'F':
                if entity not in (1, 2, 3, 4):
                    return []
            elif scope == 'N':
                if not any(entity in ids for ids in session.entity_ids.values()):
                    return []
            else:
                return []
            if (
                not CODE.fullmatch(code)
                or not NAME.fullmatch(name)
                or known not in ('0', '1')
                or contact not in ('0', '1')
            ):
                return []
            decoded = bytes.fromhex(name).decode('utf-8')
            if any(ord(character) < 32 or ord(character) == 127 for character in decoded):
                return []
            key = scope, entity
            old = self.rows.get(key)
            if old and seq <= old[0]:
                return []
            self.rows[key] = seq, now, fields[3:]
        except (ValueError, UnicodeError, OverflowError):
            pass
        return []

    def snapshots(self, now):
        if now < self.next_send:
            return []
        self.next_send = now + 0.1
        session = self.session
        host, guest = session.peers.get('p1'), session.peers.get('p2')
        if not host or not guest:
            return []
        live = set().union(*session.entity_ids.values()) if session.entity_ids else set()
        self.rows = {
            entry_key: entry_value
            for entry_key, entry_value in self.rows.items()
            if entry_key[0] in ('P', 'F') or entry_key[1] in live
        }
        # Only forward fresh observations; the guest expires forced visibility.
        rows = [
            (entry_key, entry_value)
            for entry_key, entry_value in self.rows.items()
            if entry_value[2][0] == host.lease and now - entry_value[1] < 2
        ]
        if self.forwarded_lease != guest.lease:
            self.forwarded, self.forwarded_lease = {}, guest.lease
        self.forwarded = {
            entry_key: entry_value
            for entry_key, entry_value in self.forwarded.items()
            if entry_key in self.rows
        }
        # New sequences only, oldest arrival first, within the former per-tick budget.
        fresh = sorted(
            (kv for kv in rows if self.forwarded.get(kv[0]) != kv[1][0]), key=lambda kv: kv[1][1]
        )
        result = []
        for key, (seq, _, fields) in fresh[: min(256, max(32, (len(rows) + 9) // 10))]:
            self.forwarded[key] = seq
            result.append(
                (guest.address, ('IDENT|1|' + guest.lease + '|' + '|'.join(fields)).encode())
            )
        return result
