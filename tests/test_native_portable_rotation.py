"""Independent bounded import receipts and actual LuaJIT negative tests."""
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest

import native_portable_lane as lane
import native_portable_rotation as receipt
import portable_import_capability as cap
from test_native_portable import TOKEN, participants
import test_native_portable_windows as windows_fixture
from test_portable_import_capability import reviewed_euler_contract, imported_contract

SOURCE = windows_fixture.SOURCE


def status():
    row = {**receipt.TEXT, **{key: 0 for key in receipt.NUMBERS}}
    for key in ('configured', 'stop', 'load_calls', 'load_returns', 'load_true', 'restored', 'complete'):
        row[key] = 1
    row['phase'] = 4
    for role in (0, 1):
        for key in ('calls', 'returns', 'euler'):
            row[f'role{role}_{key}'] = cap.EULER_PER_BANK
    row['lifetimes'] = row['paired'] = cap.EULER_PER_BANK
    for role in (0, 1):
        for cls, expected in (('nav', cap.NAVIGATION_EULER), ('player', cap.PLAYER_EULER)):
            for field in ('calls', 'returns', 'euler'):
                row[f'role{role}_{cls}_{field}'] = expected
    return row


def text(row):
    return ' '.join(f'{key}={value}' for key, value in row.items())


class RotationReceipt(unittest.TestCase):
    def test_historical_v1_receipts_stay_separate_and_failed_counts_stay_failed(self):
        from test_portable_import_capability_v1 import reviewed_euler_contract as v1_contract
        old = {**receipt.v1.TEXT, **{key: 0 for key in receipt.v1.NUMBERS}}
        for key in ('configured', 'stop', 'load_calls', 'load_returns', 'load_true', 'restored', 'complete'):
            old[key] = 1
        old['phase'] = 4
        for role in (0, 1):
            for field in ('calls', 'returns', 'euler'):
                old[f'role{role}_{field}'] = cap.v1.EULER_PER_BANK
        prefix = '[X4LAN] PORTABLE_ROTATION TERMINAL result=complete slot=1 run=test agent=' + 'd' * 64 + ' '
        self.assertTrue(receipt.rotation_evidence([prefix + text(old)], v1_contract(), 'test', 'd' * 64)['valid'])
        self.assertFalse(receipt.rotation_evidence([prefix + text(old)], reviewed_euler_contract(), 'test', 'd' * 64)['valid'])
        old.update(failure=6, phase=5, complete=0, role0_calls=10006, role0_returns=10006,
                   role0_euler=0, role1_calls=10006, role1_returns=10006, role1_euler=0, foreign=210755)
        line = prefix.replace('result=complete', 'result=failure') + text(old)
        expected = receipt.v1.rotation_evidence([line], v1_contract(), 'test', 'd' * 64)
        self.assertEqual(receipt.rotation_evidence([line], v1_contract(), 'test', 'd' * 64), expected)
        self.assertFalse(expected['valid'])
        self.assertEqual(expected['status']['failure'], 6)

    def test_matching_totals_do_not_replace_class_or_lifetime_coverage(self):
        for field in ('calls', 'returns', 'euler'):
            value = status()
            value['role0_nav_' + field] += 1
            value['role0_player_' + field] -= 1
            self.assertFalse(receipt.terminal_complete(receipt.parse_rotation(text(value))))
        for key in ('lifetimes', 'paired'):
            value = status();value[key] -= 1
            self.assertFalse(receipt.terminal_complete(receipt.parse_rotation(text(value))))

    def test_exact_counts_and_capability_required(self):
        self.assertTrue(receipt.terminal_complete(receipt.parse_rotation(text(status()))))
        for key in receipt.NUMBERS:
            if key == 'foreign':
                continue
            with self.subTest(key=key):
                value = status(); value[key] = 0 if value[key] else 1
                self.assertFalse(receipt.terminal_complete(value))
        for key in receipt.TEXT:
            with self.subTest(key=key):
                value = status(); value[key] = 'wrong'
                with self.assertRaises(ValueError): receipt.parse_rotation(text(value))
        for value in (True, -1, receipt.MAX_INTEGER + 1, float('nan')):
            row = status(); row['foreign'] = value
            self.assertFalse(receipt.terminal_complete(row))

    def test_closed_canonical_parser(self):
        raw = text(status())
        bad = (raw + ' extra=1', raw + ' open=0', raw.replace(' open=0', ''),
               ' ' + raw, raw + ' ', raw.replace(' phase=4', ' phase=04'),
               raw.replace(' open=0', ' open=-1'), raw.replace(' open=0', ' open=9007199254740992'),
               raw.replace(' stop=1', ' stop=2'), raw.replace(' phase=4', ' phase=6'),
               raw.replace(' owned=0', ' owned=4'), raw + '\n', 'x' * 2048)
        for value in bad:
            with self.subTest(value=value), self.assertRaises(ValueError): receipt.parse_rotation(value)

    def test_receipt_requires_exact_run_agent_single_terminal_and_complete_restoration(self):
        prefix = '[X4LAN] PORTABLE_ROTATION TERMINAL result=complete slot=1 run=test agent=' + 'd' * 64 + ' '
        line = prefix + text(status())
        result = receipt.rotation_evidence([line], reviewed_euler_contract(), 'test', 'd' * 64)
        self.assertTrue(result['valid']); self.assertFalse(result['native_processes_verified'])
        for lines in ([], [line, line], [line.replace('run=test', 'run=other')],
                      [line.replace('agent=' + 'd' * 64, 'agent=' + 'e' * 64)],
                      [line.replace('restored=1', 'restored=0')],
                      [line.replace('result=complete', 'result=failure')]):
            with self.subTest(lines=lines):
                self.assertFalse(receipt.rotation_evidence(lines, reviewed_euler_contract(), 'test', 'd' * 64)['valid'])
        old = reviewed_euler_contract(); del old['import_math']
        self.assertFalse(receipt.rotation_evidence([line], old, 'test', 'd' * 64)['valid'])


