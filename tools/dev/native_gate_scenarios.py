"""Census scenario fixtures and other player starts (no installation, no launch).

Scenario fixtures (docs/LOCKSTEP-SCENARIOS.txt) add mission activity or highway travel to a
full-stack census overlay; the probe raises their MD cues at fixed gated ticks on every client.
Other player starts need one imported snapshot per gamestart; this tool builds the capture
overlay and rebases a census overlay onto the captured save.

  python tools/dev/native_gate_scenarios.py config missions
  python tools/dev/native_gate_scenarios.py config highway [--mode warp|fly] [--super] [--target highway|beacon]
  python tools/dev/native_gate_scenarios.py overlay missions --base <full-stack overlay> --out <new dir>
  python tools/dev/native_gate_scenarios.py compare <run>/client1/x4lan-debug.txt <run>/client2/x4lan-debug.txt
  python tools/dev/native_gate_scenarios.py starts
  python tools/dev/native_gate_scenarios.py capture-overlay --gamestart <id> --fixedtick-dll <dll> --out <new dir>
  python tools/dev/native_gate_scenarios.py snapshot-info <save_009.xml.gz>          (read only)
  python tools/dev/native_gate_scenarios.py rebase --base <full-stack overlay> --snapshot <save> --out <new dir>
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

ROOT = Path(__file__).resolve().parents[2]
DEV = ROOT / 'tools/dev'
sys.path.insert(0, str(DEV))

LINE = re.compile(r'^X4LAN_CONFIG\.([a-z0-9_]+)\s*=\s*(.*?)\s*$')
HEADER = '-- Census scenario fixture (tools/dev/native_gate_scenarios.py); docs/LOCKSTEP-SCENARIOS.txt'
# Keys this tool sets; base lines with these keys are replaced.
OWNED = ('native_gate_missions', 'native_gate_highway', 'native_gate_highway_super', 'native_gate_highway_target',
         'native_gate_highway_trace', 'native_gate_gravidar_watch', 'native_gate_speech_game_time')
MD = {'missions': 'native_gate_missions.xml', 'highway': 'native_gate_highway.xml'}
# Base options the scenario fixtures refuse (the probe refuses them too).
REFUSED = {'none': (), 'missions': ('native_gate_player_tour',),
           'highway': ('native_gate_player_tour', 'native_gate_pilot_inputs', 'native_gate_player_probe', 'native_gate_player_orders')}
# Lines of interest in a client debug log, without the per-client log prefix.
PAYLOAD = re.compile(r'\[X4LAN\] (NATIVE_GATE (?:MISSION|HIGHWAY|HIGHWAY_TRACE|SCENARIO) .*?)\s*$')

# Player starts that put the player in a ship in open space of the main galaxy. Others
# (on foot, docked in a station, spacesuit, fullscreen plot cutscene, Timelines) are refused.
STARTS = {
    'custom_creative': 'snapshot 005: Argon scout, cluster_14_sector001 (Argon Prime), no plot',
    'x4ep1_gamestart_fight': 'base game: Paranid fighter, zone001_cluster_47_sector001, map-uncover script only',
    'x4ep1_gamestart_terran1': 'Cradle of Humanity: Terran fighter, cluster_101_sector001, gs_terran1 plot (wingmate, satellites, plot missions)',
    'x4ep1_gamestart_split2': 'Split Vendetta: Split fighter, cluster_421_sector001, gs_split2 plot',
    'x4ep1_gamestart_discover': 'base game: Argon scout, zone001_cluster_23_sector001, map-uncover script only',
}
RECOMMENDED = ('x4ep1_gamestart_fight', 'x4ep1_gamestart_terran1', 'x4ep1_gamestart_split2')
MODULE_GUARD = "player.module == 'custom_creative'"


def parse_config(text):
    values = {}
    for line in text.splitlines():
        m = LINE.match(line)
        if m:
            values[m[1]] = m[2]
    return values


def scenario_lines(scenario, mode='warp', super_=False, target='highway', trace=None):
    """The config block (Lua assignments) and the recommended relay tick count."""
    if scenario in ('missions', 'none'):
        if mode != 'warp' or super_ or target != 'highway' or trace:
            raise ValueError('highway options apply to the highway scenario only')
        if scenario == 'none':
            # Control run: the same overlay without any scenario fixture.
            return [], 1500
        return [HEADER, 'X4LAN_CONFIG.native_gate_missions = true'], 1800
    if scenario != 'highway':
        raise ValueError('scenario is missions or highway')
    if mode not in ('warp', 'fly') or target not in ('highway', 'beacon'):
        raise ValueError('highway mode is warp or fly, target is highway or beacon')
    lines = [HEADER, f'X4LAN_CONFIG.native_gate_highway = "{mode}"']
    if super_:
        lines.append('X4LAN_CONFIG.native_gate_highway_super = true')
    if target != 'highway':
        lines.append(f'X4LAN_CONFIG.native_gate_highway_target = "{target}"')
    if trace:
        first, last = trace
        if not 1 <= first <= last or last - first > 3600:
            raise ValueError('highway trace window is FIRST..LAST ticks, at most 3600 apart')
        lines.append(f'X4LAN_CONFIG.native_gate_highway_trace = {{{first},{last}}}')
    ticks = {('warp', False): 1500, ('warp', True): 2700, ('fly', False): 3000, ('fly', True): 4500}[mode, super_]
    return lines, ticks


def merged_config(base_text, scenario, lines):
    """Base config without scenario-owned keys, then the scenario block."""
    values = parse_config(base_text)
    if values.get('native_gate_hash') != '"battle"' or 'native_gate_snapshot_sha' not in values:
        raise ValueError('base overlay needs native_gate_hash = "battle" and a snapshot boot')
    refused = [key for key in REFUSED[scenario] if values.get(key) not in (None, 'false', 'nil')]
    if refused:
        raise ValueError(f'base overlay has options the {scenario} fixture excludes: ' + ', '.join(refused))
    kept = []
    for line in base_text.splitlines():
        m = LINE.match(line)
        if (m and m[1] in OWNED) or line == HEADER:
            continue
        kept.append(line)
    while kept and not kept[-1].strip():
        kept.pop()
    return '\n'.join(kept + lines) + '\n'


def write_manifest(out):
    manifest = {str(f.relative_to(out)).replace('\\', '/'): hashlib.sha256(f.read_bytes()).hexdigest()
                for f in sorted(out.rglob('*')) if f.is_file() and f.name != 'x4lane-manifest.json'}
    (out / 'x4lane-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')


def gate_overlay(base):
    base = Path(base)
    config = base / 'ui/config.lua.append'
    files = (base / 'ui.xml.files').read_text().split() if (base / 'ui.xml.files').is_file() else []
    if 'ui/native_gate_probe.lua' not in files or not config.is_file() or not (base / 'native/x4lan_gate_64.dll').is_file():
        raise ValueError('base is not a native gate overlay (probe, config or gate DLL missing)')
    return config


def copy_base(base, out):
    out = Path(out)
    if out.exists():
        raise ValueError(f'{out} exists; refusing to overwrite')
    shutil.copytree(base, out, ignore=shutil.ignore_patterns('x4lane-manifest.json'))
    return out


def watch_line(ids):
    """Config line for the read-only Gravidar contact watch (census_gravidar.py)."""
    if not 1 <= len(ids) <= 8 or len(set(ids)) != len(ids) or any(not re.fullmatch(r'[0-9a-f]{16}', i) or int(i, 16) == 0 for i in ids):
        raise ValueError('Gravidar watch takes 1..8 unique nonzero saved IDs (16 lowercase hex digits)')
    return 'X4LAN_CONFIG.native_gate_gravidar_watch = {' + ','.join(repr(i) for i in ids) + '}'


def build_overlay(scenario, base, out, lines, probe=None, gate_dll=None, watch=(), speech_game_time=False):
    """Copy a full-stack overlay, add the scenario MD and config block and this checkout's probe."""
    config = gate_overlay(base)
    if speech_game_time:
        lines = lines + ['X4LAN_CONFIG.native_gate_speech_game_time = true']
    if watch:
        if parse_config(config.read_text()).get('native_gate_gravidar_key_all') != 'true':
            raise ValueError('the Gravidar watch rides native_gate_gravidar_key_all, which the base does not set')
        lines = lines + [watch_line(list(watch))]
    if gate_dll and not Path(gate_dll).is_file():
        raise ValueError(f'gate DLL {gate_dll} missing')
    text = merged_config(config.read_text(), scenario, lines)
    out = copy_base(base, out)
    (out / 'md').mkdir(exist_ok=True)
    for name in MD.values():
        if (out / 'md' / name).is_file():
            (out / 'md' / name).unlink()  # a scenario MD without its config stays idle; drop it anyway
    if scenario in MD:
        shutil.copy2(DEV / MD[scenario], out / 'md' / MD[scenario])
    shutil.copy2(probe or DEV / 'native_gate_probe.lua', out / 'ui/native_gate_probe.lua')
    if gate_dll:
        shutil.copy2(gate_dll, out / 'native/x4lan_gate_64.dll')
    (out / 'ui/config.lua.append').write_text(text)
    write_manifest(out)
    return out


