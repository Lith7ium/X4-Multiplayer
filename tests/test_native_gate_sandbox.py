"""Lockstep sandbox kit: overlay builder, relay desync notices/status file, desync watcher,
and the generated config block through the real probe (LuaJIT, mocked gate)."""
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock

from lupa.luajit21 import LuaRuntime

ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT/path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


sandbox = load('native_gate_sandbox', 'tools/dev/native_gate_sandbox.py')
relay = load('native_gate_relay_sandbox', 'tools/dev/native_gate_relay.py')
watch = load('native_gate_desync_watch', 'tools/dev/native_gate_desync_watch.py')
PROBE = (ROOT/'tools/dev/native_gate_probe.lua').read_text()
MUX = (ROOT/'tools/dev/native_gate_inputs_core.lua').read_text()
TOKEN = 'a'*32
BASE_CONFIG = '''X4LAN_CONFIG.enabled = false
X4LAN_CONFIG.native_gate_test = true
X4LAN_CONFIG.native_gate_hash = "battle"
X4LAN_CONFIG.slot = "@X4LANE_SLOT@"
X4LAN_CONFIG.native_gate_snapshot_sha = "%s"
X4LAN_CONFIG.native_gate_snapshot_fixture = {'0000000000176a13'}
X4LAN_CONFIG.native_gate_serial_pathing = true
X4LAN_CONFIG.native_gate_world_identities = true
X4LAN_CONFIG.native_gate_world_hash_stride = 60
X4LAN_CONFIG.native_gate_world_hash_schema = 'WORLD_IMPORTED_2'
X4LAN_CONFIG.native_gate_bullet_rng = true
X4LAN_CONFIG.native_gate_nav_rng = true
X4LAN_CONFIG.native_gate_agent_rng = true
X4LAN_CONFIG.native_gate_ship_rng = true
X4LAN_CONFIG.native_gate_script_rng_rows = true
X4LAN_CONFIG.native_gate_pilot_inputs = true
X4LAN_CONFIG.native_gate_pilot_slots = {'0000000000176ad5','0000000000176b36'}
X4LAN_CONFIG.native_gate_pilot_source = 'script'
X4LAN_CONFIG.native_gate_clients = @X4LANE_CLIENTS@
''' % ('7'*64)


def config_args(*extra):
    """The config block printed by the real command line (session notes dropped)."""
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        sandbox.main(['config', *extra])
    return [line for line in out.getvalue().splitlines() if not line.startswith(('-- relay', '-- coordinator', '-- desync watcher'))]