class WindowsEulerAdapter(unittest.TestCase):
    def runtime(self):
        lua = windows_fixture.WindowsPortableAdapter().runtime()
        cfg = lua.globals().X4MP_PORTABLE_CONFIG
        cfg.mode, cfg.contract_sha256 = cap.MODE, cap.CONTRACT_SHA256
        cfg.import_math_sha256, cfg.snapshot_sha256 = cap.DESCRIPTOR_SHA256, cap.SNAPSHOT_SHA256
        lua.execute('''
          math_calls=0;math_ok=1;math_ready=1;math_status=nil
          diagnostic_mode=nil
          native.portable_rotation_rejection_enable=function(value)
            if diagnostic_mode~=nil and diagnostic_mode~=value then return 0 end
            diagnostic_mode=value;return active and 1 or 0 end
          native.boot=function(slot,port,token,timeout,snapshot,options)
            assert(snapshot==X4MP_PORTABLE_CONFIG.snapshot_sha256 and options==70368744177664)
            boots=boots+1;return active and 0 or 10 end
          native.portable_config=function(ids,contract,ticks)
            assert(contract==X4MP_PORTABLE_CONFIG.contract_sha256 and ticks==12)
            return fixture_ok and 1 or 0 end
          native.portable_rotation_config=function(mode,descriptor,snapshot,contract)
            local c=X4MP_PORTABLE_CONFIG
            assert(mode==c.mode and descriptor==c.import_math_sha256 and snapshot==c.snapshot_sha256 and contract==c.contract_sha256)
            math_calls=math_calls+1;return math_ok end
          native.portable_rotation_ready=function()return math_ready end
          native.portable_rotation_status=function()return math_status end
          local transport=native.portable_transport
          native.portable_transport=function(...)assert(math_calls>=1);return transport(...)end
        ''')
        return lua

    def test_only_explicit_capability_configures_before_transport(self):
        lua = self.runtime(); lua.execute(SOURCE)
        self.assertEqual(lua.globals().math_calls, 1)
        self.assertTrue(lua.eval('X4MP_PORTABLE_BACKEND.ready()'))
        for value in (0, True, None):
            lua.globals().math_ready = value
            self.assertFalse(lua.eval('X4MP_PORTABLE_BACKEND.ready()'))
        lua.globals().math_ready = 1
        self.assertTrue(lua.eval('X4MP_PORTABLE_BACKEND.before_load(X4MP_PORTABLE_CONFIG.snapshot_sha256)'))

    def test_fresh_ui_state_reattaches_once_owned_configuration_without_second_load(self):
        # LuaJIT states are genuinely distinct. This retained backend is only a
        # native API contract model; actual config/CAS/lifetime tests live in C.
        shared = dict(configs=[], installs=0, requested=0, cancelled=0)
        expected = (cap.MODE, cap.DESCRIPTOR_SHA256, cap.SNAPSHOT_SHA256, cap.CONTRACT_SHA256)

        def configure(*args):
            self.assertEqual(args, expected)
            shared['configs'].append(args)
            if shared['cancelled']: return 0
            shared['installs'] = 1
            return 1

        def request():
            if shared['requested'] or shared['cancelled']: return 0
            shared['requested'] += 1
            return 1

        def cancel():
            shared['cancelled'] += 1
            return 1

        def fresh(menu):
            lua = self.runtime(); g = lua.globals()
            g.retained_config = configure
            g.retained_ready = lambda: int(not shared['requested'] and not shared['cancelled'])
            g.retained_request = request; g.retained_cancel = cancel; g.menu = menu
            lua.execute('''
              native.portable_rotation_config=function(...)math_calls=math_calls+1;return retained_config(...)end
              native.portable_ready=function()return retained_ready()end
              native.portable_rotation_ready=function()return retained_ready()end
              native.load_requested=function()return retained_request()end
              native.portable_cancel=function()cancels=cancels+1;return retained_cancel()end
              local C=package.loaded.ffi.C
              C.IsStartmenu=function()return menu end;C.IsPlayerValid=function()return not menu end
              C.IsSaveListLoadingComplete=function()return true end;C.ReloadSaveList=function()end
              C.IsGamePaused=function()return false end;C.GetCurrentGameTime=function()return 7 end
              GetSaveList=function()return {{filename='save_009'}} end
              loadgame_calls=0;LoadGame=function()loadgame_calls=loadgame_calls+1;menu=false end
              SetScript=function(_,fn)update=fn end
            ''')
            self.assertIsNone(g.X4MP_PORTABLE_BACKEND)
            lua.execute(SOURCE)
            lua.execute((windows_fixture.ROOT / 'tools/dev/native_portable_loader.lua').read_text())
            return lua

        first = fresh(True); first.execute('update();update()')
        self.assertEqual(first.globals().loadgame_calls, 1)
        self.assertEqual(shared['requested'], 1)
        second = fresh(False); second.execute('update();update()')
        self.assertEqual(shared['configs'], [expected, expected])
        self.assertEqual(shared['installs'], 1); self.assertEqual(shared['cancelled'], 0)
        self.assertEqual(second.globals().loadgame_calls, 0)
        second.execute('menu=true;update();update();menu=false')
        self.assertEqual(shared['requested'], 1)
        self.assertFalse(second.eval('X4MP_PORTABLE_BACKEND.before_load(X4MP_PORTABLE_CONFIG.snapshot_sha256)'))
        # The replacement UI cannot promote its own ordinary terminal while the
        # independent math evidence is absent.
        second.execute('close_status();update()')
        self.assertFalse(any('result=success' in row for row in second.globals().logs.values()))
        second.globals().math_status = text(status()); second.execute('update();update()')
        logs = list(second.globals().logs.values())
        self.assertEqual(sum('PORTABLE_NATIVE TERMINAL result=success' in row for row in logs), 1)
        self.assertEqual(sum('PORTABLE_ROTATION TERMINAL result=complete' in row for row in logs), 1)
        self.assertEqual(shared['requested'], 1); self.assertEqual(shared['cancelled'], 0)

    def test_reload_failure_retains_cancellation_and_bounded_nonterminal_diagnostic(self):
        lua = self.runtime(); lua.execute(SOURCE)
        self.assertTrue(lua.eval('X4MP_PORTABLE_BACKEND.before_load(X4MP_PORTABLE_CONFIG.snapshot_sha256)'))
        lua.execute('math_ok=0;X4MP_PORTABLE_BACKEND=nil')
        raw = status(); raw.update(complete=0, failure=1)
        lua.globals().math_status = text(raw)
        with self.assertRaises(Exception): lua.execute(SOURCE)
        logs = list(lua.globals().logs.values())
        self.assertEqual(lua.globals().requests, 1); self.assertEqual(lua.globals().cancels, 1)
        self.assertIsNone(lua.globals().X4MP_PORTABLE_BACKEND)
        self.assertEqual(sum('PORTABLE_ROTATION STARTUP_REFUSED' in row for row in logs), 1)
        self.assertFalse(any('TERMINAL result=success' in row for row in logs))
        self.assertFalse(receipt.rotation_evidence(logs, reviewed_euler_contract(), 'test', 'd' * 64)['valid'])
        self.assertNotIn(TOKEN, '\n'.join(logs))
        for malformed in (text(raw) + '\nforged', 'x' * 1600):
            broken = self.runtime(); broken.globals().math_status = malformed; broken.globals().math_ok = 0
            with self.assertRaises(Exception): broken.execute(SOURCE)
            self.assertEqual(broken.globals().cancels, 1)
            self.assertFalse(any('STARTUP_REFUSED' in row for row in broken.globals().logs.values()))

    def test_mixed_capabilities_refuse_before_native_module(self):
        changes = ("mode='portable-native-v1'", "import_math_sha256=nil", "import_math_sha256='wrong'",
                   "contract_sha256=string.rep('c',64)", "snapshot_sha256=string.rep('b',64)")
        for change in changes:
            lua = self.runtime(); lua.execute('X4MP_PORTABLE_CONFIG.' + change)
            with self.subTest(change=change), self.assertRaises(Exception): lua.execute(SOURCE)
            self.assertEqual(lua.globals().loads, 0)

    def test_missing_or_refused_math_configuration_cancels_once(self):
        for change in ('math_ok=0', 'math_ok=true', 'native.portable_rotation_config=nil'):
            lua = self.runtime(); lua.execute(change)
            with self.subTest(change=change), self.assertRaises(Exception): lua.execute(SOURCE)
            self.assertEqual(lua.globals().cancels, 1)
            self.assertEqual(lua.globals().requests, 0)

    def test_terminal_waits_for_math_and_never_accepts_forged_counts(self):
        for key in (None, *[k for k in receipt.NUMBERS if k != 'foreign']):
            lua = self.runtime(); lua.execute(SOURCE); lua.execute('close_status()')
            lua.execute('X4MP_PORTABLE_BACKEND.observe()')
            self.assertEqual(len(lua.globals().logs), 0)
            row = status()
            if key: row[key] = 0 if row[key] else 1
            lua.globals().math_status = text(row)
            lua.execute('X4MP_PORTABLE_BACKEND.observe();X4MP_PORTABLE_BACKEND.observe()')
            logs = list(lua.globals().logs.values())
            self.assertEqual(len(logs), 2)
            self.assertIn('result=' + ('failure' if key else 'success'), logs[-1])
            self.assertEqual(receipt.rotation_evidence(logs, reviewed_euler_contract(), 'test', 'd' * 64)['valid'], key is None)

    def test_malformed_math_receipt_never_logs_success(self):
        for raw in (text(status()) + ' extra=1', text(status()).replace(' phase=4', ''),
                    text(status()).replace(' phase=4', ' phase=04')):
            lua = self.runtime(); lua.execute(SOURCE); lua.execute('close_status()')
            lua.globals().math_status = raw
            with self.assertRaises(Exception): lua.execute('X4MP_PORTABLE_BACKEND.observe()')
            self.assertEqual(len(lua.globals().logs), 0)

    def test_settings_use_exact_reviewed_contract(self):
        with tempfile.TemporaryDirectory() as temporary:
            run = SimpleNamespace(extra_tokens={'@X4LANE_TOKEN@': TOKEN, '@X4LANE_PORT@': '31001'},
                lane=SimpleNamespace(profile='45850', profile_dir=Path(temporary) / '45850'), run_id='owned-euler')
            result = lane.settings_text(run, reviewed_euler_contract(), participants(), 12, 120000, 600000)
            self.assertIn('mode="' + cap.MODE + '"', result)
            self.assertIn('import_math_sha256="' + cap.DESCRIPTOR_SHA256 + '"', result)
            with self.assertRaisesRegex(ValueError, 'unreviewed Windows Euler'):
                lane.settings_text(run, imported_contract(), participants(), 12, 120000, 600000)


if __name__ == '__main__': unittest.main()
