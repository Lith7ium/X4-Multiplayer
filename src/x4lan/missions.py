"""Host-native faction mission journal. Never awards money or invents progress.

Complete, checksummed records survive packet loss and late joins. IDs are scoped
to the host lease; terminal records are retained, never evicted or reused.
"""

import re
import zlib

from .host import MAX_PACKET, sequence

LIMIT, PART_SIZE, MAX_PARTS = 256, 500, 24
TERMINAL = {'C', 'F', 'X', 'W'}
TRANSITIONS = {'O': {'O', 'A'} | TERMINAL, 'A': {'A', 'C', 'F', 'X'}}


def text_field(value, maximum):
    if (
        not value
        or len(value) > maximum * 2
        or len(value) % 2
        or not re.fullmatch('[0-9a-f]+', value)
    ):
        raise ValueError('mission text encoding')
    text = bytes.fromhex(value).decode('utf-8')
    if any(ord(character) < 32 and character not in '\n\t' for character in text) or '\x7f' in text:
        raise ValueError('mission text control')
    return text


def parse_record(payload):
    fields = payload.split(',')
    if len(fields) != 9 or fields[0] != 'quota' or fields[1] not in {'O', 'A'} | TERMINAL:
        raise ValueError('mission record')
    done, total, reward = map(sequence, fields[2:5])
    if (
        not 0 <= done <= total <= 1000000
        or not total
        or not re.fullmatch('[a-z][a-z0-9_]{0,63}', fields[5])
    ):
        raise ValueError('mission bounds')
    if fields[1] == 'O' and done or fields[1] == 'C' and done != total:
        raise ValueError('mission state')
    return dict(
        kind=fields[0],
        state=fields[1],
        done=done,
        total=total,
        reward=reward,
        faction=fields[5],
        title=text_field(fields[6], 256),
        description=text_field(fields[7], 4096),
        objective=text_field(fields[8], 512),
    )


def advances(old, new):
    if old is None:
        return True  # A terminal fact may be the first retained record after joining.
    return (
        new['state'] in TRANSITIONS.get(old['state'], set())
        and new['done'] >= old['done']
        and all(
            new[field] == old[field]
            for field in ('kind', 'total', 'reward', 'faction', 'title', 'description', 'objective')
        )
    )


class MissionJournal:
    def __init__(self, session):
        self.session = session
        self.epoch = None
        self.records, self.pending, self.requests = {}, {}, {}
        self.cursor, self.next_send = 0, 0
        self.changed = []

    def current_host(self):
        host = self.session.peers.get('p1')
        if host and self.epoch != host.lease:
            self.epoch = host.lease
            self.records.clear()
            self.pending.clear()
            self.requests.clear()
            self.cursor = 0
            self.changed.clear()
        return host

    def receive(self, raw, address, now):
        session = self.session
        session.expire(now)
        if len(raw) > MAX_PACKET:
            return []
        try:
            fields = raw.decode('ascii').split('|')
            if len(fields) < 7 or fields[1] != '1':
                return []
            host = self.current_host()
            slot, peer = session._peer(fields[2], address)
            if not host or not peer or fields[3] != host.lease:
                return []
            ident, rev = sequence(fields[4]), sequence(fields[5])
            if not 1 <= ident <= LIMIT or not rev:
                return []
            old = self.records.get(ident)
            if fields[0] == 'MREQ' and len(fields) == 7 and slot == 'p2':
                if (
                    old
                    and old['rev'] == rev
                    and (old['row']['state'], fields[6]) in {('O', 'ACCEPT'), ('A', 'ABORT')}
                ):
                    self.requests[ident] = (peer.lease, rev, fields[6])
            elif fields[0] == 'MSTATE' and len(fields) == 10 and slot == 'p1':
                if old and rev <= old['rev']:
                    return []
                part, total, digest = int(fields[6]), int(fields[7]), int(fields[8])
                if (
                    not 1 <= part <= total <= MAX_PARTS
                    or not 0 <= digest <= 0xFFFFFFFF
                    or not 1 <= len(fields[9]) <= PART_SIZE
                ):
                    return []
                if not re.fullmatch('[a-zA-Z0-9_,]+', fields[9]):
                    return []
                pending = self.pending.get(ident)
                if pending and rev < pending['rev']:
                    return []
                if not pending or rev > pending['rev'] or now - pending['at'] >= 10:
                    pending = dict(rev=rev, total=total, digest=digest, parts={}, at=now)
                    self.pending[ident] = pending
                if (total, digest) != (pending['total'], pending['digest']):
                    return []
                if part in pending['parts'] and pending['parts'][part] != fields[9]:
                    return []
                pending['parts'][part] = fields[9]
                if len(pending['parts']) != total:
                    return []
                payload = ''.join(pending['parts'][i] for i in range(1, total + 1))
                if zlib.adler32(payload.encode()) != digest:
                    return []
                row = parse_record(payload)
                if not advances(old and old['row'], row):
                    return []
                self.records[ident] = dict(rev=rev, row=row, payload=payload, digest=digest)
                self.pending.pop(ident, None)
                if ident not in self.changed:
                    self.changed.append(ident)
                self.requests.pop(ident, None)
        except (ValueError, UnicodeError):
            pass
        return []

    def snapshots(self, now):
        host = self.current_host()
        if not host or now < self.next_send:
            return []
        self.next_send = now + 0.25
        guest = self.session.peers.get('p2')
        if not guest:
            return []
        out = []
        ids = sorted(self.records)
        # Bound each tick; independent of either player's sector or attention.
        if ids:
            self.cursor %= len(ids)
            ident = self.changed.pop(0) if self.changed else ids[self.cursor]
            self.cursor += 1
            record = self.records[ident]
            payload = record['payload']
            parts = [payload[i : i + PART_SIZE] for i in range(0, len(payload), PART_SIZE)]
            for i, part in enumerate(parts, 1):
                out.append(
                    (
                        guest.address,
                        f'MSTATE|1|{guest.lease}|{host.lease}|{ident}|{record["rev"]}|{i}|{len(parts)}|{record["digest"]}|{part}'.encode(),
                    )
                )
        for ident, (lease, rev, action) in list(self.requests.items()):
            if guest.lease != lease:
                self.requests.pop(ident)
            else:
                out.append(
                    (
                        host.address,
                        f'MREQ|1|{host.lease}|{host.lease}|{ident}|{rev}|{action}'.encode(),
                    )
                )
        return out