PROBE_STUBS = '''
              X4LAN_CONFIG={native_gate_test=true,test_profile='45841',slot='%s',native_gate_hash='clock',
                native_gate_snapshot_sha=string.rep('a',64),port=12345,token=string.rep('c',32),
                native_gate_serial_pathing=true,native_gate_world_identities=true,
                native_gate_bullet_rng=true,native_gate_nav_rng=true,native_gate_agent_rng=true,native_gate_ship_rng=true}  -- the base overlay always has both (merged_config checks)
              X4LAN_SHA256={digest=function(data) hashed=data;return string.rep('b',64) end}
              logs={};loads=0;reports=0;calls={}
              state={active=1,boot_phase=0,issued=0,report_tick=0,rng='0'}
              local gate={boot=function() return 0 end,poll=function() return state end,
                load_requested=function() state.boot_phase=1;return 1 end,
                report=function(tick) reports=reports+1;state.report_tick=tick;return 1 end,stop=function() state.active=0;return 1 end}
              gate.pilot_inputs=function(...) calls[#calls+1]='inputs:'..table.concat({...},'/');return 1 end
              gate.pilot_device=function(kind,pad,invert) calls[#calls+1]='device:'..kind;return 1,1 end
              gate.pilot_camera_follow=function() calls[#calls+1]='follow';return 1 end
              gate.pilot_native=function(requests) calls[#calls+1]='native:'..requests;return 1 end
              gate.pilot_native_refusals=function() return nil end
              gate.render_rng_split=function(mode) return mode=='split' and 1 or 0 end
              gate.bullet_cadence=function(mode) return mode=='every' and 1 or 0 end
              gate.bullet_cadence_status=function() return 'mode=every' end
              gate.nav_cadence=function(mode) return mode=='far' and 1 or 0 end
              gate.nav_cadence_status=function() return 'mode=far' end
              gate.render_rng_split_status=function() return 'mode=split' end
              gate.view_points=function(mode) return mode=='players' and 1 or 0 end
              gate.view_points_frame=function() return 1 end
              gate.view_points_status=function() return 'mode=players' end
              gate.actor_rng_all=function() return 1 end
              gate.actor_rng_all_status=function() return 'armed=1' end
              gate.pilot_ship=function(slot) return nil,'0000000000000000' end
              gate.propose=function() return 1 end
              gate.wire=function(version,clients) return (version==3 and clients==2) and 1 or 0 end
              gate.world_capture_nav_v2=function(tick) return 'tick='..tick..' state=WORLD_IMPORTED_2:'..tick end
              package.loadlib=function() return function() return gate end end
              local ffi={cdef=function() end,typeof=function() return true end,string=function(s) return s end,
                C={GetSaveFolderPath=function() return 'fake/45841/save' end,IsStartmenu=function() return true end,
                  ReloadSaveList=function() end,IsSaveListLoadingComplete=function() return true end}}
              require=function() return ffi end
              DebugError=function(s) logs[#logs+1]=s end
              RegisterEvent=function() end
              SetScript=function(name,fn) update=fn end
              GetSaveList=function() return {{filename='save_009'}} end
              LoadGame=function() loads=loads+1 end
'''


def probe_runtime(slot):
    """LuaJIT with the mocked gate, FFI and save list the probe needs to arm and load."""
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.execute(PROBE_STUBS % slot)
    return lua