STOP = re.compile(r'\[X4LAN\] NATIVE_GATE STOP .*?completed=(\d+)')


def session(text):
    """Scenario lines of the gated session and the completed tick (None without a STOP line).

    The relay ends the session with STOP; the clients keep running ungated afterwards
    (sc-highway2 logged a player zone change 190 ticks after STOP), so lines after it
    are not lockstep evidence. A frame's MD lines are logged before its STOP line."""
    out = []
    for line in text.splitlines():
        m = STOP.search(line)
        if m:
            return out, int(m.group(1)), sum(1 for rest in text[text.index(line) + len(line):].splitlines() if PAYLOAD.search(rest))
        m = PAYLOAD.search(line)
        if m:
            out.append(m.group(1))
    return out, None, 0


def payloads(text):
    return session(text)[0]


def compare(texts):
    """Return (ok, details) for the ordered scenario lines of several client logs (gated session only)."""
    sessions = [session(t) for t in texts]
    ends = ', '.join(f'client {i} stop={s[1]} after={s[2]}' for i, s in enumerate(sessions, 1))
    # Per-tick traces and event-timed lines are separate streams: an event line that moves
    # by a tick must not be reported as a trace difference (and the other way round).
    streams = {'events': [[l for l in s[0] if not l.startswith('NATIVE_GATE HIGHWAY_TRACE ')] for s in sessions],
               'trace': [[l for l in s[0] if l.startswith('NATIVE_GATE HIGHWAY_TRACE ')] for s in sessions]}
    failures = []
    for stream, lines in streams.items():
        reference = lines[0]
        for index, other in enumerate(lines[1:], 2):
            mismatch = next(((row, a, b) for row, (a, b) in enumerate(zip(reference, other), 1) if a != b), None)
            if mismatch:
                failures.append(f'{stream}: client {index} differs at line {mismatch[0]}: {mismatch[1]!r} != {mismatch[2]!r}')
            elif len(reference) != len(other):
                failures.append(f'{stream}: client {index} has {len(other)} lines, client 1 has {len(reference)}')
    if failures:
        return False, '; '.join(failures) + f' ({ends})'
    reference = sessions[0][0]
    kinds = {}
    for line in reference:
        parts = line.split() + ['']
        kind = parts[1] if '=' in parts[2] or not parts[2] else ' '.join(parts[1:3])
        kinds[kind] = kinds.get(kind, 0) + 1
    summary = ', '.join(f'{k}={v}' for k, v in sorted(kinds.items()))
    return True, f'{len(reference)} identical lines on {len(texts)} clients ({summary}; {ends})'


