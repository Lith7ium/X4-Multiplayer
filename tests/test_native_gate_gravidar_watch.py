"""Gravidar contact watch: probe arming/refusals/row drain (LuaJIT, mock gate) and
the census join (census_gravidar.py). Native recording is covered by
src/native/engine/test_lockstep_gravidar_watch.h; no X4 is involved here."""
import importlib.util
from pathlib import Path
import unittest

from lupa.luajit21 import LuaRuntime, LuaError

ROOT = Path(__file__).resolve().parents[1]
PROBE = (ROOT / 'tools/dev/native_gate_probe.lua').read_text(encoding='utf-8')
spec = importlib.util.spec_from_file_location('census_gravidar', ROOT / 'tools/dev/census_gravidar.py')
census = importlib.util.module_from_spec(spec)
spec.loader.exec_module(census)

HARNESS = '''
  X4LAN_CONFIG={native_gate_test=true,test_profile='45841',slot='p1',native_gate_hash='clock',
    native_gate_snapshot_sha=string.rep('a',64),port=12345,token=string.rep('c',32),
    native_gate_radar_serial=true,native_gate_world_identities=true,native_gate_gravidar_key_all=true,
    native_gate_gravidar_watch={'000000000009fb3b','000000000009f121'}}
  X4LAN_SHA256={digest=function() return string.rep('b',64) end}
  logs={};loads=0;reports=0;boots=0;is_menu=true;watched={};calls={};rows={};sets={}
  state={active=1,boot_phase=0,issued=0,report_tick=0,rng='0'}
  gate={boot=function() boots=boots+1;return 0 end,poll=function() return state end,
    load_requested=function() state.boot_phase=1;return 1 end,
    report=function(tick) reports=reports+1;state.report_tick=tick;return 1 end,
    stop=function() state.active=0;return 1 end,
    gravidar_key=function() return 1 end,gravidar_key_status=function() return 'armed=1' end,
    gravidar=function() return 'tick=0 calls=0' end,
    -- Native slot semantics (lockstep_gravidar.h gravidar_watch_add): before frame 1 only;
    -- an armed slot accepts its own ID again, the next free slot appends a new one.
    gravidar_watch=function(id,slot)
      calls[#calls+1]=id..'@'..tostring(slot)
      if refuse_watch or state.issued~=0 or type(slot)~='number' then return 0 end
      if slot<=#watched then return watched[slot]==id and 1 or 0 end
      if slot~=#watched+1 then return 0 end
      for _,known in ipairs(watched) do if known==id then return 0 end end
      watched[slot]=id;return 1
    end,
    gravidar_watch_row=function(i) return rows[i] end,
    gravidar_watch_set=function(i,set) return sets[i..'/'..set] end,
    gravidar_watch_status=function() return 'watched=2 rows=1' end}
  package.loadlib=function(path) assert(not path:find('fixedtick'));return function() return gate end end
  local ffi={cdef=function() end,typeof=function() return true end,string=function(s) return s end,
    C={GetSaveFolderPath=function() return 'fake/45841/save' end,GetCurrentGameTime=function() return 7 end,
      IsStartmenu=function() return is_menu end,ReloadSaveList=function() end,IsSaveListLoadingComplete=function() return true end}}
  require=function(name) assert(name=='ffi');return ffi end
  DebugError=function(s) logs[#logs+1]=s end
  RegisterEvent=function() end
  SetScript=function(name,fn) update=fn end
  GetSaveList=function() return {{filename='save_009'}} end
  LoadGame=function(slot) assert(slot=='save_009');loads=loads+1 end
'''


def lua(config=''):
    runtime = LuaRuntime(unpack_returned_tuples=True)
    runtime.execute(HARNESS)
    runtime.execute(config)
    return runtime


