"""Build and test the Linux lockstep gate core offline (Linux host or WSL; never launches X4).

  python3 tools/dev/check_lockstep_gate_linux.py [--cc gcc | --zig ZIG] [--x4 PATH/TO/LINUX/X4]

Steps, all under .local/linux-gate/<compiler>/:
  1. x4lan_gate.so, the production module (src/native/engine/linux/gate_linux.c);
     exports and dynamic dependencies checked. With --zig (0.15.2) it targets
     x86_64-linux-gnu.2.17 like x4lan_net.so, so it loads under the Steam Runtime.
  2. test_gate_linux: white-box tests on a synthetic fake image; with --x4 also on
     an image loaded (read only) from the Linux executable: file pin, every site
     pin, the FrameTick call, the patches and their byte-exact restoration.
  3. test_gate_lua.lua: the production module through a real LuaJIT package.loadlib.
  4. Engine scenarios: N emulated peers (test_gate_engine: X4 main loop + MoveWorker
     threads, the real tools/dev/native_gate_probe.lua in LuaJIT, the gate test module
     on a fake image) against tools/dev/native_gate_relay.py over loopback.
  5. With x86_64-w64-mingw32-gcc: the Windows branch of lockstep_tls.h compiles.
These are synthetic peers, not native X4 runs.
"""
import argparse
import json
import os
from pathlib import Path
import platform
import re
import secrets
import shutil
import socket
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
ENGINE = ROOT / 'src/native/engine'
LINUX = ENGINE / 'linux'
RELAY = ROOT / 'tools/dev/native_gate_relay.py'
PROBE = ROOT / 'tools/dev/native_gate_probe.lua'
INPUTS_CORE = ROOT / 'tools/dev/native_gate_inputs_core.lua'
PREFIX = '[X4LAN] NATIVE_GATE '
EXPORTS = {'luaopen_x4lan_gate', 'x4gate_start', 'x4gate_wire', 'x4gate_lookahead', 'x4gate_report', 'x4gate_poll', 'x4gate_stop'}
FLAGS = ['-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation', '-mcx16', '-ffp-contract=off',
         '-fasynchronous-unwind-tables', '-I', str(ENGINE), '-I', str(LINUX)]


def compiler(args):
    if args.zig:
        zig = str(args.zig.resolve())
        version = subprocess.check_output([zig, 'version'], text=True).strip()
        if version != '0.15.2':
            raise SystemExit('the project Zig compiler must be version 0.15.2')
        return [zig, 'cc', '-target', 'x86_64-linux-gnu.2.17'], f'zig {version}', 'zig'
    cc = shutil.which(args.cc)
    if not cc:
        raise SystemExit(f'C compiler {args.cc} missing')
    return [cc], subprocess.check_output([cc, '--version'], text=True).splitlines()[0], Path(args.cc).name


def run(command, **kw):
    return subprocess.run([str(c) for c in command], check=True, **kw)


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


def module_checks(so):
    symbols = subprocess.check_output(['nm', '-D', '--defined-only', str(so)], text=True)
    exported = {line.split()[-1] for line in symbols.splitlines() if line.split()[1:2] == ['T']}
    if exported != EXPORTS:
        raise AssertionError(f'unexpected exports: {sorted(exported ^ EXPORTS)}')
    dynamic = subprocess.check_output(['readelf', '-dW', str(so)], text=True)
    needed = set(re.findall(r'\(NEEDED\).*\[(.+)\]', dynamic))
    if not needed <= {'libc.so.6', 'libdl.so.2', 'libpthread.so.0', 'libm.so.6'}:
        raise AssertionError(f'unexpected dependencies {needed}')
    if 'TEXTREL' in dynamic:
        raise AssertionError('text relocations')
    versions = subprocess.check_output(['readelf', '-VW', str(so)], text=True)
    glibc = sorted({tuple(map(int, v.split('.'))) for v in re.findall(r'GLIBC_([0-9.]+)', versions)})
    return {'exports': sorted(exported), 'needed': sorted(needed), 'max_glibc': '.'.join(map(str, glibc[-1])) if glibc else None}


def lua_config(values):
    def value(v):
        if isinstance(v, bool): return 'true' if v else 'false'
        if isinstance(v, (int, float)): return repr(v)
        if isinstance(v, str): return json.dumps(v)
        if isinstance(v, dict): return '{' + ','.join(f'[{value(k)}]={value(x)}' for k, x in v.items()) + '}'
        if isinstance(v, list): return '{' + ','.join(value(x) for x in v) + '}'
        raise TypeError(v)
    return 'X4LAN_CONFIG=' + value(values) + '\n'