def capture_battle_md(gamestart):
    """The battle fixture MD with only its gamestart guard changed (capture overlays only)."""
    if gamestart not in STARTS:
        raise ValueError(f'unsupported gamestart {gamestart!r}; see the starts command')
    text = (DEV / 'native_gate_battle.xml').read_text(encoding='utf-8')
    if text.count(MODULE_GUARD) != 1:
        raise ValueError('battle fixture gamestart guard not found exactly once')
    return text.replace(MODULE_GUARD, f"player.module == '{gamestart}'")


def build_capture_overlay(gamestart, fixedtick_dll, out):
    """Overlay for tools/dev/native_gate_lane.py --capture-snapshot with another gamestart."""
    md = capture_battle_md(gamestart)
    fixedtick_dll = Path(fixedtick_dll)
    if not fixedtick_dll.is_file():
        raise ValueError(f'fixed-tick DLL {fixedtick_dll} missing')
    out = Path(out)
    if out.exists():
        raise ValueError(f'{out} exists; refusing to overwrite')
    for directory in ('md', 'native', 'ui'):
        (out / directory).mkdir(parents=True)
    (out / 'md/native_gate_battle.xml').write_text(md, encoding='utf-8')
    shutil.copy2(fixedtick_dll, out / 'native/x4lan_fixedtick_64.dll')
    shutil.copy2(DEV / 'native_gate_snapshot.lua', out / 'ui/native_gate_snapshot.lua')
    (out / 'ui.xml.files').write_text('ui/native_gate_snapshot.lua\n')
    (out / 'ui/config.lua.append').write_text(
        'X4LAN_CONFIG.enabled = false\nX4LAN_CONFIG.test_profile = "45841"\n'
        'X4LAN_CONFIG.native_gate_snapshot = "capture"\n'
        f'X4LAN_CONFIG.native_gate_snapshot_gamestart = "{gamestart}"\n')
    write_manifest(out)
    return out


