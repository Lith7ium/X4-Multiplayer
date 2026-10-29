"""Bounded authoritative market snapshots and idempotent trading requests.

Only the X4 host can publish inventory/balances or complete a transaction.
The relay validates and routes; it never fabricates a trade's result.
"""

from collections import OrderedDict
import re
from .bubble import BubbleSession
from .host import HEX, sequence

WARE = re.compile(r'[a-z][a-z0-9_]{0,63}\Z')
MAX_AMOUNT = 1000000
MAX_MONEY = 1000000000000
RESULTS = {
    'OK',
    'REJECTED',
    'STOCK',
    'FUNDS',
    'CAPACITY',
    'PRICE',
    'DOCK',
    'BUSY',
    'PARTIAL',
    'UNCERTAIN',
    'UNSUPPORTED',
}


def integer(value, limit=MAX_MONEY, signed=False):
    if not re.fullmatch(r'-?[0-9]+' if signed else r'[0-9]+', value):
        raise ValueError('integer')
    number = int(value)
    if abs(number) > limit or (not signed and number < 0):
        raise ValueError('range')
    return number


def parse_snapshot(payload):
    """Validate a complete market snapshot and return balances and ware rows."""
    rows = payload.split(';')
    funds = rows[0].split(',')
    if len(funds) != 5 or funds[0] != 'M':
        raise ValueError('balances')
    balances = [integer(value) for value in funds[1:]]
    wares = {}
    if not 2 <= len(rows) <= 17:
        raise ValueError('ware count')
    for row in rows[1:]:
        fields = row.split(',')
        if (
            len(fields) != 13
            or fields[0] != 'W'
            or not WARE.fullmatch(fields[1])
            or fields[1] in wares
        ):
            raise ValueError('ware')
        # volume, p1, p2, market1, market2, bid1, ask1, limit1, bid2, ask2, limit2
        values = [integer(value) for value in fields[2:]]
        if not 1 <= values[0] <= 100000:
            raise ValueError('volume')
        if any(values[i] > MAX_AMOUNT for i in (1, 2, 3, 4, 7, 10)):
            raise ValueError('quantity')
        if any(values[i] < 1 for i in (5, 6, 8, 9)):
            raise ValueError('price')
        wares[fields[1]] = values
    return balances, wares


def parse_lines(payload):
    """Validate basket lines, including signed quantities and total cost."""
    rows = payload.split(';')
    result = []
    seen = set()
    if not 1 <= len(rows) <= 16:
        raise ValueError('basket size')
    for row in rows:
        fields = row.split(',')
        if len(fields) != 3 or not WARE.fullmatch(fields[0]) or fields[0] in seen:
            raise ValueError('line')
        quantity = integer(fields[1], MAX_AMOUNT, True)
        price = integer(fields[2])
        if quantity == 0 or price == 0 or abs(quantity) * price > MAX_MONEY:
            raise ValueError('cost')
        seen.add(fields[0])
        result.append((fields[0], quantity, price))
    return result


class Assembly:
    def __init__(self):
        self.pending = {}
        self.revision = -1

    def add(self, rev, part, total, payload, now):
        if (
            rev <= self.revision
            or not 1 <= total <= 8
            or not 1 <= part <= total
            or len(payload) > 500
        ):
            return None
        self.pending = {
            r: v for r, v in self.pending.items() if now - v[0] < 3 and r > self.revision
        }
        if rev not in self.pending:
            if len(self.pending) >= 4:
                self.pending.pop(min(self.pending))
            self.pending[rev] = (now, total, {})
        _, expected, parts = self.pending[rev]
        if total != expected:
            return None
        if part in parts and parts[part] != payload:
            return None
        parts[part] = payload
        if len(parts) != total:
            return None
        value = ''.join(parts[i] for i in range(1, total + 1))
        parse_snapshot(value)
        self.revision = rev
        self.pending = {r: v for r, v in self.pending.items() if r > rev}
        return value


