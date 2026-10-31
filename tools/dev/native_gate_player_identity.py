"""Lockstep player identity: Python reference for the session plan (format v1) and log comparison.

Independent implementation of tools/dev/native_gate_player_identity_core.lua (tests cross-check
validation reasons, canonical text and step heads). See reports/LOCKSTEP-PLAYER-IDENTITY-20261029.txt.

    python tools/dev/native_gate_player_identity.py plan --players 3 --mode factions
    python tools/dev/native_gate_player_identity.py compare client1.log client2.log client3.log
"""
import argparse
import hashlib
import json
import re
import sys

VERSION = 1
MAX_SLOTS = 16
TICK_MIN, TICK_MAX = 2, 29
FOLD_LAG = 10
COORD_LIMIT = 10_000_000
SEED_MAX = 2147483647
MAX_STARTS = 16
RELATIONS = {'allied': 0.75, 'neutral': 0.0, 'hostile': -0.5}
RELATION_TEXT = {'allied': '0.75', 'neutral': '0', 'hostile': '-0.5'}   # Lua tostring of the value
MODES = ('factions', 'coop')
OCCUPIED = ('player', 'pool')
REPUTATION = ('mirror', 'keep')
PLAN_KEYS = {'version', 'mode', 'tick', 'occupied', 'reputation', 'slots', 'relations', 'starts'}
SLOT_KEYS = {'ship', 'create', 'faction'}
CREATE_KEYS = {'macro', 'seed', 'sector', 'x', 'y', 'z', 'yaw'}
RELATION_KEYS = {'a', 'b', 'relation'}
START_KEYS = {'object', 'sector', 'x', 'y', 'z', 'yaw'}
SAVED = re.compile(r'[0-9a-f]{16}')
MACRO = re.compile(r'[a-z][a-z0-9_]*_macro')
APPLIED = re.compile(r'PLAYER_IDENTITY_APPLIED (tick=\d+ ordinal=\d+ .*?)\s*$')
# Glue before the digest fix (live run pf3) logged the canonical plan text instead of its SHA-256.
RAW_PLAN = re.compile(r'plan=(PIDENT_PLAN_1\|\S+)')
READY = re.compile(r'PLAYER_IDENTITY READY tick=(\d+) steps=(\d+) md=(\S+)')
# Defaults: snapshot capture 005, all in the same zone of Argon Prime (sector saved ID 0x729).
PLAYER_SHIP = '00000000000ae79c'                     # the player's Argon scout, occupied on every client
DEFAULT_SHIPS = (PLAYER_SHIP, '0000000000176ad5', '0000000000176b36', '0000000000176b97')  # + fixture actors 3, 4, 5
# Stock slots with a working first-person cockpit: Argon scouts, the macro of the player's own ship
# (ship_arg_s_scout_01_a_macro; the fixture fighters show one flat panel, live st-C1). Two primary guns each,
# none near the player in the save, so MD moves them to STOCK_STARTS at the setup tick. Live st-C2 flew the first.
STOCK_SHIPS = (PLAYER_SHIP, '000000000007a8fe', '000000000007a8dc', '000000000007a931')
STOCK_SECTOR = '0000000000000729'    # Argon Prime, slot 1's sector; slot 1 starts at 0,100000,0 facing +z
STOCK_STARTS = {2: (0, 100000, 2000, 180), 3: (-1500, 100000, 1000, 90), 4: (1500, 100000, 1000, -90)}  # x, y, z, yaw: facing the middle
# Where the default ships sit in snapshot 005 before any start (PLAYER_IDENTITY_APPLIED from=, live p4-C), and the sector's two names.
STOCK_SECTOR_MACRO = 'cluster_14_sector001_macro'
SECTOR_ALIASES = {STOCK_SECTOR_MACRO: STOCK_SECTOR}
SHIP_SECTORS = dict({ship: STOCK_SECTOR for ship in DEFAULT_SHIPS},
                    **{'000000000007a8fe': 'cluster_408_sector002_macro', '000000000007a8dc': 'cluster_04_sector002_macro',
                       '000000000007a931': 'cluster_112_sector002_macro'})