def digest(path):
    with open(path, 'rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def snapshot_header(path):
    """Read-only: savegame start/seed/build and the player's ship and sector."""
    info, player, stack = {}, None, []
    with gzip.open(path, 'rb') as source:
        for event, element in ET.iterparse(source, events=('start', 'end')):
            if event == 'start':
                stack.append(element)
                if element.tag == 'game' and len(stack) == 3 and stack[1].tag == 'info':
                    info = {k: element.get(k) for k in ('start', 'seed', 'version', 'build', 'time')}
                if element.tag == 'component' and element.get('class') == 'player':
                    components = [s for s in stack if s.tag == 'component']
                    ship = next((s for s in reversed(components) if (s.get('class') or '').startswith('ship_')), None)
                    sector = next((s for s in reversed(components) if s.get('class') == 'sector'), None)
                    if ship is None or not re.fullmatch(r'\[0x[0-9a-f]{1,16}\]', ship.get('id', '')):
                        raise ValueError('snapshot player is not inside a ship')
                    player = {'ship': f'{int(ship.get("id")[3:-1], 16):016x}', 'macro': ship.get('macro'),
                              'sector': sector.get('macro') if sector is not None else None}
                    break
                continue
            stack.pop()
            if stack:
                stack[-1].remove(element)
            element.clear()
    if not info.get('start') or player is None:
        raise ValueError('snapshot header or player ship missing')
    return {**info, 'player': player}


def snapshot_lines(path):
    """Config lines that bind a census overlay to this save (checksum, fixture, watch list)."""
    import build_native_gate_overlay as builder
    before = digest(path)
    fixture = builder.snapshot_fixture(path)
    header = snapshot_header(path)
    if digest(path) != before:
        raise ValueError('snapshot changed while reading it')
    watch = fixture + ([header['player']['ship']] if header['player']['ship'] not in fixture else [])
    lines = [f'X4LAN_CONFIG.native_gate_snapshot_sha = "{before}"',
             'X4LAN_CONFIG.native_gate_snapshot_fixture = {' + ','.join(repr(s) for s in fixture) + '}',
             'X4LAN_CONFIG.native_gate_world_hash_watch = {' + ','.join(repr(s) for s in watch) + '}']
    return lines, header


SNAPSHOT_KEYS = ('native_gate_snapshot_sha', 'native_gate_snapshot_fixture', 'native_gate_world_hash_watch')


def rebased_config(base_text, lines):
    values = parse_config(base_text)
    if 'native_gate_snapshot_sha' not in values or 'native_gate_snapshot_fixture' not in values:
        raise ValueError('base overlay needs the snapshot boot and its fixture manifest')
    for key in ('native_gate_player_origin_token', 'native_gate_player_transition_manifest', 'native_gate_npc_pilot'):
        if key in values:
            raise ValueError(f'base overlay has snapshot-005-specific option {key}; rebuild it for the new snapshot')
    replaced = {LINE.match(line)[1]: line for line in lines}
    kept = []
    for line in base_text.splitlines():
        m = LINE.match(line)
        if m and m[1] in SNAPSHOT_KEYS:
            if m[1] in replaced:
                kept.append(replaced.pop(m[1]))
            continue
        kept.append(line)
    while kept and not kept[-1].strip():
        kept.pop()
    kept += [line for key, line in replaced.items() if key != 'native_gate_world_hash_watch' or 'native_gate_world_hash_stride' in values]
    return '\n'.join(kept) + '\n'


def build_rebased(base, snapshot, out):
    config = gate_overlay(base)
    lines, header = snapshot_lines(snapshot)
    text = rebased_config(config.read_text(), lines)
    out = copy_base(base, out)
    (out / 'ui/config.lua.append').write_text(text)
    write_manifest(out)
    return out, header


def build_parser():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='command', required=True)
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument('scenario', choices=('missions', 'highway', 'none'), help='none: control overlay without a scenario')
    common.add_argument('--mode', choices=('warp', 'fly'), default='warp', help='highway: warp to the entry (default) or fly there')
    common.add_argument('--super', dest='super_', action='store_true', help='highway: add the Grand Exchange superhighway stage')
    common.add_argument('--target', choices=('highway', 'beacon'), default='highway', help='highway: autopilot target')
    common.add_argument('--trace', type=int, nargs=2, metavar=('FIRST', 'LAST'),
                        help='highway: per-tick exact pose of the player ship and the convoy in this tick window')
    sub.add_parser('config', parents=[common], help='print the config block and relay ticks')
    overlay = sub.add_parser('overlay', parents=[common], help='copy a full-stack overlay and add the scenario')
    overlay.add_argument('--base', required=True)
    overlay.add_argument('--out', required=True)
    overlay.add_argument('--probe', help='probe to install (default: this checkout)')
    overlay.add_argument('--gate-dll', help='gate DLL to install (e.g. with the Gravidar watch)')
    overlay.add_argument('--speech-game-time', action='store_true',
                         help='complete speech lines on the game clock (native_gate_speech_game_time; needs a matching gate DLL)')
    overlay.add_argument('--gravidar-watch', action='append', default=[], metavar='SAVED_ID',
                         help='read-only Gravidar contact watch for a saved unit ID (repeat, up to 8)')
    c = sub.add_parser('compare', help='compare NATIVE_GATE MISSION/HIGHWAY lines across client logs')
    c.add_argument('logs', nargs='+')
    sub.add_parser('starts', help='list supported player starts for snapshot capture')
    cap = sub.add_parser('capture-overlay', help='overlay for native_gate_lane.py --capture-snapshot with another gamestart')
    cap.add_argument('--gamestart', required=True)
    cap.add_argument('--fixedtick-dll', required=True)
    cap.add_argument('--out', required=True)
    info = sub.add_parser('snapshot-info', help='read-only: header, player ship and census config lines of a save')
    info.add_argument('snapshot')
    rebase = sub.add_parser('rebase', help='copy a full-stack overlay and bind it to another snapshot')
    rebase.add_argument('--base', required=True)
    rebase.add_argument('--snapshot', required=True)
    rebase.add_argument('--out', required=True)
    return p


