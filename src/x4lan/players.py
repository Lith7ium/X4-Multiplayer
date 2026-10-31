"""Relay side of player factions (client flag player_factions; see ui/player_factions_core.lua).

Every human slot pK owns pool faction x4lan_playerK. The relay only routes and caches; the host
(p1) is the authority for inter-player relations and sector owners.

  client -> relay  PFID|1|lease|seq|nameHex|icon                 own display identity
  relay -> client  PFROSTER|1|lease|slot|nameHex|icon|seq         every connected human, 1 Hz
  host   -> relay  PFREL|1|lease|rev|a|b|value                    authoritative relation (a < b)
  relay -> guest   PFREL|1|lease|rev|a|b|value                    cached, 1 Hz and on change
  guest  -> relay  PFREQ|1|lease|seq|target|value                 relation intent
  relay -> host    PFREQ|1|hostLease|from|authoritySeq|target|value
  host   -> relay  PFSEC|1|lease|seq|sectorMacro|owner            owner = S<index> or faction id
  relay -> guest   PFSEC|1|lease|seq|sectorMacro|owner            cached rows, every 5 s and on change
  guest  -> relay  PFSEC|1|lease|seq|sectorMacro|S<own index>     claim intent, forwarded to the host
Clients without the flag send nothing; relays without this module drop PF* packets.
"""
from __future__ import annotations

import hmac
import re

PREFIX = b'PF'
POOL = 16
NAME_HEX = re.compile(r'(?:[0-9a-f]{2}){1,48}\Z')
ATOM = re.compile(r'[A-Za-z0-9_]{1,64}\Z')
MACRO = re.compile(r'[a-z0-9_]{1,90}_macro\Z')
SLOT = re.compile(r'p([1-9][0-9]?)\Z')
MAX_SEQ = 2**31 - 1
MAX_SECTORS = 1024
ROSTER_SECONDS, SECTOR_SECONDS = 1.0, 5.0


def number(value, limit=MAX_SEQ):
    if not value.isascii() or not value.isdecimal() or len(value) > 10:
        raise ValueError('number')
    result = int(value)
    if not 0 <= result <= limit:
        raise ValueError('number bounds')
    return result


def relation(value):
    result = float(value)
    if not -1.0 <= result <= 1.0 or len(value) > 12:
        raise ValueError('relation')
    return result


def index(slot):
    match = SLOT.fullmatch(slot)
    if not match or not 1 <= int(match[1]) <= POOL:
        return None
    return int(match[1])