# Scripted hit claims (the probe's Lua 'carry' section): non-empty blobs for the input wire.
HITS = {
    2: {2: [{'kind': 2, 'back': 0, 'projectile': '00000000000000b1', 'target': '000000000000a003', 'point': [0, 0, 8]}],
        17: [{'kind': 1, 'back': 2, 'projectile': '00000000000000b3', 'target': '000000000000a001', 'point': [1, 2, 3]}]},
    3: {3: [{'kind': 1, 'back': 1, 'projectile': '00000000000000b2', 'target': '000000000000a003', 'point': [0, 4, 0]}],
        30: [{'kind': 2, 'back': 0, 'projectile': '00000000000000b4', 'target': '000000000000a002', 'point': [9, 9, 9]}]},
}
SCENARIOS = [
    # name, peers, protocol-3 clients (None: protocol 2), lookahead, ticks, extras, per-peer game time
    ('protocol2_seed_relay_host', 2, None, 0, 120, {'native_gate_seed_rng': True, 'native_gate_relay_host': '127.0.0.1'}, None, 'pass'),
    ('protocol3_three_peers_hits', 3, 3, 0, 90, {'native_gate_hit_claims': 'carry', 'native_gate_hit_claims_script': HITS}, None, 'pass'),
    ('protocol4_lookahead2_hits', 3, 3, 2, 90, {'native_gate_hit_claims': 'carry', 'native_gate_hit_claims_script': HITS}, None, 'pass'),
    ('clock_mismatch_stops_all', 2, None, 0, 60, {}, {2: 1000.5}, 'native_clock_mismatch_tick_0'),
    # 30 Hz: dt from the gate's own page, the relay's --tick-hz 30, held to real time.
    ('tick_rate_30_paced', 2, None, 0, 45, {'native_gate_tick_hz': 30}, None, 'pass'),
    # A gate at 30 Hz against a relay at 60 Hz stops at the first step.
    ('tick_rate_mismatch_stops_all', 2, None, 0, 45, {'native_gate_tick_hz': 30, 'relay_tick_hz': 60}, None, 'native_dt_mismatch_player_2_tick_1'),
    # World identities through the hooked map insert during the load, WORLD_IMPORTED_2 every 30 ticks.
    ('protocol2_world_hash', 2, None, 0, 90, {'native_gate_world_identities': True, 'native_gate_seed_rng': True, 'native_gate_world_hash_stride': 30,
                                               'native_gate_world_hash_schema': 'WORLD_IMPORTED_2'}, None, 'pass'),
    # The pass joined and the nav refresh stamped per frame; the peers' batches run in opposite order.
    ('protocol2_serial_pathing', 2, None, 0, 90, {'native_gate_world_identities': True, 'native_gate_seed_rng': True, 'native_gate_serial_pathing': True,
                                                  'native_gate_path_determinism': 3, 'native_gate_world_hash_stride': 30,
                                                  'native_gate_world_hash_schema': 'WORLD_IMPORTED_2'}, None, 'pass'),
    # Waits restored during the load on keyed streams; each peer's natural thread word differs.
    ('protocol2_ai_wait', 2, None, 0, 90, {'native_gate_world_identities': True, 'native_gate_ai_wait_rng': True, 'native_gate_world_hash_stride': 30,
                                           'native_gate_world_hash_schema': 'WORLD_IMPORTED_2'}, None, 'pass'),
    # MD delays through the load's call on keyed streams; AIDirector::Move held until tick 1.
    ('protocol2_boot_hold_md_resume', 2, None, 0, 90, {'native_gate_world_identities': True, 'native_gate_boot_hold': True, 'native_gate_md_resume_rng': True,
                                                       'native_gate_world_hash_stride': 30, 'native_gate_world_hash_schema': 'WORLD_IMPORTED_2'}, None, 'pass'),
    # Every script entry from another natural word and in the opposite order on the other peer; keyed, the draws agree.
    ('protocol2_script_rng', 2, None, 0, 90, {'native_gate_world_identities': True, 'native_gate_script_rng': 'key', 'native_gate_script_rng_mask': 7, 'native_gate_script_rng_rows': True,
                                              'native_gate_script_rng_rows_ticks': 30, 'native_gate_script_rng_unmapped': 'key', 'native_gate_md_timed_rng': True,
                                              'native_gate_world_hash_stride': 30, 'native_gate_world_hash_schema': 'WORLD_IMPORTED_2'}, None, 'pass'),
    # The same entries observed: the words stay natural, so the draws differ and the keys do not.
    ('protocol2_script_rng_observe', 2, None, 0, 90, {'native_gate_world_identities': True, 'native_gate_script_rng': 'observe', 'native_gate_script_rng_mask': 3, 'native_gate_script_rng_rows': True,
                                                      'native_gate_script_rng_rows_ticks': 30, 'native_gate_script_rng_natural': True, 'native_gate_md_timed_rng': True,
                                                      'native_gate_world_hash_stride': 30, 'native_gate_world_hash_schema': 'WORLD_IMPORTED_2'}, None, 'pass'),
    # Components made in the load and in two ticks, each peer with its own runtime IDs: the same created IDs and census rows.
    ('protocol2_created', 2, None, 0, 90, {'native_gate_world_identities': True, 'native_gate_runtime_ids': True, 'native_gate_runtime_ids_callers': True, 'native_gate_created_identities': True, 'native_gate_created_rows': True,
                                           'native_gate_created_load': True, 'native_gate_portable_created': True, 'native_gate_world_hash_stride': 30,
                                           'native_gate_world_hash_schema': 'WORLD_IMPORTED_2'}, None, 'pass'),
    # A parameter deadline of each process's own from the load: reset at the first call, the refreshes and draws agree.
    ('protocol2_nav_deadlines', 2, None, 0, 90, {'native_gate_world_identities': True, 'native_gate_nav_cadence': 'far', 'native_gate_nav_params_init': True, 'native_gate_nav_probe_init': True,
                                                 'native_gate_world_hash_stride': 30, 'native_gate_world_hash_schema': 'WORLD_IMPORTED_2'}, None, 'pass'),
    # The same with another natural word on each peer: the refresh draws keyed per ship agree.
    ('protocol2_nav_key', 2, None, 0, 90, {'native_gate_world_identities': True, 'native_gate_nav_cadence': 'far', 'native_gate_nav_params_init': True, 'native_gate_nav_probe_init': True,
                                           'native_gate_nav_key': True, 'native_gate_world_hash_stride': 30, 'native_gate_world_hash_schema': 'WORLD_IMPORTED_2'}, None, 'pass'),
    # Generate from another natural word on each peer: keyed by the explicit state, the loadout draws agree.
    ('protocol2_spawn_rng', 2, None, 0, 90, {'native_gate_world_identities': True, 'native_gate_spawn_rng': 'key', 'native_gate_world_hash_stride': 30,
                                             'native_gate_world_hash_schema': 'WORLD_IMPORTED_2'}, None, 'pass'),
    # A candidate list in each peer's own order: the store gets the saved-ID order, a repeat gone, the unmapped object last.
    ('protocol2_find_order', 2, None, 0, 90, {'native_gate_world_identities': True, 'native_gate_find_order': 'sort', 'native_gate_world_hash_stride': 30,
                                              'native_gate_world_hash_schema': 'WORLD_IMPORTED_2'}, None, 'pass'),
    # The Linear step trace (diagnostic): armed through the probe and kept over the loading-UI reload; three ticks of a
    # watched ship's steps recorded, the other ship passed through.
    ('protocol2_step_trace', 2, None, 0, 90, {'native_gate_world_identities': True, 'native_gate_step_trace': ['0000000000000101'],
                                              'native_gate_world_hash_stride': 30, 'native_gate_world_hash_schema': 'WORLD_IMPORTED_2'}, None, 'pass'),
    # Attention timers whose loading dues and words differ by peer: the saved objects' dues come out the same (no frame
    # seed: the object without a saved ID draws from each peer's own word, as in the game).
    ('protocol2_attention_all', 2, None, 0, 90, {'native_gate_world_identities': True, 'native_gate_attention_initial_refresh': True,
                                                 'native_gate_attention_all': True, 'native_gate_world_hash_stride': 30, 'native_gate_world_hash_schema': 'WORLD_IMPORTED_2'}, None, 'pass'),
    # Three drone macros in each peer's own address order: the unit target's map and the rest come out in id order.
    ('protocol2_loadout_order', 2, None, 0, 90, {'native_gate_world_identities': True, 'native_gate_loadout_order': 'canonical', 'native_gate_world_hash_stride': 30,
                                                 'native_gate_world_hash_schema': 'WORLD_IMPORTED_2'}, None, 'pass'),
    # Each peer's ID map has its own history before the load; the load's IDs come out the same.
    ('protocol2_runtime_ids', 2, None, 0, 90, {'native_gate_world_identities': True, 'native_gate_runtime_ids': True, 'native_gate_runtime_ids_callers': True, 'native_gate_world_hash_stride': 30,
                                               'native_gate_world_hash_schema': 'WORLD_IMPORTED_2'}, None, 'pass'),
    # Wingmen read their leader's flag while the batches run in opposite order on the peers.
    ('protocol2_travel_snapshot', 2, None, 0, 90, {'native_gate_world_identities': True, 'native_gate_seed_rng': True, 'native_gate_serial_pathing': True,
                                                   'native_gate_path_determinism': 3, 'native_gate_travel_snapshot': True, 'native_gate_world_hash_stride': 30,
                                                   'native_gate_world_hash_schema': 'WORLD_IMPORTED_2'}, None, 'pass'),
    # A due batch per frame in opposite address order on the peers; delivered in one order.
    ('protocol2_event_order', 2, None, 0, 90, {'native_gate_world_identities': True, 'native_gate_event_order': True, 'native_gate_event_rows': 700, 'native_gate_world_hash_stride': 30,
                                               'native_gate_world_hash_schema': 'WORLD_IMPORTED_2'}, None, 'pass'),
    # The Gravidar: sector lists in opposite address order, a keyed thread, the same passes on both.
    # The zone groups in each peer's own address order: the pass sees the portable order, the publish finds them back.
    ('protocol2_gravidar_sort', 2, None, 0, 90, {'native_gate_world_identities': True, 'native_gate_seed_rng': True, 'native_gate_serial_pathing': True, 'native_gate_path_determinism': 3, 'native_gate_radar_serial': True,
                                                 'native_gate_gravidar_key_all': True, 'native_gate_gravidar_time_sync': True, 'native_gate_ai_schedule': True,
                                                 'native_gate_gravidar_load_pass': True, 'native_gate_gravidar_sector_order': True, 'native_gate_gravidar_deadlines': True, 'native_gate_gravidar_wide': 'sort', 'native_gate_world_hash_stride': 30,
                                                 'native_gate_world_hash_schema': 'WORLD_IMPORTED_2'}, None, 'pass'),
    ('protocol2_gravidar', 2, None, 0, 90, {'native_gate_world_identities': True, 'native_gate_seed_rng': True, 'native_gate_serial_pathing': True, 'native_gate_path_determinism': 3, 'native_gate_radar_serial': True,
                                            'native_gate_gravidar_key_all': True, 'native_gate_gravidar_time_sync': True, 'native_gate_ai_schedule': True,
                                            'native_gate_gravidar_load_pass': True, 'native_gate_gravidar_sector_order': True, 'native_gate_gravidar_deadlines': True, 'native_gate_world_hash_stride': 30,
                                            'native_gate_world_hash_schema': 'WORLD_IMPORTED_2'}, None, 'pass'),
    # One region on a keyed stream; each peer's natural thread word differs and must survive.
    ('protocol2_region_rng', 2, None, 0, 90, {'native_gate_world_identities': True, 'native_gate_region_rng': 'key', 'native_gate_world_hash_stride': 30,
                                              'native_gate_world_hash_schema': 'WORLD_IMPORTED_2'}, None, 'pass'),
]
WORLD_IMPORTS = 12
REGION_ROW = re.compile(r'REGION_RNG_ROW (index=\d+ tick=\d+ region=[0-9a-f]+ field=\d+ ord=\d+ mapped=1 seeded=1 key=[0-9a-f]+ word=[0-9a-f]+ exit=[0-9a-f]+ '
                        r'draws=\d+ last=[0-9a-f]+ due=[0-9a-f]+ prior=[0-9a-f]+)')
