"""Interactive two-player lockstep sandbox: overlay config builder (no installation, no launch).

Two humans each fly one ship in a protocol-3 lockstep session ("pilot by wire",
reports/NATIVE-LOCKSTEP-PILOTING-20261029.txt): every client drives every slot ship
through its own Steering model from the per-tick pilot records; the local
PlayerController is parked (one share of the native control-deactivation count, the
way menus park it, so menus and the map are tolerated) and the mod-owned device
sampler captures the local record. --controls native (default off) instead gives each
client X4's own controls from start-2: the stock input processing fills the capture,
the Agent keeps motion authority and flight actions that change ship state are refused
(lockstep_pilot_native.h, stop reason 113). --controls stock (default off) flies slot 1,
the ship both clients sit in, with X4's own player control path on every client: client 1's
stock input dispatch stream is recorded and replayed through the stock dispatchers on every
client at the admitted tick (lockstep_pilot_stock.h, stop reason 155); client 2 keeps the
device sampler for slot 2. --stock-slots all flies slot 2 through the stock path too: client
2 records its own stock dispatch stream and every client runs X4's player control path for
slot 2 with its own player and PlayerController in a slot scope (lockstep_pilot_slots.h, stop
reason 161). --stock-script demo adds a scripted stock input sequence on client 1,
--stock-script2 demo2 one on client 2 (the stock smokes without a human). --present (with --stock-slots
all) makes client k's stock camera, HUD and UI present slot k's ship as the player
(lockstep_pilot_present.h, stop reason 162); --present camera leaves the HUD's anchor out. --script-pilots flies both slots from the
deterministic script (no human input needed: the native-controls smoke).
How-to: docs/LOCKSTEP-SANDBOX.txt.

Defaults (snapshot capture 005):
  slot 1 = 00000000000ae79c  the player's own Argon scout, occupied on both clients
                             (client 1 flies it with keyboard/mouse from the cockpit)
  slot 2 = 0000000000176ad5  fixture actor 3, an Argon S fighter (ship_arg_s_fighter_01_a)
                             in the same zone ~0.8 km from slot 1 (offset -395/0/+700 m),
                             the ship driven in live pilot tests A, B and C (census 0);
                             client 2 flies it with an XInput pad. Keep both slot ships in
                             one sector (physics scene per local sector is not yet keyed).
  cameras                    both clients stay in the cockpit of slot 1 (both occupy it), so
                             both views are identical. --camera-follow (variant 2) makes
                             client 2 watch its own ship through the stock target view; the
                             attention viewpoints (native_gate_view_points='players') are in
                             every build, but other camera/player-position reads remain.
  --battle engage            (default) the scripted 6v6 runs: slot 2's Attack order keeps its
                             Steering model alive (it is hunted by Xenon actor 9 and its own
                             AI shoots back). --battle calm never sends Engage, but then the
                             waiting fixture has no Steering solver and cannot be flown
                             (sandbox-play1 smoke: s2 applied 0, no_solver=571).

  python tools/dev/native_gate_sandbox.py config
  python tools/dev/native_gate_sandbox.py overlay --base <protocol-3 full-stack overlay> \\
      --gate-dll <x4lan_gate_64.dll> --probe tools/dev/native_gate_probe.lua --out <new overlay dir>
  python tools/dev/native_gate_sandbox.py ships --snapshot <save_009.xml.gz>   (read only)
"""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys
import xml.etree.ElementTree as ET

PLAYER_SHIP = '00000000000ae79c'
FIXTURE_SHIP = '0000000000176ad5'
FIXTURES = ('0000000000176a13', '0000000000176a74', '0000000000176ad5', '0000000000176b36', '0000000000176b97', '0000000000176bf8',
            '0000000000176c59', '0000000000176c77', '0000000000176c95', '0000000000176cb3', '0000000000176cd1', '0000000000176cef')
