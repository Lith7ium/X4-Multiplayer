"""N-player lockstep session builder: player factions or coop, one ship per player (no installation, no launch).

Sibling of the two-player sandbox (tools/dev/native_gate_sandbox.py, docs/LOCKSTEP-SANDBOX.txt); it reuses
the sandbox's base-overlay checks and adds the player identity plan
(tools/dev/native_gate_player_identity*.{lua,xml,py}, reports/LOCKSTEP-PLAYER-IDENTITY-20261029.txt):

  --mode factions  slot k's ship is owned by x4lan_player<k>; the slot flying the occupied player ship keeps
                   the vanilla `player` faction (--occupied pool moves it too); relations between the slot
                   factions come from --relations / --relation A-B=NAME
  --mode coop      every slot ship is owned by `player` (one faction, different ships)

  --source wire    humans: each client samples its device and flies its own slot ship (pilot by wire)
  --source script  no humans: every slot ship follows the deterministic pilot script on every client
  --source stock   every slot flies through X4's own player control path on every client, recorded from
                   its own client's stock input (lockstep_pilot_stock.h, lockstep_pilot_slots.h; default
                   window 40..640 scripted, 40..72000 for people); slot ships default to Argon scouts with a
                   working cockpit, moved near slot 1 at the setup tick (identity.STOCK_SHIPS/STOCK_STARTS,
                   --ships replaces them, --start replaces one slot's place); --stock-script demo flies them without humans (demo,
                   demo2..demo4 per slot), --present makes client k's stock camera, HUD and UI present slot
                   k's ship (lockstep_pilot_present.h), --fire record lets each slot fire its primary guns
                   (lockstep_pilot_fire.h: straight ahead), --fire script on a fixed schedule, --ui-replay turns stock UI
                   actions into events of the slot (lockstep_ui_replay.h: travel, soft target, orders; needs --present)
  --source none    identity only (no piloting); allows ships created by MD (--create)
  --orders demo    scripted orders check the ownership table (default with --source script); they share the
                   input blob with the pilot records through the input multiplexer, so wire works too
  --start K=SECTOR@X,Y,Z[/YAW]
                   different starts: after ownership, MD moves slot K's ship (or a saved ship ID in place of K)
                   to a fixed sector-local point of the saved sector; --start-counterparts also moves each
                   moved fixture actor's battle counterpart 1500 m further along z, so its Attack order (and
                   with it the Steering solver a pilot slot needs) stays local. SECTOR is a sector macro
                   (e.g. cluster_01_sector001_macro; any sector, known or not) or a saved sector ID of a
                   sector the player knows (snapshot 005: 0000000000000729 Argon Prime, the start)
  --third-sector   slots may start in more sectors than are covered. Without census keys, physics scenes,
                   camera-centred traffic and highway parameters answer for two sectors (lockstep_region_camera.h):
                   slot 1's and the first other one in slot order. --set native_gate_view_scenes=true with
                   --set native_gate_viewpoint_standins=N (needs native_gate_viewpoint_policy) covers N sectors, up to
                   four; a scene a slot leaves serves the next sector once its bodies are gone, VSC_GRACE (600) ticks
                   after the release at the latest (lockstep_view_scenes.h). A slot beyond the covered sectors has none
                   of them on any client, so it is refused unless this asks for it; keys in the base overlay are not
                   seen here, so pass this for those. A ship without a --start that the builder does not know gets a
                   warning; in a run, VIEW_POINTS_UNCOVERED lines and REGION_CAMERA uncovered= show what happened.

Defaults (snapshot capture 005, one zone of Argon Prime): slot 1 = 00000000000ae79c (the player's scout,
occupied on every client), slots 2..4 = fixture actors 3..5 (0000000000176ad5, 0000000000176b36,
0000000000176b97: Argon S fighters at zone offset -395/0/+700..900 m from slot 1). --battle engage
(default, as in the sandbox) gives them their scripted Attack orders: a waiting fixture has no Steering
solver, so calm slots 2..N cannot be flown (live pf3/pc3: s2/s3 applied 0, no_solver).

  --tick187 observe|canonical, --fp default|observe, --set native_gate_KEY=VALUE
                   census keys on top of the base overlay (they replace the base's own lines)

  python tools/dev/native_gate_players.py config --players 3 --mode coop --source script
  python tools/dev/native_gate_players.py overlay --base <overlay> --gate-dll <dll> --probe tools/dev/native_gate_probe.lua \\
      --out <new overlay dir> --players 4 --source stock --stock-script demo --present --tick187 observe --fp default
  python tools/dev/native_gate_players.py overlay --base <protocol-3 full-stack overlay> --gate-dll <dll> \\
      --probe tools/dev/native_gate_probe.lua --out <new overlay dir> --players 3 --mode factions --source script
  python tools/dev/native_gate_players.py plan --players 3 --mode factions
  python tools/dev/native_gate_players.py compare <client logs> --players 3 --mode factions --source script
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import native_gate_sandbox as sandbox          # noqa: E402  (shared base checks and helpers; not modified here)
import native_gate_player_identity as identity  # noqa: E402
import native_gate_player_orders as player_orders  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
HEADER = '-- Lockstep players (tools/dev/native_gate_players.py); reports/LOCKSTEP-PLAYER-IDENTITY-20261029.txt'
SOURCES = ('wire', 'script', 'stock', 'none')
STOCK_DEMOS = ('demo', 'demo2', 'demo3', 'demo4')   # --stock-script demo: one per slot (native_gate_probe.lua)
ORDERS_DELAY, ORDERS_TICK = 3, 40
IDENTITY_FILES = ('ui/native_gate_player_identity_core.lua', 'ui/native_gate_player_identity.lua')
RESPAWN_MD = 'md/native_gate_pilot_respawn.xml'   # native_gate_pilot_respawn: X4's own create_ship and teleport_player
ORDERS_FILES = ('ui/native_gate_player_orders_core.lua', 'ui/native_gate_player_orders.lua')
MUX_FILES = ('ui/native_gate_inputs_core.lua',)   # protocol 3 input multiplexer (pilot, orders, hit claims)
UI_REPLAY_FILES = ('ui/native_gate_ui_replay_core.lua',)
UI_REPLAY_FAMILIES = ('travel', 'target', 'orders')
# The scripted run without people, per client, ticks relative to the window start: the J key's record and the FFI
# entry for travel, target and match speed; slot 1 as well, it takes the same path through its own functions.
# Travel spans 240..300 ticks: a drive charges for seconds before it engages. Meant for runs without --stock-script: the
# demos' full stop ends a travel, as it should, and the J key is a toggle. Slot 2 accelerates while slot 1 matches it.
UI_REPLAY_DEMO = {1: ((60, 'travel'), (300, 'travel_stop'), (540, 'target_slot', 2), (580, 'match', 1), (680, 'match', 0), (720, 'target_none')),
                  2: ((30, 'travel_key'), (330, 'travel_key'), (360, 'travel'), (600, 'travel_stop'), (605, 'input', 'state', 0xd, 1), (655, 'input', 'state', 0xd, 0),
                      (620, 'target_slot', 1), (690, 'match', 1), (740, 'match', 0), (780, 'target_none'))}
UI_REPLAY_FLEET_ORDERS = 500        # window-relative tick of the scripted orders, client 1's
UI_REPLAY_WINDOW = 800              # the script's last event, window-relative
OWN_KEYS = re.compile(r'native_gate_player_identity[a-z_]*|native_gate_player_orders[a-z_]*')
CREATE = re.compile(r'(\d+)=([a-z][a-z0-9_]*_macro)@(-?\d+),(-?\d+),(-?\d+)(?:/(-?\d+))?(?:#(\d+))?')
START = re.compile(r'(\d{1,2}|[0-9a-f]{16})=([0-9a-f]{16}|[a-z][a-z0-9_]*_macro)@(-?\d+),(-?\d+),(-?\d+)(?:/(-?\d+))?')
COUNTERPART_OFFSET = 1500   # metres along z between a moved fixture actor and its moved counterpart


def ids(text):
    values = [sandbox.saved_id(v) for v in text.split(',') if v]
    if len(set(values)) != len(values):
        raise argparse.ArgumentTypeError('slot ships must be distinct')
    return values


def relation_override(text):
    m = re.fullmatch(r'(\d+)-(\d+)=(allied|neutral|hostile)', text)
    if not m or not int(m[1]) < int(m[2]):
        raise argparse.ArgumentTypeError('relation overrides are A-B=allied|neutral|hostile with A < B')
    return (int(m[1]), int(m[2])), m[3]


def create_spec(text):
    """K=MACRO@X,Y,Z[/YAW][#SEED]: a ship MD creates at the setup tick in the occupied ship's sector, at
    sector-local metres (snapshot 005: the fixture area is at y=100000, e.g. 3=ship_arg_s_fighter_01_a_macro@0,100000,-600)."""
    m = CREATE.fullmatch(text)
    if not m:
        raise argparse.ArgumentTypeError('create specs are SLOT=MACRO@X,Y,Z[/YAW][#SEED] (sector-local metres, degrees)')
    return int(m[1]), {'macro': m[2], 'seed': int(m[7] or 4800 + int(m[1])), 'sector': 'player',
                       'x': int(m[3]), 'y': int(m[4]), 'z': int(m[5]), 'yaw': int(m[6] or 0)}


def start_spec(text):
    """K=SECTOR@X,Y,Z[/YAW] or SHIP=SECTOR@X,Y,Z[/YAW]: slot K's ship (or a saved ship) moved at the setup tick
    to sector-local metres of a sector macro or saved sector (e.g. 2=cluster_01_sector001_macro@0,30000,0)."""
    m = START.fullmatch(text)
    if not m:
        raise argparse.ArgumentTypeError('start specs are K=SECTOR@X,Y,Z[/YAW] or SHIP=SECTOR@X,Y,Z[/YAW] '
                                         '(SECTOR: sector macro or saved ID; metres, degrees)')
    return {'target': int(m[1]) if len(m[1]) < 16 else m[1], 'sector': m[2],
            'x': int(m[3]), 'y': int(m[4]), 'z': int(m[5]), 'yaw': int(m[6] or 0)}


COVERED_SECTORS = 2   # without the census keys below: slot 1's sector and the first other one in slot order


def covered_sectors(args):
    """The sectors a run covers, from its --set keys: native_gate_view_scenes (a physics scene per sector, up to four)
    with native_gate_viewpoint_standins=N (traffic and highway parameters for N sectors) cover N, anything else the
    region camera's two. Keys in the base overlay are not seen here."""
    sets = {}
    for text in getattr(args, 'set', None) or []:
        m = re.fullmatch(r'(native_gate_[a-z0-9_]+)=(.+)', text)
        if m:
            sets[m[1]] = m[2].strip()
    standins = sets.get('native_gate_viewpoint_standins')
    if sets.get('native_gate_view_scenes') == 'true' and standins in ('3', '4'):
        return int(standins)
    return COVERED_SECTORS


def sector_name(token):
    """One name per sector: the macro and the saved ID of the start sector are the same place."""
    token = str(token).lower()
    return identity.SECTOR_ALIASES.get(token, token)


def slot_sectors(args, ships, starts, creates):
    """Slot -> sector after the setup tick, in slot order; None where the builder cannot know (a ship it has no record of, no --start)."""
    moved = {row['object']: sector_name(row['sector']) for row in starts}
    def of(ship):
        if ship in moved:
            return moved[ship]
        return sector_name(identity.SHIP_SECTORS[ship]) if ship in identity.SHIP_SECTORS else None
    return {k + 1: of(args.occupied_ship) if k + 1 in creates else of(ship) for k, ship in enumerate(ships)}


def sector_warnings(args, ships, starts, creates):
    """The warnings of a session's sectors; a slot beyond the covered sectors is a ValueError without --third-sector."""
    sectors = slot_sectors(args, ships, starts, creates)
    limit = covered_sectors(args)
    out, covered = [], []
    for slot, sector in sectors.items():
        if sector and sector not in covered and len(covered) < limit:
            covered.append(sector)
    beyond = [f'slot {slot} ({sector})' for slot, sector in sectors.items() if sector and sector not in covered]
    if beyond:
        message = (', '.join(beyond) + f': beyond the {limit} covered sectors ({", ".join(covered)}). A view point there has no physics scene '
                   '(without native_gate_view_scenes), no camera-centred mass traffic and no highway parameter (without '
                   'native_gate_viewpoint_standins=N) on any client (lockstep_region_camera.h). --set native_gate_view_scenes=true with '
                   '--set native_gate_viewpoint_standins=N covers N sectors, up to four.')
        if not args.third_sector:
            raise ValueError(message + ' Put the slot in a covered sector, set those keys, or pass --third-sector to build it anyway '
                             '(keys in the base overlay are not seen here).')
        out.append('WARNING ' + message)
    unknown = [str(slot) for slot, sector in sectors.items() if sector is None]
    if unknown:
        out.append('WARNING slot ' + ', '.join(unknown) + ': no --start and a ship this builder has no sector for: check VIEW_POINTS_UNCOVERED in the run.')
    return out


def starts_of(args, ships):
    """Start placements in command-line order; slot targets resolve to the slot's saved ship."""
    out = []
    for spec in args.start or []:
        target = spec['target']
        if isinstance(target, int):
            if not 1 <= target <= len(ships) or ships[target - 1] is None:
                raise ValueError(f'--start slot {target} has no saved slot ship')
            target = ships[target - 1]
        row = {'object': target, 'sector': spec['sector'], 'x': spec['x'], 'y': spec['y'], 'z': spec['z'], 'yaw': spec['yaw']}
        out.append(row)
        if args.start_counterparts and target in sandbox.FIXTURES[:6]:
            out.append(dict(row, object=counterpart(target), z=row['z'] + COUNTERPART_OFFSET))
    return out


def defaults(args):
    """Fill source-dependent defaults in place (window, world stride, orders, capture log, devices)."""
    people = args.source == 'stock' and not args.stock_script
    if args.source == 'stock' and not args.ships and not args.create:
        # The stock ships sit far apart in the save: every slot without its own --start comes to slot 1's sector, or a partial
        # --start would leave the rest in sectors of their own.
        given = {s['target'] for s in args.start or []}
        args.start = list(args.start or []) + [{'target': k, 'sector': identity.STOCK_SECTOR, 'x': x, 'y': y, 'z': z, 'yaw': yaw}
                                               for k, (x, y, z, yaw) in sorted(identity.STOCK_STARTS.items())
                                               if k <= args.players and k not in given and identity.STOCK_SHIPS[k - 1] not in given]
        args.ships = list(identity.STOCK_SHIPS[:args.players])
    if args.window is None:
        args.window = [40, sandbox.PILOT_WINDOW_PEOPLE] if people else {'wire': [30, sandbox.PILOT_WINDOW_DEFAULT], 'stock': [40, 640]}.get(args.source, [30, 600])
    if args.world_stride is None:
        args.world_stride = 18000 if people else 1800 if args.source in ('wire', 'stock') else 300
    if args.orders is None:
        args.orders = 'demo' if args.source == 'script' else 'none'
    if args.capture_log is None:
        args.capture_log = args.source in ('wire', 'stock')
    if args.block_orders is None:
        args.block_orders = args.source in ('wire', 'stock')
    if args.devices is None:
        args.devices = ['kbm'] + ['xinput'] * (args.players - 1)
    if args.pads is None:
        args.pads = [0] + list(range(args.players - 1))
    return args


def plan_of(args):
    if not 2 <= args.players <= 4:
        raise ValueError('2..4 players (the pilot module drives at most four slot ships)')
    ships = args.ships or list(identity.DEFAULT_SHIPS[:args.players])
    creates = dict(args.create or [])
    if creates and args.source != 'none':
        raise ValueError('created slot ships have no saved ID: the pilot module cannot drive them yet (use --source none)')
    if args.start_counterparts and not args.start:
        raise ValueError('--start-counterparts needs --start')
    starts = starts_of(args, [None if k + 1 in creates else s for k, s in enumerate(ships)])
    args.sector_warnings = sector_warnings(args, ships, starts, creates)
    return identity.build_plan(args.players, args.mode, ships, args.relations, dict(args.relation or []), args.occupied_ship,
                               args.occupied, args.reputation, args.setup_tick, creates, starts)


def ui_replay_lines(args):
    """The UI replay block: families, the script without people, the test fleet (a `player` ship with a pilot)."""
    if args.ui_replay is None:
        if args.ui_replay_script or args.ui_replay_fleet or args.ui_replay_dock:
            raise ValueError('--ui-replay-script, --ui-replay-fleet and --ui-replay-dock need --ui-replay')
        return []
    if args.source != 'stock' or not args.present:
        raise ValueError('--ui-replay needs --source stock --present')
    names = [n for n in args.ui_replay.split(',') if n]
    if args.ui_replay != 'all' and (not names or any(n not in UI_REPLAY_FAMILIES for n in names) or len(set(names)) != len(names)):
        raise ValueError(f"--ui-replay is all or a comma list of {', '.join(UI_REPLAY_FAMILIES)}")
    orders = args.ui_replay == 'all' or 'orders' in names
    if args.ui_replay_fleet and not orders:
        raise ValueError('--ui-replay-fleet needs the orders family')
    if args.ui_replay_fleet and args.mode != 'coop':
        raise ValueError("--ui-replay-fleet needs --mode coop: slot factions are not `player`, the stock UI orders `player` ships")
    if args.ui_replay_dock and not args.ui_replay_fleet:
        raise ValueError('--ui-replay-dock needs --ui-replay-fleet')
    lines = ['X4LAN_CONFIG.native_gate_ui_replay = ' + ('true' if args.ui_replay == 'all' else '{' + ','.join(repr(n) for n in names) + '}')]
    if args.ui_replay_fleet:
        lines.append(f'X4LAN_CONFIG.native_gate_ui_replay_fleet = {sandbox.lua_list([repr(s) for s in args.ui_replay_fleet])}')
    if args.ui_replay_script:
        if args.window[1] - args.window[0] < UI_REPLAY_WINDOW:
            raise ValueError(f'--ui-replay-script needs a window of {UI_REPLAY_WINDOW} ticks (--window 40 {40 + UI_REPLAY_WINDOW})')
        families = names if args.ui_replay != 'all' else list(UI_REPLAY_FAMILIES)
        plans = []
        for client in range(1, args.players + 1):
            plan = []
            for row in UI_REPLAY_DEMO[1 if client == 1 else 2]:
                kind = {'travel': 'travel', 'travel_stop': 'travel', 'travel_key': 'travel'}.get(row[1], 'target')
                if kind in families:
                    plan.append(row)
            # Client 1 commands the fleet: attack the next fixture, then move, then dock if a station was given.
            if client == 1 and args.ui_replay_fleet:
                ship = args.ui_replay_fleet[0]
                plan += [(UI_REPLAY_FLEET_ORDERS, 'attack', ship, sandbox.FIXTURES[-1]),
                         (UI_REPLAY_FLEET_ORDERS + 60, 'clear', ship),
                         (UI_REPLAY_FLEET_ORDERS + 61, 'move', ship, identity.STOCK_SECTOR, 3000, 100000, 3000)]
                if args.ui_replay_dock:
                    plan += [(UI_REPLAY_FLEET_ORDERS + 90, 'clear', ship), (UI_REPLAY_FLEET_ORDERS + 91, 'dock', ship, args.ui_replay_dock)]
            def item(v):
                return repr(v) if isinstance(v, str) else str(v)
            plans.append('{' + ','.join('{' + ','.join(item(v) for v in row) + '}' for row in plan) + '}')
        lines.append('X4LAN_CONFIG.native_gate_ui_replay_script = {' + ','.join(plans) + '}')
    return lines


def counterpart(ship):
    """The Xenon fixture actor an Argon fixture actor attacks in the scripted battle (actor i -> i+6)."""
    argon = sandbox.FIXTURES[:6]
    if ship not in argon:
        raise ValueError(f'the orders demo needs Argon fixture actors in slots 2..3 (got {ship}); use --orders none')
    return sandbox.FIXTURES[argon.index(ship) + 6]


def demo_rows(args, plan):
    """(slot, subject, target, result) of the orders demo. Slot 2 orders its own ship to attack its battle
    counterpart (applied: an Attack order keeps the Steering model a pilot slot needs, where a Wait/hold
    would remove it); slot 3 orders slot 2's ship and slot 1 slot 3's (2 players: slot 2's) ship
    (refused_not_owner). With different starts every other moved fixture slot (3..N) also orders its own ship
    to attack its moved counterpart: the move drops the Steering solver (live st-F3: slot 3 no_solver 589)."""
    slots = plan['slots']
    if any('ship' not in s for s in slots):
        raise ValueError('the orders demo needs saved slot ships')
    ship = [s['ship'] for s in slots]
    other = ship[2] if args.players >= 3 else ship[1]
    rows = [(1, other, counterpart(other), 'refused_not_owner'), (2, ship[1], counterpart(ship[1]), 'applied')]
    if args.players >= 3:
        rows.append((3, ship[1], counterpart(ship[1]), 'refused_not_owner'))
    moved = {s['object'] for s in plan.get('starts', [])}
    for k in range(3, args.players + 1):
        if ship[k - 1] in moved and ship[k - 1] in sandbox.FIXTURES[:6]:
            rows.append((k, ship[k - 1], counterpart(ship[k - 1]), 'applied'))
    return rows


def orders_plan(args, plan):
    """Scripted ownership check over the input wire (demo_rows)."""
    slots = plan['slots']
    script = {}
    for slot, subject, target, _ in demo_rows(args, plan):
        script.setdefault(str(slot), {}).setdefault(str(ORDERS_TICK), []).append({'op': 'attack', 'subject': subject, 'target': target})
    ship = [s['ship'] for s in slots]
    result = {'players': args.players, 'delay': ORDERS_DELAY, 'script': script}
    if args.mode == 'factions':
        # Slot k may order every ship its faction owns (MD checks the owner at application).
        result.update(ownership='faction', factions=[s['faction'] for s in slots])
    else:
        # Coop: one faction, but slot k may order only its own slot ship. Another slot's ship is flown by
        # that human; an AI order would take its flight away (refused not_owner by the agreed table).
        result.update(ownership='table', owners={s: k for k, s in enumerate(ship, 1)})
    return result


def expected_orders(args, plan):
    if args.orders != 'demo':
        return []
    tick = ORDERS_TICK + ORDERS_DELAY
    return [f'tick={tick} slot={slot} op=attack subject={subject} target={target} result={result}'
            for slot, subject, target, result in demo_rows(args, plan)]


def pilot_lines(args, plan):
    if args.source == 'none':
        if args.fire != 'off' or args.hit_claims:
            raise ValueError('--fire and --hit-claims need piloted slots')
        return []
    ships = [s['ship'] for s in plan['slots']]
    start, stop = args.window
    stock, wired = args.source == 'stock', args.source in ('wire', 'stock')
    delay = args.delay if wired else 0
    if not 5 <= start <= stop <= sandbox.PILOT_TICK_MAX or (wired and not (2 <= delay <= 30 and delay < start)):
        raise ValueError(f'pilot window needs 5 <= start <= stop <= {sandbox.PILOT_TICK_MAX} (wire: delay 2..30 < start)')
    if start <= args.setup_tick + 4:
        raise ValueError('the identity setup tick must come before the controls park (window start - 3)')
    if stock and start - 5 <= args.setup_tick + identity.FOLD_LAG:
        raise ValueError('--source stock: the identity must be ready before the slot players are built (window start - 5)')
    if args.stock_script and not stock:
        raise ValueError('--stock-script needs --source stock')
    if args.death and not stock:
        raise ValueError('--death needs --source stock')
    if args.respawn is not None and not args.death:
        raise ValueError('--respawn needs --death')
    if args.respawn is not None and not 1 <= args.respawn <= sandbox.PILOT_TICK_MAX:
        raise ValueError(f'--respawn is a delay of 1..{sandbox.PILOT_TICK_MAX} ticks')
    if args.death_suit and not (args.death and args.present):
        raise ValueError('--death-suit needs --death and --present')
    if args.fire_slots is not None:
        if args.fire != 'script':
            raise ValueError('--fire-slots needs --fire script')
        if not args.fire_slots or len(set(args.fire_slots)) != len(args.fire_slots) or not all(1 <= s <= args.players for s in args.fire_slots):
            raise ValueError(f'--fire-slots is a list of slots 1..{args.players}, each once')
    if args.present and not stock:
        raise ValueError('--present needs --source stock')
    if args.present and args.camera_follow:
        raise ValueError("--present excludes --camera-follow (each client sits in its own ship with X4's own camera)")
    if stock and not args.capture_log:
        raise ValueError('--source stock needs the capture rows')
    lines = ['X4LAN_CONFIG.native_gate_pilot_inputs = true',
             f"X4LAN_CONFIG.native_gate_pilot_slots = {sandbox.lua_list([repr(s) for s in ships])}",
             f"X4LAN_CONFIG.native_gate_pilot_source = '{'wire' if wired else args.source}'"]
    if wired:
        lines.append(f'X4LAN_CONFIG.native_gate_pilot_delay = {delay}')
    lines += [f'X4LAN_CONFIG.native_gate_pilot_window = {{{start},{stop}}}',
              'X4LAN_CONFIG.native_gate_pilot_suppress_local = true']
    if args.source == 'wire':
        if len(args.devices) != args.players or len(args.pads) != args.players:
            raise ValueError('one device and one pad per client')
        if any(d not in sandbox.DEVICES for d in args.devices) or any(not 0 <= p <= 3 for p in args.pads):
            raise ValueError('devices are kbm|xinput|both|none, pads 0..3')
        device = [repr(d) if d != 'none' else 'false' for d in args.devices]
        lines += [f'X4LAN_CONFIG.native_gate_pilot_device = {sandbox.lua_list(device)}',
                  f'X4LAN_CONFIG.native_gate_pilot_pad = {sandbox.lua_list([str(p) for p in args.pads])}']
        invert = [name for name in args.invert.split(',') if name] if args.invert else []
        if any(name not in sandbox.INVERT for name in invert) or len(set(invert)) != len(invert):
            raise ValueError(f'invert axes are {", ".join(sandbox.INVERT)}, each once')
        if invert:
            lines.append(f"X4LAN_CONFIG.native_gate_pilot_invert = {sandbox.lua_list([repr(n) for n in invert])}")
    elif stock:
        # Every client captures its own slot's stock dispatch stream and replays every slot.
        if args.invert:
            raise ValueError('--invert needs the device sampler (--source wire)')
        lines += ['X4LAN_CONFIG.native_gate_pilot_stock = true',
                  "X4LAN_CONFIG.native_gate_pilot_stock_slots = 'all'",
                  f"X4LAN_CONFIG.native_gate_pilot_capture_source = {sandbox.lua_list([repr('stock')] * args.players)}",
                  f"X4LAN_CONFIG.native_gate_pilot_device = {sandbox.lua_list(['false'] * args.players)}"]
        if args.stock_script:
            lines.append("X4LAN_CONFIG.native_gate_pilot_stock_script = 'aim'" if args.stock_script == 'aim' else
                         f'X4LAN_CONFIG.native_gate_pilot_stock_script = {sandbox.lua_list([repr(s) for s in STOCK_DEMOS[:args.players]])}')
        if args.present:
            lines.append('X4LAN_CONFIG.native_gate_pilot_present = ' + ('true' if args.present == 'hud' else "'camera'"))
            lines.append("X4LAN_CONFIG.native_gate_camera_anchor = nil  -- a play base's chase camera: X4's own camera instead")
        if args.death:
            lines.append('X4LAN_CONFIG.native_gate_pilot_death = true')
        if args.respawn is not None:
            lines.append(f'X4LAN_CONFIG.native_gate_pilot_respawn = {args.respawn}')
        if args.death_suit:
            lines.append('X4LAN_CONFIG.native_gate_pilot_death_suit = true')
    elif args.invert or args.camera_follow:
        raise ValueError('--invert and --camera-follow need human pilots (--source wire or stock)')
    if args.crash_note:
        lines.append('X4LAN_CONFIG.native_gate_crash_note = true')
    if args.capture_log:
        lines.append('X4LAN_CONFIG.native_gate_pilot_capture = true')
    lines.append('X4LAN_CONFIG.native_gate_pilot_ship_state = true')
    if args.fire != 'off':
        lines.append(f"X4LAN_CONFIG.native_gate_pilot_fire = '{args.fire}'")
    if args.fire_slots is not None:
        lines.append(f'X4LAN_CONFIG.native_gate_pilot_fire_slots = {sandbox.lua_list([str(s) for s in args.fire_slots])}')
    if args.hit_claims:
        lines.append('X4LAN_CONFIG.native_gate_hit_claims = ' + ('false' if args.hit_claims == 'off' else repr(args.hit_claims)))
    if args.camera_follow:
        # Clients whose slot ship is not the occupied one watch it through the stock target view.
        follow = ['false' if s == args.occupied_ship else 'true' for s in ships]
        lines.append('-- camera follow: client cameras differ; attention uses view_points, other camera reads remain')
        lines.append(f'X4LAN_CONFIG.native_gate_pilot_camera_follow = {sandbox.lua_list(follow)}')
    if args.block_orders:
        # Human sessions: orders, travel, SETA and autopilot from the stock menus would run on one client
        # only (the probe's pilot order block, as in the sandbox). The UI replay opens what it captures.
        lines.append('X4LAN_CONFIG.native_gate_pilot_block_orders = true')
    lines += ui_replay_lines(args)
    return lines


def config_block(args):
    """(lines, plan, orders plan or None) for the session; validates everything."""
    if args.world_stride < 1 or args.world_stride > 18000 or not 1 <= args.trace_stride <= 18000:
        raise ValueError('world capture and trace strides must be 1..18000')
    plan = plan_of(args)
    # Orders and pilot records share each slot's blob (input multiplexer, priority pilot > orders > hits).
    orders = orders_plan(args, plan) if args.orders == 'demo' else None
    lines = [HEADER, 'X4LAN_CONFIG.native_gate_clients = @X4LANE_CLIENTS@']
    lines += identity.config_lines(plan)
    if orders is not None:
        def reference(text):
            raise ValueError(f'fixture references are not resolved here: {text}')
        lines += player_orders.overlay_config(orders, reference).splitlines()
    lines += pilot_lines(args, plan)
    # The sandbox stack (native_gate_sandbox.py): render RNG split, camera-independent bullet and nav
    # cadence, keyed fixture guns, attention from the pilot-slot ships, every portable ship keyed.
    lines += ['X4LAN_CONFIG.native_gate_render_rng_split = true',
              "X4LAN_CONFIG.native_gate_bullet_cadence = 'every'",
              "X4LAN_CONFIG.native_gate_nav_cadence = 'far'",
              'X4LAN_CONFIG.native_gate_actor_diag = true',
              'X4LAN_CONFIG.native_gate_weapon_diag = true',
              "X4LAN_CONFIG.native_gate_view_points = 'players'",
              'X4LAN_CONFIG.native_gate_actor_rng_all = true',
              f'X4LAN_CONFIG.native_gate_world_hash_stride = {args.world_stride}',
              f'X4LAN_CONFIG.native_gate_trace_stride = {args.trace_stride}']
    if args.battle == 'calm':
        lines.append('X4LAN_CONFIG.native_gate_battle_engage = false')
    lines += sandbox.census_lines(args)
    return lines, plan, orders


def strip_own(base_text):
    """Drop a previous players block (marker and everything after it that this builder owns)."""
    kept, own = [], False
    for line in base_text.splitlines():
        if line.startswith(HEADER.split(';')[0]):
            own = True
            continue
        m = sandbox.LINE.match(line)
        if m and OWN_KEYS.fullmatch(m[1]) and (own or m[1].startswith('native_gate_player_identity')):
            continue
        if line.startswith('-- camera follow:'):
            continue
        kept.append(line)
    return '\n'.join(kept) + '\n'


def build_overlay(args):
    base, out = Path(args.base), Path(args.out)
    if out.exists():
        raise ValueError(f'{out} exists; refusing to overwrite')
    config = base/'ui/config.lua.append'
    files = (base/'ui.xml.files').read_text().split()
    if 'ui/native_gate_probe.lua' not in files or not config.is_file() or not (base/'native/x4lan_gate_64.dll').is_file():
        raise ValueError('base is not a native gate overlay (probe, config or gate DLL missing)')
    lines, plan, orders = config_block(args)
    stripped = strip_own(config.read_text())
    # The UI replay carries runtime IDs: a base overlay without them gets the default, one with them keeps its own.
    if args.ui_replay is not None and sandbox.parse_config(stripped).get('native_gate_runtime_ids') in (None, 'false', 'nil', "'observe'", '"observe"'):
        lines.append('X4LAN_CONFIG.native_gate_runtime_ids = true')
    if sandbox.parse_config(stripped).get('native_gate_player_orders') not in (None, 'false', 'nil'):
        raise ValueError('base overlay already has a player orders block; this builder writes its own (--orders)')
    text = sandbox.merged_config(stripped, lines, lean=args.lean_logs)
    probe = Path(args.probe) if args.probe else base/'ui/native_gate_probe.lua'
    if 'X4LAN_NATIVE_PLAYER_IDENTITY' not in probe.read_text(encoding='utf-8'):
        raise ValueError(f'{probe} has no player identity hook; pass --probe tools/dev/native_gate_probe.lua')
    shutil.copytree(base, out, ignore=shutil.ignore_patterns('x4lane-manifest.json'))
    if args.gate_dll:
        shutil.copy2(args.gate_dll, out/'native/x4lan_gate_64.dll')
    if args.probe:
        shutil.copy2(args.probe, out/'ui/native_gate_probe.lua')
    added = list(MUX_FILES + IDENTITY_FILES)
    shutil.copy2(ROOT/'tools/dev/native_gate_player_identity.xml', out/'md/native_gate_player_identity.xml')
    if args.ui_replay is not None:
        added += UI_REPLAY_FILES
        if args.ui_replay_fleet:
            shutil.copy2(ROOT/'tools/dev/native_gate_ui_replay.xml', out/'md/native_gate_ui_replay.xml')
    if orders is not None:
        added += ORDERS_FILES
        shutil.copy2(ROOT/'tools/dev/native_gate_player_orders.xml', out/'md/native_gate_player_orders.xml')
    if args.respawn is not None or args.death_suit:
        shutil.copy2(ROOT/'tools/dev/native_gate_pilot_respawn.xml', out/RESPAWN_MD)
    for name in added:
        shutil.copy2(ROOT/'tools/dev'/Path(name).name, out/name)
    # Adapters load before the probe, which binds them while arming.
    entries = [e for e in files if e not in MUX_FILES + IDENTITY_FILES + ORDERS_FILES + UI_REPLAY_FILES]
    (out/'ui.xml.files').write_text('\n'.join(added + entries) + '\n')
    (out/'ui/config.lua.append').write_text(text)
    (out/'player-identity-plan.json').write_text(json.dumps(plan, indent=1, sort_keys=True) + '\n')
    manifest = {str(f.relative_to(out)).replace('\\', '/'): hashlib.sha256(f.read_bytes()).hexdigest()
                for f in sorted(out.rglob('*')) if f.is_file()}
    (out/'x4lane-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return out, plan


def session_notes(args, plan, overlay='<overlay>'):
    ticks = args.window[1] + 2
    lanes = ','.join(f'b{k}' for k in range(1, args.players + 1))
    # Four clients: the released lane tool caps at 3 (.local/census/x4lane4.py through runN4.sh).
    people = args.source == 'stock' and not args.stock_script
    runner = 'runN4.sh' if args.players == 4 else 'runPlay.sh' if people else 'runSandbox.sh' if args.source == 'wire' else 'runN.sh'
    P = identity.validate(plan)
    notes = [f'clients: {args.players} (lanes {lanes}); relay ticks: {ticks}' + (
                 f' (pilot window {args.window[0]}-{args.window[1]}, controls restored at {args.window[1] + 1})' if args.source != 'none' else ''),
             f'coordinator: .local/census/{runner} <name> {overlay} {lanes} {ticks} {args.players}',
             f'identity: setup tick {P["tick"]}, READY at {P["tick"] + identity.FOLD_LAG}, plan {identity.digest(P)}',
             f'check: python tools/dev/native_gate_players.py compare <client logs> {" ".join(echo_args(args))}']
    if args.source == 'wire':
        notes.append('desync watcher: python tools/dev/native_gate_desync_watch.py --status <collect>/desync-status.json --beep --toast')
    return notes


def echo_args(args):
    """The plan-relevant options, to repeat for `compare`."""
    out = ['--players', str(args.players), '--mode', args.mode, '--source', args.source]
    if args.ships:
        out += ['--ships', ','.join(args.ships)]
    for flag, value, default in (('--relations', args.relations, 'neutral'), ('--occupied', args.occupied, 'player'),
                                 ('--reputation', args.reputation, 'mirror'), ('--setup-tick', str(args.setup_tick), '20'),
                                 ('--orders', args.orders, 'demo' if args.source == 'script' else 'none')):
        if value != default:
            out += [flag, value]
    for (a, b), name in args.relation or []:
        out += ['--relation', f'{a}-{b}={name}']
    for s in args.start or []:
        out += ['--start', f"{s['target']}={s['sector']}@{s['x']},{s['y']},{s['z']}/{s['yaw']}"]
    if args.start_counterparts:
        out.append('--start-counterparts')
    if args.third_sector:
        out.append('--third-sector')
    return out


def compare_logs(args, paths):
    texts = [Path(p).read_text(encoding='utf-8', errors='replace') for p in paths]
    if len(texts) != args.players:
        return False, f'{args.players} client logs expected, got {len(texts)}'
    lines, plan, orders = config_block(args)
    ok, details = identity.compare(texts, plan)
    if not ok:
        return False, 'identity: ' + details
    report = ['identity: ' + details]
    if orders is not None:
        ok, more = player_orders.compare(texts)
        if not ok:
            return False, 'orders: ' + more
        # Refusals by the agreed table are logged by Lua at once, MD results a frame later: compare in
        # canonical (tick, slot) order, the order in which every client decided them.
        def canonical(lines):
            return sorted(lines, key=lambda line: tuple(int(v) for v in re.match(r'tick=(\d+) slot=(\d+)', line).groups()))
        got = canonical([line.split(' seq=')[0] for line in player_orders.applied_lines(texts[0])])
        want = canonical(expected_orders(args, plan))
        if got != want:
            return False, f'orders: {got} != {want}'
        report.append('orders: ' + more)
    return True, '; '.join(report)


def build_parser():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='command', required=True)
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument('--players', type=int, default=2, help='clients = slots = players, 2..4')
    common.add_argument('--mode', choices=identity.MODES, default='factions')
    common.add_argument('--source', choices=SOURCES, default='wire')
    common.add_argument('--ships', type=ids, help='comma list of saved slot ship IDs (default: player scout, fixtures 3..5)')
    common.add_argument('--create', type=create_spec, action='append', help='SLOT=MACRO@X,Y,Z[/YAW][#SEED], sector-local metres (fixture area y=100000); needs --source none')
    common.add_argument('--start', type=start_spec, action='append',
                        help='K=SECTOR@X,Y,Z[/YAW] (or SHIP=...): different starts, moved at the setup tick')
    common.add_argument('--third-sector', action='store_true',
                        help='allow slots beyond the covered sectors (two; N with --set native_gate_view_scenes=true and '
                             'native_gate_viewpoint_standins=N); they have no physics, traffic or highways there')
    common.add_argument('--start-counterparts', action='store_true',
                        help="also move each moved fixture actor's battle counterpart (+1500 m z)")
    common.add_argument('--relations', choices=sorted(identity.RELATIONS), default='neutral', help='factions mode: every slot pair')
    common.add_argument('--relation', type=relation_override, action='append', help='factions mode: A-B=allied|neutral|hostile')
    common.add_argument('--occupied', choices=identity.OCCUPIED, default='player',
                        help='factions mode: the occupied player ship keeps `player` (default) or moves to its pool faction')
    common.add_argument('--occupied-ship', type=sandbox.saved_id, default=identity.PLAYER_SHIP)
    common.add_argument('--reputation', choices=identity.REPUTATION, default='mirror')
    common.add_argument('--setup-tick', type=int, default=20)
    common.add_argument('--orders', choices=('demo', 'none'), help='scripted ownership check over the input wire, also with --source wire (default: demo for script)')
    common.add_argument('--no-block-orders', dest='block_orders', action='store_false', default=None,
                        help='wire sessions: leave the stock order/travel menus active (they act on one client only)')
    common.add_argument('--stock-script', choices=('demo', 'aim'),
                        help='--source stock without humans: demo, demo2..demo4 per slot; aim: stop and steer at the next slot (hit test)')
    common.add_argument('--fire', choices=('off', 'record', 'script'), default='off',
                        help='primary guns: the trigger in each record, or a fixed schedule for runs without humans')
    common.add_argument('--hit-claims', choices=('off', 'shooter', 'victim', 'first'),
                        help='owners decide hits on and by slot ships (reports/LOCKSTEP-HIT-CLAIMS-NATIVE-20261029.txt); '
                             'not given: as the base overlay has it, off: native hits')
    common.add_argument('--death', action='store_true',
                        help="--source stock: a slot ship's death does not end the session (stop reason 163: the slot is dead on the "
                             "same tick everywhere, slot 1's player in X4's own emergency eject suit)")
    common.add_argument('--respawn', type=int, metavar='TICKS',
                        help='--death: a new ship of the slot\'s macro at its start after that many ticks (X4\'s create_ship and teleport_player)')
    common.add_argument('--death-suit', action='store_true',
                        help="--death --present: a dead slot 2..N waits in a spacesuit of its own (MD's create_ship, the slot's seat), its client "
                             "presents that instead of slot 1's view")
    common.add_argument('--crash-note', action='store_true',
                        help='an unhandled exception writes one CRASH line (address, registers, chain, tick) to x4lan-crash.txt in the lane profile')
    common.add_argument('--fire-slots', type=lambda t: [int(v) for v in t.split(',') if v],
                        help='--fire script: only these slots fire, a list (a hit test with one shooter: 1 kills slot 2)')
    common.add_argument('--present', nargs='?', choices=('hud', 'camera'), const='hud', default=None,
                        help="--source stock: client k's stock camera, HUD and UI present slot k's ship as the player "
                             "(camera: the HUD's anchor stays on slot 1's cockpit)")
    common.add_argument('--ui-replay', nargs='?', const='all', default=None, metavar='FAMILIES',
                        help='stock UI actions as events of the slot: all (no value) or a comma list of travel, target, orders '
                             '(lockstep_ui_replay.h, stop reason 164); needs --source stock --present and runtime IDs')
    common.add_argument('--ui-replay-script', action='store_true', help='--ui-replay without people: a fixed plan through the stock entries')
    common.add_argument('--ui-replay-fleet', type=ids, help='saved IDs: ships handed to `player` at the setup tick, to give orders to (--mode coop)')
    common.add_argument('--ui-replay-dock', type=sandbox.saved_id, help='a saved station ID for the scripted dock order')
    sandbox.add_census_options(common)
    common.add_argument('--delay', type=int, default=3)
    common.add_argument('--window', type=int, nargs=2, metavar=('START', 'STOP'))
    common.add_argument('--devices', type=lambda t: t.split(','), help='wire: one of kbm|xinput|both|none per client')
    common.add_argument('--pads', type=lambda t: [int(v) for v in t.split(',')], help='wire: XInput pad index per client')
    common.add_argument('--invert', default='', help='wire: comma list of axes to invert on every client')
    common.add_argument('--camera-follow', action='store_true', help='wire: clients not in the occupied ship watch their own ship')
    common.add_argument('--world-stride', type=int)
    common.add_argument('--trace-stride', type=int, default=60)
    common.add_argument('--battle', choices=('calm', 'engage'), default='engage',
                        help='engage (default): fixture Attack orders keep the slot ships flyable; calm: they wait (no solver)')
    common.add_argument('--capture-log', dest='capture_log', action='store_true', default=None)
    common.add_argument('--no-capture-log', dest='capture_log', action='store_false')
    sub.add_parser('config', parents=[common], help='print the config block')
    overlay = sub.add_parser('overlay', parents=[common], help='copy a protocol-3 base overlay and apply the players block')
    overlay.add_argument('--base', required=True)
    overlay.add_argument('--out', required=True)
    overlay.add_argument('--gate-dll')
    overlay.add_argument('--probe')
    overlay.add_argument('--full-logs', dest='lean_logs', action='store_false')
    sub.add_parser('plan', parents=[common], help='print the identity plan, digest and expected lines')
    compare = sub.add_parser('compare', parents=[common], help='check client logs against the plan')
    compare.add_argument('logs', nargs='+')
    return p


def main(argv=None):
    p = build_parser()
    args = defaults(p.parse_args(argv))
    try:
        if args.command == 'config':
            lines, plan, _ = config_block(args)
            print('\n'.join(lines))
            for note in getattr(args, 'sector_warnings', []) + session_notes(args, plan):
                print('-- ' + note)
        elif args.command == 'overlay':
            out, plan = build_overlay(args)
            print(f'overlay {out}')
            for note in getattr(args, 'sector_warnings', []) + session_notes(args, plan, str(out)):
                print(note)
        elif args.command == 'plan':
            _, plan, orders = config_block(args)
            P = identity.validate(plan)
            print(json.dumps(plan, indent=1, sort_keys=True))
            print(identity.canonical(P))
            print('digest', identity.digest(P))
            for step in identity.steps(P):
                print('PLAYER_IDENTITY_APPLIED ' + step['head'] + ' result=applied|unchanged')
            for line in expected_orders(args, plan):
                print('PLAYER_ORDER_APPLIED ' + line)
        else:
            ok, details = compare_logs(args, args.logs)
            print(('PASS ' if ok else 'FAIL ') + details)
            return 0 if ok else 1
    except (ValueError, OSError) as error:
        p.error(str(error))
    return 0


if __name__ == '__main__':
    sys.exit(main())
