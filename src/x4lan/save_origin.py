"""Bounded native save header readback; not validation of the whole saved game."""

import gzip
import re
import uuid
import xml.etree.ElementTree as ET
from . import seed

MAX_HEADER_BYTES = 1024 * 1024


def read(snapshot, format):
    if format not in ('x4-xml', 'x4-xml-gzip'):
        raise seed.SeedError('unsupported native save format')
    opener = gzip.open if format == 'x4-xml-gzip' else open
    parser = ET.XMLPullParser(events=('start', 'end'))
    path = []
    raw = b''
    try:
        with opener(snapshot, 'rb') as stream:
            while len(raw) < MAX_HEADER_BYTES:
                block = stream.read(min(4096, MAX_HEADER_BYTES - len(raw)))
                if not block:
                    break
                if b'\0' in block:
                    raise seed.SeedError('native save header must use UTF-8 XML')
                raw += block
                if b'<!DOCTYPE' in raw.upper() or b'<!ENTITY' in raw.upper():
                    raise seed.SeedError('native save header declarations are not supported')
                parser.feed(block)
                for event, node in parser.read_events():
                    if event == 'end':
                        path.pop()
                        continue
                    path.append(node.tag)
                    if len(path) == 1 and node.tag != 'savegame':
                        raise seed.SeedError('not a native X4 save')
                    if path != ['savegame', 'info', 'game']:
                        continue
                    attributes = node.attrib
                    if attributes.get('id') != 'X4' or not re.fullmatch(
                        r'[1-9][0-9]{0,9}', attributes.get('seed', '')
                    ):
                        raise seed.SeedError('invalid native world generation seed')
                    number = int(attributes['seed'])
                    if not 1 <= number <= 4294967295:
                        raise seed.SeedError('native seed outside supported bounds')
                    for field in ('start', 'version', 'build'):
                        if not seed.ATOM.fullmatch(attributes.get(field, '')):
                            raise seed.SeedError('invalid native game header')
                    guid = str(uuid.UUID(attributes['guid']))
                    if guid != attributes['guid'].lower():
                        raise seed.SeedError('noncanonical native world GUID')
                    return dict(
                        game_guid=guid,
                        generation_seed=number,
                        gamestart=attributes['start'],
                        game_version=attributes['version'],
                        game_build=attributes['build'],
                    )
    except (OSError, EOFError, ValueError, KeyError, ET.ParseError) as exc:
        if isinstance(exc, seed.SeedError):
            raise
        raise seed.SeedError('unreadable native save header') from exc
    raise seed.SeedError('native game header absent or exceeds bound')