DEVICES = ('kbm', 'xinput', 'both', 'none')
INVERT = ('pitch', 'yaw', 'roll', 'strafe_x', 'strafe_y')
PILOT_TICK_MAX = 8388607   # pilot window bound (lockstep_pilot_inputs.h)
PILOT_WINDOW_PEOPLE = 72000   # default last tick with people at the controls: 40 minutes at 30 Hz
PILOT_WINDOW_DEFAULT = 18000
# Keys the sandbox block sets; base lines with these keys are replaced.
OWNED = re.compile(r'native_gate_pilot_[a-z_]+|native_gate_world_hash_stride|native_gate_render_rng_split|'
                   r'native_gate_bullet_cadence|native_gate_nav_cadence|native_gate_weapon_diag|native_gate_actor_diag|native_gate_trace_stride|'
                   r'native_gate_battle_engage|native_gate_clients|native_gate_view_points|native_gate_actor_rng_all')
# Base options that cannot run with wire piloting, or would steer the sandbox.
REFUSED = ('native_gate_command_test', 'native_gate_management_test', 'native_gate_construction_test', 'native_gate_player_probe',
           'native_gate_docking_state', 'native_gate_docking_shape_rng', 'native_gate_docking_shape_refresh',
           'native_gate_creation_rng_schema', 'native_gate_registry_creation_schema', 'native_gate_player_request_observer',
           'native_gate_player_tour', 'native_gate_camera_perturb', 'native_gate_big_war',
           'native_gate_big_spawn')
# Diagnostic row output only (the keyed streams stay): about 40 % of the per-tick log
# volume of the full stack; dropped unless --full-logs.
LOG_ONLY = ('native_gate_script_rng_rows',)
REQUIRED = {'native_gate_hash': '"battle"', 'native_gate_serial_pathing': 'true', 'native_gate_world_identities': 'true'}
LINE = re.compile(r'^X4LAN_CONFIG\.([a-z0-9_]+)\s*=\s*(.*?)\s*$')
ROOT = Path(__file__).resolve().parents[2]
MUX_CORE = 'ui/native_gate_inputs_core.lua'   # protocol 3 input multiplexer (pilot, orders, hit claims)


def saved_id(text):
    if not re.fullmatch(r'[0-9a-f]{16}', text) or int(text, 16) == 0:
        raise argparse.ArgumentTypeError('saved IDs are 16 lowercase hex digits, not zero')
    return text


def lua_list(values):
    return '{'+','.join(values)+'}'