def main(argv=None):
    p = build_parser()
    args = p.parse_args(argv)
    try:
        if args.command in ('config', 'overlay'):
            lines, ticks = scenario_lines(args.scenario, args.mode, args.super_, args.target, args.trace)
            if args.command == 'config':
                print('\n'.join(lines))
                print(f'-- copy tools/dev/{MD[args.scenario]} to <overlay>/md/; relay ticks: {ticks}' if args.scenario in MD else f'-- control: no scenario lines; relay ticks: {ticks}')
            else:
                out = build_overlay(args.scenario, args.base, args.out, lines, args.probe, args.gate_dll, args.gravidar_watch,
                                    args.speech_game_time)
                print(f'overlay {out}')
                print(f'relay ticks: {ticks}   (.local/census/run.sh <name> {out} b1,b2 {ticks})')
        elif args.command == 'compare':
            texts = [Path(path).read_text(encoding='utf-8', errors='replace') for path in args.logs]
            ok, details = compare(texts)
            print(('PASS ' if ok else 'FAIL ') + details)
            return 0 if ok else 1
        elif args.command == 'starts':
            for gamestart, note in STARTS.items():
                print(f'{gamestart:28} {"recommended " if gamestart in RECOMMENDED else ""}{note}')
        elif args.command == 'capture-overlay':
            out = build_capture_overlay(args.gamestart, args.fixedtick_dll, args.out)
            print(f'capture overlay {out}')
        elif args.command == 'snapshot-info':
            lines, header = snapshot_lines(args.snapshot)
            print(json.dumps(header, sort_keys=True))
            print('\n'.join(lines))
        else:
            out, header = build_rebased(args.base, args.snapshot, args.out)
            print(f'overlay {out} start={header["start"]} player_ship={header["player"]["ship"]} sector={header["player"]["sector"]}')
    except (ValueError, OSError, ET.ParseError) as error:
        p.error(str(error))
    return 0


if __name__ == '__main__':
    sys.exit(main())