class Refused(ValueError):
    """A strict plan refusal; str(error) is the reason token shared with the Lua core."""


def saved(value):
    return isinstance(value, str) and SAVED.fullmatch(value) is not None and value != '0' * 16


def _int(value, low, high):
    return isinstance(value, int) and not isinstance(value, bool) and low <= value <= high


def _keys(value, allowed, reason):
    if not isinstance(value, dict):
        raise Refused(reason)
    if set(value) - allowed:
        raise Refused('key_unknown')


def pool(slot):
    return f'x4lan_player{slot}'


def validate(plan):
    """Normalized plan or Refused(reason); the same rules and reasons as K.validate in Lua."""
    _keys(plan, PLAN_KEYS, 'plan_invalid')
    if plan.get('version') != VERSION or isinstance(plan.get('version'), bool):
        raise Refused('version')
    if plan.get('mode') not in MODES:
        raise Refused('mode')
    if not _int(plan.get('tick'), TICK_MIN, TICK_MAX):
        raise Refused('tick')
    occupied = plan.get('occupied', 'player')
    reputation = plan.get('reputation', 'mirror')
    if occupied not in OCCUPIED:
        raise Refused('occupied')
    if reputation not in REPUTATION:
        raise Refused('reputation')
    slots = plan.get('slots')
    if not isinstance(slots, list) or not 1 <= len(slots) <= MAX_SLOTS:
        raise Refused('slots')
    P = {'mode': plan['mode'], 'tick': plan['tick'], 'occupied': occupied, 'reputation': reputation, 'slots': [], 'relations': []}
    ships, player_slots = set(), 0
    for k, s in enumerate(slots, 1):
        _keys(s, SLOT_KEYS, 'slot_invalid')
        row = {'index': k, 'faction': s.get('faction')}
        if ('ship' in s) == ('create' in s):
            raise Refused('slot_source')
        if 'ship' in s:
            if not saved(s['ship']):
                raise Refused('ship_invalid')
            if s['ship'] in ships:
                raise Refused('ship_duplicate')
            ships.add(s['ship'])
            row['ship'] = s['ship']
        else:
            c = s['create']
            _keys(c, CREATE_KEYS, 'create_invalid')
            if not isinstance(c.get('macro'), str) or len(c['macro']) > 64 or not MACRO.fullmatch(c['macro']):
                raise Refused('macro_invalid')
            if not _int(c.get('seed'), 0, SEED_MAX):
                raise Refused('seed_invalid')
            sector = c.get('sector', 'player')
            if sector != 'player' and not saved(sector):
                raise Refused('sector_invalid')
            for axis in 'xyz':
                if not _int(c.get(axis), -COORD_LIMIT, COORD_LIMIT):
                    raise Refused('coordinate_invalid')
            yaw = c.get('yaw', 0)
            if not _int(yaw, -180, 180):
                raise Refused('rotation_invalid')
            row['create'] = {'macro': c['macro'], 'seed': c['seed'], 'sector': sector, 'x': c['x'], 'y': c['y'], 'z': c['z'], 'yaw': yaw}
        if plan['mode'] == 'coop':
            if s.get('faction') != 'player':
                raise Refused('faction_mode')
        else:
            if s.get('faction') not in ('player', pool(k)):
                raise Refused('faction_invalid')
            player_slots += s['faction'] == 'player'
        P['slots'].append(row)
    if player_slots > 1:
        raise Refused('faction_duplicate')
    relations = plan.get('relations', [])
    if not isinstance(relations, list):
        raise Refused('relations')
    n = len(slots)
    if plan['mode'] == 'coop':
        if relations:
            raise Refused('relations_mode')
    else:
        seen = set()
        for r in relations:
            _keys(r, RELATION_KEYS, 'relation_invalid')
            if not _int(r.get('a'), 1, n) or not _int(r.get('b'), 1, n) or r['a'] >= r['b']:
                raise Refused('relation_pair')
            if r.get('relation') not in RELATIONS:
                raise Refused('relation_invalid')
            if (r['a'], r['b']) in seen:
                raise Refused('relation_duplicate')
            seen.add((r['a'], r['b']))
            P['relations'].append({'a': r['a'], 'b': r['b'], 'relation': r['relation']})
        if len(relations) != n * (n - 1) // 2:
            raise Refused('relation_missing')
        P['relations'].sort(key=lambda r: (r['a'], r['b']))
    # Different starts: saved ships warped to a fixed sector-local point after ownership, in list order;
    # the sector is a saved ID or a sector macro name.
    starts = plan.get('starts', [])
    if not isinstance(starts, list) or len(starts) > MAX_STARTS:
        raise Refused('starts')
    P['starts'], placed = [], set()
    for r in starts:
        _keys(r, START_KEYS, 'start_invalid')
        if not saved(r.get('object')):
            raise Refused('start_object_invalid')
        if r['object'] in placed:
            raise Refused('start_duplicate')
        placed.add(r['object'])
        sector = r.get('sector')
        if not saved(sector) and not (isinstance(sector, str) and len(sector) <= 64 and MACRO.fullmatch(sector)):
            raise Refused('sector_invalid')
        for axis in 'xyz':
            if not _int(r.get(axis), -COORD_LIMIT, COORD_LIMIT):
                raise Refused('coordinate_invalid')
        yaw = r.get('yaw', 0)
        if not _int(yaw, -180, 180):
            raise Refused('rotation_invalid')
        P['starts'].append({'object': r['object'], 'sector': r['sector'], 'x': r['x'], 'y': r['y'], 'z': r['z'], 'yaw': yaw})
    return P