def sandbox_lines(args):
    """The sandbox config block (Lua assignments, one per line)."""
    if args.slot1 == args.slot2:
        raise ValueError('the two slots must name different ships')
    start, stop = args.window
    if not args.delay < start <= stop <= PILOT_TICK_MAX or start < 5 or not 2 <= args.delay <= 30:
        raise ValueError(f'pilot window needs delay 2..30 < start <= stop <= {PILOT_TICK_MAX} and start >= 5')
    if not 1 <= args.world_stride <= 18000 or not 1 <= args.trace_stride <= 18000:
        raise ValueError('world capture and trace strides must be 1..18000')
    for pad in (args.pad1, args.pad2):
        if not 0 <= pad <= 3:
            raise ValueError('XInput pads are 0..3')
    invert = [name for name in args.invert.split(',') if name] if args.invert else []
    if any(name not in INVERT for name in invert) or len(set(invert)) != len(invert):
        raise ValueError(f'invert axes are {", ".join(INVERT)}, each once')
    device = [repr(d) if d != 'none' else 'false' for d in (args.device1, args.device2)]
    native, stock = args.controls == 'native', args.controls == 'stock'
    if args.script_pilots and args.predict1+args.predict2 != 'offoff':
        raise ValueError('presentation prediction needs wire pilots')
    if (args.stock_script or args.stock_script2) and not stock:
        raise ValueError('--stock-script and --stock-script2 need --controls stock')
    if args.stock_slots != '1' and not stock:
        raise ValueError('--stock-slots needs --controls stock')
    slots_all = stock and args.stock_slots == 'all'
    if args.present and not slots_all:
        raise ValueError('--present needs --controls stock --stock-slots all')
    if args.present and args.camera_follow:
        raise ValueError('--present excludes --camera-follow (client 2 sits in its own ship with X4\'s own camera)')
    if args.stock_script2 and not slots_all:
        raise ValueError('--stock-script2 needs --stock-slots all (client 2 captures only when slot 2 is a stock slot)')
    if stock:
        # Slot 1 has no pilot record (its stock record replaces it) and the stock record
        # may take most of the 240-byte input blob: features reading slot 1's pilot record
        # or adding sections stay off in this first step.
        if args.script_pilots:
            raise ValueError('--controls stock needs wire pilots (client 1 captures X4\'s own input)')
        if args.predict1 != 'off':
            raise ValueError('--controls stock excludes --predict1 (slot 1 has no pilot record to predict from)')
        if slots_all and args.predict2 != 'off':
            raise ValueError('--stock-slots all excludes --predict2 (slot 2 has no pilot record to predict from)')
        if not slots_all and args.device2 == 'none':
            raise ValueError('--controls stock: client 2 flies slot 2 with the device sampler (--device2 kbm, xinput or both)')
    lines = [
        '-- Lockstep sandbox (tools/dev/native_gate_sandbox.py); docs/LOCKSTEP-SANDBOX.txt',
        'X4LAN_CONFIG.native_gate_clients = @X4LANE_CLIENTS@',
        'X4LAN_CONFIG.native_gate_pilot_inputs = true',
        f"X4LAN_CONFIG.native_gate_pilot_slots = {lua_list([repr(args.slot1), repr(args.slot2)])}",
        f"X4LAN_CONFIG.native_gate_pilot_source = '{'script' if args.script_pilots else 'wire'}'",
        f'X4LAN_CONFIG.native_gate_pilot_delay = {0 if args.script_pilots else args.delay}',
        f'X4LAN_CONFIG.native_gate_pilot_window = {{{start},{stop}}}',
        'X4LAN_CONFIG.native_gate_pilot_suppress_local = true',
    ]
    if native:
        # X4's own controls on every client (no device sampler); the occupied slot's
        # record carries the stock scheme's own requests unless --no-native-requests.
        lines.append('-- native controls: lockstep_pilot_native.h (stop reason 113); flight actions are refused and logged')
        lines.append("X4LAN_CONFIG.native_gate_pilot_capture_source = 'native'")
        if not args.native_requests:
            lines.append('X4LAN_CONFIG.native_gate_pilot_native_requests = false')
    elif stock and slots_all:
        # Every client captures its own slot's stock dispatch stream (no device); every
        # client replays slot 1 on the canonical player and slot 2 in slot 2's scope.
        lines += ["-- stock piloting: lockstep_pilot_stock.h (stop reason 155), slots 2..N lockstep_pilot_slots.h (161);"
                  " both slots fly through X4's own controls on every client",
                  'X4LAN_CONFIG.native_gate_pilot_stock = true',
                  "X4LAN_CONFIG.native_gate_pilot_stock_slots = 'all'",
                  "X4LAN_CONFIG.native_gate_pilot_capture_source = {'stock','stock'}",
                  'X4LAN_CONFIG.native_gate_pilot_device = {false,false}']
        if args.stock_script or args.stock_script2:
            scripts = [f"'{s}'" if s else 'false' for s in (args.stock_script, args.stock_script2)]
            lines.append(f'X4LAN_CONFIG.native_gate_pilot_stock_script = {lua_list(scripts)}')
        if args.present:
            lines.append('X4LAN_CONFIG.native_gate_pilot_present = ' + ('true' if args.present == 'hud' else "'camera'"))
            lines.append("X4LAN_CONFIG.native_gate_camera_anchor = nil  -- a play base's chase camera: X4's own camera instead")
            lines.append("-- --present: the fixture fighter shows no cockpit and this builder cannot move a scout here; "
                         "use native_gate_players.py --source stock --present")
    elif stock:
        # Client 1 captures its stock dispatch stream (no device); every client replays
        # it for slot 1 through X4's own handlers. Client 2 samples slot 2 with its device.
        lines += ["-- stock piloting: lockstep_pilot_stock.h (stop reason 155); slot 1 flies through X4's own controls on every client",
                  'X4LAN_CONFIG.native_gate_pilot_stock = true',
                  "X4LAN_CONFIG.native_gate_pilot_capture_source = {'stock','device'}",
                  f'X4LAN_CONFIG.native_gate_pilot_device = {lua_list(["false", device[1]])}',
                  f'X4LAN_CONFIG.native_gate_pilot_pad = {{0,{args.pad2}}}']
        if args.stock_script:
            lines.append(f"X4LAN_CONFIG.native_gate_pilot_stock_script = '{args.stock_script}'")
        if invert:
            lines.append(f"X4LAN_CONFIG.native_gate_pilot_invert = {lua_list([repr(name) for name in invert])}")
    else:
        lines += [f'X4LAN_CONFIG.native_gate_pilot_device = {lua_list(device)}',
                  f'X4LAN_CONFIG.native_gate_pilot_pad = {{{args.pad1},{args.pad2}}}']
        if invert:
            lines.append(f"X4LAN_CONFIG.native_gate_pilot_invert = {lua_list([repr(name) for name in invert])}")
    if (native or stock or args.script_pilots) and not args.capture_log:
        raise ValueError('native or stock controls and script pilots need the capture rows')
    if args.capture_log:
        lines.append('X4LAN_CONFIG.native_gate_pilot_capture = true')
    lines.append('X4LAN_CONFIG.native_gate_pilot_ship_state = true')
    if args.observe_native and not args.capture_log:
        raise ValueError('--observe-native needs the capture rows')
    if args.camera_follow:
        # Variant 2. Slot 1 is the occupied ship: its cockpit is already right; only
        # client 2 follows. Needs the simulation-viewpoint fix (not on this branch).
        follow = ['false' if args.slot1 == PLAYER_SHIP or not args.follow1 else 'true', 'true']
        lines.append('-- camera follow: client cameras differ; attention uses view_points, other camera reads remain')
        lines.append(f'X4LAN_CONFIG.native_gate_pilot_camera_follow = {lua_list(follow)}')
    predict = [args.predict1, args.predict2]
    if any(mode != 'off' for mode in predict):
        # Presentation prediction on the named client(s) only; never in the declared state.
        lines.append('-- presentation prediction (reports/LOCKSTEP-PREDICTION-20261029.txt)')
        lines.append(f"X4LAN_CONFIG.native_gate_pilot_predict = {lua_list([repr(m) if m != 'off' else 'false' for m in predict])}")
        if args.predict_raw:
            lines.append('X4LAN_CONFIG.native_gate_pilot_predict_raw = true')
    if args.fire != 'off':
        lines.append(f"X4LAN_CONFIG.native_gate_pilot_fire = '{args.fire}'  -- needs native_gate_actor_rng_all = true in the base")
    if args.hit_claims != 'off':
        lines.append('-- hit claims (reports/LOCKSTEP-HIT-CLAIMS-NATIVE-20261029.txt): owners decide hits on and by slot ships')
        lines.append(f"X4LAN_CONFIG.native_gate_hit_claims = '{args.hit_claims}'")
    lines += [
        # Camera-dependent simulation sites with a fix on the coordinator branch:
        # render draws stay off the simulation word (667ff7e), far bullets are hit-tested
        # every frame (a4439f7), nav refresh no longer depends on camera distance
        # (ddaac1f); weapon_diag registers the fixture guns so bullet/weapon RNG keys them
        # (the probe registers them inside its actor_diag pass, so both are set).
        'X4LAN_CONFIG.native_gate_render_rng_split = true',
        "X4LAN_CONFIG.native_gate_bullet_cadence = 'every'",
        "X4LAN_CONFIG.native_gate_nav_cadence = 'far'",
        'X4LAN_CONFIG.native_gate_actor_diag = true',
        'X4LAN_CONFIG.native_gate_weapon_diag = true',
        # Attention from the pilot-slot ships instead of each client's camera (cb26eb0) and
        # every portable ship's Move and pathing Agent update keyed (50f04d1).
        "X4LAN_CONFIG.native_gate_view_points = 'players'",
        'X4LAN_CONFIG.native_gate_actor_rng_all = true',
        f'X4LAN_CONFIG.native_gate_world_hash_stride = {args.world_stride}',
        # Per-actor diagnostic rows (ACTOR, SPLINE, AI, FLIGHT, WEAPON) every 60th tick
        # only; the declared state is built every tick regardless.
        f'X4LAN_CONFIG.native_gate_trace_stride = {args.trace_stride}',
    ]
    if args.battle == 'calm':
        lines.append('X4LAN_CONFIG.native_gate_battle_engage = false')
    # Orders, travel, SETA and autopilot from the interact menu and the map would run
    # on one client only: refused in the sandbox (scripted orders ride the input multiplexer).
    lines.append('X4LAN_CONFIG.native_gate_pilot_block_orders = true')
    if args.observe_native:
        lines.append('X4LAN_CONFIG.native_gate_pilot_observe_native = true')
    return lines + census_lines(args)


