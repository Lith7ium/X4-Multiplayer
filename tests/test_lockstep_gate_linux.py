"""Linux gate core: site table, executable pins, probe library choice and the native checks."""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('gate_linux_sites', ROOT / 'tools/dev/check_lockstep_gate_linux_sites.py')
sites = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sites)
# The read-only executables of the x4-re workspace next to this checkout (or X4_LINUX_EXE / X4_WINDOWS_EXE).
LINUX_EXE = Path(os.environ.get('X4_LINUX_EXE', ROOT.parent / 'x4-re/bin-linux/X4'))
WINDOWS_EXE = Path(os.environ.get('X4_WINDOWS_EXE', ROOT.parent / 'x4-re/bin/X4.exe'))


class SiteTable(unittest.TestCase):
    def setUp(self):
        self.c, self.sites = sites.parse_sites()

    def test_sites_are_well_formed_and_disjoint(self):
        names = [s['name'] for s in self.sites]
        self.assertEqual(len(names), len(set(names)))
        self.assertGreaterEqual(len(self.sites), 12)
        ordered = sorted(self.sites, key=lambda s: s['va'])
        for a, b in zip(ordered, ordered[1:]):
            self.assertLessEqual(a['va'] + a['length'], b['va'], f"{a['name']} overlaps {b['name']}")
        for s in self.sites:
            # site_ok hashes in place; only the dt site goes through its 0x200-byte copy.
            limit = 0x200 if s['name'] == 'frame_tick_time_advance' else 0x3000
            self.assertTrue(0x400000 <= s['va'] < 0x3e00000 and 5 <= s['length'] <= limit, s['name'])

    def test_no_pin_or_patch_in_an_exact_reciprocal_window(self):
        text = (ROOT / 'src/native/portable_math/preload/exact_reciprocal_linux_sites.h').read_text(encoding='utf-8')
        windows = [(int(a, 16), int(n)) for a, n in re.findall(r'\{0x([0-9a-f]+)u,(\d+),', text)]
        self.assertEqual(len(windows), 765)
        c = self.c
        spans = [(s['va'], s['length'], s['name']) for s in self.sites] + \
                [(c['LX_CALL_WINDOW'], 16, 'call window'), (c['LX_DT_LOAD_VA'], 16, 'dt'), (c['LX_IMPORT_INSERT_VA'], 8, 'import entry'),
                 (c['LX_IMPORT_CLEAR_VA'], 8, 'clear entry')]
        for va, length, name in spans:
            self.assertFalse(any(a < va + length and va < a + n for a, n in windows), name)

    def test_world_identity_patch_geometry(self):
        c = self.c
        self.assertEqual(c['LX_IMPORT_INSERT_VA'] % 16, 0)  # one aligned 8-byte compare-exchange
        original = c['LX_IMPORT_ENTRY_ORIGINAL'].to_bytes(8, 'little')
        self.assertEqual(original[:c['LX_IMPORT_PROLOGUE']], b'\x41\x56\x41\x55\x41\x54')  # push r14/r13/r12, relocatable
        self.assertEqual(original[5:], b'\x54\x49\x89')  # the bytes kept after the 5-byte jump
        self.assertEqual(c['LX_IMPORT_CLEAR_VA'] % 16, 0)
        original = c['LX_IMPORT_CLEAR_ORIGINAL'].to_bytes(8, 'little')
        self.assertEqual(c['LX_IMPORT_CLEAR_PROLOGUE'], 5)  # exactly two whole instructions under the 5-byte jump
        self.assertEqual(original[:5], b'\x53\x48\x83\xec\x40')  # push rbx; sub rsp,0x40, relocatable
        self.assertEqual(original[5:], b'\x48\x8b\x57')  # mov rdx,[rdi+8], where the trampoline returns

    def test_patch_geometry(self):
        c = self.c
        self.assertEqual(c['LX_CALL_WINDOW'] % 16, 0)
        self.assertTrue(c['LX_CALL_WINDOW'] <= c['LX_CALL_VA'] + 1 and c['LX_CALL_VA'] + 5 <= c['LX_CALL_WINDOW'] + 16)
        self.assertEqual((c['LX_DT_LOAD_VA'] % 8, c['LX_DT_MIN_VA'] - c['LX_DT_LOAD_VA']), (0, 8))
        original, patched = c['LX_DT_MIN_ORIGINAL'].to_bytes(8, 'little'), c['LX_DT_MIN_PATCHED'].to_bytes(8, 'little')
        self.assertEqual((patched[:4], patched[4:]), (b'\x0f\x1f\x40\x00', original[4:]))  # nop4, the pxor kept
        self.assertEqual(c['LX_DT_LOAD_ORIGINAL'].to_bytes(8, 'little')[:4], b'\xf2\x0f\x10\x0d')  # movsd xmm1,[rip+d]

    def test_tls_offsets_are_inside_the_static_block(self):
        tls = sites.parse_tls()
        self.assertEqual((tls['LINUX_RNG'], tls['LINUX_TIME'], tls['LINUX_VIEW']), (-0x2720, -0x2728, -0x2620))
        self.assertEqual((tls['WINDOWS_RNG'], tls['WINDOWS_TIME'], tls['WINDOWS_VIEW']), (0x318, 0x300, 0x314))
        for name, offset in tls.items():
            if name.startswith('LINUX_'):
                self.assertTrue(-self.c['LX_TLS_MEMSZ'] <= offset and offset + 8 <= 0, name)

    def test_every_replaced_windows_constant_has_a_known_pair(self):
        self.assertEqual(len(sites.check_pairs(self.c)), len(sites.PAIRS) + len(sites.EXTRA_PAIRS))