class EconomySession(BubbleSession):
    def __init__(self, *args):
        super().__init__(*args)
        self.economy = None
        self.economy_owner = None
        self.assembly = Assembly()
        self.orders = OrderedDict()
        self.docks = {}
        self.next_economy = 0
        self.next_docks = 0

    def expire(self, now):
        old = self.peers.get('p1')
        super().expire(now)
        if old and 'p1' not in self.peers:
            self.economy = None
            self.economy_owner = None
            self.assembly = Assembly()
            for cmd in self.orders.values():
                if cmd['result'] == 'PENDING':
                    cmd['result'] = 'UNCERTAIN'
        live = {p.lease for p in self.peers.values()}
        self.docks = {lease: v for lease, v in self.docks.items() if lease in live}
        for cmd in self.orders.values():
            if cmd['lease'] not in live and cmd['result'] == 'PENDING':
                cmd['result'] = 'UNCERTAIN'

    def reply(self, peer, request, status):
        return f'ORDER_RESULT|1|{peer.lease}|{request}|{status}'.encode()

    def receive(self, raw, address, now):
        self.expire(now)
        if len(raw) > 768:
            return []
        try:
            fields = raw.decode('ascii').split('|')
        except UnicodeError:
            return []
        if len(fields) < 2 or fields[1] != '1':
            return []
        if fields[0] not in ('ECON', 'ORDER', 'ORDER_DONE', 'DOCKSTATE'):
            return super().receive(raw, address, now)
        try:
            slot, peer = self._peer(fields[2], address)
            if peer is None:
                return []
            if fields[0] == 'ECON' and slot == 'p1' and len(fields) == 7:
                if self.economy_owner != peer.lease:
                    self.economy_owner = peer.lease
                    self.assembly = Assembly()
                payload = self.assembly.add(
                    sequence(fields[3]),
                    integer(fields[4], 8),
                    integer(fields[5], 8),
                    fields[6],
                    now,
                )
                if payload is not None:
                    self.economy = payload
            elif fields[0] == 'ORDER' and len(fields) == 7:
                request, owner, market, lines = fields[3:]
                if (
                    not HEX.fullmatch(request)
                    or not HEX.fullmatch(owner)
                    or market not in ('m1', 'm2')
                ):
                    return []
                parse_lines(lines)
                old = self.orders.get(request)
                if old:
                    if (old['slot'], old['owner'], old['market'], old['lines']) != (
                        slot,
                        owner,
                        market,
                        lines,
                    ):
                        return []
                    return [self.reply(peer, request, old['result'])]
                host = self.peers.get('p1')
                if host is None or owner != host.lease or self.economy is None:
                    return [self.reply(peer, request, 'UNCERTAIN')]
                if (
                    len(self.orders) >= 1024
                    or sum(c['result'] == 'PENDING' for c in self.orders.values()) >= 8
                ):
                    return [self.reply(peer, request, 'BUSY')]
                self.orders[request] = {
                    'slot': slot,
                    'lease': peer.lease,
                    'owner': owner,
                    'market': market,
                    'lines': lines,
                    'result': 'PENDING',
                }
                return [self.reply(peer, request, 'PENDING')]
            elif (
                fields[0] == 'ORDER_DONE'
                and slot == 'p1'
                and len(fields) == 5
                and fields[4] in RESULTS
            ):
                command = self.orders.get(fields[3])
                if command and command['result'] == 'PENDING' and command['owner'] == peer.lease:
                    command['result'] = fields[4]
            elif (
                fields[0] == 'DOCKSTATE' and len(fields) == 5 and fields[4] in ('none', 'm1', 'm2')
            ):
                seq = sequence(fields[3])
                old = self.docks.get(peer.lease, (-1, None))
                if seq > old[0]:
                    self.docks[peer.lease] = (seq, fields[4])
        except (ValueError, IndexError, OverflowError):
            return []
        return []

    def snapshots(self, now):
        result = super().snapshots(now)
        host = self.peers.get('p1')
        if host is None:
            return result
        publish_economy = now >= self.next_economy
        publish_docks = now >= self.next_docks
        if publish_economy:
            self.next_economy = now + 0.5
        if publish_docks:
            self.next_docks = now + 0.2
        for slot, peer in self.peers.items():
            if publish_economy and self.economy is not None:
                chunks = [self.economy[i : i + 500] for i in range(0, len(self.economy), 500)]
                for i, payload in enumerate(chunks, 1):
                    result.append(
                        (
                            peer.address,
                            f'ECON|1|{peer.lease}|{host.lease}|{self.assembly.revision}|{i}|{len(chunks)}|{payload}'.encode(),
                        )
                    )
            other = self.peers.get('p2' if slot == 'p1' else 'p1')
            if publish_docks and other and other.lease in self.docks:
                seq, market = self.docks[other.lease]
                result.append(
                    (
                        peer.address,
                        f'DOCKSTATE|1|{peer.lease}|{other.lease}|{seq}|{market}'.encode(),
                    )
                )
        for request, command in self.orders.items():
            if command['result'] == 'PENDING':
                result.append(
                    (
                        host.address,
                        f"ORDER_EXEC|1|{host.lease}|{request}|{command['slot']}|{command['market']}|{command['lines']}".encode(),
                    )
                )
                break
        return result