def census_lines(args):
    """The census keys both session builders can arm on top of a base overlay (they replace the base's):
    --tick187 the tick-187 rows, with the loadout order observed or canonical; --fp the FP modules
    (portable math, exact reciprocal, decimal reader, fp environment); --set any other key."""
    lines = []
    if args.tick187:
        lines += ["X4LAN_CONFIG.native_gate_ui_lazy = 'observe'", 'X4LAN_CONFIG.native_gate_ui_lazy_rows = {150,200}',
                  'X4LAN_CONFIG.native_gate_supply_wants = {150,200}',
                  f"X4LAN_CONFIG.native_gate_loadout_order = '{args.tick187}'"]
        lines.append('X4LAN_CONFIG.native_gate_loadout_order_rows = ' + ('{150,200}' if args.tick187 == 'observe' else 'nil'))
    if args.fp:
        lines += ['X4LAN_CONFIG.native_gate_portable_math = true', 'X4LAN_CONFIG.native_gate_exact_reciprocal = true',
                  'X4LAN_CONFIG.native_gate_decimal = true', f"X4LAN_CONFIG.native_gate_fp_env = '{args.fp}'"]
    for text in args.set or []:
        m = re.fullmatch(r'(native_gate_[a-z0-9_]+)=(.+)', text)
        if not m:
            raise ValueError(f'--set needs native_gate_KEY=LUA_VALUE, got {text}')
        if any(line.startswith(f'X4LAN_CONFIG.{m[1]} ') for line in lines):
            raise ValueError(f'--set {m[1]}: already set by --tick187 or --fp')
        lines.append(f'X4LAN_CONFIG.{m[1]} = {m[2]}')
    return lines


