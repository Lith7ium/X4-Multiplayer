"""Reference codec and log comparison for lockstep player orders (format v1).

Independent Python implementation of the wire format in
reports/LOCKSTEP-PLAYER-ORDERS-20261029.txt; tests cross-check it against the Lua
core (tools/dev/native_gate_player_orders_core.lua). The `compare` command checks
that every client log contains the identical ordered PLAYER_ORDER_APPLIED lines.

    python tools/dev/native_gate_player_orders.py decode HEX --slot 2
    python tools/dev/native_gate_player_orders.py compare client1.log client2.log client3.log
"""
import argparse
import json
import math
import re
import struct
import sys

CHANNEL, VERSION = 0x50, 1
MAX_HEX, MAX_SLOTS, MAX_COMMANDS = 240, 16, 15
MAX_TICK, MAX_SEQ = 0xffffffff, 0xffff
COORD_LIMIT = 1e7
MAX_AMOUNT, MAX_PRICE, MAX_GROUP = 1_000_000_000, 0xffffffff, 10
CLEAR, IMMEDIATE, HOLDFIRE = 1, 2, 4
OPS = [('move', 'position', 3), ('attack', 'object', 3), ('follow', 'object', 3),
       ('protect', 'object', 3), ('dock', 'object', 3), ('hold', 'none', 7),
       ('buy', 'trade', 3), ('sell', 'trade', 3), ('assign', 'assign', 0)]
OP = {name: code for code, (name, _, _) in enumerate(OPS, 1)}
ASSIGNMENTS = ['defence', 'attack', 'interception', 'bombardment', 'mining', 'trade',
               'supplyfleet', 'positiondefence', 'assist', 'salvage']
WARE = re.compile(r'[a-z][a-z0-9_]{0,39}')
SAVED = re.compile(r'[0-9a-f]{16}')
APPLIED = re.compile(r'PLAYER_ORDER_APPLIED (tick=\d+ slot=\d+ .*?)\s*$')


class Refused(ValueError):
    """A strict, deterministic refusal; str(error) is the reason token."""


def _saved(value):
    return isinstance(value, str) and SAVED.fullmatch(value) is not None and value != '0' * 16


def f32_bits(value):
    if not isinstance(value, (int, float)) or not math.isfinite(value):
        raise Refused('coordinate_nonfinite')
    if value == 0:
        return 0
    try:
        bits = struct.unpack('>I', struct.pack('>f', value))[0]
    except OverflowError:
        raise Refused('coordinate_range') from None
    return 0 if bits == 0x80000000 else bits  # underflow to -0 is canonical +0


def f32_value(bits):
    if bits == 0x80000000:
        raise Refused('coordinate_negative_zero')
    if (bits >> 23) & 0xff == 0xff:
        raise Refused('coordinate_nonfinite')
    return struct.unpack('>f', struct.pack('>I', bits))[0]


def _ref(value, allow_none=False):
    if value is None:
        if allow_none:
            return b'\0'
        raise Refused('reference_missing')
    if not _saved(value):
        raise Refused('reference_invalid')
    data = int(value, 16).to_bytes(8, 'big').lstrip(b'\0')
    return bytes([0x10 | len(data)]) + data


def _int(value, low, high):
    return isinstance(value, int) and not isinstance(value, bool) and low <= value <= high


def encode_command(c):
    if c.get('op') not in OP:
        raise Refused('op_unknown')
    code = OP[c['op']]
    _, kind, allowed = OPS[code - 1]
    flags = c.get('flags', 0)
    if not _int(flags, 0, 7) or flags & ~allowed:
        raise Refused('flags_invalid')
    out = bytes([code, flags]) + _ref(c.get('subject'))
    if kind == 'position':
        out += _ref(c.get('sector'))
        for axis in 'xyz':
            bits = c[axis + 'bits'] if axis + 'bits' in c else f32_bits(c.get(axis))
            if abs(f32_value(bits)) > COORD_LIMIT:
                raise Refused('coordinate_range')
            out += bits.to_bytes(4, 'big')
    elif kind == 'object':
        out += _ref(c.get('target'))
    elif kind == 'trade':
        out += _ref(c.get('target'))
        ware = c.get('ware')
        if not isinstance(ware, str) or not WARE.fullmatch(ware):
            raise Refused('ware_invalid')
        out += bytes([len(ware)]) + ware.encode('ascii')
        if not _int(c.get('amount'), 0 if c['op'] == 'sell' else 1, MAX_AMOUNT):
            raise Refused('amount_invalid')
        if not _int(c.get('price'), 0, MAX_PRICE):
            raise Refused('price_invalid')
        out += c['amount'].to_bytes(4, 'big') + c['price'].to_bytes(4, 'big')
    elif kind == 'assign':
        target = c.get('target')
        out += _ref(target, True)
        group = c.get('group', 0)
        if target is None:
            if c.get('assignment') is not None or group != 0:
                raise Refused('assignment_invalid')
            assignment = 0
        elif c.get('assignment') in ASSIGNMENTS:
            assignment = ASSIGNMENTS.index(c['assignment']) + 1
        else:
            raise Refused('assignment_invalid')
        if not _int(group, 0, MAX_GROUP):
            raise Refused('group_invalid')
        out += bytes([assignment, group])
    return out