class SandboxBuilderTests(unittest.TestCase):
    def test_default_block_is_the_documented_two_client_sandbox(self):
        lines = config_args()
        self.assertEqual(lines, [
            '-- Lockstep sandbox (tools/dev/native_gate_sandbox.py); docs/LOCKSTEP-SANDBOX.txt',
            'X4LAN_CONFIG.native_gate_clients = @X4LANE_CLIENTS@',
            'X4LAN_CONFIG.native_gate_pilot_inputs = true',
            "X4LAN_CONFIG.native_gate_pilot_slots = {'00000000000ae79c','0000000000176ad5'}",
            "X4LAN_CONFIG.native_gate_pilot_source = 'wire'",
            'X4LAN_CONFIG.native_gate_pilot_delay = 3',
            'X4LAN_CONFIG.native_gate_pilot_window = {30,18000}',
            'X4LAN_CONFIG.native_gate_pilot_suppress_local = true',
            "X4LAN_CONFIG.native_gate_pilot_device = {'kbm','xinput'}",
            'X4LAN_CONFIG.native_gate_pilot_pad = {0,0}',
            'X4LAN_CONFIG.native_gate_pilot_capture = true',
            'X4LAN_CONFIG.native_gate_pilot_ship_state = true',
            'X4LAN_CONFIG.native_gate_render_rng_split = true',
            "X4LAN_CONFIG.native_gate_bullet_cadence = 'every'",
            "X4LAN_CONFIG.native_gate_nav_cadence = 'far'",
            'X4LAN_CONFIG.native_gate_actor_diag = true',
            'X4LAN_CONFIG.native_gate_weapon_diag = true',
            "X4LAN_CONFIG.native_gate_view_points = 'players'",
            'X4LAN_CONFIG.native_gate_actor_rng_all = true',
            'X4LAN_CONFIG.native_gate_world_hash_stride = 1800',
            'X4LAN_CONFIG.native_gate_trace_stride = 60',
            'X4LAN_CONFIG.native_gate_pilot_block_orders = true'])

    def test_options_change_only_their_lines(self):
        lines = config_args('--device2', 'kbm', '--device1', 'none', '--pad2', '1', '--invert', 'pitch,roll',
                            '--battle', 'engage', '--delay', '5', '--window', '40', '900', '--world-stride', '300', '--no-capture-log')
        self.assertIn("X4LAN_CONFIG.native_gate_pilot_device = {false,'kbm'}", lines)
        self.assertIn('X4LAN_CONFIG.native_gate_pilot_pad = {0,1}', lines)
        self.assertIn("X4LAN_CONFIG.native_gate_pilot_invert = {'pitch','roll'}", lines)
        self.assertIn('X4LAN_CONFIG.native_gate_pilot_delay = 5', lines)
        self.assertIn('X4LAN_CONFIG.native_gate_pilot_window = {40,900}', lines)
        self.assertIn('X4LAN_CONFIG.native_gate_world_hash_stride = 300', lines)
        for key in ('camera_follow', 'battle_engage', 'pilot_capture'):
            self.assertFalse(any(key in line for line in lines), key)
        self.assertIn('X4LAN_CONFIG.native_gate_battle_engage = false', config_args('--battle', 'calm'))
        self.assertIn('X4LAN_CONFIG.native_gate_pilot_observe_native = true', config_args('--observe-native'))
        with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()), contextlib.redirect_stdout(io.StringIO()):
            sandbox.main(['config', '--observe-native', '--no-capture-log'])
        follow = config_args('--camera-follow')  # variant 2: client 2 watches its own ship
        self.assertEqual([line for line in follow if line not in config_args()],
                         ['-- camera follow: client cameras differ; attention uses view_points, other camera reads remain',
                          'X4LAN_CONFIG.native_gate_pilot_camera_follow = {false,true}'])
        lines = config_args('--slot1', '0000000000176a13', '--follow1', '--camera-follow')
        self.assertIn('X4LAN_CONFIG.native_gate_pilot_camera_follow = {true,true}', lines)
        self.assertFalse(any('camera_follow' in line for line in config_args('--follow1')))
        self.assertFalse(any('pilot_predict' in line for line in config_args()))
        self.assertEqual([line for line in config_args('--predict1', 'present') if line not in config_args()],
                         ['-- presentation prediction (reports/LOCKSTEP-PREDICTION-20261029.txt)',
                          "X4LAN_CONFIG.native_gate_pilot_predict = {'present',false}"])
        lines = config_args('--predict2', 'observe', '--predict-raw')
        self.assertIn("X4LAN_CONFIG.native_gate_pilot_predict = {false,'observe'}", lines)
        self.assertIn('X4LAN_CONFIG.native_gate_pilot_predict_raw = true', lines)
        self.assertFalse(any('predict_raw' in line for line in config_args('--predict-raw')))  # raw only with a predicting client
        self.assertFalse(any('hit_claims' in line for line in config_args()))
        self.assertIn("X4LAN_CONFIG.native_gate_hit_claims = 'shooter'", config_args('--hit-claims', 'shooter'))
        self.assertFalse(any('pilot_fire' in line for line in config_args()))
        self.assertTrue(any(line.startswith("X4LAN_CONFIG.native_gate_pilot_fire = 'script'") for line in config_args('--fire', 'script')))

    def test_native_controls_replace_the_device_lines(self):
        native = config_args('--controls', 'native')
        default = config_args()
        self.assertEqual([line for line in default if line not in native],
                         ["X4LAN_CONFIG.native_gate_pilot_device = {'kbm','xinput'}", 'X4LAN_CONFIG.native_gate_pilot_pad = {0,0}'])
        self.assertEqual([line for line in native if line not in default],
                         ['-- native controls: lockstep_pilot_native.h (stop reason 113); flight actions are refused and logged',
                          "X4LAN_CONFIG.native_gate_pilot_capture_source = 'native'"])
        self.assertIn('X4LAN_CONFIG.native_gate_pilot_native_requests = false', config_args('--controls', 'native', '--no-native-requests'))
        self.assertFalse(any('native_requests' in line for line in config_args('--no-native-requests')))
        script = config_args('--script-pilots')
        self.assertIn("X4LAN_CONFIG.native_gate_pilot_source = 'script'", script)
        self.assertIn('X4LAN_CONFIG.native_gate_pilot_delay = 0', script)
        notes = sandbox.session_notes(sandbox.build_parser().parse_args(['config', '--controls', 'native', '--window', '30', '600']))
        self.assertTrue(notes[0].startswith('relay ticks: 603 (pilot window 30-600, controls restored at 602)'))
        notes = sandbox.session_notes(sandbox.build_parser().parse_args(['config', '--window', '30', '600']))
        self.assertTrue(notes[0].startswith('relay ticks: 602 (pilot window 30-600, controls restored at 601)'))
        for extra in (['--controls', 'native', '--no-capture-log'], ['--script-pilots', '--no-capture-log'],
                      ['--script-pilots', '--predict1', 'observe'], ['--controls', 'keys']):
            with self.assertRaises(SystemExit, msg=extra), contextlib.redirect_stderr(io.StringIO()), contextlib.redirect_stdout(io.StringIO()):
                sandbox.main(['config', *extra])

    def test_invalid_settings_are_refused(self):
        for extra in (['--slot2', '00000000000ae79c'], ['--slot2', '0000000000176AD5'], ['--slot2', '0'*16], ['--window', '3', '600'],
                      ['--window', '30', '8388608'], ['--window', '600', '30'], ['--delay', '1'], ['--delay', '31'], ['--pad1', '4'],
                      ['--invert', 'pitch,pitch'], ['--invert', 'throttle'], ['--world-stride', '0'], ['--trace-stride', '0'], ['--device1', 'mouse'],
                      ['--predict1', 'always']):
            with self.assertRaises(SystemExit, msg=extra), contextlib.redirect_stderr(io.StringIO()), contextlib.redirect_stdout(io.StringIO()):
                sandbox.main(['config', *extra])

    def make_base(self, root, config=BASE_CONFIG):
        base = root/'base'
        (base/'ui').mkdir(parents=True);(base/'native').mkdir();(base/'md').mkdir()
        (base/'ui/config.lua.append').write_text(config)
        (base/'ui/native_gate_probe.lua').write_text('-- old probe\n')
        (base/'native/x4lan_gate_64.dll').write_bytes(b'old dll')
        (base/'md/native_gate_battle.xml').write_text('<mdscript/>\n')
        (base/'ui.xml.files').write_text('ui/native_gate_probe.lua\n')
        (base/'x4lane-manifest.json').write_text('{"stale": "manifest"}\n')
        return base

    def test_overlay_replaces_owned_keys_files_and_rewrites_the_manifest(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d);base = self.make_base(root)
            (root/'gate.dll').write_bytes(b'new dll');(root/'probe.lua').write_text('-- new probe\n')
            out = root/'out'
            with contextlib.redirect_stdout(io.StringIO()) as printed:
                sandbox.main(['overlay', '--base', str(base), '--out', str(out), '--gate-dll', str(root/'gate.dll'), '--probe', str(root/'probe.lua')])
            self.assertIn('relay ticks: 18002', printed.getvalue())
            text = (out/'ui/config.lua.append').read_text()
            self.assertEqual(text.count('native_gate_clients'), 1)
            self.assertEqual(text.count('native_gate_world_hash_stride'), 1)
            self.assertNotIn("'script'", text);self.assertNotIn('0000000000176b36', text);self.assertNotIn('script_rng_rows', text)
            self.assertIn("X4LAN_CONFIG.native_gate_world_hash_schema = 'WORLD_IMPORTED_2'", text)
            self.assertTrue(text.endswith('X4LAN_CONFIG.native_gate_pilot_block_orders = true\n'))
            self.assertEqual((out/'native/x4lan_gate_64.dll').read_bytes(), b'new dll')
            self.assertEqual((out/'ui/native_gate_probe.lua').read_text(), '-- new probe\n')
            manifest = json.loads((out/'x4lane-manifest.json').read_text())
            self.assertEqual(set(manifest), {'ui/config.lua.append', 'ui/native_gate_probe.lua', 'native/x4lan_gate_64.dll',
                                             'md/native_gate_battle.xml', 'ui.xml.files', 'ui/native_gate_inputs_core.lua'})
            # The input multiplexer loads before the probe (pilot records share the blob with orders).
            self.assertEqual((out/'ui.xml.files').read_text().split(), ['ui/native_gate_inputs_core.lua', 'ui/native_gate_probe.lua'])
            for name, digest in manifest.items():
                self.assertEqual(hashlib.sha256((out/name).read_bytes()).hexdigest(), digest)
            self.assertEqual((base/'ui/config.lua.append').read_text(), BASE_CONFIG)  # base untouched
            # Rebuilding the sandbox from a sandbox overlay is idempotent; the target must be new.
            with contextlib.redirect_stdout(io.StringIO()):
                sandbox.main(['overlay', '--base', str(out), '--out', str(root/'again'), '--full-logs'])
            self.assertEqual((root/'again/ui/config.lua.append').read_text(), text)
            self.assertEqual((root/'again/ui.xml.files').read_text().split().count('ui/native_gate_inputs_core.lua'), 1)
            with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
                sandbox.main(['overlay', '--base', str(base), '--out', str(out)])

    def test_overlay_refuses_bases_that_cannot_run_the_sandbox(self):
        cases = {'X4LAN_CONFIG.native_gate_command_test = "coop"\n': 'native_gate_command_test',
                 'X4LAN_CONFIG.native_gate_camera_perturb = 2\n': 'native_gate_camera_perturb',
                 'X4LAN_CONFIG.native_gate_player_request_observer = true\n': 'native_gate_player_request_observer'}
        with tempfile.TemporaryDirectory() as d:
            for i, (line, key) in enumerate(cases.items()):
                base = self.make_base(Path(d)/str(i), BASE_CONFIG+line)
                with self.assertRaises(ValueError) as caught:
                    sandbox.build_overlay(sandbox.build_parser().parse_args(['overlay', '--base', str(base), '--out', str(Path(d)/f'out{i}')]))
                self.assertIn(key, str(caught.exception))
            for i, (old, new) in enumerate((('"battle"', '"clock"'), ('native_gate_bullet_rng = true', 'native_gate_bullet_rng = false'),
                                            ('native_gate_nav_rng = true', 'native_gate_nav_rng = false'),
                                            ('native_gate_agent_rng = true', 'native_gate_agent_rng = false'),
                                            ('native_gate_ship_rng = true', 'native_gate_ship_rng = false'),
                                            ('native_gate_serial_pathing = true', 'native_gate_serial_pathing = false'))):
                self.assertRaises(ValueError, sandbox.merged_config, BASE_CONFIG.replace(old, new), [])
            self.assertRaises(ValueError, sandbox.merged_config, BASE_CONFIG.replace('X4LAN_CONFIG.native_gate_snapshot_fixture', '-- '), [])

    def test_block_through_the_probe_arms_device_camera_and_ship_state_on_each_client(self):
        for variant, slot, device, follow in (([], 'p1', 'kind=kbm pad=0', False), ([], 'p2', 'kind=xinput pad=0', False),
                                              (['--camera-follow'], 'p1', 'kind=kbm pad=0', False),
                                              (['--camera-follow'], 'p2', 'kind=xinput pad=0', True)):
            lines = [line.replace('@X4LANE_CLIENTS@', '2') for line in config_args(*variant)]
            lua = probe_runtime(slot)
            lua.execute('\n'.join(lines)+"\nX4LAN_CONFIG.native_gate_world_hash_schema='WORLD_IMPORTED_2'")
            lua.execute(MUX);lua.execute(PROBE);lua.execute('update();update();update()')
            logs = list(lua.globals().logs.values())
            self.assertFalse(any('ERROR' in line for line in logs), logs[-3:])
            self.assertEqual(lua.globals().loads, 1)
            calls = list(lua.globals().calls.values())
            self.assertEqual(calls[0], 'inputs:00000000000ae79c,0000000000176ad5/wire/3/30/18000/1')
            self.assertIn('device:'+device.split()[0].split('=')[1], calls)
            self.assertEqual('follow' in calls, follow)
            self.assertTrue(any('PILOT_DEVICE_ARMED '+device in line for line in logs))


    def test_native_controls_block_through_the_probe(self):
        for slot in ('p1', 'p2'):
            lines = [line.replace('@X4LANE_CLIENTS@', '2') for line in config_args('--controls', 'native')]
            lua = probe_runtime(slot)
            lua.execute('\n'.join(lines)+"\nX4LAN_CONFIG.native_gate_world_hash_schema='WORLD_IMPORTED_2'")
            lua.execute(MUX);lua.execute(PROBE);lua.execute('update();update();update()')
            logs = list(lua.globals().logs.values())
            self.assertFalse(any('ERROR' in line for line in logs), logs[-3:])
            self.assertEqual(lua.globals().loads, 1)
            calls = list(lua.globals().calls.values())
            self.assertEqual(calls[0], 'inputs:00000000000ae79c,0000000000176ad5/wire/3/30/18000/1')
            self.assertIn('native:1', calls)
            self.assertFalse(any(call.startswith('device:') for call in calls))
            self.assertTrue(any('PILOT_NATIVE_ARMED slot='+slot[1]+' requests=1' in line for line in logs))
        # The smoke variant: scripted pilots, native controls installed, no human input.
        lines = [line.replace('@X4LANE_CLIENTS@', '2') for line in config_args('--controls', 'native', '--script-pilots', '--no-native-requests')]
        lua = probe_runtime('p1')
        lua.execute('\n'.join(lines)+"\nX4LAN_CONFIG.native_gate_world_hash_schema='WORLD_IMPORTED_2'")
        lua.execute(MUX);lua.execute(PROBE);lua.execute('update();update();update()')
        calls = list(lua.globals().calls.values())
        self.assertEqual(calls[0], 'inputs:00000000000ae79c,0000000000176ad5/script/0/30/18000/1')
        self.assertIn('native:0', calls)
        self.assertFalse(any('ERROR' in line for line in lua.globals().logs.values()))