PATH_STATUS = re.compile(r'PATH_DETERMINISM tick=(?P<tick>\d+) armed=1 mask=3 frames=\d+ count=(?P<count>\d+) bank=\d time=(?P<time>[0-9a-f]+) stamped=\d+ '
                         r'current=\d+ stale=(?P<stale>\d+) skipped=\d+ unmapped=(?P<unmapped>\d+) order=(?P<order>[0-9a-f]+) stamps=(?P<stamps>[0-9a-f]+) '
                         r'budget_frames=\d+ budget_applied=\d budget_restored=\d stage=\w+ failures=(?P<failures>\d+) ')


def engine_scenario(build, name, peers, clients, lookahead, ticks, extras, game0, expect):
    work = build / 'scenarios' / name
    if work.exists():
        shutil.rmtree(work)
    native = work / 'extensions/x4_lan_lab/native'
    native.mkdir(parents=True)
    shutil.copy2(build / 'x4lan_gate_test.so', native / 'x4lan_gate.so')
    port, token = free_port(), secrets.token_hex(16)
    report = work / 'relay.json'
    relay_cmd = [sys.executable, RELAY, '--port', port, '--token', token, '--ticks', ticks, '--hold-seconds', '0',
                 '--report', report, '--relative-real', '--clients', peers]
    if lookahead:
        relay_cmd += ['--lookahead', lookahead]
    extras = dict(extras)
    relay_hz = extras.pop('relay_tick_hz', extras.get('native_gate_tick_hz'))
    if relay_hz:
        relay_cmd += ['--tick-hz', relay_hz]
    started = time.monotonic()
    relay = subprocess.Popen([str(c) for c in relay_cmd], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        if relay.stdout.readline().strip() != 'NATIVE_GATE RELAY READY':
            raise AssertionError('relay did not start')
        procs = []
        for slot in range(1, peers + 1):
            cfg = {'native_gate_test': True, 'test_profile': '45841', 'slot': f'p{slot}', 'port': port, 'token': token,
                   'native_gate_hash': 'clock', 'native_gate_snapshot_sha': 'c' * 64, **extras}
            if clients:
                cfg['native_gate_clients'] = clients
            if lookahead:
                cfg['native_gate_lookahead'] = lookahead
            (work / f'config{slot}.lua').write_text(lua_config(cfg))
            env = dict(os.environ, ENGINE_SECONDS='60', ENGINE_GAME0=str((game0 or {}).get(slot, 1000.0)))
            if extras.get('native_gate_world_identities'):
                env['ENGINE_WORLD'] = str(WORLD_IMPORTS)
            if extras.get('native_gate_region_rng'):
                env['ENGINE_REGION'] = '1'
            if extras.get('native_gate_ai_wait_rng'):
                env['ENGINE_AI_WAIT'] = '1'
            if extras.get('native_gate_runtime_ids'):
                env['ENGINE_RUNTIME_IDS'] = '1'
            if extras.get('native_gate_travel_snapshot'):
                env['ENGINE_TRAVEL'] = '1'
            if extras.get('native_gate_radar_serial'):
                env['ENGINE_GRAVIDAR'] = '1'
                if slot == 2:
                    env['ENGINE_GRAVIDAR_REVERSE'] = '1'
            if extras.get('native_gate_event_order'):
                env['ENGINE_EVENT_ORDER'] = '1'
                if slot == 2:
                    env['ENGINE_EVENT_ORDER_REVERSE'] = '1'
            if extras.get('native_gate_script_rng'):
                env['ENGINE_SCRIPT_RNG'] = '1'
                if slot == 2:
                    env['ENGINE_SCRIPT_RNG_REVERSE'] = '1'
            if extras.get('native_gate_created_identities'):
                env['ENGINE_CREATED'] = '1'
            if extras.get('native_gate_nav_cadence'):
                env['ENGINE_NAV'] = '1'
                if extras.get('native_gate_nav_key') and slot == 2:
                    env['ENGINE_NAV_WORD2'] = '1'
            if extras.get('native_gate_spawn_rng'):
                env['ENGINE_SPAWN'] = '1'
            if extras.get('native_gate_gravidar_wide'):
                env['ENGINE_GRAVIDAR_ZONES'] = '1'
            if extras.get('native_gate_step_trace'):
                env['ENGINE_STEP_TRACE'] = '1'
            if extras.get('native_gate_attention_all'):
                env['ENGINE_ATTENTION'] = '1'
                if slot == 2:
                    env['ENGINE_ATTENTION_PEER2'] = '1'
            if extras.get('native_gate_loadout_order'):
                env['ENGINE_LOADOUT'] = '1'
                if slot == 2:
                    env['ENGINE_LOADOUT_REVERSE'] = '1'
            if extras.get('native_gate_find_order'):
                env['ENGINE_FIND_ORDER'] = '1'
                if slot == 2:
                    env['ENGINE_FIND_ORDER_REVERSE'] = '1'
            if extras.get('native_gate_md_resume_rng'):
                env['ENGINE_MD_RESUME'] = '1'
            if extras.get('native_gate_boot_hold'):
                env['ENGINE_BOOT_HOLD'] = '1'
            if extras.get('native_gate_serial_pathing'):
                env['ENGINE_PATH'] = str(WORLD_IMPORTS)
                if slot == 2:
                    env['ENGINE_PATH_REVERSE'] = '1'
            command = [build / 'test_gate_engine', LINUX / 'test_gate_engine.lua', work / f'config{slot}.lua', PROBE]
            if clients:
                command.append(INPUTS_CORE)
            # To a file, not a pipe: a peer writing more than a pipe holds would stall while the
            # other peer is being waited for, and the relay would then time both out.
            with (work / f'peer{slot}.log').open('wb') as log:
                procs.append(subprocess.Popen([str(c) for c in command], cwd=work, env=env, stdout=log, stderr=subprocess.STDOUT))
        for p in procs:
            p.wait(timeout=90)
        outputs = [(work / f'peer{slot}.log').read_text(errors='replace') for slot in range(1, peers + 1)]
        # The relay writes its final report right after its last NGSTOP; wait for it.
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            try:
                if json.loads(report.read_text())['verdict'] != 'incomplete':
                    break
            except (OSError, ValueError):
                pass
            time.sleep(0.1)
    finally:
        relay.terminate()
        relay.wait(timeout=10)
    summary = json.loads(report.read_text())
    result = {'name': name, 'peers': peers, 'protocol': summary.get('protocol_version'), 'verdict': summary['verdict'],
              'failure': summary['failure'], 'released': max(summary['released_at'], key=int, default=None)}
    stops, inputs, rng, world = [], [], [], []
    for slot, text in enumerate(outputs, 1):
        stop = re.search(r'STOP slot=p(\d) completed=(\d+) reason=(\d+) restored=(\d+)', text)
        engine = re.search(r'ENGINE_RESULT (.*)', text)
        if not stop or not engine or 'ENGINE_LUA_ERROR' in text or 'REFUSED' in text or ' ERROR ' in text:
            raise AssertionError(f'{name} peer {slot} did not stop cleanly; see {work / f"peer{slot}.log"}')
        fields = dict(item.split('=', 1) for item in engine.group(1).split())
        if fields['window_restored'] != '1' or fields['dt_original'] != '1' or fields['reloads'] != '1':
            raise AssertionError(f'{name} peer {slot} engine state {fields}')
        stops.append({'slot': int(stop.group(1)), 'completed': int(stop.group(2)), 'reason': int(stop.group(3)),
                      'restored': int(stop.group(4)), 'reloads': int(fields['reloads'])})
        lines = [line[len(PREFIX):] for line in text.splitlines() if line.startswith(PREFIX)]
        inputs.append([line for line in lines if line.startswith(('INPUTS ', 'HIT_CLAIM '))])
        rng.append([line for line in lines if line.startswith('RNG tick=')])
        world.append([line for line in lines if line.startswith(('WORLD_HASH ', 'WORLD_OBJECT ', 'WORLD_BANKS '))])
    result['stops'] = stops
    if expect == 'pass' and extras.get('native_gate_tick_hz'):
        # Paced: the released ticks span at least (ticks-1)/hz of wall clock.
        at = sorted(summary['released_at'].values())
        result['paced_seconds'] = round(at[-1]-at[0], 3)
        if at[-1]-at[0] < (len(at)-1)/extras['native_gate_tick_hz']*0.9:
            raise AssertionError(f'{name}: not held to real time: {result}')
    if expect == 'pass':
        if summary['verdict'] != 'pass' or any(s['completed'] != ticks or s['reason'] != 1 or s['restored'] != 1 for s in stops):
            raise AssertionError(f'{name}: {result}')
    elif summary['failure'] != expect or any(s['reason'] != 1 or s['restored'] != 1 for s in stops):
        raise AssertionError(f'{name}: expected {expect}: {result}')
    if any(lines != inputs[0] for lines in inputs):
        raise AssertionError(f'{name}: peers applied different inputs')
    result['input_lines'] = len(inputs[0])
    if extras.get('native_gate_hit_claims') and sum(line.startswith('HIT_CLAIM ') for line in inputs[0]) != 4:
        raise AssertionError(f'{name}: the four scripted hit claims did not all arrive')
    if extras.get('native_gate_seed_rng'):
        # MoveWorker words: keyed per admitted tick, identical on every peer from the second gated frame.
        if any(lines != rng[0] for lines in rng) or len(rng[0]) < ticks - 1:
            raise AssertionError(f'{name}: per-frame RNG words differ between peers')
        result['rng_lines'] = len(rng[0])
    if extras.get('native_gate_runtime_ids'):
        seen = []
        for text in outputs:
            engine = re.search(r'ENGINE_RUNTIME_IDS entry_restored=(\d) left=0 hash=([0-9a-f]+) assign_restored=(\d)', text)
            stop = re.findall(r'RUNTIME_IDS tick=90 mode=set base=65536 installed=\d restored=\d stage=set failures=0 frame=\d+ head_before=(\d+) cut=(\d+) skipped=(\d+) next=([0-9a-f]+) allocs=(\d+) head=(\d+) tail=(\d+) .*? dropped=(\d+) digest=([0-9a-f]+)', text)
            if not engine or engine[1] != '1' or not stop or int(stop[-1][7]) < 16:
                raise AssertionError(f'{name}: runtime IDs wrong: {engine and engine.group(0)} {re.findall(r"RUNTIME_IDS tick=90 .*", text)[:1]}')
            # The table saw every allocation of the load through AssignNewID's entry, and gave the entry back.
            loads = sum(int(n) for n in re.findall(r'RUNTIME_IDS_CALLER tick=1 phase=load thread=\w+ route=shared count=(\d+)', text))
            if engine[3] != '1' or loads != 4000 + (2 if extras.get('native_gate_created_identities') else 0):
                raise AssertionError(f'{name}: the table of who allocates is wrong: {engine.group(0)} load rows {loads}')
            seen.append((engine[2], stop[-1][3], stop[-1][4], stop[-1][5], stop[-1][6], stop[-1][7], stop[-1][8], stop[-1][0], stop[-1][1]))
        if seen[0][:7] != seen[1][:7]:
            raise AssertionError(f'{name}: the peers got different IDs: {seen}')
        if seen[0][7:] == seen[1][7:]:
            raise AssertionError(f'{name}: the peers had the same history, the scenario proves nothing: {seen}')
        result['runtime_ids_dropped'] = int(seen[0][5])
    if extras.get('native_gate_travel_snapshot'):
        seen = []
        for text in outputs:
            engine = re.search(r'ENGINE_TRAVEL read_restored=(\d) job_restored=(\d) jobs=(\d+) results=([0-9a-f]+)', text)
            stop = re.search(r'TRAVEL_SNAPSHOT_STOP armed=0 installed=0 read=0 tick=(\d+) frames=(\d+) agents=(\d+) new=0 flagged=(\d+) snapshot=([0-9a-f]+) unmapped=0 unmapped_flagged=0 jobs=2 .*?restored=1 stage=\w+ failures=0', text)
            if not engine or not stop or engine[1] != '1' or engine[2] != '1' or int(engine[3]) < 180 or int(stop[2]) < 88:
                raise AssertionError(f'{name}: travel wrong: {engine and engine.group(0)} {re.findall(r"TRAVEL_SNAPSHOT_STOP .*", text)[:1]}')
            seen.append((engine[4], stop[1], stop[2], stop[3], stop[4], stop[5]))
        if seen[0] != seen[1]:
            raise AssertionError(f'{name}: the wingmen decided differently: {seen}')
        result['travel_frames'] = int(seen[0][2])
    if extras.get('native_gate_radar_serial'):
        seen = []
        for text in outputs:
            engine = re.search(r'ENGINE_GRAVIDAR call_restored=(\d) slot_restored=(\d) passes=(\d+) hash=([0-9a-f]+)', text)
            key = re.search(r'GRAVIDAR_KEY_STOP armed=1 time_sync=1 installed=0 frames=(\d+) keyed=(\d+) unkeyed=(\d+) time_syncs=(\d+) time_stale=(\d+) time_failures=0 boot_skipped=\d+ boot_resets=0 boot_drains=1 failures=0', text)
            schedule = re.search(r'AI_SCHEDULE_STOP armed=1 mask=7 binds=1 .*?admitted=1 .*?irregular=0 hash=([0-9a-f]+) held=0 released=1 .*?orders=(\d+) order_restores=(\d+) permuted=0 order_count=13 order_moved=(\d+) order_fallback=1 order_unmapped=0 order_collisions=0 order_hash=([0-9a-f]+) .*?restored=1 stage=\w+ failures=0', text)
            if not engine or not key or not schedule or engine[1] != '1' or engine[2] != '1' or int(engine[3]) < 89 or int(key[1]) < 89 or schedule[2] != schedule[3]:
                raise AssertionError(f'{name}: gravidar wrong: {engine and engine.group(0)} {re.findall(r"GRAVIDAR_KEY_STOP .*", text)[:1]} {re.findall(r"AI_SCHEDULE_STOP .*", text)[:1]}')
            sector = re.search(r'ENGINE_SECTOR slot_restored=1 word_lost=0', text)
            deadlines = re.search(r'GRAVIDAR_DEADLINES_STOP armed=1 installed=0 active=0 restored=1 calls=\d+ passed=\d+ threads=0 nested=0 fields=\d+ first=2 rebased=2 due=(\d+) keyed=(\d+) unmapped=0 native=(\d+) unscoped=0 passes=(\d+) low=(\d+) high=(\d+) sectors=2 .*?last_dueset=([0-9a-f]+) admitted=1 .*?failures=0', text)
            if not sector or not deadlines or int(deadlines[2]) < 1 or int(deadlines[3]) < 1:
                raise AssertionError(f'{name}: deadlines wrong: {re.findall(r"ENGINE_SECTOR .*", text)[:1]} {re.findall(r"GRAVIDAR_DEADLINES_STOP .*", text)[:1]}')
            seen.append((engine[3], engine[4], key[2], key[3], schedule[1], schedule[5], int(schedule[4]), deadlines[1], deadlines[2], deadlines[3], deadlines[4], deadlines[7]))
        if seen[0][:6] != seen[1][:6] or seen[0][7:] != seen[1][7:] or not (seen[0][6] or seen[1][6]) or int(seen[0][2]) == 0 or int(seen[0][3]) == 0:
            raise AssertionError(f'{name}: the peers scanned differently: {seen}')
        result['gravidar_passes'] = int(seen[0][0])
    if extras.get('native_gate_event_order'):
        seen = []
        for text in outputs:
            engine = re.search(r'ENGINE_EVENT_ORDER word_restored=(\d) delivered=(\d+) sequence=([0-9a-f]+) native=([0-9a-f]+)', text)
            stop = re.search(r'EVENT_ORDER_STOP armed=1 installed=0 calls=(\d+) passes=(\d+) boot_passes=0 entries=(\d+) runs=(\d+) local=(\d+) ties=\d+ local_ties=\d+ collisions=0 moved=(\d+) saturated=0 last=6/6 order=([0-9a-f]+) stage=done restored=1 failures=0', text)
            if not engine or not stop or engine[1] != '1' or int(engine[2]) != int(stop[1]):
                raise AssertionError(f'{name}: delivery or status wrong: {engine and engine.group(0)} {re.findall(r"EVENT_ORDER_STOP .*", text)[:1]}')
            seen.append((engine[2], engine[3], engine[4], stop[7], int(stop[6])))
            if extras.get('native_gate_event_rows'):
                rows = len(re.findall(r'EVENT_ROW index=\d+ pass=', text))
                if f"EVENT_ROWS_ARMED ticks={extras['native_gate_event_rows']}" not in text or rows != int(stop[3]):
                    raise AssertionError(f'{name}: event rows wrong: {rows} rows, {stop[3]} entries')
        if seen[0][:2] != seen[1][:2] or seen[0][3] != seen[1][3] or seen[0][2] == seen[1][2] or not (seen[0][4] or seen[1][4]):
            raise AssertionError(f'{name}: peers delivered in different order: {seen}')
        result['event_order_delivered'] = int(seen[0][0])
    if extras.get('native_gate_script_rng'):
        keyed = extras['native_gate_script_rng'] == 'key'
        seen, sets, row_keys = [], [], []
        for text in outputs:
            engine = re.search(r'ENGINE_SCRIPT_RNG entries_restored=(\d) slots_restored=(\d) blocks_restored=(\d) word_lost=(\d+) calls=(\d+) sum=([0-9a-f]+)', text)
            stop = re.search(r'SCRIPT_RNG_STOP installed=1 active=0 mode=(\d) mask=(\d) restored=(\d) exec_hook=0 exec_owned=1 cue_slot=0 stack_slot=0 .*? failures=(\d+)', text)
            timed = re.search(r'MD_TIMED_RNG_STOP armed=1 sites=0 timed_calls=(\d+) wait_calls=(\d+) raise_calls=(\d+) foreign=0 restored=1 failures=0', text)
            if not engine or not stop or not timed or engine[1] != '1' or engine[2] != '1' or engine[3] != '1' or stop[1] != ('1' if keyed else '0') or stop[3] != '1' or stop[4] != '0' or int(engine[5]) != 800:
                raise AssertionError(f'{name}: entries, status or call count wrong: {engine and engine.group(0)} {re.findall(r"SCRIPT_RNG_STOP .*", text)[:1]} {re.findall(r"MD_TIMED_RNG_STOP .*", text)[:1]}')
            if int(timed[1]) < 80 or int(timed[2]) < 80 or int(timed[3]) < 80:
                raise AssertionError(f'{name}: a direct call was not scoped: {timed.group(0)}')
            rows = re.findall(r'SCRIPT_RNG_ROW index=\d+ tick=(\d+) src=(\d) state=(\d) kind=(\d) inst=(\d) depth=\d+ owner=(\S+) stream=(\S+) detail=(\S+) ord=(\d+) key=(\S+) parent=\S+ seeded=(\d) seed=\S+ exit=\S+ draws=(-?\d+) name=(\S+)', text)
            if not rows or any(int(r[0]) > 31 for r in rows):
                raise AssertionError(f'{name}: rows past their limit or none: {len(rows)}')
            sources, names = {r[1] for r in rows}, {r[12] for r in rows}
            if sources != set('12345678') or 'order.fight.attack' not in names or 'FactionLogic.Manager_Delay' not in names:
                raise AssertionError(f'{name}: a source or a name is missing: {sorted(sources)} {sorted(names)}')
            if any(r[10] != ('1' if keyed else '0') or r[11] != '1' for r in rows if r[2] in '12'):
                raise AssertionError(f'{name}: a scope was seeded wrong or did not draw once')
            row_keys.append(sorted((r[0], r[9]) for r in rows if r[2] in '12'))
            sets.append([re.sub(r'.*(cur_keyset=\S+).*', r'\1', line) for line in re.findall(r'SCRIPT_RNG tick=(?:[1-9]|[1-7]\d) (.*)', text)])
            seen.append((engine[6], int(engine[4])))
        if row_keys[0] != row_keys[1]:
            raise AssertionError(f'{name}: the peers have different keys: {len(row_keys[0])} {len(row_keys[1])}')
        if keyed and (seen[0][0] != seen[1][0] or seen[0][1] or seen[1][1]):
            raise AssertionError(f'{name}: keyed draws differ or a word was lost: {seen}')
        if not keyed and (seen[0][0] == seen[1][0] or not seen[0][1]):
            raise AssertionError(f'{name}: the natural words did not reach the draws, the scenario proves nothing: {seen}')
        result['script_rng_rows'] = len(row_keys[0])
    if extras.get('native_gate_created_identities'):
        seen = []
        for text in outputs:
            stop = re.search(r'CREATED_IDENTITIES_STOP armed=1 installed=0 recording=0 restored=1 .*? assigned=(\d+) last=\d+ live=(\d+) .*? retired=(\d+) transient=(\d+) other=(\d+) excluded=(\d+) foreign_main=(\d+) foreign_other=(\d+) orphans=0 .*? digest=([0-9a-f]+) .*? failures=0 portable=1 .*? load=1 .*? load_assigned=(\d+) load_skipped=0', text)
            rows = re.findall(r'CREATED_ROW index=\d+ kind=assign tick=(\d+) id=([0-9a-f]+) parent=([0-9a-f]+) type=([0-9a-f]+) ordinal=(\d+) macro=(\S+)', text)
            census = re.findall(r'WORLD_OBJECT tick=(\d+) id=(8[0-9a-f]+) type=([0-9a-f]+) flags=([0-9a-f]+) parent=([0-9a-f]+) topology=([0-9a-f]+) health=([0-9a-f]+) motion=([0-9a-f]+)', text)
            if not stop or int(stop[1]) != 8 or int(stop[2]) != 8 or int(stop[3]) != 0 or int(stop[4]) != 2 or int(stop[5]) != 2 or int(stop[6]) != 2 or int(stop[10]) != 2:
                raise AssertionError(f'{name}: the wrong objects got IDs: {re.findall(r"CREATED_IDENTITIES_STOP .*", text)[:1]}')
            if len(rows) != 8 or not census or any(r[3] != '2b82440' for r in rows) or not any(r[1].startswith('8') and r[2] != '0000000000000000' for r in rows):
                raise AssertionError(f'{name}: rows wrong: {rows[:4]} census {census[:2]}')
            seen.append((stop[9], rows, census))
        if seen[0] != seen[1]:
            raise AssertionError(f'{name}: the peers differ: {seen[0][0]} {seen[1][0]} {seen[0][1][:2]} {seen[1][1][:2]} {seen[0][2][:1]} {seen[1][2][:1]}')
        result['created_digest'] = seen[0][0]
    if extras.get('native_gate_nav_cadence'):
        seen = []
        for text in outputs:
            engine = re.search(r'ENGINE_NAV slots_restored=(\d) sites_restored=(\d) refreshes=(\d+) hash=([0-9a-f]+)', text)
            stop = re.search(r'NAV_CADENCE_STOP mode=far armed=0 installed=0 restored=1 stage=\S+ failures=0 probe_init=1 .*? params_init=1 .*? params_pins=(\d+) params_inits=1 params_ids=2 params_unmapped=0 ', text)
            if not engine or not stop or engine[1] != '1' or engine[2] != '1' or int(engine[3]) < (1 if extras.get('native_gate_nav_key') else 2) or int(stop[1]) < 70:
                raise AssertionError(f'{name}: slots, sites or deadlines wrong: {engine and engine.group(0)} {re.findall(r"NAV_CADENCE_STOP .*", text)[:1]}')
            if extras.get('native_gate_nav_key'):
                key = re.search(r'NAV_CADENCE_STOP .*? key=1 key_calls=(\d+) key_keyed=(\d+) key_unmapped=0 key_full=0', text)
                if not key or key[1] != '160' or key[2] != '160':
                    raise AssertionError(f'{name}: the nav key did not key every call: {key and key.group(0)[-80:]}')
            seen.append((engine[4], engine[3]))
        if seen[0] != seen[1]:
            raise AssertionError(f'{name}: the peers refreshed apart: {seen}')
        result['nav_hash'] = seen[0][0]
    if extras.get('native_gate_spawn_rng'):
        seen = []
        for text in outputs:
            engine = re.search(r'ENGINE_SPAWN entry_restored=(\d) lost=(\d+) calls=(\d+) hash=([0-9a-f]+)', text)
            stop = re.search(r'SPAWN_RNG_STOP armed=1 installed=0 active=0 mode=1 restored=1 calls=4 boot_calls=4 keyed=3 null=1 unreadable=0 records=3 dropped=0 ring=\d+ failures=0', text)
            if not engine or not stop or engine[1] != '1' or engine[2] != '0' or engine[3] != '4':
                raise AssertionError(f'{name}: entry, word or status wrong: {engine and engine.group(0)} {re.findall(r"SPAWN_RNG_STOP .*", text)[:1]}')
            seen.append(engine[4])
        if seen[0] != seen[1]:
            raise AssertionError(f'{name}: the loadout draws differ: {seen}')
        result['spawn_hash'] = seen[0]
    if extras.get('native_gate_gravidar_wide'):
        seen = []
        for text in outputs:
            engine = re.search(r'ENGINE_GRAVIDAR_ZONES lost=(\d+) hash=([0-9a-f]+)', text)
            stop = re.search(r'GRAVIDAR_WIDE_STOP mode=sort armed=1 installed=0 active=0 restored=1 .*? sorts=(\d+) settles=(\d+) sectors=(\d+) moved=(\d+) unmapped=(\d+) order=([0-9a-f]+) permuted=0 stage=\S+ failures=0', text)
            if not engine or not stop or engine[1] != '0' or int(stop[1]) < 80 or int(stop[2]) < 80 or int(stop[5]) == 0:
                raise AssertionError(f'{name}: zone groups, settle or status wrong: {engine and engine.group(0)} {re.findall(r"GRAVIDAR_WIDE_STOP .*", text)[:1]}')
            seen.append((engine[2], stop[6], int(stop[4])))
        if seen[0][:2] != seen[1][:2] or not (seen[0][2] or seen[1][2]):
            raise AssertionError(f'{name}: the peers saw the zones in another order, or nothing moved: {seen}')
        result['gravidar_zones'] = seen[0][0]
    if extras.get('native_gate_step_trace'):
        for text in outputs:
            engine = re.search(r'ENGINE_STEP_TRACE entry_restored=(\d) calls=(\d+)', text)
            stop = re.search(r'STEP_TRACE_STOP installed=0 restored=1 ids=1 ticks=3 calls=10 watched_calls=3 records=3 dropped=0', text)
            rows = re.findall(r'STEP_TRACE_ROW index=\d+ tick=(\d) key=0000000000000101 ordinal=0 thread=ui part=(\w+)', text)
            if not engine or not stop or engine[1] != '1' or engine[2] != '10' or len(rows) != 3 * 25 or 'STEP_TRACE_ARMED watched=1 ticks=3' not in text:
                raise AssertionError(f'{name}: entry, status or rows wrong: {engine and engine.group(0)} {re.findall(r"STEP_TRACE_STOP .*", text)[:1]} rows={len(rows)}')
            if [part for _, part in rows[17:25]] != ['pose_pre'] * 4 + ['pose_post'] * 4:
                raise AssertionError(f'{name}: pose rows wrong: {[part for _, part in rows[17:25]]}')
        result['step_trace_rows'] = 3 * 25
    if extras.get('native_gate_attention_all'):
        seen = []
        for text in outputs:
            engine = re.search(r'ENGINE_ATTENTION entry_restored=(\d) evaluations=(\d+) hash=([0-9a-f]+)', text)
            stop = re.search(r'ATTENTION_ALL_STOP armed=1 installed=0 frame=\d+ calls=\d+ inits=\d+ total_calls=(\d+) total_inits=(\d+) ids=(\d+) unmapped=(\d+) unmapped_ids=(\d+) first=(\d+) stage=\w+ failures=0', text)
            if not engine or not stop or engine[1] != '1' or stop.groups() != ('160', '2', '2', '80', '1', '1'):
                raise AssertionError(f'{name}: entry, status or counts wrong: {engine and engine.group(0)} {re.findall(r"ATTENTION_ALL_STOP .*", text)[:1]}')
            seen.append(engine[3])
        if seen[0] != seen[1]:
            raise AssertionError(f'{name}: the saved objects got different dues: {seen}')
        result['attention_all_hash'] = seen[0]
    if extras.get('native_gate_loadout_order'):
        seen = []
        for text in outputs:
            engine = re.search(r'ENGINE_LOADOUT sites_restored=(\d) calls=(\d+) rests=(\d+) hash=([0-9a-f]+)', text)
            stop = re.search(r'LOADOUT_ORDER_STOP installed=0 mode=off frame=\d+ calls=0/0/0/(\d+)/0/0 rests=(\d+) moved=(\d+) relinked=(\d+) rows=0 window=0-0 restored=1 stage=\w+ failures=0', text)
            if not engine or not stop or engine[1] != '1' or int(engine[2]) != 80 or int(engine[3]) != 80 or int(stop[1]) != 80 or int(stop[2]) != 80 or int(stop[4]) != 80:
                raise AssertionError(f'{name}: sites, status or calls wrong: {engine and engine.group(0)} {re.findall(r"LOADOUT_ORDER_STOP .*", text)[:1]}')
            seen.append((engine[4], int(stop[3])))
        if seen[0][0] != seen[1][0] or not (seen[0][1] or seen[1][1]):
            raise AssertionError(f'{name}: the peers ordered the macros differently, or nothing moved: {seen}')
        result['loadout_order_hash'] = seen[0][0]
    if extras.get('native_gate_find_order'):
        seen = []
        for text in outputs:
            engine = re.search(r'ENGINE_FIND_ORDER site_restored=(\d) calls=(\d+) hash=([0-9a-f]+)', text)
            stop = re.search(r'FIND_ORDER_STOP armed=1 installed=0 active=0 mode=2 restored=1 calls=(\d+) applied=(\d+) observed=0 passed=0 threads=0 empty=0 candidates=(\d+) duplicates=(\d+) unmapped=(\d+) reordered=(\d+) collisions=0 oversize=0 unreadable=0 foreign=0 .*? failures=0', text)
            row = re.search(r'FIND_ORDER_ROW index=\d+ tick=\d+ scope=\S+ ord=\d+ mode=2 raw=5 dups=1 unmapped=1 n=4 moved=\d+ set=(\S+) native=\S+ name=\S+ ids=([0-9a-f,]+)', text)
            if not engine or not stop or not row or engine[1] != '1' or int(engine[2]) != 80 or int(stop[1]) != 80 or int(stop[4]) != 80 or int(stop[5]) != 80:
                raise AssertionError(f'{name}: site, status or rows wrong: {engine and engine.group(0)} {re.findall(r"FIND_ORDER_STOP .*", text)[:1]} {row and row.group(0)}')
            if row[2] != '101,102,103,0':
                raise AssertionError(f'{name}: not the saved order with the unmapped object last: {row[2]}')
            seen.append((engine[3], row[1]))
        if seen[0] != seen[1]:
            raise AssertionError(f'{name}: the peers got different lists: {seen}')
        result['find_order_hash'] = seen[0][0]
    if extras.get('native_gate_md_resume_rng'):
        rows = []
        for text in outputs:
            rows.append([re.sub(r' natural=[0-9a-f]+', '', row) for row in re.findall(r'MD_RESUME_RNG_ROW (.*)', text)])
            engine = re.search(r'ENGINE_MD_RESUME site_restored=(\d) word_lost=(\d+)', text)
            stop = re.search(r'MD_RESUME_RNG_STOP armed=1 installed=0 active=0 restored=1 calls=3 keyed=3 passed=0 unnamed=1 threads=3 foreign=0 table_full=0 draws=3 draws_unknown=0 .*? failures=0', text)
            if not engine or engine[1] != '1' or engine[2] != '0' or not stop:
                raise AssertionError(f'{name}: site, word or status wrong: {engine and engine.group(0)} {re.findall(r"MD_RESUME_RNG_STOP .*", text)[:1]}')
        if len(rows[0]) != 3 or rows[0] != rows[1] or 'name=FactionLogic.Manager_Delay' not in rows[0][0] or ' ord=1 ' not in rows[0][0] or ' ord=2 ' not in rows[0][1] or 'unnamed=1' not in rows[0][2]:
            raise AssertionError(f'{name}: delays differ or are missing: {rows[0]} {rows[1]}')
        result['md_resume_rows'] = len(rows[0])
    if extras.get('native_gate_boot_hold'):
        for text in outputs:
            engine = re.search(r'ENGINE_BOOT_HOLD entry_restored=(\d) phase2_native=(\d+) native=(\d+)', text)
            stop = re.search(r'BOOT_HOLD_STOP armed=1 installed=0 active=0 held=1 moves=\d+ pending=0 admitted=1 admit_pending=0 released=1 release_pending=0 restored=1 stage=released failures=0', text)
            if not engine or engine[1] != '1' or engine[2] != '0' or int(engine[3]) < 90 or not stop:
                raise AssertionError(f'{name}: the hold is wrong: {engine and engine.group(0)} {re.findall(r"BOOT_HOLD_STOP .*", text)[:1]}')
        result['boot_hold'] = 1
    if extras.get('native_gate_ai_wait_rng'):
        rows = []
        for text in outputs:
            rows.append([re.sub(r' natural=[0-9a-f]+', '', row) for row in re.findall(r'AI_WAIT_RNG_ROW (.*)', text)])
            engine = re.search(r'ENGINE_AI_WAIT slot_restored=(\d) word_lost=(\d+)', text)
            stop = re.search(r'AI_WAIT_RNG_STOP armed=1 installed=0 shared=0 active=0 restored=1 calls=4 keyed=2 passed=0 foreign=1 unmapped=1 .*? failures=0', text)
            if not engine or engine[1] != '1' or engine[2] != '0' or not stop:
                raise AssertionError(f'{name}: slot, word or status wrong: {engine and engine.group(0)} {re.findall(r"AI_WAIT_RNG_STOP .*", text)[:1]}')
        if len(rows[0]) != 2 or rows[0] != rows[1]:
            raise AssertionError(f'{name}: restores differ or are missing: {rows[0]} {rows[1]}')
        result['ai_wait_rows'] = len(rows[0])
    if extras.get('native_gate_region_rng'):
        rows = []
        for text in outputs:
            rows.append(REGION_ROW.findall(text))
            engine = re.search(r'ENGINE_REGION slot_restored=(\d) word_lost=(\d+)', text)
            stop = re.search(r'REGION_RNG_STOP armed=1 installed=0 active=0 mode=1 restored=1 calls=\d+ scoped=(\d+) keyed=(\d+) .*? failures=0', text)
            if not engine or engine[1] != '1' or engine[2] != '0' or not stop or int(stop[2]) < ticks - 1:
                raise AssertionError(f'{name}: slot, word or status wrong: {engine and engine.group(0)} {stop and stop.group(0)[:160]}')
        if len(rows[0]) < 3 or rows[0] != rows[1]:
            raise AssertionError(f'{name}: region passes differ or are missing: {len(rows[0])}/{len(rows[1])}')
        result['region_passes'] = len(rows[0])
    if extras.get('native_gate_serial_pathing'):
        stamps = []
        for text in outputs:
            rows = {}
            for m in PATH_STATUS.finditer(text):
                rows[int(m['tick'])] = (m['count'], m['time'], m['order'], m['stamps'], m['stale'], m['unmapped'], m['failures'])
            joins = re.search(r'NATIVE_GATE PATH joins=(\d+) restored=(\d+) rng_seeds=(\d+)', text)
            engine = re.search(r'ENGINE_PATH passes=(\d+) site_restored=(\d) budget_native=(\d)', text)
            if not joins or int(joins[1]) < ticks - 1 or joins[2] != '1' or int(joins[3]) < ticks - 1:
                raise AssertionError(f'{name}: joins or restoration wrong: {joins and joins.group(0)}')
            if not engine or engine[2] != '1' or engine[3] != '1':
                raise AssertionError(f'{name}: release site or budget not restored: {engine and engine.group(0)}')
            stamps.append(rows)
        common = sorted(set(stamps[0]) & set(stamps[1]))
        if len(common) < ticks - 2 or any(stamps[0][t] != stamps[1][t] for t in common):
            raise AssertionError(f'{name}: path determinism rows differ or are missing ({len(common)} common)')
        if any(row[0] != str(WORLD_IMPORTS) or row[4] != '0' or row[5] != '0' or row[6] != '0' for row in stamps[0].values()):
            raise AssertionError(f'{name}: path determinism status wrong: {next(iter(stamps[0].values()))}')
        result['path_rows'] = len(common)
    if extras.get('native_gate_world_identities'):
        # Ticks 1, 30, 60 and 90; every imported identity hashed (absent rows) and equal on every peer.
        hashes = [line for line in world[0] if line.startswith('WORLD_HASH ')]
        # Created identities add their own rows after the imported ones, and those objects exist.
        created = bool(extras.get('native_gate_created_identities'))
        complete = all((created or f'objects={WORLD_IMPORTS} present=0 ' in line) and 'state=WORLD_IMPORTED_2:' in line for line in hashes)
        if any(lines != world[0] for lines in world) or len(hashes) != 4 or not complete:
            raise AssertionError(f'{name}: world hash lines missing or different between peers')
        result['world_hash_lines'] = len(hashes)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--cc', default='gcc')
    group.add_argument('--zig', type=Path)
    parser.add_argument('--x4', type=Path, help='Linux X4 executable (read only) for the image tests')
    parser.add_argument('--skip-scenarios', action='store_true')
    args = parser.parse_args()
    if platform.system() != 'Linux':
        raise SystemExit('Linux host required (on Windows run it in WSL)')
    cc, version, tag = compiler(args)
    build = ROOT / '.local/linux-gate' / tag
    build.mkdir(parents=True, exist_ok=True)
    report = {'scope': 'Linux gate core, offline: synthetic peers and the executable read from disk; no native X4 run',
              'compiler': version, 'x4': str(args.x4) if args.x4 else None}
    so = build / 'x4lan_gate.so'
    run(cc + FLAGS + ['-O2', '-fPIC', '-shared', '-fvisibility=hidden', LINUX / 'gate_linux.c', '-ldl', '-lpthread', '-o', so])
    report['module'] = module_checks(so)
    if args.zig and report['module']['max_glibc'] not in (None,) and tuple(map(int, report['module']['max_glibc'].split('.'))) > (2, 17):
        raise AssertionError('module needs glibc newer than 2.17')
    unit = build / 'test_gate_linux'
    run(cc + FLAGS + ['-O1', '-g', LINUX / 'test_gate_linux.c', '-ldl', '-lpthread', '-lm', '-o', unit])
    env = dict(os.environ, GATE_WORLD_GOLDEN=str(LINUX / 'test_gate_world_golden.txt'))
    out = subprocess.run([str(unit)] + ([str(args.x4)] if args.x4 else []), check=True, capture_output=True, text=True, timeout=300,
                         env=env).stdout
    print(out, end='')
    report['unit'] = 'pass' + (' with executable image' if args.x4 else ' (synthetic image only)')
    luajit = shutil.which('luajit')
    if luajit:
        out = subprocess.run([luajit, str(LINUX / 'test_gate_lua.lua'), str(so)], check=True, capture_output=True, text=True).stdout
        print(out, end='')
        report['lua_bridge'] = out.strip()
    else:
        report['lua_bridge'] = 'skipped: luajit missing'
    if not args.skip_scenarios:
        run(cc + FLAGS + ['-O2', '-fPIC', '-shared', '-fvisibility=hidden', '-DGATE_LINUX_TEST', LINUX / 'gate_linux.c',
                          '-ldl', '-lpthread', '-o', build / 'x4lan_gate_test.so'])
        run(cc + FLAGS + ['-O1', '-g', '-rdynamic', LINUX / 'test_gate_engine.c', '-ldl', '-lpthread', '-o', build / 'test_gate_engine'])
        report['scenarios'] = []
        for scenario in SCENARIOS:
            started = time.monotonic()
            result = engine_scenario(build, *scenario)
            result['seconds'] = round(time.monotonic() - started, 1)
            print('SCENARIO', json.dumps(result))
            report['scenarios'].append(result)
    mingw = shutil.which('x86_64-w64-mingw32-gcc')
    if mingw:
        probe = build / 'tls_windows.c'
        probe.write_text('#include <windows.h>\n#include <stdint.h>\n#include <string.h>\n#include "lockstep_tls.h"\n'
                         'int tls_probe(void) {unsigned char *b=gate_tls_self();return gate_tls_worker(b) && gate_tls_rng(b) && gate_tls_time(b);}\n')
        run([mingw, '-std=c11', '-Wall', '-Wextra', '-Werror', '-c', '-I', ENGINE, probe, '-o', build / 'tls_windows.o'])
        report['windows_tls_header'] = 'compiles'
    (build / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print('LINUX GATE CHECK PASS', build / 'result.json')


if __name__ == '__main__':
    main()
