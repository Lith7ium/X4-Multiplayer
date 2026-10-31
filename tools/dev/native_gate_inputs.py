"""Protocol 3 input container: Python reference of the blob multiplexing and hit claim payload.

Independent implementation of input_mux_decode/encode and hit_payload_decode/encode in
src/native/engine/lockstep_hit_claims.h; tests cross-check it against the Lua mirror
(tools/dev/native_gate_inputs_core.lua) and, opt-in, against the C functions themselves.
Report: reports/LOCKSTEP-INPUT-MUX-20261029.txt.

    python tools/dev/native_gate_inputs.py decode 4d0101...
    python tools/dev/native_gate_inputs.py encode --pilot 01... --orders 50...
"""
import argparse
import json
import re
import sys

TAG, VERSION = 0x4d, 1
MAX_BYTES = 240
PILOT_BYTES, PILOT_VERSION = 28, 1
HIT_BYTES, HIT_VERSION, HITS_MAX, HIT_WINDOW = 26, 1, 8, 8
INCOMING, OUTGOING = 1, 2
KIND = {'pilot': 0x01, 'hits': 0x48, 'octree': 0x4f, 'orders': 0x50, 'stock': 0x53, 'ui': 0x55}
SECTIONS = ('pilot', 'hits', 'octree', 'orders', 'stock', 'ui')     # wire order
PRIORITY = ('stock', 'ui', 'pilot', 'orders', 'hits', 'octree')     # default local packing order (stock first; the octree report waits)
STOCK_VERSION = 3                                             # lockstep_pilot_stock.h record version byte
UI_VERSION = 1                                                # lockstep_ui_replay.h section version byte
OCTREE_BYTES = 20                                     # lockstep_nav_octree.h 'agreed' report
HEX = re.compile(r'(?:[0-9a-f]{2})*')
ZERO = '0' * 16


class Refused(ValueError):
    """A strict refusal; str(error) is the reason token shared with the Lua mirror."""


def _bytes(text):
    if not isinstance(text, str) or not HEX.fullmatch(text):
        raise Refused('encoding')
    return bytes.fromhex(text)


def decode(text):
    """Blob hex -> {section: payload hex} (absent sections omitted)."""
    b = _bytes(text)
    n = len(b)
    if n > MAX_BYTES:
        raise Refused('too_long')
    if n == 0:
        return {}
    if b[0] == KIND['pilot']:
        if n != PILOT_BYTES:
            raise Refused('pilot_length')
        return {'pilot': text}
    if b[0] == KIND['orders']:
        return {'orders': text}
    if b[0] != TAG:
        raise Refused('channel')
    if n < 2 or b[1] != VERSION:
        raise Refused('version')
    out, last, at = {}, 0, 2
    while at < n:
        if n - at < 2:
            raise Refused('truncated')
        kind, length = b[at], b[at + 1]
        at += 2
        if kind <= last:
            raise Refused('order')
        if not length or length > n - at:
            raise Refused('section_length')
        payload = b[at:at + length]
        if kind == KIND['pilot']:
            if length != PILOT_BYTES or payload[0] != PILOT_VERSION:
                raise Refused('pilot_length')
            out['pilot'] = payload.hex()
        elif kind == KIND['hits']:
            out['hits'] = payload.hex()
        elif kind == KIND['octree']:
            if length != OCTREE_BYTES:
                raise Refused('octree_length')
            out['octree'] = payload.hex()
        elif kind == KIND['orders']:
            if payload[0] != KIND['orders']:
                raise Refused('orders_channel')
            out['orders'] = payload.hex()
        elif kind == KIND['stock']:
            if payload[0] != STOCK_VERSION:
                raise Refused('stock_version')
            out['stock'] = payload.hex()
        elif kind == KIND['ui']:
            if payload[0] != UI_VERSION:
                raise Refused('ui_version')
            out['ui'] = payload.hex()
        else:
            raise Refused('kind')
        last = kind
        at += length
    if not out or (len(out) == 1 and 'hits' not in out and 'octree' not in out and 'stock' not in out and 'ui' not in out):
        raise Refused('noncanonical')
    return out


def check_section(name, text):
    b = _bytes(text)
    if not b or len(b) > 255:
        raise Refused('section_length')
    if name == 'pilot' and (len(b) != PILOT_BYTES or b[0] != PILOT_VERSION):
        raise Refused('pilot_length')
    if name == 'orders' and b[0] != KIND['orders']:
        raise Refused('orders_channel')
    if name == 'hits':
        decode_hits(text)
    if name == 'octree' and len(b) != OCTREE_BYTES:
        raise Refused('octree_length')
    if name == 'stock' and b[0] != STOCK_VERSION:
        raise Refused('stock_version')
    if name == 'ui' and b[0] != UI_VERSION:
        raise Refused('ui_version')
    if name not in KIND:
        raise Refused('kind')