def encode(blob):
    commands = blob.get('commands') or []
    if not _int(blob.get('slot'), 1, MAX_SLOTS):
        raise Refused('slot_invalid')
    if not _int(blob.get('tick'), 1, MAX_TICK):
        raise Refused('tick_invalid')
    if not _int(blob.get('seq'), 1, MAX_SEQ):
        raise Refused('seq_invalid')
    if not 1 <= len(commands) <= MAX_COMMANDS:
        raise Refused('count_invalid')
    data = bytes([CHANNEL, VERSION, blob['slot']]) + blob['tick'].to_bytes(4, 'big') + \
        blob['seq'].to_bytes(2, 'big') + bytes([len(commands)])
    data += b''.join(encode_command(c) for c in commands)
    if len(data) * 2 > MAX_HEX:
        raise Refused('too_long')
    return data.hex()


class _Reader:
    def __init__(self, data):
        self.data, self.pos = data, 0

    def take(self, n):
        if self.pos + n > len(self.data):
            raise Refused('truncated')
        chunk = self.data[self.pos:self.pos + n]
        self.pos += n
        return chunk

    def u8(self):
        return self.take(1)[0]

    def uint(self, n):
        return int.from_bytes(self.take(n), 'big')

    def ref(self, allow_none=False):
        tag = self.u8()
        if tag == 0:
            if allow_none:
                return None
            raise Refused('reference_missing')
        if tag >> 4 != 1:
            raise Refused('reference_kind')
        if not 1 <= tag & 15 <= 8:
            raise Refused('reference_length')
        data = self.take(tag & 15)
        if data[0] == 0:
            raise Refused('reference_noncanonical')
        return data.hex().rjust(16, '0')


def _command(r):
    code = r.u8()
    if not 1 <= code <= len(OPS):
        raise Refused('op_unknown')
    name, kind, allowed = OPS[code - 1]
    flags = r.u8()
    if flags & ~allowed:
        raise Refused('flags_invalid')
    c = {'op': name, 'flags': flags, 'subject': r.ref()}
    if kind == 'position':
        c['sector'] = r.ref()
        for axis in 'xyz':
            bits = r.uint(4)
            value = f32_value(bits)
            if abs(value) > COORD_LIMIT:
                raise Refused('coordinate_range')
            c[axis + 'bits'], c[axis] = bits, value
    elif kind == 'object':
        c['target'] = r.ref()
    elif kind == 'trade':
        c['target'] = r.ref()
        length = r.u8()
        if not 1 <= length <= 40:
            raise Refused('ware_invalid')
        try:
            c['ware'] = r.take(length).decode('ascii')
        except UnicodeDecodeError:
            raise Refused('ware_invalid') from None
        if not WARE.fullmatch(c['ware']):
            raise Refused('ware_invalid')
        c['amount'], c['price'] = r.uint(4), r.uint(4)
        if c['amount'] > MAX_AMOUNT or (name == 'buy' and c['amount'] == 0):
            raise Refused('amount_invalid')
    elif kind == 'assign':
        c['target'] = r.ref(True)
        assignment, group = r.u8(), r.u8()
        if c['target'] is not None:
            if not 1 <= assignment <= len(ASSIGNMENTS):
                raise Refused('assignment_invalid')
            c['assignment'] = ASSIGNMENTS[assignment - 1]
        elif assignment or group:
            raise Refused('assignment_invalid')
        if group > MAX_GROUP:
            raise Refused('group_invalid')
        c['group'] = group
    return c