@unittest.skipUnless(LINUX_EXE.is_file(), 'Linux X4 executable not available (set X4_LINUX_EXE)')
class Executables(unittest.TestCase):
    def test_linux_pins_and_decoded_references(self):
        c, table = sites.parse_sites()
        findings = sites.check_linux(LINUX_EXE, c, table, sites.parse_tls())
        self.assertIn('site:frame_tick_time_advance', findings)
        self.assertIn('tls_rng_store', findings)

    @unittest.skipUnless(WINDOWS_EXE.is_file(), 'Windows X4.exe not available (set X4_WINDOWS_EXE)')
    def test_windows_counterparts(self):
        self.assertIn('windows_movie_capture_path', sites.check_windows(WINDOWS_EXE))

    def test_world_hash_references(self):
        c, table = sites.parse_sites()
        findings = sites.check_linux(LINUX_EXE, c, table, sites.parse_tls())
        for name in ('import_insert_entry', 'import_universe_map', 'component_registry', 'component_find_call', 'bank_read_selector',
                     'bank_write_selector', 'shield_source_vtable', 'no_pin_in_exact_reciprocal_window', 'no_patch_in_exact_reciprocal_window'):
            self.assertIn(name, findings)

    @unittest.skipUnless(WINDOWS_EXE.is_file(), 'Windows X4.exe not available (set X4_WINDOWS_EXE)')
    def test_world_class_table_is_current(self):
        """gate_world_classes.h equals what the generator derives from both executables, and the
        Linux IsRealClass stubs give the Windows class flags for every row."""
        generator = load_tool('lockstep_linux_world_classes')
        rows, unmatched, mismatched, linux_only = generator.build(WINDOWS_EXE, LINUX_EXE)
        self.assertEqual(mismatched, [])
        self.assertEqual(generator.render(rows), generator.OUT.read_text())
        families = {r[1]: r[3] for r in rows}
        self.assertEqual((families[0x2c51158], families[0x2c3fdc0], families[0x2c3e568], families[0x2a34b38], families[0x2abd888]),
                         (1, 2, 3, 4, 5))
        # What the census shows: a controller is found through its interface subobject's vtable.
        by_windows = {r[1]: r for r in rows}
        for windows, linux, top in ((0x2c40b40, 0x3b8e878, -88), (0x2c406b8, 0x3b6c440, -88), (0x2c3f5f0, 0x3affe50, -88),
                                    (0x2ad0c20, 0x3b37030, -88), (0x2c3eec0, 0x3b5edf8, -88), (0x2c39488, 0x3b7c208, 0),
                                    (0x2c3fdc0, 0x3b8c1a8, -88), (0x2c3e568, 0x3b4bcb8, -88), (0x2abd888, 0x3b0d600, -88),
                                    (0x2a34b38, 0x3b00110, 0), (0x2c51158, 0x3b697e8, 0)):
            self.assertEqual(by_windows[windows][0], linux)
            self.assertEqual(by_windows[windows][4], top)
        self.assertEqual(mismatched, [])
        self.assertEqual(linux_only, [])
        # Only abstract classes stay unpaired: they cannot be an object or a controller.
        self.assertEqual(sorted(name for name, _ in unmatched), sorted([
            '.?AVMovementControllerInterface@U@@', '.?AVCollectable@U@@', '.?AVShip@U@@',
            '.?AV?$MovementControllerBase@V?$IndirectValueSource@VPosRot@Math@@@XLib@@@U@@',
            '.?AV?$NestableMovementControllerBase@V?$IndirectValueSource@VPosRot@Math@@@XLib@@@U@@']))
        self.assertTrue(all(reason.startswith('abstract') for _, reason in unmatched))