def add_census_options(parser):
    parser.add_argument('--tick187', choices=('observe', 'canonical'),
                        help='the tick-187 rows (ui lazy, supply wants 150..200) and native_gate_loadout_order')
    parser.add_argument('--fp', choices=('default', 'observe'),
                        help='the FP modules: portable math, exact reciprocal, decimal, native_gate_fp_env')
    parser.add_argument('--set', action='append', metavar='native_gate_KEY=LUA_VALUE',
                        help='one more config key on top of the base (repeatable); the option checker still applies')


def parse_config(text):
    values = {}
    for line in text.splitlines():
        m = LINE.match(line)
        if m:
            values[m[1]] = m[2]
    return values


def merged_config(base_text, lines, lean=True):
    """Base config without the keys the sandbox owns, then the sandbox block."""
    values = parse_config(base_text)
    refused = [key for key in REFUSED if key in values and values[key] not in ('false', 'nil')]
    if refused:
        raise ValueError('base overlay has options that exclude wire piloting or the sandbox: '+', '.join(refused))
    for key, value in REQUIRED.items():
        if values.get(key) != value:
            raise ValueError(f'base overlay needs X4LAN_CONFIG.{key} = {value}')
    # Player orders share the input blob with the pilot records (input multiplexer). Scripted orders only:
    # UI capture would wrap the same menu entries as the sandbox order block.
    if values.get('native_gate_player_orders') == 'true':
        if values.get('native_gate_player_orders_capture') == 'true':
            raise ValueError('player orders UI capture and the sandbox order block both own the order menus; build the orders plan without capture_ui')
        if values.get('native_gate_player_orders_players', '2') != '2':
            raise ValueError('the sandbox has two clients: the player orders plan must have "players": 2')
    if 'native_gate_snapshot_sha' not in values or 'native_gate_snapshot_fixture' not in values:
        raise ValueError('base overlay needs the snapshot boot and its fixture manifest')
    if values.get('native_gate_bullet_rng') != 'true' and values.get('native_gate_bullet_diag') != 'true':
        raise ValueError('native_gate_bullet_cadence needs native_gate_bullet_rng or native_gate_bullet_diag in the base overlay')
    if not all(values.get(key) == 'true' for key in ('native_gate_agent_rng', 'native_gate_ship_rng')):
        raise ValueError('native_gate_actor_rng_all needs native_gate_agent_rng and native_gate_ship_rng in the base overlay')
    if not any(values.get(key) == 'true' for key in ('native_gate_nav_rng', 'native_gate_nav_diag', 'native_gate_nav_initial_refresh')):
        raise ValueError('native_gate_nav_cadence needs native_gate_nav_rng, native_gate_nav_diag or native_gate_nav_initial_refresh in the base overlay')
    kept = []
    block = {m[1] for m in map(LINE.match, lines) if m}
    for line in base_text.splitlines():
        m = LINE.match(line)
        if m and (OWNED.fullmatch(m[1]) or m[1] in block or (lean and m[1] in LOG_ONLY)):
            continue
        if line.startswith(('-- Lockstep sandbox', '-- camera follow:', '-- native controls:', '-- stock piloting:')):
            continue
        kept.append(line)
    while kept and not kept[-1].strip():
        kept.pop()
    return '\n'.join(kept+lines)+'\n'