def decode(text, wire):
    """Decode one slot's blob delivered for wire slot `wire`; raises Refused(reason)."""
    if not isinstance(text, str) or not re.fullmatch(r'(?:[0-9a-f]{2})*', text):
        raise Refused('encoding')
    if len(text) > MAX_HEX:
        raise Refused('too_long')
    r = _Reader(bytes.fromhex(text))
    if r.u8() != CHANNEL:
        raise Refused('channel')
    if r.u8() != VERSION:
        raise Refused('version')
    blob = {'slot': r.u8(), 'tick': r.uint(4), 'seq': r.uint(2)}
    count = r.u8()
    if not 1 <= blob['slot'] <= MAX_SLOTS:
        raise Refused('slot_invalid')
    if blob['slot'] != wire:
        raise Refused('slot_mismatch')
    if blob['tick'] < 1:
        raise Refused('tick_invalid')
    if blob['seq'] < 1:
        raise Refused('seq_invalid')
    if not 1 <= count <= MAX_COMMANDS:
        raise Refused('count_invalid')
    blob['commands'] = [_command(r) for _ in range(count)]
    if r.pos != len(r.data):
        raise Refused('trailing')
    return blob


PLAN_KEYS = {'owners', 'ownership', 'delay', 'players', 'capture_ui', 'script', 'factions'}
COMMAND_KEYS = {'op', 'flags', 'subject', 'target', 'sector', 'x', 'y', 'z', 'xbits', 'ybits', 'zbits',
                'ware', 'amount', 'price', 'assignment', 'group'}


def lua_literal(value):
    """Exact Lua literal for plan data (strings are restricted to safe identifiers)."""
    if isinstance(value, bool):
        return 'true' if value else 'false'
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        if not math.isfinite(value):
            raise ValueError('non-finite plan number')
        return repr(value)
    if isinstance(value, str):
        if not re.fullmatch(r'[A-Za-z0-9_]*', value):
            raise ValueError(f'unsafe plan string {value!r}')
        return '"' + value + '"'
    if isinstance(value, list):
        return '{' + ','.join(lua_literal(v) for v in value) + '}'
    if isinstance(value, dict):
        return '{' + ','.join(f'[{lua_literal(k)}]={lua_literal(v)}' for k, v in sorted(value.items())) + '}'
    raise ValueError(f'unsupported plan value {value!r}')


def fixture_sector(path, saved, starts):
    """Saved ID of the sector component that contains imported component `saved`."""
    wanted = f'[0x{int(saved, 16):x}]'
    found = None
    for nodes in starts(path):
        element = nodes[-1]
        if element.tag == 'component' and element.get('id') == wanted:
            sectors = [n for n in nodes[:-1] if n.tag == 'component' and n.get('class') == 'sector']
            if found is not None or len(sectors) != 1 or not re.fullmatch(r'\[0x[0-9a-f]{1,16}\]', sectors[0].get('id', '')):
                raise ValueError('fixture actor must have exactly one saved sector ancestor')
            found = f'{int(sectors[0].get("id")[3:-1], 16):016x}'
    if found is None:
        raise ValueError('fixture actor not found in snapshot')
    return found


