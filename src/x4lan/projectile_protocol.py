"""Strict BOLT/BEND codec for observed projectile visuals (reports/PROJECTILE-VISUAL-PROTOCOL.txt).

INERT and unused: nothing imports this module yet. Decoding checks syntax and bounds only. It
never establishes a hit, damage, pilot ownership, authority epoch or a live HEALTH generation;
the relay and receiver must still check those against their own state. Every message is parsed
and serialized in an explicit direction: 'client' (client -> relay) or 'forwarded' (relay ->
other client, sourceLease right after the recipient lease, as in FIRE2). Numbers serialize as
'%.17g' (the same canonical text as projectile_protocol.lua), which reads back to the exact
same double.
"""

from __future__ import annotations

from dataclasses import dataclass
import math
import re

from .host import ATOM, HEX, MAX_PACKET, MAX_SEQ

DIRECTIONS = ('client', 'forwarded')
KINDS = {
    1: 'weapon_ter_s_plasma_01_mk1_macro',
    2: 'weapon_gen_s_laser_01_mk1_macro',
    3: 'weapon_gen_s_laser_01_mk2_macro',
}
MAX_TIME = 1e12
MAX_POSITION = 50000000.0
MAX_VELOCITY = 100000.0
MAX_ANGLE = 2 * math.pi
MAX_TTL = 3.0
_INTEGER = re.compile(r'[1-9][0-9]{0,9}\Z')
_NUMBER = re.compile(r'-?[0-9]+(?:\.[0-9]+)?(?:[eE][-+]?[0-9]+)?\Z')
_PRINTABLE = re.compile(rb'[\x21-\x7e]*\Z')


class ProtocolError(ValueError):
    pass


@dataclass(frozen=True)
class Bolt:
    """A projectile birth. `lease` is the sender's lease (client) or the recipient's (forwarded)."""

    direction: str
    lease: str
    source_lease: str | None
    entity: int
    epoch: int
    gen: int
    id: int
    time: float
    sector: str
    kind: int
    x: float
    y: float
    z: float
    vx: float
    vy: float
    vz: float
    yaw: float
    pitch: float
    roll: float
    ttl: float


@dataclass(frozen=True)
class End:
    """A projectile end under the same (sourceLease, entity, epoch, gen, id) identity."""

    direction: str
    lease: str
    source_lease: str | None
    entity: int
    epoch: int
    gen: int
    id: int
    time: float


BOLT_NUMBERS = (
    ('x', MAX_POSITION),
    ('y', MAX_POSITION),
    ('z', MAX_POSITION),
    ('vx', MAX_VELOCITY),
    ('vy', MAX_VELOCITY),
    ('vz', MAX_VELOCITY),
    ('yaw', MAX_ANGLE),
    ('pitch', MAX_ANGLE),
    ('roll', MAX_ANGLE),
)


def _fail(why):
    raise ProtocolError(why)


def _integer(text, name):
    if not _INTEGER.fullmatch(text) or not 1 <= int(text) <= MAX_SEQ:
        _fail('invalid ' + name)
    return int(text)


def _number(text, name, limit):
    if not _NUMBER.fullmatch(text):
        _fail('invalid ' + name)
    value = float(text)
    if not math.isfinite(value) or abs(value) > limit:
        _fail(name + ' out of bounds')
    return value


def _check(message):
    """Validates a message's fields; the same checks back decoding and serialization."""
    if message.direction not in DIRECTIONS:
        _fail('invalid direction')
    leases = (
        (message.lease, message.source_lease)
        if message.direction == 'forwarded'
        else (message.lease,)
    )
    if message.direction == 'client' and message.source_lease is not None:
        _fail('client form has no source lease')
    for lease in leases:
        if not isinstance(lease, str) or not HEX.fullmatch(lease):
            _fail('invalid lease')
    for name in ('entity', 'epoch', 'gen', 'id') + (('kind',) if isinstance(message, Bolt) else ()):
        value = getattr(message, name)
        if type(value) is not int or not 1 <= value <= MAX_SEQ:
            _fail('invalid ' + name)
    if message.entity not in (1, 2):
        _fail('invalid entity')
    floats = [('time', MAX_TIME)]
    if isinstance(message, Bolt):
        if message.kind not in KINDS:
            _fail('invalid kind')
        if not isinstance(message.sector, str) or not ATOM.fullmatch(message.sector):
            _fail('invalid sector')
        floats += list(BOLT_NUMBERS) + [('ttl', MAX_TTL)]
    for name, limit in floats:
        value = getattr(message, name)
        # Range first: an oversized int (10**400) compares exactly but overflows math.isfinite.
        if type(value) not in (int, float) or not -limit <= value <= limit:
            _fail(name + ' out of bounds')
    if message.time < 0:
        _fail('time out of bounds')
    if isinstance(message, Bolt) and not message.ttl > 0:
        _fail('ttl out of bounds')


def parse(raw, direction):
    """Decodes one BOLT/BEND packet of the stated direction; raises ProtocolError otherwise."""
    if direction not in DIRECTIONS:
        _fail('invalid direction')
    if isinstance(raw, str):
        if not raw.isascii():
            _fail('not ascii')
        raw = raw.encode('ascii')
    if not isinstance(raw, bytes) or len(raw) > MAX_PACKET or not _PRINTABLE.fullmatch(raw):
        _fail('invalid packet')
    fields = raw.decode('ascii').split('|')
    forwarded = direction == 'forwarded'
    head = 4 if forwarded else 3
    kind = fields[0]
    if len(fields) < 2 or fields[1] != '1' or kind not in ('BOLT', 'BEND'):
        _fail('unknown message')
    if len(fields) != (head + 17 if kind == 'BOLT' else head + 5):
        _fail('field count')
    common = dict(
        direction=direction,
        lease=fields[2],
        source_lease=fields[3] if forwarded else None,
        entity=_integer(fields[head], 'entity'),
        epoch=_integer(fields[head + 1], 'epoch'),
        gen=_integer(fields[head + 2], 'gen'),
        id=_integer(fields[head + 3], 'id'),
        time=_number(fields[head + 4], 'time', MAX_TIME),
    )
    if kind == 'BEND':
        message = End(**common)
    else:
        values = {
            name: _number(fields[head + 7 + i], name, limit)
            for i, (name, limit) in enumerate(BOLT_NUMBERS)
        }
        message = Bolt(
            **common,
            sector=fields[head + 5],
            kind=_integer(fields[head + 6], 'kind'),
            **values,
            ttl=_number(fields[head + 16], 'ttl', MAX_TTL),
        )
    _check(message)
    return message


def _text(value):
    return '%.17g' % value


def serialize(message):
    """Encodes a validated Bolt/End in its own direction; raises ProtocolError otherwise."""
    if not isinstance(message, (Bolt, End)):
        _fail('not a projectile message')
    _check(message)
    fields = ['BOLT' if isinstance(message, Bolt) else 'BEND', '1', message.lease]
    if message.direction == 'forwarded':
        fields.append(message.source_lease)
    fields += [
        str(message.entity),
        str(message.epoch),
        str(message.gen),
        str(message.id),
        _text(message.time),
    ]
    if isinstance(message, Bolt):
        fields += (
            [message.sector, str(message.kind)]
            + [_text(getattr(message, n)) for n, _ in BOLT_NUMBERS]
            + [_text(message.ttl)]
        )
    raw = '|'.join(fields).encode('ascii')
    if len(raw) > MAX_PACKET:
        _fail('packet too long')
    return raw