def build_overlay(args):
    base, out = Path(args.base), Path(args.out)
    if out.exists():
        raise ValueError(f'{out} exists; refusing to overwrite')
    config = base/'ui/config.lua.append'
    files = (base/'ui.xml.files').read_text().split()
    if 'ui/native_gate_probe.lua' not in files or not config.is_file() or not (base/'native/x4lan_gate_64.dll').is_file():
        raise ValueError('base is not a native gate overlay (probe, config or gate DLL missing)')
    text = merged_config(config.read_text(), sandbox_lines(args), lean=args.lean_logs)
    if parse_config(text).get('native_gate_player_orders') == 'true' and 'ui/native_gate_player_orders.lua' not in files:
        raise ValueError('base config enables player orders but the overlay lacks ui/native_gate_player_orders.lua')
    shutil.copytree(base, out, ignore=shutil.ignore_patterns('x4lane-manifest.json'))
    if args.gate_dll:
        shutil.copy2(args.gate_dll, out/'native/x4lan_gate_64.dll')
    if args.probe:
        shutil.copy2(args.probe, out/'ui/native_gate_probe.lua')
    shutil.copy2(ROOT/'tools/dev/native_gate_inputs_core.lua', out/MUX_CORE)
    if MUX_CORE not in files:
        (out/'ui.xml.files').write_text('\n'.join([MUX_CORE] + files) + '\n')
    (out/'ui/config.lua.append').write_text(text)
    manifest = {str(f.relative_to(out)).replace('\\', '/'): hashlib.sha256(f.read_bytes()).hexdigest()
                for f in sorted(out.rglob('*')) if f.is_file()}
    (out/'x4lane-manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    return out


def session_notes(args, overlay='<overlay>'):
    # Parked controls return at stop+1; native and stock controls are removed at stop+1
    # and handed back through the stock path at stop+2. Both before the relay's STOP.
    back = args.window[1]+(2 if args.controls in ('native', 'stock') else 1)
    ticks = back+1
    return [f'relay ticks: {ticks} (pilot window {args.window[0]}-{args.window[1]}, controls restored at {back})',
            f'coordinator: .local/census/runPlay.sh <name> {overlay} b1,b2 {ticks} 2   (relay --observe; add '
            '--status-file {outdir}/desync-status.json to its --server-cmd for the status file)',
            'desync watcher: python tools/dev/native_gate_desync_watch.py --log <collect>/server.log --beep --toast']


def snapshot_ships(path, wanted):
    """Read-only stream over a native .xml.gz save: class/macro/owner, zone and offset of the wanted ships."""
    wanted = {int(s, 16): s for s in wanted}
    found, cues, stack = {}, {}, []

    def owner_index():
        for i in range(len(stack)-1, -1, -1):
            if stack[i].tag == 'component':
                m = re.fullmatch(r'\[0x([0-9a-f]+)\]', stack[i].get('id', ''))
                return (int(m[1], 16) if m else None), i
        return None, None
    with gzip.open(path, 'rb') as source:
        for event, element in ET.iterparse(source, events=('start', 'end')):
            if event == 'start':
                stack.append(element)
                if element.tag == 'component':
                    m = re.fullmatch(r'\[0x([0-9a-f]+)\]', element.get('id', ''))
                    if m and int(m[1], 16) in wanted:
                        zone = next((s.get('macro') for s in reversed(stack[:-1]) if s.tag == 'component' and s.get('class') == 'zone'), None)
                        sector = next((s.get('macro') for s in reversed(stack[:-1]) if s.tag == 'component' and s.get('class') == 'sector'), None)
                        found[int(m[1], 16)] = {'class': element.get('class'), 'macro': element.get('macro'), 'owner': element.get('owner'),
                                                'sector': sector, 'zone': zone, 'position': {}, 'orders': []}
                if element.tag == 'cue' and element.get('name') in ('Spawn', 'Engage') and \
                        any(s.tag == 'script' and s.get('name') == 'X4LAN_NativeGate' for s in stack):
                    cues[element.get('name')] = element.get('state')
                continue
            stack.pop()
            ship, depth = owner_index()
            if ship in found:
                if element.tag == 'position' and stack and stack[-1].tag == 'offset' and depth == len(stack)-2:
                    found[ship]['position'] = {k: float(v) for k, v in element.attrib.items() if k in 'xyz'}
                if element.tag == 'order':
                    found[ship]['orders'].append(element.get('order'))
            if stack:
                stack[-1].remove(element)
            element.clear()
    return {wanted[k]: v for k, v in found.items()}, cues


def build_parser():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='command', required=True)
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument('--slot1', type=saved_id, default=PLAYER_SHIP)
    common.add_argument('--slot2', type=saved_id, default=FIXTURE_SHIP)
    common.add_argument('--device1', choices=DEVICES, default='kbm')
    common.add_argument('--device2', choices=DEVICES, default='xinput')
    common.add_argument('--pad1', type=int, default=0)
    common.add_argument('--pad2', type=int, default=0)
    common.add_argument('--invert', default='', help='comma list of axes to invert on every client: '+','.join(INVERT))
    common.add_argument('--delay', type=int, default=3)
    common.add_argument('--window', type=int, nargs=2, default=[30, PILOT_WINDOW_DEFAULT], metavar=('START', 'STOP'))
    common.add_argument('--world-stride', type=int, default=1800, help='world census capture stride (~3 s per capture)')
    common.add_argument('--battle', choices=('calm', 'engage'), default='engage')
    common.add_argument('--trace-stride', type=int, default=60, help='log per-actor diagnostic rows every N ticks (log volume only)')
    common.add_argument('--camera-follow', action='store_true',
                        help='variant 2: client 2 watches its own ship (needs the simulation-viewpoint fix; desyncs without it)')
    common.add_argument('--follow1', action='store_true', help='also follow slot 1 on client 1 (only if slot 1 is not the player ship)')
    common.add_argument('--no-capture-log', dest='capture_log', action='store_false', help='omit PILOT_CAPTURE rows')
    common.add_argument('--observe-native', action='store_true',
                        help='also log the native processed channels in each PILOT_CAPTURE row (observation only)')
    for client in (1, 2):
        common.add_argument(f'--predict{client}', choices=('off', 'observe', 'present'), default='off',
                            help=f'presentation prediction on client {client} (present draws its ship ahead; this client only)')
    common.add_argument('--predict-raw', action='store_true', help='log PILOT_PREDICT_RAW samples every tick (offline model fitting)')
    common.add_argument('--fire', choices=('off', 'record', 'script'), default='off',
                        help="slot ships fire their primary guns from the record's trigger bit (record) or in fixed bursts (script)")
    common.add_argument('--hit-claims', choices=('off', 'shooter', 'victim', 'first'), default='off',
                        help='hits involving the slot ships are decided by their owners (player-versus-player policy)')
    common.add_argument('--controls', choices=('device', 'native', 'stock'), default='device',
                        help="capture source: the device sampler with parked controls (default), X4's own controls (native) "
                             "or slot 1 flown by X4's own player control path on every client (stock)")
    add_census_options(common)
    common.add_argument('--present', nargs='?', choices=('hud', 'camera'), const='hud', default=None,
                        help="stock slots: client k's stock camera, HUD and UI present slot k's ship as the player "
                             "(camera: the HUD's anchor stays on slot 1's cockpit)")
    common.add_argument('--stock-slots', choices=('1', 'all'), default='1',
                        help='stock controls: slot 1 only (client 2 keeps the device sampler) or all slots (client 2 records its '
                             'own stock input for slot 2, flown in its own scope on every client)')
    common.add_argument('--stock-script', choices=('demo', 'demo2'),
                        help='stock controls: client 1 adds a scripted stock input sequence (smoke without a human)')
    common.add_argument('--stock-script2', choices=('demo', 'demo2'),
                        help='stock controls with --stock-slots all: client 2 adds a scripted stock input sequence for slot 2')
    common.add_argument('--no-native-requests', dest='native_requests', action='store_false',
                        help='native controls: carry the processed channels for the occupied slot too (no stock requests)')
    common.add_argument('--script-pilots', action='store_true',
                        help='fly both slots from the deterministic script (smoke without human input)')
    sub.add_parser('config', parents=[common], help='print the sandbox config block')
    overlay = sub.add_parser('overlay', parents=[common], help='copy a protocol-3 base overlay and apply the sandbox block')
    overlay.add_argument('--base', required=True)
    overlay.add_argument('--out', required=True)
    overlay.add_argument('--gate-dll')
    overlay.add_argument('--probe')
    overlay.add_argument('--full-logs', dest='lean_logs', action='store_false',
                         help='keep the per-row script RNG logging of the base (several GB per client over a long window)')
    ships = sub.add_parser('ships', help='read-only: where the slot candidates are in a snapshot')
    ships.add_argument('--snapshot', required=True)
    ships.add_argument('ids', nargs='*', type=saved_id)
    return p


def main(argv=None):
    p = build_parser()
    args = p.parse_args(argv)
    try:
        if args.command == 'config':
            print('\n'.join(sandbox_lines(args)))
            for note in session_notes(args):
                print('-- '+note)
        elif args.command == 'overlay':
            out = build_overlay(args)
            print(f'overlay {out}')
            for note in session_notes(args, str(out)):
                print(note)
        else:
            found, cues = snapshot_ships(args.snapshot, args.ids or (PLAYER_SHIP,)+FIXTURES)
            for saved, info in found.items():
                print(saved, json.dumps(info, sort_keys=True))
            print('cues', json.dumps(cues, sort_keys=True))
    except (ValueError, OSError) as error:
        p.error(str(error))
    return 0


if __name__ == '__main__':
    sys.exit(main())