class RelayDesyncTests(unittest.TestCase):
    def send(self, b, slot, tick, digest='b'*64):
        bits = struct.pack('>d', 20+tick/60).hex()
        return b.receive(f'NGSTATE|1|{TOKEN}|{slot}|{tick}|{bits}|{bits}|{digest}'.encode(), ('127.0.0.1', 10000+slot), tick)

    def run_ticks(self, b, differing):
        notices = []
        for tick in range(b.limit+1):
            self.send(b, 1, tick);self.send(b, 2, tick, 'c'*64 if tick in differing else 'b'*64)
            notices += [(tick, n) for n in b.take_notices()]
        return notices

    def test_observe_prints_desync_once_per_run_repeats_rarely_and_resyncs(self):
        b = relay.Barrier(TOKEN, ticks=1300, hold_tick=9999, observe=True)
        notices = self.run_ticks(b, set(range(5, 8)) | set(range(20, 1301)))
        self.assertEqual(notices, [
            (5, 'NATIVE_GATE DESYNC tick=5 slots=2 first=5 mismatches=1'),
            (8, 'NATIVE_GATE RESYNC tick=8 mismatches=3'),
            (20, 'NATIVE_GATE DESYNC tick=20 slots=2 first=5 mismatches=4'),
            (620, 'NATIVE_GATE DESYNC tick=620 slots=2 first=5 mismatches=604'),
            (1220, 'NATIVE_GATE DESYNC tick=1220 slots=2 first=5 mismatches=1204')])
        status = b.status(12.3456)
        self.assertEqual(status, {'tick': 1299, 'desync': True, 'diverged_now': True, 'first_mismatch': 5, 'last_mismatch': 1300,
                                  'mismatches': 1284, 'slots': [2], 'failure': None, 'done': True, 'observe': True, 'updated': 12.346})
        self.assertNotIn('failure=', ''.join(n for _, n in notices))  # the lane runner fails on failure=<non-none>

    def test_strict_mode_has_no_notices_and_clean_runs_none(self):
        b = relay.Barrier(TOKEN, ticks=3, hold_tick=99)
        self.send(b, 1, 0);self.send(b, 2, 0, 'c'*64)
        self.assertEqual(b.take_notices(), []);self.assertEqual(b.failed, 'battle_state_mismatch_tick_0')
        b = relay.Barrier(TOKEN, ticks=10, hold_tick=99, observe=True)
        self.assertEqual(self.run_ticks(b, set()), [])
        self.assertFalse(b.status(0)['desync'])

    def test_main_writes_the_status_file_and_prints_the_notice(self):
        def packet(slot, tick, digest):
            bits = struct.pack('>d', 20+tick/60).hex()
            return (f'NGSTATE|1|{TOKEN}|{slot}|{tick}|{bits}|{bits}|{digest}'.encode(), ('127.0.0.1', 10000+slot))
        class FakeSocket:
            def __init__(self):
                self.inputs = iter([packet(1, 0, 'b'*64), packet(2, 0, 'b'*64), packet(1, 1, 'b'*64), packet(2, 1, 'c'*64)])
            def bind(self, address): pass
            def settimeout(self, value): pass
            def recvfrom(self, size): return next(self.inputs)
            def sendto(self, packet, address): pass
            def close(self): pass
        with tempfile.TemporaryDirectory() as d:
            status = Path(d)/'desync-status.json'
            argv = ['relay', '--port', '1', '--token', TOKEN, '--ticks', '1', '--report', str(Path(d)/'r.json'), '--observe',
                    '--status-file', str(status)]
            out = io.StringIO()
            with mock.patch('sys.argv', argv), mock.patch.object(relay.socket, 'socket', return_value=FakeSocket()), contextlib.redirect_stdout(out):
                with self.assertRaises(StopIteration): relay.main()
            value = json.loads(status.read_text())
        self.assertIn('NATIVE_GATE DESYNC tick=1 slots=2 first=1 mismatches=1', out.getvalue())
        self.assertTrue(value['desync'] and value['done'] and value['first_mismatch'] == 1 and value['slots'] == [2])


