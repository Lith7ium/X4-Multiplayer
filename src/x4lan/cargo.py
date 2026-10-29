"""Reliable native cargo intentions; only host X4 mutates authoritative inventory.

The relay binds each request to the authenticated pilot and ship generation.
It retains outcomes through reconnect and never infers success from a timeout.
"""

from .host import HEX, sequence
from .economy import WARE

RESULTS = {'OK', 'REJECTED', 'CAPACITY', 'BUSY', 'STALE', 'UNCERTAIN'}


class CargoTransactions:
    def __init__(self, session):
        self.session = session
        self.requests = {}
        self.inventory = {}
        self.next_inventory = 0
        self.inventory_cursor = 0

    def busy(self, ship):
        return any(r['ship'] == ship and r['result'] == 'PENDING' for r in self.requests.values())

    def reply(self, lease, request, status, fence=0):
        return f'CARGOREPLY|1|{lease}|{request}|{status}|{fence}'.encode()

    def receive(self, raw, address, now):
        s = self.session
        s.expire(now)
        if len(raw) > 768:
            return []
        try:
            fields = raw.decode('ascii').split('|')
            if len(fields) < 4 or fields[1] != '1':
                return []
            slot, peer = s._peer(fields[2], address)
            if not peer:
                return []
            if (
                fields[0] == 'CINV'
                and len(fields) == 8
                and slot == 'p1'
                and fields[3] == peer.lease
            ):
                seq, ware, first, second = (
                    sequence(fields[4]),
                    fields[5],
                    sequence(fields[6]),
                    sequence(fields[7]),
                )
                if not WARE.fullmatch(ware) or max(first, second) > 1000000:
                    return []
                old = self.inventory.get(ware)
                if old and old[2][0] == peer.lease and seq <= old[0]:
                    return []
                if not old and len(self.inventory) >= 256:
                    return []
                self.inventory[ware] = (seq, now, fields[3:])
                return []
            if not HEX.fullmatch(fields[3]):
                return []
            request = fields[3]
            if (
                fields[0] == 'CARGODONE'
                and len(fields) == 6
                and slot == 'p1'
                and fields[4] in RESULTS
            ):
                fence = sequence(fields[5])
                old = self.requests.get(request)
                if old and old['host'] == peer.lease and old['result'] == 'PENDING':
                    old['result'] = fields[4]
                    old['fence'] = fence
                return []
            if fields[0] != 'CARGOREQ' or len(fields) != 11:
                return []
            host, ship, epoch, verb, obj, ware, amount = fields[4:]
            ship, epoch, obj, amount = map(sequence, (ship, epoch, obj, amount))
            if (
                not HEX.fullmatch(host)
                or ship not in (1, 2)
                or epoch < 1
                or verb not in ('DROP', 'PICKUP')
                or not WARE.fullmatch(ware)
                or not 0 < amount <= 1000000
                or (verb == 'DROP' and obj != 0)
                or (verb == 'PICKUP' and obj == 0)
            ):
                return []
            signature = (slot, host, ship, epoch, verb, obj, ware, amount)
            old = self.requests.get(request)
            if old:
                if old['signature'] != signature:
                    return [self.reply(peer.lease, request, 'REJECTED')]
                return [self.reply(peer.lease, request, old['result'], old.get('fence', 0))]
            authority = s.peers.get('p1')
            if not authority or authority.lease != host:
                return [self.reply(peer.lease, request, 'STALE')]
            if (
                s.owners[ship] != slot
                or s.epochs[ship] != epoch
                or s.pilot(ship) != slot
                or s.dead(ship)
            ):
                return [self.reply(peer.lease, request, 'REJECTED')]
            if s.busy(ship):
                return [self.reply(peer.lease, request, 'BUSY')]
            # Reject new requests at the cap; never evict a completed ID into reuse.
            if len(self.requests) >= 8192:
                return [self.reply(peer.lease, request, 'BUSY')]
            self.requests[request] = dict(
                signature=signature,
                ship=ship,
                host=host,
                requester=slot,
                result='PENDING',
                next_send=now,
            )
            return [self.reply(peer.lease, request, 'PENDING')]
        except (ValueError, UnicodeError, OverflowError):
            return []

    def snapshots(self, now):
        host = self.session.peers.get('p1')
        result = []
        guest = self.session.peers.get('p2')
        if host and guest and now >= self.next_inventory:
            self.next_inventory = now + 0.2
            values = list(self.inventory.values())
            selected = [
                values[(self.inventory_cursor + i) % len(values)]
                for i in range(min(32, len(values)))
            ]
            self.inventory_cursor += len(selected)
            for _, at, fields in selected:
                if fields[0] == host.lease and now - at < 2:
                    result.append(
                        (guest.address, ('CINV|1|' + guest.lease + '|' + '|'.join(fields)).encode())
                    )
        for request, item in self.requests.items():
            if item['result'] != 'PENDING':
                continue
            # A timeout is uncertainty, never permission to replay into a new host.
            if not host or host.lease != item['host']:
                item['result'] = 'UNCERTAIN'
                continue
            if now < item['next_send']:
                continue
            item['next_send'] = now + 0.25
            slot, _, ship, epoch, verb, obj, ware, amount = item['signature']
            packet = f'CARGOEXEC|1|{host.lease}|{request}|{ship}|{epoch}|{verb}|{obj}|{ware}|{amount}|{slot}'
            result.append((host.address, packet.encode()))
        return result