def _create_label(c):
    return f"{c['macro']}/{c['seed']}/{c['sector']}/{c['x']},{c['y']},{c['z']}/{c['yaw']}"


def ship_label(row):
    return row['ship'] if 'ship' in row else 'create:' + _create_label(row['create'])


def _start_label(r):
    return f"{r['object']}@{r['sector']}/{r['x']},{r['y']},{r['z']}/{r['yaw']}"


def canonical(P):
    rows = [f"{r['index']}:ship:{r['ship']}:{r['faction']}" if 'ship' in r else
            f"{r['index']}:create:{_create_label(r['create'])}:{r['faction']}" for r in P['slots']]
    relations = ','.join(f"{r['a']}-{r['b']}:{r['relation']}" for r in P['relations']) or '-'
    starts = ','.join(_start_label(r) for r in P['starts'])   # plans without starts keep their earlier text
    return (f"PIDENT_PLAN_1|mode={P['mode']}|tick={P['tick']}|occupied={P['occupied']}|reputation={P['reputation']}"
            f"|slots={','.join(rows)}|relations={relations}" + (f'|starts={starts}' if starts else ''))


def digest(P):
    return hashlib.sha256(canonical(P).encode()).hexdigest()


def steps(P, plan_digest=None):
    """Ordered setup steps as (step, slot, head) dicts; heads equal the Lua K.steps heads."""
    plan_digest = plan_digest or digest(P)
    out = []
    factions = P['mode'] == 'factions'

    def add(step, slot, text):
        out.append({'step': step, 'slot': slot, 'text': text})
    add('begin', 0, None)
    for row in P['slots']:
        if factions and row['faction'] != 'player':
            add('faction', row['index'], f"faction={row['faction']} mirror={1 if P['reputation'] == 'mirror' else 0}")
    for row in P['slots']:
        add('ship', row['index'], f"ship={ship_label(row)} source={'saved' if 'ship' in row else 'create'} faction={row['faction']}")
    for i, r in enumerate(P['starts'], 1):
        add('start', 0, f"start={i} object={r['object']} sector={r['sector']} at={r['x']},{r['y']},{r['z']}/{r['yaw']}")
    for r in P['relations']:
        fa, fb = P['slots'][r['a'] - 1]['faction'], P['slots'][r['b'] - 1]['faction']
        add('relation', 0, f"pair={r['a']}-{r['b']} factions={fa}/{fb} relation={r['relation']} value={RELATION_TEXT[r['relation']]}")
    add('end', 0, f'steps={len(out) + 1}')
    out[0]['text'] = (f"mode={P['mode']} slots={len(P['slots'])} occupied={P['occupied']} reputation={P['reputation']} "
                      f"steps={len(out)} plan={plan_digest}")
    for ordinal, step in enumerate(out, 1):
        step['ordinal'] = ordinal
        step['head'] = f"tick={P['tick']} ordinal={ordinal} step={step['step']} slot={step['slot']} {step['text']}"
    return out