class PlayerRoster:
    def __init__(self):
        self.identities = {}    # slot -> (lease, seq, nameHex, icon)
        self.relations = {}     # (a, b) -> (rev, value text)
        self.sectors = {}       # macro -> (seq, owner token)
        self.host_lease = None
        self.request_sequences = {}  # (slot, target) -> (client lease, last relation intent)
        self.authority_request_sequence = 0  # monotonic envelope sequence within host lease
        self.next_roster = self.next_sectors = 0.0

    @staticmethod
    def _peer(peers, address, lease):
        for slot, peer in peers.items():
            if peer.address == address and hmac.compare_digest(peer.lease, lease):
                return slot, peer
        return None, None

    def _host(self, peers):
        host = peers.get('p1')
        lease = host.lease if host else None
        if lease and lease != self.host_lease:
            # A new host lease is a new authority epoch: forget its predecessor's state.
            self.host_lease = lease
            self.relations.clear()
            self.sectors.clear()
            self.authority_request_sequence = 0
        return host

    def handle(self, raw, address, peers, now):
        """Returns (address, packet) pairs to send immediately."""
        if len(raw) > 768:
            return []
        try:
            f = raw.decode('ascii').split('|')
        except UnicodeError:
            return []
        if len(f) < 4 or f[1] != '1':
            return []
        slot, peer = self._peer(peers, address, f[2])
        own = index(slot) if slot else None
        if not own:
            return []
        host = self._host(peers)
        out = []
        try:
            kind = f[0]
            if kind == 'PFID' and len(f) == 6:
                seq = number(f[3])
                if not NAME_HEX.fullmatch(f[4]) or not ATOM.fullmatch(f[5]):
                    return []
                old = self.identities.get(slot)
                if old and old[0] == peer.lease and seq <= old[1]:
                    return []
                self.identities[slot] = (peer.lease, seq, f[4], f[5])
            elif kind == 'PFREL' and len(f) == 7 and slot == 'p1':
                rev, a, b = number(f[3]), number(f[4], POOL), number(f[5], POOL)
                value = relation(f[6])
                if not 1 <= a < b:
                    return []
                old = self.relations.get((a, b))
                if old and rev <= old[0]:
                    return []
                self.relations[(a, b)] = (rev, f'{value:.4f}')
                for other, target in peers.items():
                    if other != 'p1':
                        out.append((target.address, f'PFREL|1|{target.lease}|{rev}|{a}|{b}|{value:.4f}'.encode()))
            elif kind == 'PFREQ' and len(f) == 6 and slot != 'p1' and host:
                seq, target = number(f[3]), number(f[4], POOL)
                value = relation(f[5])
                if not 1 <= target or target == own:
                    return []
                request_key = slot, target
                previous = self.request_sequences.get(request_key)
                if previous and previous[0] == peer.lease and seq <= previous[1]:
                    return []
                if self.authority_request_sequence >= MAX_SEQ:
                    return []
                # UDP can duplicate/reorder intents with DIFFERENT values. Reapplying an
                # older intent would undo the human's latest choice. Fence per client
                # lease AND target; independent target pairs must not discard each other.
                self.request_sequences[request_key] = (peer.lease, seq)
                # The second UDP hop can reorder too. A fresh client lease resets its
                # ingress counter, but must not reset the authority's ordering fence.
                self.authority_request_sequence += 1
                order = self.authority_request_sequence
                out.append((host.address, f'PFREQ|1|{host.lease}|{own}|{order}|{target}|{value:.4f}'.encode()))
            elif kind == 'PFSEC' and len(f) == 6:
                seq = number(f[3])
                if not MACRO.fullmatch(f[4]) or not ATOM.fullmatch(f[5]):
                    return []
                if slot == 'p1':
                    if f[5].startswith('S') and not re.fullmatch(r'S([1-9][0-9]?)', f[5]):
                        return []
                    old = self.sectors.get(f[4])
                    if old and old[1] == f[5]:
                        self.sectors[f[4]] = (seq, f[5])
                        return []
                    if f[4] not in self.sectors and len(self.sectors) >= MAX_SECTORS:
                        return []
                    self.sectors[f[4]] = (seq, f[5])
                    for other, target in peers.items():
                        if other != 'p1':
                            out.append((target.address, f'PFSEC|1|{target.lease}|{seq}|{f[4]}|{f[5]}'.encode()))
                elif host and f[5] == f'S{own}':
                    # A guest may only claim for its own human; the host decides.
                    out.append((host.address, f'PFSEC|1|{host.lease}|{seq}|{f[4]}|{f[5]}'.encode()))
        except ValueError:
            return []
        return out

    def snapshots(self, peers, now):
        host = self._host(peers)
        out = []
        live = {slot: ident for slot, ident in self.identities.items()
                if slot in peers and peers[slot].lease == ident[0]}
        if now >= self.next_roster:
            self.next_roster = now + ROSTER_SECONDS
            for slot, peer in peers.items():
                for other, (_, seq, name, icon) in sorted(live.items()):
                    out.append((peer.address, f'PFROSTER|1|{peer.lease}|{index(other)}|{name}|{icon}|{seq}'.encode()))
                if slot != 'p1' and host:
                    for (a, b), (rev, value) in sorted(self.relations.items()):
                        out.append((peer.address, f'PFREL|1|{peer.lease}|{rev}|{a}|{b}|{value}'.encode()))
        if now >= self.next_sectors and host:
            self.next_sectors = now + SECTOR_SECONDS
            for slot, peer in peers.items():
                if slot != 'p1':
                    for macro, (seq, token) in sorted(self.sectors.items()):
                        out.append((peer.address, f'PFSEC|1|{peer.lease}|{seq}|{macro}|{token}'.encode()))
        return out