def overlay_config(plan, reference):
    """Validate a JSON plan and return the X4LAN_CONFIG Lua lines for the overlay.

    reference(text) resolves '@N' (fixture actor N) and '@sector:N' (its sector) to saved IDs;
    literal saved IDs are 16 lowercase hex digits. Every scripted command is encoded here, so a
    bad plan fails at build time rather than inside a running native session."""
    if not isinstance(plan, dict) or set(plan) - PLAN_KEYS:
        raise ValueError('player order plan keys: ' + ','.join(sorted(PLAN_KEYS)))

    def ref(value):
        if isinstance(value, str) and value.startswith('@'):
            value = reference(value)
        if not _saved(value):
            raise ValueError(f'invalid saved reference {value!r}')
        return value

    ownership = plan.get('ownership', 'table')
    delay = plan.get('delay', 3)
    players = plan.get('players')
    if ownership not in ('table', 'faction'):
        raise ValueError('ownership must be table or faction')
    if not _int(delay, 1, 64):
        raise ValueError('delay must be 1..64 ticks')
    if players is not None and not _int(players, 1, MAX_SLOTS):
        raise ValueError('players must be 1..16')
    owners = {}
    for key, slot in (plan.get('owners') or {}).items():
        saved = ref(key)
        if not _int(slot, 1, players or MAX_SLOTS) or saved in owners:
            raise ValueError(f'invalid or duplicate owner row {key!r}')
        owners[saved] = slot
    if ownership == 'table' and not owners:
        raise ValueError('table ownership needs at least one owner row')
    factions = plan.get('factions')
    if factions is not None:
        # Faction ownership: slot k's faction, x4lan_player<k> or (one slot at most) the vanilla player.
        if ownership != 'faction' or not isinstance(factions, list) or not 1 <= len(factions) <= (players or MAX_SLOTS) or (
                len(set(factions)) != len(factions) or any(f not in ('player', f'x4lan_player{k}') for k, f in enumerate(factions, 1))):
            raise ValueError('factions needs faction ownership and one faction per slot: player or x4lan_player<slot>')
    script = {}
    for slot_text, ticks in (plan.get('script') or {}).items():
        slot = int(slot_text)
        if str(slot) != str(slot_text) or not 1 <= slot <= (players or MAX_SLOTS) or not isinstance(ticks, dict):
            raise ValueError(f'invalid script slot {slot_text!r}')
        for tick_text, commands in ticks.items():
            tick = int(tick_text)
            if str(tick) != str(tick_text) or not 1 <= tick <= 18000 or not isinstance(commands, list):
                raise ValueError(f'invalid script tick {tick_text!r}')
            rows = []
            for command in commands:
                if not isinstance(command, dict) or set(command) - COMMAND_KEYS:
                    raise ValueError(f'invalid script command keys at tick {tick}')
                row = dict(command)
                for key in ('subject', 'target', 'sector'):
                    if key in row:
                        row[key] = ref(row[key])
                if row.get('target') is not None and row.get('target') == row.get('subject'):
                    raise ValueError(f'script command targets its own subject at slot {slot} tick {tick}')
                try:
                    encode_command(row)
                except Refused as error:
                    raise ValueError(f'script command refused at slot {slot} tick {tick}: {error}') from None
                rows.append(row)
            script.setdefault(slot, {})[tick] = rows
    lines = ['X4LAN_CONFIG.native_gate_player_orders = true',
             f'X4LAN_CONFIG.native_gate_player_orders_delay = {delay}',
             f'X4LAN_CONFIG.native_gate_player_orders_ownership = "{ownership}"',
             'X4LAN_CONFIG.native_gate_player_orders_owners = ' + lua_literal(owners)]
    if script:
        lines.append('X4LAN_CONFIG.native_gate_player_orders_script = ' + lua_literal(script))
    if players is not None:
        lines.append(f'X4LAN_CONFIG.native_gate_player_orders_players = {players}')
    if factions is not None:
        lines.append('X4LAN_CONFIG.native_gate_player_orders_factions = ' + lua_literal(factions))
    if plan.get('capture_ui'):
        lines.append('X4LAN_CONFIG.native_gate_player_orders_capture = true')
    return '\n'.join(lines) + '\n'


def applied_lines(text):
    """Machine-neutral PLAYER_ORDER_APPLIED payloads, in log order (log prefixes stripped)."""
    return [m.group(1) for m in map(APPLIED.search, text.splitlines()) if m]


def compare(texts):
    """Return (ok, details) for the ordered applied lines of several client logs."""
    lines = [applied_lines(t) for t in texts]
    reference = lines[0]
    for index, other in enumerate(lines[1:], 2):
        for row, (a, b) in enumerate(zip(reference, other), 1):
            if a != b:
                return False, f'client {index} differs at line {row}: {a!r} != {b!r}'
        if len(reference) != len(other):
            return False, f'client {index} has {len(other)} lines, client 1 has {len(reference)}'
    applied = sum(' result=applied ' in f'{line} ' for line in reference)
    return True, f'{len(reference)} identical lines on {len(texts)} clients, {applied} applied'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command', required=True)
    d = sub.add_parser('decode', help='decode one proposal blob')
    d.add_argument('hex')
    d.add_argument('--slot', type=int, required=True, help='wire slot the blob was delivered for')
    c = sub.add_parser('compare', help='compare PLAYER_ORDER_APPLIED lines across client logs')
    c.add_argument('logs', nargs='+')
    args = parser.parse_args(argv)
    if args.command == 'decode':
        try:
            print(json.dumps(decode(args.hex, args.slot), indent=1, sort_keys=True))
        except Refused as error:
            print('refused', error)
            return 1
        return 0
    texts = []
    for path in args.logs:
        with open(path, encoding='utf-8', errors='replace') as handle:
            texts.append(handle.read())
    ok, details = compare(texts)
    print(('PASS ' if ok else 'FAIL ') + details)
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