def relations_for(players, default='neutral', overrides=None):
    """Complete pair list for a factions plan; overrides maps (a, b) -> relation name."""
    overrides = dict(overrides or {})
    rows = []
    for a in range(1, players + 1):
        for b in range(a + 1, players + 1):
            rows.append({'a': a, 'b': b, 'relation': overrides.pop((a, b), default)})
    if overrides:
        raise ValueError(f'relation override for unknown pair {sorted(overrides)[0]}')
    return rows


def build_plan(players, mode, ships=None, relation='neutral', overrides=None, occupied_ship=PLAYER_SHIP, occupied='player',
               reputation='mirror', tick=20, creates=None, starts=None):
    """Session plan for N slots. ships: saved IDs per slot (default DEFAULT_SHIPS); creates maps slot ->
    create spec (replaces that slot's saved ship). In factions mode slot k owns x4lan_player<k>, except the
    slot flying the occupied player ship, which keeps the vanilla player faction unless occupied='pool'.
    starts: list of {object, sector, x, y, z[, yaw]} placements (saved ship ID; saved sector ID or sector macro)."""
    ships = list(ships or DEFAULT_SHIPS[:players])
    creates = dict(creates or {})
    if mode == 'coop' and overrides:
        raise ValueError('coop has one faction: no relations between slots')
    if len(ships) != players:
        raise ValueError(f'{players} players need {players} ships, got {len(ships)}')
    slots = []
    for k in range(1, players + 1):
        row = {'create': creates.pop(k)} if k in creates else {'ship': ships[k - 1]}
        if mode == 'coop':
            row['faction'] = 'player'
        else:
            row['faction'] = 'player' if row.get('ship') == occupied_ship and occupied == 'player' else pool(k)
        slots.append(row)
    if creates:
        raise ValueError(f'create spec for unknown slot {sorted(creates)[0]}')
    plan = {'version': VERSION, 'mode': mode, 'tick': tick, 'occupied': occupied, 'reputation': reputation, 'slots': slots}
    if mode == 'factions':
        plan['relations'] = relations_for(players, relation, overrides)
    if starts:
        plan['starts'] = [dict(s) for s in starts]
    try:
        validate(plan)
    except Refused as error:
        raise ValueError(f'player identity plan refused: {error}') from None
    return plan


def lua_literal(value):
    """Exact Lua literal for plan data (strings restricted to safe identifiers)."""
    if isinstance(value, bool):
        return 'true' if value else 'false'
    if isinstance(value, int):
        return str(value)
    if isinstance(value, str):
        if not re.fullmatch(r'[A-Za-z0-9_]*', value):
            raise ValueError(f'unsafe plan string {value!r}')
        return '"' + value + '"'
    if isinstance(value, list):
        return '{' + ','.join(lua_literal(v) for v in value) + '}'
    if isinstance(value, dict):
        return '{' + ','.join(f'[{lua_literal(k)}]={lua_literal(v)}' for k, v in sorted(value.items())) + '}'
    raise ValueError(f'unsupported plan value {value!r}')