class ProbeTests(unittest.TestCase):
    def test_watch_arms_after_unit_keying_and_drains_rows_with_their_nonempty_sets(self):
        runtime = lua('''
          rows[0]='index=0 tick=1 unit=000000000009fb3b ord=1 ok=1 counts=1/0/2/0/0/0/0/1'
          sets['0/0']='index=0 set=0 present=1 count=1 keys=s:0000000000000010'
          sets['0/2']='index=0 set=2 present=1 count=2 keys=s:0000000000000010,u:000000000000cc77'
          sets['0/7']='index=0 set=7 present=1 count=1 keys=c:8000000000000001'
        ''')
        runtime.execute(PROBE)
        runtime.execute('update();update();update()')
        self.assertEqual(list(runtime.globals().watched.values()), ['000000000009fb3b', '000000000009f121'])
        runtime.execute('state.boot_phase=3;is_menu=false')
        runtime.execute(PROBE)
        runtime.execute('update();state.issued=1;update()')
        logs = [line for line in runtime.globals().logs.values()]
        self.assertTrue(any('GRAVIDAR_WATCH_ARMED ids=000000000009fb3b,000000000009f121' in line for line in logs))
        watch = [line.split('NATIVE_GATE ')[1] for line in logs if 'GRAVIDAR_WATCH_' in line and 'ARMED' not in line]
        self.assertEqual(watch, ['GRAVIDAR_WATCH_ROW index=0 tick=1 unit=000000000009fb3b ord=1 ok=1 counts=1/0/2/0/0/0/0/1',
                                 'GRAVIDAR_WATCH_SET index=0 set=0 present=1 count=1 keys=s:0000000000000010',
                                 'GRAVIDAR_WATCH_SET index=0 set=2 present=1 count=2 keys=s:0000000000000010,u:000000000000cc77',
                                 'GRAVIDAR_WATCH_SET index=0 set=7 present=1 count=1 keys=c:8000000000000001'])
        self.assertEqual(runtime.globals().reports, 1)
        self.assertFalse(any('ERROR' in line for line in logs))

    def test_loading_ui_reload_re_arms_the_identical_list_before_frame_one(self):
        # sc-missions2: the probe armed at the start menu, the loading UI reloaded it and the
        # repeated configuration was refused as a duplicate. Arm, reload, arm again, admit tick 1.
        runtime = lua()
        runtime.execute(PROBE)
        runtime.execute('update();update();update()')
        self.assertEqual(runtime.globals().loads, 1)
        runtime.execute('state.boot_phase=3;is_menu=false')
        runtime.execute(PROBE)  # the reloaded UI repeats the identical configuration
        runtime.execute('update();state.issued=1;update()')
        self.assertEqual(list(runtime.globals().calls.values()),
                         ['000000000009fb3b@1', '000000000009f121@2', '000000000009fb3b@1', '000000000009f121@2'])
        self.assertEqual(list(runtime.globals().watched.values()), ['000000000009fb3b', '000000000009f121'])
        logs = list(runtime.globals().logs.values())
        self.assertEqual(sum('GRAVIDAR_WATCH_ARMED' in line for line in logs), 2)
        self.assertFalse(any('ERROR' in line or 'refused' in line for line in logs))
        self.assertGreaterEqual(runtime.globals().reports, 1)
        # A different list after the reload is a different configuration and refuses.
        runtime = lua()
        runtime.execute(PROBE)
        runtime.execute('update();update();update()')
        runtime.execute("state.boot_phase=3;is_menu=false;X4LAN_CONFIG.native_gate_gravidar_watch={'000000000009f121','000000000009fb3b'}")
        runtime.execute(PROBE)
        runtime.execute('update()')
        self.assertTrue(any('Gravidar watch refused 000000000009f121' in line for line in runtime.globals().logs.values()))

    def test_invalid_watch_configuration_refuses_before_booting(self):
        for config in ("X4LAN_CONFIG.native_gate_gravidar_watch={}", "X4LAN_CONFIG.native_gate_gravidar_watch='000000000009fb3b'",
                       "X4LAN_CONFIG.native_gate_gravidar_watch={'9fb3b'}", "X4LAN_CONFIG.native_gate_gravidar_watch={'000000000009FB3B'}",
                       "X4LAN_CONFIG.native_gate_gravidar_watch={'0000000000000000'}",
                       "X4LAN_CONFIG.native_gate_gravidar_watch={'000000000009fb3b','000000000009fb3b'}",
                       "X4LAN_CONFIG.native_gate_gravidar_watch={'1','2','3','4','5','6','7','8','9'}",
                       'X4LAN_CONFIG.native_gate_gravidar_key_all=nil'):
            with self.subTest(config=config):
                runtime = lua(config)
                with self.assertRaises(LuaError):
                    runtime.execute(PROBE)
                self.assertEqual(runtime.globals().boots, 0)

    def test_native_refusal_stops_arming(self):
        runtime = lua('refuse_watch=true')
        runtime.execute(PROBE)
        runtime.execute('update();update();update()')
        self.assertEqual(runtime.globals().loads, 0)
        self.assertTrue(any('Gravidar watch refused' in line for line in runtime.globals().logs.values()))


