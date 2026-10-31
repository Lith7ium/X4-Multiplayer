"""Production Lua drain/config and independent transition evidence wiring."""
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
from lupa.luajit21 import LuaRuntime

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/dev'))
import build_native_gate_overlay as builder
from native_transition_evidence import configured_evidence
from validate_native_gate import validate
from test_native_gate_transition import fixture,log
import test_native_gate_probe as probe_tests

SOURCE=(ROOT/'tools/dev/native_gate_transition.lua').read_text()
CONFIG='''native_gate_player_probe = "observe"
native_gate_world_trace_early = true
native_gate_world_queue_trace = true
native_gate_player_transition_observer = true'''
ENABLE='NATIVE_GATE PLAYER_TRANSITION_ENABLED schema=1 resumed=0 actions=0 singleton=1 early_import_coverage=0\n'


class TransitionProbeTests(unittest.TestCase):
    def runtime(self,records=None):
        lua=LuaRuntime(unpack_returned_tuples=True)
        lua.execute('''
          X4LAN_CONFIG={native_gate_player_probe='observe',native_gate_player_transition_observer=true,
            native_gate_world_trace_early=true,native_gate_world_queue_trace=true}
          logs={};DebugError=function(row) logs[#logs+1]=row end
          cursor=0;ready=0;stopped=false
          gate={player_transition_config=function() return 1 end,
            player_transition_row=function(i)
              if i==0 then return ready end
              assert(i==cursor+1 and i<=ready);cursor=i;return rows[i]
            end,
            player_transition_status=function()
              return footer:gsub('rows=%d+','rows='..cursor):gsub('sites=0','sites='..(stopped and 0 or 7)):gsub('enabled=0','enabled='..(stopped and 0 or 1))
            end}
        ''')
        text=log(fixture() if records is None else records).splitlines()
        # Native formatter places event first, unlike the sorted Python fixture.
        rows=[]
        for i,line in enumerate(text[:-1],1):
            words=line.split('PLAYER_TRANSITION ',1)[1].split();words.remove(f'event={i}')
            rows.append(f'PLAYER_TRANSITION event={i} '+' '.join(words))
        lua.globals().rows=lua.table_from(rows)
        lua.globals().footer=text[-1].split('PLAYER_TRANSITION_STOP ',1)[1]
        lua.execute(SOURCE)
        return lua

    def validate(self,text):
        errors=[];result=configured_evidence(text,CONFIG,1,{99},errors)
        return result,errors

    def test_consuming_drain_survives_reload_and_stop_emits_once(self):
        lua=self.runtime()
        lua.execute('X4LAN_NATIVE_TRANSITION.configure(gate);ready=2;X4LAN_NATIVE_TRANSITION.drain(gate,false)')
        lua.execute(SOURCE) # Recreated UI uses native consuming cursor.
        lua.execute('X4LAN_NATIVE_TRANSITION.configure(gate);ready=5;X4LAN_NATIVE_TRANSITION.drain(gate,false);stopped=true;X4LAN_NATIVE_TRANSITION.drain(gate,true);X4LAN_NATIVE_TRANSITION.drain(gate,true)')
        result,errors=self.validate('\n'.join(lua.globals().logs.values()))
        self.assertEqual(errors,[]);self.assertEqual(result['observed_model_completions'],[5])
        self.assertEqual(lua.globals().cursor,5);self.assertFalse(result['action_origin_proven'])
        self.assertFalse(result['independent_player_context'])

    def test_empty_observation_is_not_a_transition_or_authority_proof(self):
        lua=self.runtime([])
        lua.execute('X4LAN_NATIVE_TRANSITION.configure(gate);stopped=true;X4LAN_NATIVE_TRANSITION.drain(gate,true)')
        result,errors=self.validate('\n'.join(lua.globals().logs.values()))
        self.assertEqual(errors,[]);self.assertFalse(result['transition_exercised'])
        self.assertEqual(result['observed_model_completions'],[])

    def test_versioned_direct_exception_footer_configures_and_drains(self):
        lua=self.runtime()
        lua.globals().footer+=' exception_schema=1 direct_calls=3 direct_returns=3 direct_unwinds=0 queue_exception_cleanup=0'
        lua.execute('X4LAN_NATIVE_TRANSITION.configure(gate);ready=5;stopped=true;X4LAN_NATIVE_TRANSITION.drain(gate,true)')
        result,errors=self.validate('\n'.join(lua.globals().logs.values()))
        self.assertEqual(errors,[])
        self.assertEqual(result['exception_cleanup_scope'],'direct_wrappers_only')
        self.assertFalse(result['queue_exception_cleanup'])
        for extra in (' exception_schema=1',' direct_calls=3',
                      ' exception_schema=2 direct_calls=3 direct_returns=3 direct_unwinds=0 queue_exception_cleanup=0',
                      ' exception_schema=1 direct_calls=3 direct_returns=3 direct_unwinds=0 queue_exception_cleanup=1',
                      ' exception_schema=1 direct_calls=2 direct_returns=3 direct_unwinds=0 queue_exception_cleanup=0',
                      ' exception_schema=1 direct_calls=3x direct_returns=3 direct_unwinds=0 queue_exception_cleanup=0',
                      ' exception_schema=1 direct_calls=03 direct_returns=3 direct_unwinds=0 queue_exception_cleanup=0',
                      ' exception_schema=1 direct_calls=3 direct_returns=3 direct_unwinds=0 queue_exception_cleanup=0 unknown=0'):
            lua=self.runtime();lua.globals().footer+=extra
            with self.assertRaises(Exception):lua.execute('X4LAN_NATIVE_TRANSITION.configure(gate)')
        lua=self.runtime()
        old=lua.globals().footer
        lua.globals().footer+=' exception_schema=1 direct_calls=3 direct_returns=3 direct_unwinds=0 queue_exception_cleanup=0'
        lua.execute('X4LAN_NATIVE_TRANSITION.configure(gate)')
        lua.globals().footer=old
        with self.assertRaisesRegex(Exception,'exception schema changed'):
            lua.execute('X4LAN_NATIVE_TRANSITION.drain(gate,false)')

    def test_direct_unwind_footer_retained_before_refusing_success(self):
        lua=self.runtime()
        lua.globals().footer+=' exception_schema=1 direct_calls=4 direct_returns=3 direct_unwinds=1 queue_exception_cleanup=0'
        lua.execute('X4LAN_NATIVE_TRANSITION.configure(gate);ready=5;stopped=true')
        with self.assertRaisesRegex(Exception,'direct transition unwound'):
            lua.execute('X4LAN_NATIVE_TRANSITION.drain(gate,true)')
        result,errors=self.validate('\n'.join(lua.globals().logs.values()))
        self.assertTrue(errors)
        self.assertEqual(result['stop']['direct_unwinds'],1)
        lua=self.runtime()
        lua.globals().footer+=' exception_schema=1 direct_calls=4 direct_returns=3 direct_unwinds=0 queue_exception_cleanup=0'
        lua.execute('X4LAN_NATIVE_TRANSITION.configure(gate);ready=5;stopped=true')
        with self.assertRaisesRegex(Exception,'return coverage incomplete'):
            lua.execute('X4LAN_NATIVE_TRANSITION.drain(gate,true)')
        result,errors=self.validate('\n'.join(lua.globals().logs.values()))
        self.assertTrue(errors)
        self.assertEqual(result['stop']['direct_calls'],4)

    def test_exact_footer_fields_cannot_be_replaced_by_exception_fields(self):
        # Equal field counts are insufficient: every base field is mandatory,
        # and legacy status must contain none of the exception extension.
        from test_native_gate_transition_saved import captures,text,WIRE
        extension='exception_schema=1 direct_calls=0 direct_returns=0 direct_unwinds=0 queue_exception_cleanup=0'
        for schema in (1,2):
            def runtime():
                lua=self.runtime([])
                if schema==2:
                    lua.globals().footer=text(*captures([])).splitlines()[-1].split('PLAYER_TRANSITION_STOP ',1)[1]
                    lua.globals().X4LAN_CONFIG.native_gate_player_transition_manifest=WIRE
                    lua.globals().X4LAN_CONFIG.native_gate_player_transition_last_tick=120
                    lua.execute('gate.player_transition_watch_config=function() return 1 end')
                    lua.execute(SOURCE)
                return lua
            base=runtime().globals().footer.split()
            for field in base:
                for replacement in extension.split():
                    for stage in ('configure','drain'):
                        with self.subTest(schema=schema,field=field,replacement=replacement,stage=stage):
                            lua=runtime()
                            if stage=='drain':lua.execute('X4LAN_NATIVE_TRANSITION.configure(gate)')
                            lua.globals().footer=' '.join(replacement if item==field else item for item in base)
                            with self.assertRaisesRegex(Exception,'status field set'):
                                lua.execute('X4LAN_NATIVE_TRANSITION.'+('configure(gate)' if stage=='configure' else 'drain(gate,true)'))
            for missing in extension.split():
                lua=runtime();lua.globals().footer+=' '+' '.join(item for item in extension.split() if item!=missing)
                with self.subTest(schema=schema,missing=missing),self.assertRaisesRegex(Exception,'status field set'):
                    lua.execute('X4LAN_NATIVE_TRANSITION.configure(gate)')

    def test_stop_failure_exports_raw_receipts_and_footer_before_refusal(self):
        lua=self.runtime()
        lua.execute("footer=footer:gsub('restore_open=0','restore_open=1');X4LAN_NATIVE_TRANSITION.configure(gate);ready=5;stopped=true")
        with self.assertRaisesRegex(Exception,'restoration/capture incomplete'):
            lua.execute('X4LAN_NATIVE_TRANSITION.drain(gate,true)')
        result,errors=self.validate('\n'.join(lua.globals().logs.values()))
        self.assertEqual(len(result['events']),5);self.assertTrue(errors)
        self.assertEqual(result['stop']['restore_open'],1)

    def test_ready_capacity_regression_missing_and_oversize_rows_refuse(self):
        for code in ('ready=513','ready=-1','ready=1;rows[1]=nil','ready=1;rows[1]=string.rep("x",2048)'):
            lua=self.runtime();lua.execute('X4LAN_NATIVE_TRANSITION.configure(gate);'+code)
            with self.assertRaises(Exception):lua.execute('X4LAN_NATIVE_TRANSITION.drain(gate,false)')
        lua=self.runtime();lua.execute('X4LAN_NATIVE_TRANSITION.configure(gate);ready=2;X4LAN_NATIVE_TRANSITION.drain(gate,false);ready=1')
        with self.assertRaisesRegex(Exception,'ready count'):lua.execute('X4LAN_NATIVE_TRANSITION.drain(gate,false)')

    def test_validator_requires_configuration_cursor_and_selected_saved_owner(self):
        original=ENABLE+log(fixture())
        for text in (log(fixture()),original.replace('resumed=0','resumed=1'),
                     original.replace('actions=0','actions=1'),original+'\n'+ENABLE,
                     original.replace('s0=63,','s0=64,'),original+'\nNATIVE_GATE PLAYER_TRANSITION_DRAIN_ERROR test'):
            with self.subTest(text=text[-80:]):self.assertTrue(self.validate(text)[1])
        rows=fixture();rows[4]['queue_errors']=256
        result,errors=self.validate(ENABLE+log(rows))
        self.assertTrue(errors);self.assertEqual(result['observed_model_completions'],[5])

    def test_builder_opt_in_and_dependencies_before_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);dll=root/'gate.dll';dll.write_bytes(b'fixture');save=root/'snapshot';save.write_bytes(b'fixture')
            base=['builder','--gate-dll',str(dll),'--snapshot',str(save),'--mode','battle','--serial-pathing',
                  '--player-probe','observe','--world-hash-stride','1','--world-hash-watch','0000000000000063',
                  '--world-trace-ticks','12','--world-trace-early','--world-queue-trace']
            for enabled in (False,True):
                out=root/str(enabled);args=base+['--out',str(out)]+(['--player-transition-observer'] if enabled else [])
                with patch.object(sys,'argv',args),contextlib.redirect_stdout(io.StringIO()):builder.main()
                self.assertEqual((out/'ui/native_gate_transition.lua').exists(),enabled)
                self.assertEqual('native_gate_player_transition_observer = true' in (out/'ui/config.lua.append').read_text(),enabled)
                if enabled:self.assertLess((out/'ui.xml.files').read_text().index('native_gate_transition.lua'),(out/'ui.xml.files').read_text().index('native_gate_probe.lua'))
            for removed,added in [(['--world-queue-trace'],[]),([],['--command-test','coop']),(['--player-probe','observe'],[])]:
                args=[x for x in base if x not in removed]+added+['--player-transition-observer','--out',str(root/'refused')]
                with patch.object(sys,'argv',args),contextlib.redirect_stderr(io.StringIO()),self.assertRaises(SystemExit):builder.main()
                self.assertFalse((root/'refused').exists())

    def test_main_validator_retains_existing_failures_and_local_receipts(self):
        with tempfile.TemporaryDirectory() as directory:
            run=Path(directory)
            (run/'result.json').write_text(json.dumps({'verdict':'fail','exit_code':1}))
            states={str(tick):{str(slot):{'game':20+tick/60,'real':20+tick/60,'digest':'a'*64,'command':'00000000'}
                    for slot in (1,2)} for tick in (0,1)}
            (run/'native-barrier.json').write_text(json.dumps({'states':states,'hold_seconds':0,'verdict':'fail','failure':'fixture'}))
            for slot in (1,2):
                client=run/f'client{slot}';(client/'installed').mkdir(parents=True)
                (client/'result.json').write_text(json.dumps({'lane_restored':True}))
                (client/'installed/x4_lan_lab-config.lua').write_text(CONFIG+'\nnative_gate_world_hash_watch = {"0000000000000063"}\n')
                rows=fixture();rows[4]['queue_errors']=256
                (client/'engine-output.log').write_text(f'NATIVE_GATE STOP slot=p{slot} completed=1 reason=1 restored=1 wait_game_delta=0 max_dt_error=0\n'+ENABLE+log(rows))
            result=validate(run,'battle')
            self.assertFalse(result['session_pass']);self.assertIn('three-second hold not proven',result['errors'])
            self.assertEqual(len(result['native_player_transitions']),2)
            self.assertTrue(any('failed transition or raw queue record' in e for e in result['errors']))
            self.assertEqual([c['observed_model_completions'] for c in result['native_player_transitions']],[[5],[5]])
            (run/'client2/installed/x4_lan_lab-config.lua').write_text('')
            self.assertIn('native transition observer capture/configuration missing from one player',validate(run,'battle')['errors'])

    def test_production_probe_configs_before_load_and_drains_before_other_failure(self):
        harness=probe_tests.SnapshotProbeTests();harness.setUp();lua=harness.lua
        lua.execute('''
          X4LAN_CONFIG.native_gate_player_transition_observer=true
          X4LAN_CONFIG.native_gate_world_trace_early=true
          X4LAN_CONFIG.native_gate_world_queue_trace=true
          X4LAN_CONFIG.native_gate_world_trace_ticks=12
          X4LAN_CONFIG.native_gate_world_hash_watch={'0000000000000063'}
          local gate=package.loadlib('gate')()
          gate.world_trace_config=function() return 1 end
          gate.world_queue_trace_config=function() queue_configured=true;return 1 end
          X4LAN_NATIVE_TRANSITION={configure=function() assert(queue_configured);transition_configured=true end,
            drain=function(_,stopped) if stopped then stop_drained=(stop_drained or 0)+1 else drained=(drained or 0)+1 end end}
          local load=LoadGame;LoadGame=function(slot) assert(transition_configured);load(slot) end
        ''')
        lua.execute(probe_tests.SOURCE);lua.execute('update();update();update()')
        self.assertEqual(lua.globals().loads,1);self.assertTrue(lua.globals().drained>0)
        # A separate observer throws after transition's regular drain; abort
        # still invokes transition STOP drain before the failed family again.
        lua.execute('''
          X4LAN_CONFIG.native_gate_registry_md_trace=true
          state.registry_md_count=nil
          update()
        ''')
        self.assertEqual(lua.globals().stop_drained,1)
        self.assertTrue(any('ABORT ' in line for line in lua.globals().logs.values()))


if __name__=='__main__':unittest.main()