class DesyncWatchTests(unittest.TestCase):
    def test_log_lines_drive_desync_resync_and_end(self):
        w = watch.Watch()
        self.assertEqual(w.line('NATIVE_GATE tick=10 failure=none done=False'), [])
        self.assertEqual(w.summary(), 'OK tick=10')
        self.assertEqual(w.line('NATIVE_GATE DESYNC tick=11 slots=2 first=11 mismatches=1'), [('desync', 11, '2', 11, 1)])
        self.assertEqual(w.summary(), 'DESYNC tick=11 first=11 mismatches=1 slots=2')
        self.assertEqual(w.line('NATIVE_GATE RESYNC tick=14 mismatches=3'), [('resync', 14, 3)])
        self.assertTrue(w.summary().endswith('resynced'))
        self.assertEqual(w.line('NATIVE_GATE tick=20 failure=none done=True first_mismatch=11 mismatches=3'), [('end', 20, None)])
        self.assertEqual(w.line('NATIVE_GATE tick=20 failure=none done=True'), [])
        self.assertEqual(w.summary(), 'END tick=20 verdict=diverged mismatches=3')
        w = watch.Watch()
        self.assertEqual(w.line('NATIVE_GATE tick=7 failure=native_clock_mismatch_tick_7 done=False'), [('end', 7, 'native_clock_mismatch_tick_7')])
        self.assertEqual(w.summary(), 'END tick=7 verdict=failure=native_clock_mismatch_tick_7 mismatches=0')

    def test_status_snapshots(self):
        w = watch.Watch()
        self.assertEqual(w.snapshot({'tick': 3, 'mismatches': 0, 'diverged_now': False}), [])
        events = w.snapshot({'tick': 9, 'mismatches': 2, 'first_mismatch': 8, 'last_mismatch': 9, 'slots': [2], 'diverged_now': True})
        self.assertEqual(events, [('desync', 9, '2', 8, 2)])
        self.assertEqual(w.snapshot({'tick': 10, 'mismatches': 3, 'first_mismatch': 8, 'last_mismatch': 10, 'slots': [2], 'diverged_now': True}), [])
        self.assertEqual(w.snapshot({'tick': 12, 'mismatches': 3, 'diverged_now': False}), [('resync', 12, 3)])
        self.assertEqual(w.snapshot({'tick': 13, 'mismatches': 3, 'done': True}), [('end', 13, None)])

    def test_follower_keeps_partial_lines_and_waits_for_the_file(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d)/'server.log';f = watch.LogFollower(path)
            self.assertEqual(f.lines(), [])
            path.write_bytes(b'NATIVE_GATE tick=1 failure=none done=False\r\nNATIVE_GATE DES')
            self.assertEqual(f.lines(), ['NATIVE_GATE tick=1 failure=none done=False'])
            with path.open('ab') as out: out.write(b'YNC tick=2 slots=2 first=2 mismatches=1\n')
            self.assertEqual(f.lines(), ['NATIVE_GATE DESYNC tick=2 slots=2 first=2 mismatches=1'])

    def test_alerts_banner_beep_title_and_safe_toast(self):
        out, commands, beeps, titles = io.StringIO(), [], [], []
        alerts = watch.Alerts(beep=True, toast=True, out=out, runner=commands.append, beeper=lambda: beeps.append(1), title=titles.append)
        alerts(('desync', 42, '2', 40, 3))
        self.assertIn('*** X4 LOCKSTEP DESYNC at tick 42: client(s) 2 differ from client 1 (first 40, 3 differing ticks)', out.getvalue())
        self.assertEqual(beeps, [1]);self.assertEqual(titles, ['X4 lockstep: DESYNC tick=42'])
        self.assertEqual(commands[0][:2], ['powershell.exe', '-NoProfile'])
        self.assertIn("CreateTextNode('X4 lockstep DESYNC')", commands[0][-1])
        alerts(('resync', 50, 3));self.assertEqual(len(commands), 1);self.assertEqual(beeps, [1])
        alerts(('end', 60, None));self.assertEqual(len(commands), 2)
        for text in ("x'y", 'a;b', '$(x)', 'a"b', 'a`b'):
            self.assertRaises(ValueError, watch.toast_command, 'title', text)

    def test_run_once_and_until_the_end_with_status_out(self):
        with tempfile.TemporaryDirectory() as d:
            log, summary = Path(d)/'server.log', Path(d)/'sandbox-status.txt'
            log.write_text('NATIVE_GATE tick=5 failure=none done=False\nNATIVE_GATE DESYNC tick=6 slots=2 first=6 mismatches=1\n')
            events = []
            args = watch.argparse.Namespace(log=str(log), status=None, status_out=str(summary), once=True, timeout=0, interval=0.01)
            w = watch.run(args, events.append, sleep=lambda s: None)
            self.assertEqual(events, [('desync', 6, '2', 6, 1)]);self.assertTrue(w.desync)
            self.assertEqual(summary.read_text(), 'DESYNC tick=6 first=6 mismatches=1 slots=2\n')
            appended = iter(['NATIVE_GATE tick=9 failure=none done=True\n'])
            def sleep(seconds):
                with log.open('a') as f: f.write(next(appended))
            args.once = False
            w = watch.run(args, events.append, sleep=sleep)
            self.assertTrue(w.ended);self.assertEqual(summary.read_text(), 'END tick=9 verdict=diverged mismatches=1\n')
            status = Path(d)/'status.json';status.write_text(json.dumps({'tick': 4, 'mismatches': 0, 'done': True}))
            args = watch.argparse.Namespace(log=None, status=str(status), status_out=None, once=False, timeout=0, interval=0.01)
            self.assertEqual(watch.run(args, lambda e: None, sleep=lambda s: None).summary(), 'END tick=4 verdict=pass mismatches=0')


if __name__ == '__main__':
    unittest.main()