def log(rows):
    return ['[X4LAN] NATIVE_GATE ' + row for row in rows]


CLIENT1 = log([
    'GRAVIDAR_WATCH_ROW index=0 tick=328 unit=000000000009f121 ord=1 ok=1 counts=1/0/1/0/1/0/0/0',
    'GRAVIDAR_WATCH_SET index=0 set=0 present=1 count=1 keys=s:0000000000000010',
    'GRAVIDAR_WATCH_SET index=0 set=2 present=1 count=1 keys=s:0000000000000010',
    'GRAVIDAR_WATCH_SET index=0 set=4 present=1 count=1 keys=s:0000000000000010',
    'GRAVIDAR_WATCH_ROW index=1 tick=644 unit=000000000009fb3b ord=1 ok=1 counts=0/0/2/0/2/0/1/0',
    'GRAVIDAR_WATCH_SET index=1 set=2 present=1 count=2 keys=s:0000000000000020,u:0000000000001111',
    'GRAVIDAR_WATCH_SET index=1 set=4 present=1 count=2 keys=s:0000000000000020,u:0000000000001111',
    'GRAVIDAR_WATCH_SET index=1 set=6 present=1 count=1 keys=s:0000000000000020',
    'GRAVIDAR_WATCH_STOP watched=2 rows=2'])
CLIENT2 = log([
    'GRAVIDAR_WATCH_ROW index=0 tick=328 unit=000000000009f121 ord=1 ok=1 counts=1/0/1/0/1/0/0/0',
    'GRAVIDAR_WATCH_SET index=0 set=0 present=1 count=1 keys=s:0000000000000010',
    'GRAVIDAR_WATCH_SET index=0 set=2 present=1 count=1 keys=s:0000000000000010',
    'GRAVIDAR_WATCH_SET index=0 set=4 present=1 count=1 keys=s:0000000000000010',
    'GRAVIDAR_WATCH_ROW index=1 tick=644 unit=000000000009fb3b ord=1 ok=1 counts=0/0/1/0/1/0/0/0',
    'GRAVIDAR_WATCH_SET index=1 set=2 present=1 count=1 keys=u:0000000000002222',
    'GRAVIDAR_WATCH_SET index=1 set=4 present=1 count=1 keys=u:0000000000002222',
    'GRAVIDAR_WATCH_ROW index=2 tick=645 unit=000000000009fb3b ord=1 ok=1 counts=0/0/0/0/0/0/0/0'])


class CensusTests(unittest.TestCase):
    def test_first_mismatch_names_the_contact_one_client_did_not_see(self):
        result = census.compare([census.parse(CLIENT1), census.parse(CLIENT2)])
        self.assertEqual(result['rows'], [2, 3])
        self.assertEqual(result['status'], ['watched=2 rows=2', None])
        pair = result['pairs'][0]
        self.assertEqual((pair['joined'], pair['mismatches'], pair['only']), (2, 1, [0, 1]))
        first = pair['first_mismatch']
        self.assertEqual((first['tick'], first['unit'], first['ord']), (644, '000000000009fb3b', 1))
        self.assertEqual(first['sets']['scan_primary'], {'only_reference': ['s:0000000000000020'], 'only_other': [], 'unmapped': [1, 1]})
        self.assertEqual(first['sets']['added_primary'], {'only_reference': ['s:0000000000000020'], 'only_other': [], 'unmapped': [0, 0]})
        self.assertEqual(pair['first_only'], [645, '000000000009fb3b', 1])
        # Unmapped contacts compare by count only: runtime IDs differ between clients.
        same = census.compare([census.parse(CLIENT1[:4]), census.parse(CLIENT2[:4])])
        self.assertEqual(same['pairs'][0]['mismatches'], 0)

    def test_malformed_or_inconsistent_rows_are_refused(self):
        for broken in (CLIENT1[:1] + [CLIENT1[1].replace('count=1', 'count=2')],
                       [CLIENT1[1]],
                       CLIENT1[:1],
                       CLIENT1[:2] + [CLIENT1[2].replace('s:0000000000000010', 'x:0000000000000010')]):
            with self.subTest(broken=broken), self.assertRaises(ValueError):
                census.parse(broken)


if __name__ == '__main__':
    unittest.main()