def encode(sections):
    """{section: payload hex} -> canonical blob hex ('' when empty)."""
    for name, text in sections.items():
        if name not in KIND:
            raise Refused('kind')
        check_section(name, text)
    if not sections:
        return ''
    if len(sections) == 1 and 'hits' not in sections and 'octree' not in sections and 'stock' not in sections and 'ui' not in sections:
        text = sections.get('pilot') or sections['orders']
        if len(text) > 2 * MAX_BYTES:
            raise Refused('too_long')
        return text
    out = bytearray([TAG, VERSION])
    for name in SECTIONS:
        if name in sections:
            payload = bytes.fromhex(sections[name])
            out += bytes([KIND[name], len(payload)]) + payload
            if len(out) > MAX_BYTES:
                raise Refused('too_long')
    return out.hex()


def _id(value):
    if not isinstance(value, str) or not re.fullmatch(r'[0-9a-f]{16}', value) or value == ZERO:
        raise Refused('identity')
    return int(value, 16)


def encode_hits(claims):
    """[{kind, observe16, projectile, target, point:[x,y,z]}] -> payload hex (count + 26-byte claims, LE)."""
    if not isinstance(claims, list) or not 1 <= len(claims) <= HITS_MAX:
        raise Refused('count')
    out, seen = bytearray([len(claims)]), set()
    for c in claims:
        if c.get('kind') not in (INCOMING, OUTGOING) or not isinstance(c.get('observe16'), int) or not 0 <= c['observe16'] <= 65535:
            raise Refused('claim')
        projectile, target = _id(c.get('projectile')), _id(c.get('target'))
        if projectile in seen:
            raise Refused('duplicate')
        seen.add(projectile)
        point = c.get('point')
        if not isinstance(point, (list, tuple)) or len(point) != 3 or any(not isinstance(v, int) or not -32767 <= v <= 32767 for v in point):
            raise Refused('point')
        out += bytes([HIT_VERSION << 4 | c['kind'], 0]) + c['observe16'].to_bytes(2, 'little')
        out += projectile.to_bytes(8, 'little') + target.to_bytes(8, 'little')
        for v in point:
            out += (v & 0xffff).to_bytes(2, 'little')
    return out.hex()


def decode_hits(text):
    b = _bytes(text)
    if len(b) < 1 or not b[0] or b[0] > HITS_MAX or len(b) != 1 + b[0] * HIT_BYTES:
        raise Refused('length')
    claims, seen = [], set()
    for i in range(b[0]):
        c = b[1 + i * HIT_BYTES:1 + (i + 1) * HIT_BYTES]
        if c[0] >> 4 != HIT_VERSION or c[0] & 15 not in (INCOMING, OUTGOING) or c[1]:
            raise Refused('claim')
        point = []
        for k in range(3):
            v = int.from_bytes(c[20 + 2 * k:22 + 2 * k], 'little')
            if v == 0x8000:
                raise Refused('point')
            point.append(v - 0x10000 if v >= 0x8000 else v)
        projectile, target = int.from_bytes(c[4:12], 'little'), int.from_bytes(c[12:20], 'little')
        if not projectile or not target:
            raise Refused('identity')
        if projectile in seen:
            raise Refused('duplicate')
        seen.add(projectile)
        claims.append({'kind': c[0] & 15, 'observe16': int.from_bytes(c[2:4], 'little'), 'projectile': f'{projectile:016x}',
                       'target': f'{target:016x}', 'point': point})
    return claims


def hit_observe(observe16, r):
    """Observe tick for a blob admitted at r (hit_stage), or Refused('far')."""
    if r < 2:
        raise Refused('far')   # hit_stage admits from tick 2
    back = ((r - 1) - observe16) % 65536
    if back > HIT_WINDOW or back > r - 1:
        raise Refused('far')
    return r - 1 - back


def pack(proposals, priority=PRIORITY):
    """The local proposal rule: sections in priority order, each within the bytes left after the
    container header and earlier sections (2 bytes per section header). Returns (blob, refused names)."""
    chosen, refused, used = {}, [], 2
    for name in priority:
        text = proposals.get(name)
        if not text:
            continue
        check_section(name, text)
        if len(text) // 2 > MAX_BYTES - used - 2:
            refused.append(name)
            continue
        chosen[name] = text
        used += 2 + len(text) // 2
    return encode(chosen), refused


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command', required=True)
    d = sub.add_parser('decode', help='decode one admitted blob')
    d.add_argument('hex')
    e = sub.add_parser('encode', help='encode sections (payload hex) into the canonical blob')
    for name in SECTIONS:
        e.add_argument('--' + name)
    args = parser.parse_args(argv)
    try:
        if args.command == 'decode':
            sections = decode(args.hex)
            if 'hits' in sections:
                sections['hit_claims'] = decode_hits(sections['hits'])
            print(json.dumps(sections, indent=1, sort_keys=True))
        else:
            print(encode({name: getattr(args, name) for name in SECTIONS if getattr(args, name)}))
    except Refused as error:
        print('refused', error)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