def load_tool(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'tools/dev' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class WorldClassNames(unittest.TestCase):
    def test_msvc_to_itanium(self):
        generator = load_tool('lockstep_linux_world_classes')
        self.assertEqual(generator.itanium('.?AVShipSmall@U@@'), 'N1U9ShipSmallE')
        self.assertEqual(generator.itanium('.?AVDefaults@ShieldGenerator@U@@'), 'N1U15ShieldGenerator8DefaultsE')
        self.assertEqual(generator.itanium('.?AVPlain@@'), '5Plain')
        self.assertEqual(generator.itanium('.?AV?$MovementControllerBase@V?$ValueSource@VPosRot@Math@@@XLib@@@U@@'),
                         'N1U22MovementControllerBaseIN4XLib11ValueSourceIN4Math6PosRotEEEEE')
        self.assertIsNone(generator.itanium('.?AV?$Other@H@U@@'))


class LiveRunner(unittest.TestCase):
    """tools/dev/linux_gate_live.py: the extension composition follows the lane tool's overlay rules."""
    def test_overlay_composition(self):
        live = load_tool('linux_gate_live')
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            target, overlay = root / 'lab', root / 'overlay'
            (target / 'ui').mkdir(parents=True)
            (overlay / 'ui').mkdir(parents=True)
            (overlay / 'native').mkdir()
            (target / 'ui.xml').write_text('<addon>\n    <file name="ui/config.lua" />\n    <file name="ui/main.lua" />\n</addon>\n')
            (target / 'ui/config.lua').write_text('X4LAN_CONFIG={}')
            (overlay / 'ui/config.lua.append').write_text('X4LAN_CONFIG.port = @X4LANE_PORT@\n')
            (overlay / 'ui.xml.files').write_text('# probe\nui/native_gate_probe.lua\n')
            (overlay / 'ui/native_gate_probe.lua').write_text('-- probe')
            (overlay / 'native/x4lan_gate.so').write_bytes(b'gate')
            (overlay / 'x4lane-manifest.json').write_text('{}')
            live.apply_overlay(target, overlay)
            self.assertFalse((target / 'x4lane-manifest.json').exists())
            self.assertEqual((target / 'ui/config.lua').read_text(), 'X4LAN_CONFIG={}\nX4LAN_CONFIG.port = @X4LANE_PORT@\n')
            self.assertEqual((target / 'ui.xml').read_text(), '<addon>\n    <file name="ui/config.lua" />\n'
                             '    <file name="ui/native_gate_probe.lua" />\n    <file name="ui/main.lua" />\n</addon>\n')
            self.assertEqual((target / 'native/x4lan_gate.so').read_bytes(), b'gate')
            self.assertFalse((target / 'ui.xml.files').exists())

    def test_failed_stop_ends_the_run_soon(self):
        """The first live runs: both clients stopped with 72 at boot and ran free for 30 minutes."""
        live = load_tool('linux_gate_live')
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / 'engine-output.log'
            log.write_text('[=ERROR=] 7.12 [X4LAN] NATIVE_GATE STOP slot=p1 completed=0 reason=72 restored=1 wait_game_delta=0\n'
                           '[=ERROR=] 7.12 [X4LAN] NATIVE_GATE WORLD_IDENTITY_STOP calls=5 count=1 late=0 restored=1 failures=4\n')
            watch = live.LogWatch(log)
            watch.poll()
            self.assertTrue(watch.stopped)
            self.assertEqual(watch.stop_reason, 72)
        error = live.stop_error
        # One failed: the peer gets a moment to report its own, never long.
        self.assertIsNone(error({1: 72}, {1: 100.0}, 100.0 + live.FAILED_STOP_PEER - 1))
        self.assertIn('client 1: NATIVE_GATE STOP reason=72', error({1: 72}, {1: 100.0}, 100.0 + live.FAILED_STOP_PEER))
        # Both in: only the trailing lines are still awaited, counted from the last STOP.
        both = {1: 100.0, 2: 150.0}
        self.assertIsNone(error({1: 72, 2: 72}, both, 150.0 + live.FAILED_STOP_SETTLE - 1))
        message = error({1: 72, 2: 72}, both, 150.0 + live.FAILED_STOP_SETTLE)
        self.assertIn('client 1', message)
        self.assertIn('client 2', message)
        self.assertIn('client 2: NATIVE_GATE STOP reason=8', error({1: 1, 2: 8}, both, 150.0 + live.FAILED_STOP_SETTLE))
        # Clean stops wait for the relay; a peer that never stops ends the run too.
        self.assertIsNone(error({1: 1, 2: 1}, both, 1e9))
        self.assertIsNone(error({}, {}, 1e9))
        self.assertIsNone(error({1: 1}, {1: 100.0}, 100.0 + live.PEER_STOP - 1))
        self.assertIn('no peer STOP', error({1: 1}, {1: 100.0}, 100.0 + live.PEER_STOP))
        # A gate error stops a client too: the peer gets its moment, then the text is the error.
        text = 'NATIVE_GATE ERROR capture failed stage=controller_class_unmapped'
        self.assertIsNone(error({1: text}, {1: 100.0}, 101.0))
        self.assertEqual(error({1: text, 2: text}, both, 150.0 + live.FAILED_STOP_SETTLE), f'client 1: {text}; client 2: {text}')

    def test_portable_math_preload(self):
        """The FP run: the preload and both patches, and a run that counts only if they are active."""
        live = load_tool('linux_gate_live')
        with tempfile.TemporaryDirectory() as tmp:
            case = Path(tmp)
            env = live.portable_math_env(case)
            self.assertEqual(env['LD_PRELOAD'], str(case / 'libx4mp_portable_math.so'))
            self.assertEqual((env['X4MP_PORTABLE_DECIMAL'], env['X4MP_EXACT_RECIPROCAL']), ('1', '1'))
            self.assertEqual(live.portable_math_state(case), (False, []))
            log = case / 'portable-math.log'
            active = ('X4MP_PORTABLE_MATH ACTIVE version=1 kat=06e026f4169223c1 symbols=28 decimal=1 exact_reciprocal=1185/1185 '
                      f'mxcsr_control=1f80 pid=7 exe={case}/game/X4\n')
            log.write_text(active)
            self.assertTrue(live.portable_math_state(case)[0])
            # The live run: a child of X4 inherits the preload, is refused and says so. That is not X4.
            child = 'X4MP_PORTABLE_MATH REFUSED engine patches requested but the executable is not the pinned Linux X4 9.00 exe=/usr/bin/dbus-launch\n'
            log.write_text(active + child)
            self.assertTrue(live.portable_math_state(case)[0])
            self.assertEqual(len(live.portable_math_state(case)[1]), 2)
            log.write_text(child)
            self.assertFalse(live.portable_math_state(case)[0])
            log.write_text(active.replace('decimal=1', 'decimal=0'))
            self.assertFalse(live.portable_math_state(case)[0])
            log.write_text(active.replace('1185/1185', '1184/1185'))
            self.assertFalse(live.portable_math_state(case)[0])
            log.write_text(active + f'X4MP_PORTABLE_MATH REFUSED pin exe={case}/game/X4\n')
            self.assertFalse(live.portable_math_state(case)[0])

    @unittest.skipUnless(sys.platform.startswith('linux'), 'Linux /proc')
    def test_x4_is_found_by_its_executable_not_its_name(self):
        """X4 renames its main thread: comm says 'Main()', only the executable says X4."""
        live = load_tool('linux_gate_live')
        with tempfile.TemporaryDirectory() as tmp:
            program = Path(tmp) / 'X4'
            shutil.copy2(shutil.which('bash'), program)
            # Only a process itself may rename itself: the shell's builtin does, then it waits.
            process = subprocess.Popen([str(program), '-c', 'printf "Main()" > /proc/$$/comm; echo ready; read line'],
                                       stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
            try:
                self.assertEqual(process.stdout.readline().strip(), 'ready')
                self.assertEqual(Path(f'/proc/{process.pid}/comm').read_text().strip(), 'Main()')
                self.assertIn((process.pid, str(program.resolve())), live.x4_executables())
            finally:
                process.communicate('')

    @unittest.skipUnless(platform.system() == 'Linux', 'symbolic links of the private view need Linux')
    def test_private_instance_staging(self):
        live = load_tool('linux_gate_live')
        with tempfile.TemporaryDirectory() as folder:
            root, game, home = Path(folder) / 'bundle', Path(folder) / 'game', Path(folder) / 'home'
            (root / 'snapshot').mkdir(parents=True)
            (root / 'snapshot' / live.SLOT_SAVE).write_bytes(b'owned snapshot')
            (root / 'extension/x4_lan_lab/ui').mkdir(parents=True)
            (root / 'extension/x4_lan_lab/ui/config.lua').write_text('port=@X4LANE_PORT@ token="@X4LANE_TOKEN@" slot="@X4LANE_SLOT@" clients=@X4LANE_CLIENTS@\n')
            for name in ('ego_dlc_boron', 'ego_dlc_split', 'workshop_mod'):
                (game / 'extensions' / name).mkdir(parents=True)
            (home / '.steam').mkdir(parents=True)
            (home / '.steam/steam.pid').write_text('1')
            run = Path(folder) / 'run'
            case, profile, binds, lock, ids = live.stage_case(root, run, 2, game, home, 45999, 'ab' * 16, 4)
            self.assertEqual(ids, ['ego_dlc_boron', 'ego_dlc_split', 'x4_lan_lab'])  # never other mods
            self.assertEqual([b[0].name for b in binds], ['ego_dlc_boron', 'ego_dlc_split'])
            self.assertEqual((profile / 'save' / live.SLOT_SAVE).read_bytes(), b'owned snapshot')
            self.assertIn('<autosave>false</autosave>', (profile / 'config.xml').read_text())
            self.assertIn('<uisafemode>false</uisafemode>', (profile / 'config.xml').read_text())
            self.assertEqual((case / 'extensions/x4_lan_lab/ui/config.lua').read_text(),
                             f'port=45999 token="{"ab" * 16}" slot="p2" clients=4\n')
            self.assertTrue((case / 'home/.steam/steam.pid').is_symlink() and lock.is_file())
            command = live.namespace_command(case, game, binds, lock, {'HOME': str(case / 'home')}, ['X4'], 'x4server')
            self.assertEqual(command[:5], ['runuser', '-u', 'x4server', '--', 'bwrap'])
            self.assertIn('/tmp/EGOSOFT_X4.lock', command)


class ProbeLibrary(unittest.TestCase):
    """The probe loads the platform's gate module and names a refused verification stage."""
    def run_probe(self, separator, stage):
        from lupa.luajit21 import LuaRuntime
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.execute('''
          X4LAN_CONFIG={native_gate_test=true,test_profile='45841',slot='p1',native_gate_hash='clock',
            native_gate_snapshot_sha=string.rep('a',64),port=12345,token=string.rep('c',32)}
          X4LAN_SHA256={digest=function() return string.rep('b',64) end}
          logs={};paths={}
          local gate={boot=function() return 11 end,poll=function() return nil end,stop=function() return 1 end}
          if %s then gate.verify_stage=function() return 'frame_tick_head' end end
          package.config='%s'..string.sub(package.config,2)
          package.loadlib=function(path) paths[#paths+1]=path;return function() return gate end end
          local ffi={cdef=function() end,typeof=function() return true end,string=function(s) return s end,
            C={GetSaveFolderPath=function() return 'fake/45841/save' end,GetCurrentGameTime=function() return 7 end,
              IsStartmenu=function() return true end,ReloadSaveList=function() end,IsSaveListLoadingComplete=function() return true end}}
          require=function(name) assert(name=='ffi');return ffi end
          DebugError=function(s) logs[#logs+1]=s end
          RegisterEvent=function() end
          SetScript=function(name,fn) update=fn end
        ''' % ('true' if stage else 'false', separator))
        lua.execute((ROOT / 'tools/dev/native_gate_probe.lua').read_text())
        lua.execute('update()')
        return list(lua.globals().paths.values()), list(lua.globals().logs.values())

    def test_windows_loads_the_dll(self):
        paths, logs = self.run_probe('\\\\', False)
        self.assertEqual(paths, ['./extensions/x4_lan_lab/native/x4lan_gate_64.dll'])
        self.assertTrue(logs[-1].endswith('REFUSED code=11'), logs)

    def test_linux_loads_the_so_and_names_the_stage(self):
        paths, logs = self.run_probe('/', True)
        self.assertEqual(paths, ['./extensions/x4_lan_lab/native/x4lan_gate.so'])
        self.assertTrue(logs[-1].endswith('REFUSED code=11 stage=frame_tick_head'), logs)


class Overlay(unittest.TestCase):
    """build_native_gate_overlay.py --gate-so: the Linux module in place of the DLL, core options only."""
    def build(self, *extra):
        sys.path.insert(0, str(ROOT / 'tools/dev'))
        builder = importlib.import_module('build_native_gate_overlay')
        root = Path(self.tmp.name)
        (root / 'gate.so').write_bytes(b'linux gate');(root / 'save.xml.gz').write_bytes(b'owned snapshot')
        out = root / ('out%d' % len(list(root.iterdir())))
        argv = ['overlay', '--gate-so', str(root / 'gate.so'), '--out', str(out), *extra]
        with mock.patch('sys.argv', argv), contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            builder.main()
        return out

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.snapshot = ['--snapshot', str(Path(self.tmp.name) / 'save.xml.gz')]

    def tearDown(self):
        self.tmp.cleanup()

    def test_linux_overlay_ships_the_so_and_the_core_configuration(self):
        out = self.build(*self.snapshot, '--mode', 'clock', '--session-clients', '--seed-rng')
        manifest = {name.replace(os.sep, '/') for name in json.loads((out / 'x4lane-manifest.json').read_text())}
        self.assertIn('native/x4lan_gate.so', manifest)
        self.assertFalse(any(name.endswith('.dll') for name in manifest))
        self.assertEqual((out / 'native/x4lan_gate.so').read_bytes(), b'linux gate')
        config = (out / 'ui/config.lua.append').read_text()
        for line in ('native_gate_snapshot_sha', 'native_gate_seed_rng = true', 'native_gate_clients = @X4LANE_CLIENTS@'):
            self.assertIn(line, config)

    def test_linux_overlay_refuses_unported_options(self):
        for extra in ([*self.snapshot, '--mode', 'battle'], ['--mode', 'clock'],
                      [*self.snapshot, '--mode', 'clock', '--path-determinism', '1'],
                      [*self.snapshot, '--mode', 'clock', '--radar-diag'],
                      [*self.snapshot, '--mode', 'clock', '--world-identities', '--world-hash-stride', '60', '--world-trace-early']):
            with self.assertRaises(SystemExit, msg=extra):
                self.build(*extra)

    def test_live_launch_order(self):
        live = load_tool('linux_gate_live')
        self.assertEqual(live.launch_order(None, 3), [1, 2, 3])
        self.assertEqual(live.launch_order('2,1', 2), [2, 1])
        for wrong in ('1', '1,1', '1,2,3', '0,1'):
            with self.assertRaises(SystemExit):
                live.launch_order(wrong, 2)

    def test_linux_overlay_takes_the_nav_deadlines(self):
        base = (*self.snapshot, '--mode', 'clock', '--world-identities')
        config = (self.build(*base, '--nav-cadence', 'far', '--nav-params-init', '--nav-probe-init') / 'ui/config.lua.append').read_text()
        for line in ("native_gate_nav_cadence = 'far'", 'native_gate_nav_params_init = true', 'native_gate_nav_probe_init = true'):
            self.assertIn(line, config)
        for wrong in (('--nav-params-init',), ('--nav-probe-init',)):
            with self.assertRaises(SystemExit):
                self.build(*base, *wrong)

    def test_linux_overlay_takes_created_identities(self):
        out = self.build(*self.snapshot, '--mode', 'clock', '--world-identities', '--created-identities', '--created-load', '--portable-created')
        config = (out / 'ui/config.lua.append').read_text()
        for line in ('native_gate_created_identities = true', 'native_gate_created_load = true', 'native_gate_portable_created = true'):
            self.assertIn(line, config)
        for wrong in (('--world-identities', '--created-load'), ('--world-identities', '--portable-created'), ('--created-identities',)):
            with self.assertRaises(SystemExit):
                self.build(*self.snapshot, '--mode', 'clock', *wrong)

    def test_linux_overlay_takes_spawn_rng(self):
        out = self.build(*self.snapshot, '--mode', 'clock', '--spawn-rng', 'key')
        self.assertIn("native_gate_spawn_rng = 'key'", (out / 'ui/config.lua.append').read_text())

    def test_linux_overlay_gravidar_wide_needs_the_gravidar(self):
        with self.assertRaises(SystemExit):
            self.build(*self.snapshot, '--mode', 'clock', '--world-identities', '--gravidar-wide', 'sort')

    def test_linux_overlay_takes_find_order(self):
        out = self.build(*self.snapshot, '--mode', 'clock', '--world-identities', '--find-order', 'sort')
        self.assertIn("native_gate_find_order = 'sort'", (out / 'ui/config.lua.append').read_text())
        with self.assertRaises(SystemExit):
            self.build(*self.snapshot, '--mode', 'clock', '--find-order', 'sort')

    def test_linux_overlay_takes_script_rng(self):
        base = (*self.snapshot, '--mode', 'clock', '--world-identities')
        out = self.build(*base, '--script-rng', 'key', '--script-rng-mask', '7', '--script-rng-rows', '60', '--script-rng-unmapped', '--md-timed-rng')
        config = (out / 'ui/config.lua.append').read_text()
        for line in ("native_gate_script_rng = 'key'", 'native_gate_script_rng_mask = 7', 'native_gate_script_rng_rows = true', 'native_gate_script_rng_rows_ticks = 60',
                     "native_gate_script_rng_unmapped = 'key'", 'native_gate_md_timed_rng = true'):
            self.assertIn(line, config)
        for wrong in (('--md-timed-rng',), ('--script-rng', 'key', '--script-rng-mask', '1', '--md-timed-rng'), ('--script-rng', 'key', '--script-rng-natural'),
                      ('--script-rng', 'key', '--script-rng-mask', '2', '--script-rng-unmapped'), ('--script-rng', 'key', '--script-rng-mask', '4')):
            with self.assertRaises(SystemExit):
                self.build(*base, *wrong)
        with self.assertRaises(SystemExit):
            self.build(*self.snapshot, '--mode', 'clock', '--script-rng', 'key')

    def test_linux_overlay_takes_runtime_ids(self):
        out = self.build(*self.snapshot, '--mode', 'clock', '--world-identities', '--runtime-ids')
        self.assertIn('native_gate_runtime_ids = true', (out / 'ui/config.lua.append').read_text())
        with self.assertRaises(SystemExit):
            self.build(*self.snapshot, '--mode', 'clock', '--runtime-ids')
        out = self.build(*self.snapshot, '--mode', 'clock', '--world-identities', '--runtime-ids', '--runtime-id-callers')
        self.assertIn('native_gate_runtime_ids_callers = true', (out / 'ui/config.lua.append').read_text())
        with self.assertRaises(SystemExit):
            self.build(*self.snapshot, '--mode', 'clock', '--world-identities', '--runtime-id-callers')

    def test_linux_overlay_takes_travel_snapshot(self):
        out = self.build(*self.snapshot, '--mode', 'clock', '--world-identities', '--serial-pathing', '--path-determinism', '3', '--travel-snapshot')
        self.assertIn('native_gate_travel_snapshot = true', (out / 'ui/config.lua.append').read_text())
        with self.assertRaises(SystemExit):
            self.build(*self.snapshot, '--mode', 'clock', '--world-identities', '--serial-pathing', '--path-determinism', '1', '--travel-snapshot')

    def test_linux_overlay_takes_gravidar(self):
        out = self.build(*self.snapshot, '--mode', 'clock', '--world-identities', '--serial-pathing', '--gravidar')
        config = (out / 'ui/config.lua.append').read_text()
        for key in ('radar_serial', 'gravidar_key_all', 'gravidar_time_sync', 'ai_schedule', 'gravidar_load_pass', 'gravidar_sector_order'):
            self.assertIn(f'native_gate_{key} = true', config)
        with self.assertRaises(SystemExit):
            self.build(*self.snapshot, '--mode', 'clock', '--world-identities', '--gravidar')
        out = self.build(*self.snapshot, '--mode', 'clock', '--world-identities', '--serial-pathing', '--gravidar', '--gravidar-deadlines')
        self.assertIn('native_gate_gravidar_deadlines = true', (out / 'ui/config.lua.append').read_text())
        with self.assertRaises(SystemExit):
            self.build(*self.snapshot, '--mode', 'clock', '--world-identities', '--serial-pathing', '--gravidar-deadlines')

    def test_linux_overlay_takes_event_order(self):
        out = self.build(*self.snapshot, '--mode', 'clock', '--world-identities', '--event-order')
        self.assertIn('native_gate_event_order = true', (out / 'ui/config.lua.append').read_text())
        with self.assertRaises(SystemExit):
            self.build(*self.snapshot, '--mode', 'clock', '--event-order')
        out = self.build(*self.snapshot, '--mode', 'clock', '--world-identities', '--event-order', '--event-rows', '10')
        self.assertIn('native_gate_event_rows = 10', (out / 'ui/config.lua.append').read_text())
        with self.assertRaises(SystemExit):
            self.build(*self.snapshot, '--mode', 'clock', '--world-identities', '--event-rows', '10')

    def test_event_rows_compare(self):
        sys.path.insert(0, str(ROOT / 'tools/dev'))
        import census_event_rows as rows
        def row(tick, ident, due='401c660f04c756b3'):
            return dict(phase=3, tick=tick, slot=0, klass='saved', id=ident, stype='100', etype='200', due=due, now=due)
        names = rows.Names(None)
        out = io.StringIO()
        self.assertEqual(rows.compare({1: [row(1, 'a')], 2: [row(2, 'b'), row(2, 'c')]}, {1: [row(1, 'a')], 2: [row(2, 'b'), row(2, 'd')]}, names, 3, 12, out), 1)
        self.assertIn('different sets', out.getvalue())
        self.assertIn('saved:c', out.getvalue())
        self.assertIn('saved:d', out.getvalue())
        out = io.StringIO()
        self.assertEqual(rows.compare({1: [row(1, 'a'), row(1, 'b')]}, {1: [row(1, 'b'), row(1, 'a')]}, names, 3, 12, out), 1)
        self.assertIn('same set, other order', out.getvalue())
        out = io.StringIO()
        self.assertEqual(rows.compare({1: [row(1, 'a')]}, {1: [row(1, 'a')]}, names, 3, 12, out), 0)
        self.assertIn('identical', out.getvalue())
        other = dict(row(1, 'a'), stype='999', etype='888')
        out = io.StringIO()
        self.assertEqual(rows.compare({1: [row(1, 'a')]}, {1: [other]}, names, 3, 12, out), 1)
        out = io.StringIO()
        self.assertEqual(rows.compare({1: [row(1, 'a')]}, {1: [other]}, names, 3, 12, out, key=rows.key_any_type), 0)

    def test_linux_overlay_takes_md_resume_and_boot_hold(self):
        out = self.build(*self.snapshot, '--mode', 'clock', '--md-resume-rng', '--boot-hold')
        config = (out / 'ui/config.lua.append').read_text()
        self.assertIn('native_gate_md_resume_rng = true', config)
        self.assertIn('native_gate_boot_hold = true', config)
        with self.assertRaises(SystemExit):
            self.build('--mode', 'clock', '--md-resume-rng')

    def test_linux_overlay_takes_ai_wait_rng(self):
        out = self.build(*self.snapshot, '--mode', 'clock', '--world-identities', '--ai-wait-rng')
        self.assertIn('native_gate_ai_wait_rng = true', (out / 'ui/config.lua.append').read_text())
        with self.assertRaises(SystemExit):
            self.build(*self.snapshot, '--mode', 'clock', '--ai-wait-rng')

    def test_linux_overlay_takes_region_rng(self):
        out = self.build(*self.snapshot, '--mode', 'clock', '--world-identities', '--region-rng', 'key')
        self.assertIn("native_gate_region_rng = 'key'", (out / 'ui/config.lua.append').read_text())
        with self.assertRaises(SystemExit):
            self.build(*self.snapshot, '--mode', 'clock', '--region-rng', 'key')

    def test_linux_overlay_takes_path_determinism(self):
        out = self.build(*self.snapshot, '--mode', 'clock', '--serial-pathing', '--path-determinism', '3')
        config = (out / 'ui/config.lua.append').read_text()
        self.assertIn('native_gate_serial_pathing = true', config)
        self.assertIn('native_gate_path_determinism = 3', config)

    def test_linux_overlay_takes_the_world_hash(self):
        out = self.build(*self.snapshot, '--mode', 'clock', '--world-identities', '--world-hash-stride', '60',
                         '--world-hash-schema', 'WORLD_IMPORTED_2', '--world-hash-watch', '000000000000004d')
        config = (out / 'ui/config.lua.append').read_text()
        for line in ('native_gate_world_identities = true', 'native_gate_world_hash_stride = 60',
                     "native_gate_world_hash_schema = 'WORLD_IMPORTED_2'", "native_gate_world_hash_watch = {'000000000000004d'}"):
            self.assertIn(line, config)


@unittest.skipUnless(platform.system() == 'Linux' and shutil.which('gcc') and shutil.which('luajit'),
                     'native Linux gate checks need a Linux host with gcc and luajit')
class NativeLinux(unittest.TestCase):
    def test_build_unit_bridge_and_relay_scenarios(self):
        command = [sys.executable, str(ROOT / 'tools/dev/check_lockstep_gate_linux.py'), '--cc', 'gcc']
        if LINUX_EXE.is_file():
            command += ['--x4', str(LINUX_EXE)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=600)
        self.assertEqual(result.returncode, 0, result.stdout[-4000:] + result.stderr[-4000:])
        self.assertIn('LINUX GATE CHECK PASS', result.stdout)


if __name__ == '__main__':
    unittest.main()