def config_lines(plan, block_orders=False):
    """X4LAN_CONFIG lines that arm the identity glue with this (validated) plan."""
    validate(plan)
    lines = ['X4LAN_CONFIG.native_gate_player_identity = true',
             'X4LAN_CONFIG.native_gate_player_identity_plan = ' + lua_literal(plan)]
    if block_orders:
        lines.append('X4LAN_CONFIG.native_gate_player_identity_block_orders = true')
    return lines


def expected_heads(plan):
    P = validate(plan)
    return [s['head'] for s in steps(P)]


def applied_lines(text):
    """Machine-neutral PLAYER_IDENTITY_APPLIED payloads, in log order. Log prefixes (the engine's
    '*** Context:md...: ' and '[X4LAN] ') are stripped; a raw canonical plan is replaced by its digest."""
    def normalize(line):
        return RAW_PLAN.sub(lambda m: 'plan=' + hashlib.sha256(m.group(1).encode()).hexdigest(), line)
    return [normalize(m.group(1)) for m in map(APPLIED.search, text.splitlines()) if m]


def compare(texts, plan=None):
    """(ok, details): identical applied lines on every client, every step applied or unchanged,
    one equal READY digest per client, and (with plan) exactly the planned step heads."""
    lines = [applied_lines(t) for t in texts]
    reference = lines[0]
    for index, other in enumerate(lines[1:], 2):
        for row, (a, b) in enumerate(zip(reference, other), 1):
            if a != b:
                return False, f'client {index} differs at line {row}: {a!r} != {b!r}'
        if len(reference) != len(other):
            return False, f'client {index} has {len(other)} lines, client 1 has {len(reference)}'
    if not reference:
        return False, 'no PLAYER_IDENTITY_APPLIED lines'
    bad = [line for line in reference if not re.search(r' result=(applied|unchanged) ', line + ' ')]
    if bad:
        return False, f'refused step: {bad[0]}'
    if plan is not None:
        heads = expected_heads(plan)
        got = [line.split(' result=')[0] for line in reference]
        for row, (a, b) in enumerate(zip(got, heads), 1):
            if a != b:
                return False, f'step {row} differs from the plan: {a!r} != {b!r}'
        if len(got) != len(heads):
            return False, f'{len(got)} steps logged, the plan has {len(heads)}'
    ready = [READY.findall(t) for t in texts]
    if any(len(r) != 1 for r in ready) or len({r[0] for r in ready}) != 1:
        return False, f'READY lines differ or missing: {ready}'
    return True, f'{len(reference)} identical steps on {len(texts)} clients, md={ready[0][0][2]}'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('plan', help='print a default session plan, its canonical text and digest')
    p.add_argument('--players', type=int, default=2)
    p.add_argument('--mode', choices=MODES, default='factions')
    p.add_argument('--relations', choices=sorted(RELATIONS), default='neutral')
    c = sub.add_parser('compare', help='compare PLAYER_IDENTITY_APPLIED lines across client logs')
    c.add_argument('logs', nargs='+')
    c.add_argument('--plan', help='plan JSON: also require exactly its steps')
    args = parser.parse_args(argv)
    if args.command == 'plan':
        plan = build_plan(args.players, args.mode, relation=args.relations)
        P = validate(plan)
        print(json.dumps(plan, indent=1, sort_keys=True))
        print(canonical(P))
        print('digest', digest(P))
        for step in steps(P):
            print(step['head'])
        return 0
    texts = []
    for path in args.logs:
        with open(path, encoding='utf-8', errors='replace') as handle:
            texts.append(handle.read())
    plan = json.loads(open(args.plan, encoding='utf-8').read()) if args.plan else None
    ok, details = compare(texts, plan)
    print(('PASS ' if ok else 'FAIL ') + details)
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
