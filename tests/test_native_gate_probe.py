"""LuaJIT lifecycle checks: loading must not skip the first admitted report."""
from pathlib import Path
import unittest
from lupa.luajit21 import LuaRuntime,LuaError

SOURCE = (Path(__file__).resolve().parents[1]/'tools/dev/native_gate_probe.lua').read_text()
MUX = (Path(__file__).resolve().parents[1]/'tools/dev/native_gate_inputs_core.lua').read_text()
import importlib.util
_INPUTS_SPEC = importlib.util.spec_from_file_location('native_gate_inputs_ref', Path(__file__).resolve().parents[1]/'tools/dev/native_gate_inputs.py')
inputs_ref = importlib.util.module_from_spec(_INPUTS_SPEC)
_INPUTS_SPEC.loader.exec_module(inputs_ref)
# Fields the probe's STOP block reads when the gate ends a session.
STOP_FIELDS = ('state.completed=state.issued;state.restored=1;state.wait_game_delta=0;state.max_dt_error=0;state.path_joins=0;state.path_restored=1;state.rng_seeds=0;state.dt_restored=1;')


class SnapshotProbeTests(unittest.TestCase):
    def test_world_hash_schema_selects_only_the_explicit_export(self):
        for version in (1,2):
            self.setUp()
            self.lua.execute('''
              X4LAN_CONFIG.native_gate_world_hash_stride=1
              local gate=package.loadlib('gate')();legacy_calls=0;enriched_calls=0
              gate.world_capture=function(tick) legacy_calls=legacy_calls+1;return 'tick='..tick..' state=WORLD_IMPORTED_1:'..tick end
              gate.world_capture_nav_v2=function(tick) enriched_calls=enriched_calls+1;return 'tick='..tick..' state=WORLD_IMPORTED_2:'..tick end
              gate.world_banks=function() return 'test' end
              gate.world_page=function(n) assert(n==0);return 0 end
              gate.world_detail=function(n) assert(n==0);return 0 end
              X4LAN_SHA256.digest=function(data) hashed_data=data;return string.rep('b',64) end
              state.boot_phase=3;state.issued=1;is_menu=false
            ''')
            if version==2:self.lua.execute("X4LAN_CONFIG.native_gate_world_hash_schema='WORLD_IMPORTED_2'")
            self.lua.execute(SOURCE);self.lua.execute('update()')
            self.assertEqual(self.lua.globals().legacy_calls,int(version==1))
            self.assertEqual(self.lua.globals().enriched_calls,int(version==2))
            self.assertEqual(self.lua.globals().reports,1)
            self.assertIn(f'WORLD_IMPORTED_{version}:1',self.lua.globals().hashed_data)

    def test_play_mode_keeps_the_world_digest_and_skips_the_pages(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_world_hash_stride=1
          X4LAN_CONFIG.native_gate_play_mode=true
          local gate=package.loadlib('gate')();pages=0
          gate.play_mode=function() return 1 end
          gate.world_capture=function(tick) return 'tick='..tick..' state=WORLD_IMPORTED_1:'..tick end
          gate.world_banks=function() return 'test' end
          gate.world_page=function(n) pages=pages+1;return 0 end
          gate.world_detail=function(n) pages=pages+1;return 0 end
          X4LAN_SHA256.digest=function(data) hashed_data=data;return string.rep('b',64) end
          state.boot_phase=3;state.issued=1;is_menu=false
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update()')
        self.assertEqual(self.lua.globals().reports,1)
        self.assertIn('WORLD_IMPORTED_1:1',self.lua.globals().hashed_data)
        self.assertEqual(self.lua.globals().pages,0)

    def test_invalid_world_schema_fails_before_loading_or_booting(self):
        for value in ('false','2',"'WORLD_IMPORTED_3'","'WORLD_IMPORTED_2'"):
            self.setUp();self.lua.execute('X4LAN_CONFIG.native_gate_world_hash_schema='+value)
            with self.assertRaises(LuaError):self.lua.execute(SOURCE)
            self.assertEqual(self.lua.globals().loads,0);self.assertEqual(self.lua.globals().boots,0)
        for value in ('0','-1','1.5','18001','true',"'1'",'0/0'):
            self.setUp();self.lua.execute("X4LAN_CONFIG.native_gate_world_hash_schema='WORLD_IMPORTED_2';X4LAN_CONFIG.native_gate_world_hash_stride="+value)
            with self.assertRaises(LuaError):self.lua.execute(SOURCE)
            self.assertEqual(self.lua.globals().loads,0);self.assertEqual(self.lua.globals().boots,0)

    def test_enriched_capture_never_falls_back_or_reports_legacy_summary(self):
        for mode in ('missing','failed','legacy'):
            self.setUp()
            self.lua.execute('''
              X4LAN_CONFIG.native_gate_world_hash_stride=1
              X4LAN_CONFIG.native_gate_world_hash_schema='WORLD_IMPORTED_2'
              local gate=package.loadlib('gate')();legacy_calls=0
              gate.world_capture=function() legacy_calls=legacy_calls+1;error('legacy fallback') end
              gate.world_banks=function() return 'test' end
              state.boot_phase=3;state.issued=1;is_menu=false
            ''')
            if mode=='missing':
                with self.assertRaises(LuaError):self.lua.execute(SOURCE)
                self.assertEqual(self.lua.globals().boots,0)
            else:
                body="return nil,'nav_pending_reference_unknown'" if mode=='failed' else "return 'tick=1 state=WORLD_IMPORTED_1:1'"
                self.lua.execute('package.loadlib("gate")().world_capture_nav_v2=function() '+body+' end')
                self.lua.execute(SOURCE);self.lua.execute('update()')
                self.assertEqual(self.lua.globals().state.active,0)
            self.assertEqual(self.lua.globals().legacy_calls,0);self.assertEqual(self.lua.globals().reports,0)

    def test_early_trace_manifest_precedes_load_and_retries_identically_after_ui_reload(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_world_trace_early=true
          X4LAN_CONFIG.native_gate_world_trace_ticks=12
          X4LAN_CONFIG.native_gate_world_hash_watch={'0000000000000063'}
          local gate=package.loadlib('gate')();manifest_calls=0
          gate.world_trace_config=function(ids,last)
            assert(ids=='0000000000000063' and last==12);manifest_calls=manifest_calls+1;return 1
          end
          local load=LoadGame;LoadGame=function(slot) assert(manifest_calls==1);load(slot) end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().manifest_calls,1);self.assertEqual(self.lua.globals().loads,1)
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update()')
        self.assertEqual(self.lua.globals().manifest_calls,2);self.assertEqual(self.lua.globals().loads,1)
        self.assertFalse(any('ERROR' in line for line in self.lua.globals().logs.values()))

    def test_path_determinism_arms_before_loading_and_logs_every_admitted_tick(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_serial_pathing=true
          X4LAN_CONFIG.native_gate_path_determinism=3
          local gate=package.loadlib('gate')();arm_calls=0
          gate.path_determinism=function(mask) assert(mask==3);arm_calls=arm_calls+1;return 1 end
          gate.path_determinism_status=function() return 'armed=1 mask=3 frames=1 failures=0' end
          local load=LoadGame;LoadGame=function(slot) assert(arm_calls==1);load(slot) end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().arm_calls,1);self.assertEqual(self.lua.globals().loads,1)
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update()')
        lines=list(self.lua.globals().logs.values())
        self.assertEqual(self.lua.globals().arm_calls,2)
        self.assertTrue(any('PATH_DETERMINISM_ARMED mask=3' in line for line in lines))
        self.assertTrue(any('PATH_DETERMINISM tick=1 armed=1 mask=3' in line for line in lines))
        self.assertFalse(any('ERROR' in line for line in lines))

    def test_travel_determinism_arms_after_path_determinism_and_logs_every_admitted_tick(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_serial_pathing=true
          X4LAN_CONFIG.native_gate_path_determinism=3
          X4LAN_CONFIG.native_gate_travel_determinism=true
          local gate=package.loadlib('gate')();order={}
          gate.path_determinism=function(mask) order[#order+1]='path';return 1 end
          gate.path_determinism_status=function() return 'armed=1 mask=3' end
          gate.travel_determinism=function() order[#order+1]='travel';return 1 end
          gate.travel_determinism_status=function() return 'armed=1 installed=1 tick=1 failures=0' end
          local load=LoadGame;LoadGame=function(slot) assert(#order==2);load(slot) end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(list(self.lua.globals().order.values()),['path','travel'])
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update()')
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('TRAVEL_DETERMINISM_ARMED' in line for line in lines))
        self.assertTrue(any('TRAVEL_DETERMINISM tick=1 armed=1 installed=1' in line for line in lines))
        self.assertFalse(any('ERROR' in line for line in lines))

    def test_travel_snapshot_arms_after_path_determinism_and_logs_every_admitted_tick(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_serial_pathing=true
          X4LAN_CONFIG.native_gate_path_determinism=3
          X4LAN_CONFIG.native_gate_travel_snapshot=true
          local gate=package.loadlib('gate')();order={}
          gate.path_determinism=function(mask) order[#order+1]='path';return 1 end
          gate.path_determinism_status=function() return 'armed=1 mask=3' end
          gate.travel_snapshot=function() order[#order+1]='snapshot';return 1 end
          gate.travel_snapshot_status=function() return 'armed=1 installed=1 read=1 tick=1 failures=0' end
          local load=LoadGame;LoadGame=function(slot) assert(#order==2);load(slot) end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(list(self.lua.globals().order.values()),['path','snapshot'])
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update()')
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('TRAVEL_SNAPSHOT_ARMED' in line for line in lines))
        self.assertTrue(any('TRAVEL_SNAPSHOT tick=1 armed=1 installed=1 read=1' in line for line in lines))
        self.assertFalse(any('ERROR' in line for line in lines))

    PILOT_PREREQUISITES = '''
      X4LAN_CONFIG.native_gate_serial_pathing=true
      X4LAN_CONFIG.native_gate_world_identities=true
      X4LAN_CONFIG.native_gate_pilot_inputs=true
      X4LAN_CONFIG.native_gate_pilot_slots={'0000000000176a13','0000000000176a74'}
    '''

    def test_pilot_script_arms_before_loading_logs_ticks_hashes_receipt_and_stops(self):
        self.lua.execute(self.PILOT_PREREQUISITES+'''
          X4LAN_CONFIG.native_gate_pilot_source='script'
          X4LAN_CONFIG.native_gate_pilot_window={30,600}
          local gate=package.loadlib('gate')();arm_calls=0;captures=0
          gate.pilot_inputs=function(...)
            local manifest,source,delay,first,last,suppress=...
            assert(select('#',...)==6 and manifest=='0000000000176a13,0000000000176a74' and source=='script' and
              delay==0 and first==30 and last==600 and suppress==0);arm_calls=arm_calls+1;return 1
          end
          gate.pilot_inputs_status=function() return 'armed=1 installed=1 source=script frame_tick=1 failures=0' end
          gate.pilot_inputs_frame=function() return 'PILOT_1:1:0:0000000000000000' end
          gate.pilot_capture=function() captures=captures+1;return string.rep('0',56) end
          X4LAN_SHA256.digest=function(data) hashed_data=data;return string.rep('b',64) end
          local load=LoadGame;LoadGame=function(slot) assert(arm_calls==1);load(slot) end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().arm_calls,1);self.assertEqual(self.lua.globals().loads,1)
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update()')
        self.assertEqual(self.lua.globals().arm_calls,2);self.assertEqual(self.lua.globals().captures,0)
        self.assertIn('|PILOT_1:1:0:0000000000000000',self.lua.globals().hashed_data)
        # Optional live capture is logged, and a refused capture is not fatal for scripted records.
        self.lua.execute("""X4LAN_CONFIG.native_gate_pilot_capture=true
          package.loadlib('gate')().pilot_capture=function() captures=captures+1;if captures==1 then return string.rep('0',56) end;return nil,'no_player_controller' end
          state.issued=2;update();state.issued=3;update()""")
        self.assertEqual(self.lua.globals().captures,2);self.assertEqual(self.lua.globals().reports,3)
        self.lua.execute("state.active=0;state.completed=1;setmetatable(state,{__index=function() return 0 end})")
        self.lua.execute('update()')
        lines=list(self.lua.globals().logs.values())
        armed=next(i for i,line in enumerate(lines) if 'PILOT_INPUTS_ARMED source=script delay=0 window=30-600 slots=0000000000176a13,0000000000176a74 suppress=0' in line)
        tick=next(i for i,line in enumerate(lines) if 'PILOT_INPUTS tick=1 armed=1 installed=1' in line)
        self.assertTrue(any('PILOT_PLAYER tick=1 occupied=unknown' in line for line in lines))
        self.assertTrue(any('PILOT_CAPTURE tick=2 record='+'0'*56 in line for line in lines))
        self.assertTrue(any('PILOT_CAPTURE tick=3 refused=no_player_controller' in line for line in lines))
        stop=next(i for i,line in enumerate(lines) if 'PILOT_INPUTS_STOP armed=1' in line)
        self.assertLess(armed,tick);self.assertLess(tick,stop)
        self.assertFalse(any('ERROR' in line for line in lines))

    def test_pilot_wire_stages_every_slot_then_captures_and_proposes_the_local_record(self):
        self.lua.execute(self.PILOT_PREREQUISITES+'''
          X4LAN_CONFIG.native_gate_pilot_source='wire'
          X4LAN_CONFIG.native_gate_pilot_delay=4
          X4LAN_CONFIG.native_gate_pilot_suppress_local=true
          X4LAN_CONFIG.native_gate_clients=2
          local gate=package.loadlib('gate')();order={}
          gate.wire=function(version,clients) return (version==3 and clients==2) and 1 or 0 end
          gate.pilot_inputs=function(manifest,source,delay,first,last,suppress)
            assert(source=='wire' and delay==4 and first==30 and last==600 and suppress==1);return 1
          end
          gate.pilot_inputs_status=function() return 'armed=1 source=wire failures=0' end
          gate.pilot_inputs_frame=function() return 'PILOT_1:1:0:0000000000000000' end
          gate.pilot_apply=function(slot,record) order[#order+1]='apply'..slot..':'..record;return 1 end
          gate.pilot_capture=function() order[#order+1]='capture';return '01'..string.rep('1',54) end
          gate.propose=function(tick,record) assert(tick==state.issued);order[#order+1]='propose:'..record;return 1 end
          gate.report=function(tick) order[#order+1]='report';reports=reports+1;state.report_tick=tick;return 1 end
        ''')
        self.lua.execute(MUX);self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.lua.execute("state.boot_phase=3;state.issued=1;is_menu=false;state.wire_version=3;state.clients=2;state.inputs={'01'..string.rep('a',54),'01'..string.rep('c',54)}")
        self.lua.execute(SOURCE);self.lua.execute('update()')
        self.assertEqual(list(self.lua.globals().order.values()),
            ['apply1:01'+'a'*54,'apply2:01'+'c'*54,'capture','propose:01'+'1'*54,'report'])
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('PILOT_INPUTS_ARMED source=wire delay=4 window=30-600' in line and 'suppress=1' in line for line in lines))
        self.assertFalse(any('ERROR' in line for line in lines))
        # A refused record stops the probe before it can report the tick.
        self.lua.execute("package.loadlib('gate')().pilot_apply=function() return 0 end;state.issued=2;update()")
        self.assertEqual(self.lua.globals().reports,1)
        self.assertTrue(any('pilot record refused slot=1' in line for line in self.lua.globals().logs.values()))

    def test_pilot_requires_its_prerequisites_and_an_exclusive_request_entry(self):
        cases=['X4LAN_CONFIG.native_gate_serial_pathing=nil','X4LAN_CONFIG.native_gate_world_identities=nil',
               'X4LAN_CONFIG.native_gate_player_request_observer=true',"X4LAN_CONFIG.native_gate_pilot_source='wire'",
               "X4LAN_CONFIG.native_gate_pilot_source='keys'",'X4LAN_CONFIG.native_gate_pilot_slots={}',
               "X4LAN_CONFIG.native_gate_pilot_slots={'1','2','3','4','5'}",'X4LAN_CONFIG.native_gate_pilot_inputs=1',
               'X4LAN_CONFIG.native_gate_pilot_suppress_local=false','X4LAN_CONFIG.native_gate_pilot_window={30}']
        for case in cases:
            self.setUp()
            self.lua.execute(self.PILOT_PREREQUISITES+"""
              X4LAN_CONFIG.native_gate_pilot_source='script'
              local gate=package.loadlib('gate')();pilot_calls=0
              gate.pilot_inputs=function() pilot_calls=pilot_calls+1;return 1 end
            """+case)
            self.lua.execute(SOURCE);self.lua.execute('update()')
            lines=list(self.lua.globals().logs.values())
            self.assertTrue(any('pilot inputs refused' in line for line in lines),case)
            self.assertEqual(self.lua.globals().pilot_calls,0,case);self.assertEqual(self.lua.globals().loads,0,case)
        self.setUp()
        self.lua.execute(self.PILOT_PREREQUISITES+"""
          X4LAN_CONFIG.native_gate_pilot_source='script'
          package.loadlib('gate')().pilot_inputs=function() return 0 end
        """)
        self.lua.execute(SOURCE);self.lua.execute('update()')
        self.assertTrue(any('pilot inputs refused' in line for line in self.lua.globals().logs.values()))
        self.assertEqual(self.lua.globals().loads,0)

    PILOT_SANDBOX = PILOT_PREREQUISITES+'''
      X4LAN_CONFIG.native_gate_pilot_source='script'
      X4LAN_CONFIG.native_gate_pilot_window={30,600}
      local gate=package.loadlib('gate')();device_calls={};follow_calls=0;camera_calls={}
      gate.pilot_inputs=function() return 1 end
      gate.pilot_inputs_status=function() return 'armed=1 source=script failures=0' end
      gate.pilot_inputs_frame=function() return 'PILOT_1:1:0:0000000000000000' end
      gate.pilot_device=function(kind,pad,invert) device_calls[#device_calls+1]=kind..':'..pad..':'..invert;return 1,1 end
      gate.render_rng_split=function(mode) assert(mode=='split');return 1 end
      gate.render_rng_split_status=function() return 'mode=split' end
      gate.pilot_camera_follow=function() follow_calls=follow_calls+1;return 1 end
      gate.pilot_camera=function(tick) camera_calls[#camera_calls+1]=tick;return tick<32 and 'applied' or 'held' end
      gate.pilot_camera_status=function() return 'stage=test' end
      X4LAN_SHA256.digest=function(data) hashed_data=data;return string.rep('b',64) end
    '''

    def run_admitted(self,ticks):
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.lua.execute('state.boot_phase=3;is_menu=false')
        self.lua.execute(SOURCE)
        for tick in ticks:self.lua.execute(f'state.issued={tick};update()')
        return list(self.lua.globals().logs.values())

    def test_pilot_device_arms_this_clients_entry_with_pad_and_invert_mask(self):
        for slot,expected in (('p1','kbm:0:0'),('p2','xinput:1:5')):
            self.setUp()
            self.lua.execute(self.PILOT_SANDBOX+f'''
              X4LAN_CONFIG.slot='{slot}'
              X4LAN_CONFIG.native_gate_pilot_device={{'kbm','xinput'}}
              X4LAN_CONFIG.native_gate_pilot_pad={{0,1}}
              X4LAN_CONFIG.native_gate_pilot_invert={{nil,{{'pitch','roll'}}}}
            ''')
            lines=self.run_admitted([1])
            self.assertEqual(list(self.lua.globals().device_calls.values()),[expected,expected])  # armed again after the UI reload
            kind,pad,_=expected.split(':')
            names='pitch+roll' if slot=='p2' else 'none'
            self.assertTrue(any(f'PILOT_DEVICE_ARMED kind={kind} pad={pad} invert={names} xinput=1' in line for line in lines))
            self.assertFalse(any('ERROR' in line for line in lines))
        # One value for every client; a missing table entry leaves this client on native capture.
        self.setUp()
        self.lua.execute(self.PILOT_SANDBOX+"X4LAN_CONFIG.native_gate_pilot_device='both'")
        self.run_admitted([1]);self.assertEqual(list(self.lua.globals().device_calls.values()),['both:0:0','both:0:0'])
        self.setUp()
        self.lua.execute(self.PILOT_SANDBOX+"X4LAN_CONFIG.native_gate_pilot_device={nil,'xinput'}")
        lines=self.run_admitted([1]);self.assertEqual(len(self.lua.globals().device_calls),0)
        self.assertFalse(any('PILOT_DEVICE_ARMED' in line or 'ERROR' in line for line in lines))

    def test_pilot_device_refuses_invalid_settings_before_loading(self):
        cases=["X4LAN_CONFIG.native_gate_pilot_device='mouse'","X4LAN_CONFIG.native_gate_pilot_device='kbm';X4LAN_CONFIG.native_gate_pilot_pad=4",
               "X4LAN_CONFIG.native_gate_pilot_device='kbm';X4LAN_CONFIG.native_gate_pilot_pad=0.5",
               "X4LAN_CONFIG.native_gate_pilot_device='kbm';X4LAN_CONFIG.native_gate_pilot_invert={'pitch','pitch'}",
               "X4LAN_CONFIG.native_gate_pilot_device='kbm';X4LAN_CONFIG.native_gate_pilot_invert={'throttle'}",
               "X4LAN_CONFIG.native_gate_pilot_device='kbm';X4LAN_CONFIG.native_gate_pilot_invert='pitch'",
               "X4LAN_CONFIG.native_gate_pilot_device='kbm';package.loadlib('gate')().pilot_device=function() return 0 end"]
        for case in cases:
            self.setUp()
            self.lua.execute(self.PILOT_SANDBOX+case)
            self.lua.execute(SOURCE);self.lua.execute('update()')
            lines=list(self.lua.globals().logs.values())
            self.assertTrue(any('pilot device refused' in line or 'pilot invert' in line for line in lines),case)
            self.assertEqual(self.lua.globals().loads,0,case)

    def test_pilot_camera_follow_runs_from_window_start_logs_changes_and_stays_out_of_the_hash(self):
        self.lua.execute(self.PILOT_SANDBOX+'''
          X4LAN_CONFIG.slot='p2'
          X4LAN_CONFIG.native_gate_render_rng_split=true
          X4LAN_CONFIG.native_gate_pilot_camera_follow={false,true}
        ''')
        lines=self.run_admitted([29,30,31,32,33])
        self.assertEqual(self.lua.globals().follow_calls,2)
        self.assertEqual(list(self.lua.globals().camera_calls.values()),[30,31,32,33])
        camera=[line for line in lines if 'PILOT_CAMERA tick=' in line]
        self.assertEqual(len(camera),2)  # applied at 30, held from 32
        self.assertIn('PILOT_CAMERA tick=30 stage=test',camera[0]);self.assertIn('PILOT_CAMERA tick=32 stage=test',camera[1])
        self.assertTrue(any('PILOT_CAMERA_ARMED slot=2' in line for line in lines))
        self.assertNotIn('stage',self.lua.globals().hashed_data)
        self.lua.execute("state.active=0;state.completed=1;setmetatable(state,{__index=function() return 0 end});update()")
        self.assertTrue(any('PILOT_CAMERA_STOP stage=test' in line for line in self.lua.globals().logs.values()))
        # The occupied ship's client (false entry) never arms or calls the camera.
        self.setUp()
        self.lua.execute(self.PILOT_SANDBOX+'''
          X4LAN_CONFIG.native_gate_render_rng_split=true
          X4LAN_CONFIG.native_gate_pilot_camera_follow={false,true}
        ''')
        self.run_admitted([30,31])
        self.assertEqual(self.lua.globals().follow_calls,0);self.assertEqual(len(self.lua.globals().camera_calls),0)
        # Differing cameras require the render RNG split.
        for case in ('X4LAN_CONFIG.native_gate_render_rng_split=nil',"X4LAN_CONFIG.native_gate_render_rng_split='observe'",
                     "X4LAN_CONFIG.native_gate_pilot_camera_follow='yes'","package.loadlib('gate')().pilot_camera_follow=function() return 0 end"):
            self.setUp()
            self.lua.execute(self.PILOT_SANDBOX+'''
              X4LAN_CONFIG.native_gate_render_rng_split=true
              X4LAN_CONFIG.native_gate_pilot_camera_follow=true
              package.loadlib('gate')().render_rng_split=function() return 1 end
            '''+case)
            self.lua.execute(SOURCE);self.lua.execute('update()')
            self.assertTrue(any('pilot camera anchor refused' in line for line in self.lua.globals().logs.values()),case)
            self.assertEqual(self.lua.globals().loads,0,case)

    def test_pilot_predict_arms_per_client_logs_rows_and_stays_out_of_the_hash(self):
        self.lua.execute(self.PILOT_SANDBOX+'''
          X4LAN_CONFIG.slot='p2'
          X4LAN_CONFIG.native_gate_render_rng_split=true
          X4LAN_CONFIG.native_gate_pilot_predict={false,'present'}
          X4LAN_CONFIG.native_gate_pilot_predict_horizon=4
          X4LAN_CONFIG.native_gate_pilot_predict_tau=80
          X4LAN_CONFIG.native_gate_pilot_predict_raw=true
          local gate=package.loadlib('gate')();predict_arms={};predict_frames={}
          gate.pilot_predict=function(mode,horizon,tau) predict_arms[#predict_arms+1]=mode..':'..horizon..':'..tau;return 1 end
          gate.pilot_predict_frame=function(tick)
            predict_frames[#predict_frames+1]=tick
            if tick==31 then return nil,'frame' end
            return 'tick='..tick..' horizon=4 s2=0.0100/0.00010/0.0100/0.2000/5.0000/0.0120/7'
          end
          gate.pilot_predict_raw=function(slot) if slot==2 then return 'slot=2 tick=30 driven=1' end end
          gate.pilot_predict_status=function() return 'mode=present armed=1 present_failures=0' end
        ''')
        lines=self.run_admitted([30,31,600])
        self.assertEqual(list(self.lua.globals().predict_arms.values()),['present:4:80','present:4:80'])  # again after the UI reload
        self.assertEqual(list(self.lua.globals().predict_frames.values()),[30,31,600])
        self.assertTrue(any('PILOT_PREDICT_ARMED slot=2 mode=present horizon=4 tau_ms=80' in line for line in lines))
        self.assertTrue(any('PILOT_PREDICT tick=30 horizon=4 s2=0.0100/' in line for line in lines))
        self.assertTrue(any('PILOT_PREDICT tick=31 refused=frame' in line for line in lines))  # never fatal
        self.assertTrue(any('PILOT_PREDICT_RAW slot=2 tick=30 driven=1' in line for line in lines))
        self.assertTrue(any('PILOT_PREDICT_STATUS tick=600 mode=present armed=1' in line for line in lines))
        self.assertEqual(self.lua.globals().reports,3)
        hashed=self.lua.globals().hashed_data
        self.assertNotIn('PILOT_PREDICT',hashed);self.assertNotIn('s2=0.0100',hashed);self.assertNotIn('present',hashed)
        self.lua.execute("state.active=0;state.completed=1;setmetatable(state,{__index=function() return 0 end});update()")
        self.assertTrue(any('PILOT_PREDICT_STOP mode=present armed=1' in line for line in self.lua.globals().logs.values()))
        # The other client's false entry never arms or calls the predictor.
        self.setUp()
        self.lua.execute(self.PILOT_SANDBOX+'''
          X4LAN_CONFIG.native_gate_pilot_predict={false,'observe'}
          local gate=package.loadlib('gate')();predict_calls=0
          gate.pilot_predict=function() predict_calls=predict_calls+1;return 1 end
          gate.pilot_predict_frame=function() predict_calls=predict_calls+1;return 'tick=1' end
        ''')
        lines=self.run_admitted([30,31])
        self.assertEqual(self.lua.globals().predict_calls,0)
        self.assertFalse(any('PILOT_PREDICT' in line or 'ERROR' in line for line in lines))
        # true means observe; the script source defaults to 3 ticks ahead.
        self.setUp()
        self.lua.execute(self.PILOT_SANDBOX+'''
          X4LAN_CONFIG.native_gate_pilot_predict=true
          local gate=package.loadlib('gate')();predict_arms={}
          gate.pilot_predict=function(mode,horizon,tau) predict_arms[#predict_arms+1]=mode..':'..horizon..':'..tau;return 1 end
          gate.pilot_predict_frame=function(tick) return 'tick='..tick..' horizon=3' end
        ''')
        lines=self.run_admitted([30])
        self.assertEqual(list(self.lua.globals().predict_arms.values()),['observe:3:100','observe:3:100'])
        self.assertTrue(any('PILOT_PREDICT_ARMED slot=1 mode=observe horizon=3 tau_ms=100' in line for line in lines))

    def test_pilot_predict_refuses_invalid_settings_before_loading(self):
        cases=["X4LAN_CONFIG.native_gate_pilot_predict='present'",  # without the render RNG split
               "X4LAN_CONFIG.native_gate_pilot_predict='fast'",
               "X4LAN_CONFIG.native_gate_pilot_predict=true;X4LAN_CONFIG.native_gate_pilot_predict_horizon=0",
               "X4LAN_CONFIG.native_gate_pilot_predict=true;X4LAN_CONFIG.native_gate_pilot_predict_horizon=31",
               "X4LAN_CONFIG.native_gate_pilot_predict=true;X4LAN_CONFIG.native_gate_pilot_predict_horizon=2.5",
               "X4LAN_CONFIG.native_gate_pilot_predict=true;X4LAN_CONFIG.native_gate_pilot_predict_tau=2001",
               "X4LAN_CONFIG.native_gate_pilot_predict=true;X4LAN_CONFIG.native_gate_pilot_predict_raw='yes'",
               "X4LAN_CONFIG.native_gate_pilot_predict=true;package.loadlib('gate')().pilot_predict=function() return 0 end",
               "X4LAN_CONFIG.native_gate_pilot_predict=true;package.loadlib('gate')().pilot_predict=nil"]
        for case in cases:
            self.setUp()
            self.lua.execute(self.PILOT_SANDBOX+"package.loadlib('gate')().pilot_predict=function() return 1 end\n"+case)
            self.lua.execute(SOURCE);self.lua.execute('update()')
            lines=list(self.lua.globals().logs.values())
            self.assertTrue(any('pilot predict refused' in line for line in lines),case)
            self.assertEqual(self.lua.globals().loads,0,case)

    HIT_CLAIMS = '''
      X4LAN_CONFIG.native_gate_pilot_source='wire'
      X4LAN_CONFIG.native_gate_pilot_delay=3
      X4LAN_CONFIG.native_gate_clients=2
      X4LAN_CONFIG.native_gate_bullet_rng=true
      X4LAN_CONFIG.native_gate_bullet_cadence='every'
      X4LAN_CONFIG.native_gate_hit_claims=true
      local gate=package.loadlib('gate')();order={};arms={};budgets={}
      gate.wire=function(version,clients) return (version==3 and clients==2) and 1 or 0 end
      gate.pilot_inputs=function() return 1 end
      gate.pilot_inputs_status=function() return 'armed=1 source=wire failures=0' end
      gate.pilot_inputs_frame=function() return 'PILOT_1:1:0:0000000000000000' end
      gate.bullet_cadence=function(mode) return (mode=='every' or mode=='observe') and 1 or 0 end
      gate.bullet_cadence_status=function() return 'mode=every' end
      gate.hit_claims=function(policy) arms[#arms+1]=policy;return 1 end
      gate.pilot_apply=function(slot,record) order[#order+1]='apply'..slot..':'..record;return 1 end
      gate.hit_claims_admit=function(slot,hits) order[#order+1]='admit'..slot..':'..hits;return admit_result or 1 end
      gate.hit_claims_resolve=function(tick)
        order[#order+1]='resolve'..tick
        return tick==1 and 'HITS_1:1:1:00000000000000aa,1/b/c/applied' or 'HITS_1:'..tick..':0:0000000000000000'
      end
      gate.hit_claims_proposal=function(tick,budget) order[#order+1]='proposal'..tick;budgets[#budgets+1]=budget;return proposal_hits[tick] end
      gate.hit_claims_status=function() return 'armed=1 policy=shooter' end
      gate.pilot_capture=function() order[#order+1]='capture';return '01'..string.rep('1',54) end
      gate.propose=function(tick,record) order[#order+1]='propose:'..record;return 1 end
      gate.report=function(tick) reports=reports+1;state.report_tick=tick;return 1 end
      X4LAN_SHA256.digest=function(data) hashed_data=data;return string.rep('b',64) end
    '''

    def test_native_hit_claims_section_stages_resolves_and_proposes(self):
        claim=inputs_ref.encode_hits([{'kind':1,'observe16':0,'projectile':'00000000000000b1','target':'0000000000176a13','point':[0,0,-40]}])
        mine=inputs_ref.encode_hits([{'kind':2,'observe16':1,'projectile':'00000000000000c1','target':'0000000000176a74','point':[0,0,8]}])
        pilot1,pilot2,local='01'+'a'*54,'01'+'c'*54,'01'+'1'*54
        blob1=inputs_ref.encode({'pilot':pilot1,'hits':claim})
        self.lua.execute(self.PILOT_PREREQUISITES+self.HIT_CLAIMS+"proposal_hits={[1]='%s'}" % mine)
        self.lua.execute(MUX);self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.lua.execute("state.boot_phase=3;state.issued=1;is_menu=false;state.wire_version=3;state.clients=2;state.inputs={'%s','%s'}" % (blob1,pilot2))
        self.lua.execute(MUX);self.lua.execute(SOURCE);self.lua.execute('update()')
        self.assertEqual(list(self.lua.globals().arms.values()),['shooter','shooter'])  # armed again after the UI reload
        order=list(self.lua.globals().order.values())
        proposed=inputs_ref.encode({'pilot':local,'hits':mine})
        self.assertEqual(order,['admit1:'+claim,'apply1:'+pilot1,'apply2:'+pilot2,'resolve1','capture','proposal1','propose:'+proposed])
        budget=list(self.lua.globals().budgets.values())[0]
        self.assertEqual(budget,2*(240-2-(2+28)-2))  # after the container header and the pilot section
        data=self.lua.globals().hashed_data
        self.assertIn('|HITS_1:1:1:00000000000000aa,1/b/c/applied',data)
        self.assertIn('staged=1/refused=0',data)  # the multiplexer's fold of the hits section
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('HIT_CLAIMS_ARMED policy=shooter' in line for line in lines))
        self.assertTrue(any('HIT_CLAIMS tick=1 HITS_1:1:1:' in line for line in lines))
        self.assertFalse(any('ERROR' in line for line in lines))
        # Nothing admitted or due, nothing proposed: no row, the bare pilot record is proposed.
        self.lua.execute("order={};state.issued=2;state.inputs={'%s','%s'};update()" % (pilot1,pilot2))
        self.assertEqual(list(self.lua.globals().order.values()),['apply1:'+pilot1,'apply2:'+pilot2,'resolve2','capture','proposal2','propose:'+local])
        self.assertFalse(any('HIT_CLAIMS tick=2' in line for line in self.lua.globals().logs.values()))
        self.assertIn('|HITS_1:2:0:0000000000000000',self.lua.globals().hashed_data)
        # A refused hits section is identical on every client: logged, not fatal.
        self.lua.execute("admit_result=0;state.issued=3;state.inputs={'%s','%s'};update()" % (blob1,pilot2))
        self.assertTrue(any('HIT_CLAIMS_REFUSED tick=3 slot=1' in line for line in self.lua.globals().logs.values()))
        self.assertIn('staged=0/refused=1',self.lua.globals().hashed_data)
        self.lua.execute("state.active=0;state.completed=3;setmetatable(state,{__index=function() return 0 end});update()")
        self.assertTrue(any('HIT_CLAIMS_STOP armed=1 policy=shooter' in line for line in self.lua.globals().logs.values()))

    def test_hit_claim_events_are_written_on_held_status_ticks(self):
        claim=inputs_ref.encode_hits([{'kind':1,'observe16':0,'projectile':'00000000000000b1','target':'0000000000176a13','point':[0,0,-40]}])
        pilot1,pilot2='01'+'a'*54,'01'+'c'*54
        blob1=inputs_ref.encode({'pilot':pilot1,'hits':claim})
        self.lua.execute(self.PILOT_PREREQUISITES+self.HIT_CLAIMS+'proposal_hits={};X4LAN_CONFIG.native_gate_status_stride=2')
        self.lua.execute(MUX);self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.lua.execute("state.boot_phase=3;state.issued=1;is_menu=false;state.wire_version=3;state.clients=2;state.inputs={'%s','%s'}" % (pilot1,pilot2))
        self.lua.execute(MUX);self.lua.execute(SOURCE);self.lua.execute('update();state.issued=2;update()')
        # Tick 3 is held (not among the first two, odd): its refused claim is still written at once.
        self.lua.execute("admit_result=0;state.issued=3;state.inputs={'%s','%s'};update()" % (blob1,pilot2))
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('HIT_CLAIMS_REFUSED tick=3 slot=1' in line for line in lines))
        self.assertTrue(any('PILOT_INPUTS tick=2 ' in line for line in lines))
        self.assertFalse(any('PILOT_INPUTS tick=3 ' in line for line in lines))
        self.assertFalse(any('ERROR' in line for line in lines))

    def test_pilot_fire_arms_and_adds_its_receipt_to_the_declared_state(self):
        for mode,expected in (('true','record'),("'script'",'script')):
            self.setUp()
            self.lua.execute(self.PILOT_SANDBOX+'''
              X4LAN_CONFIG.native_gate_actor_rng_all=true
              X4LAN_CONFIG.native_gate_agent_rng=true
              X4LAN_CONFIG.native_gate_ship_rng=true
              X4LAN_CONFIG.native_gate_bullet_rng=true
              X4LAN_CONFIG.native_gate_bullet_cadence='every'
              X4LAN_CONFIG.native_gate_pilot_fire=%s
              local gate=package.loadlib('gate')();fire_arms={}
              gate.actor_rng_all=function() return 1 end
              gate.actor_rng_all_status=function() return 'armed=1' end
              gate.bullet_cadence=function(mode) return mode=='every' and 1 or 0 end
              gate.bullet_cadence_status=function() return 'mode=every' end
              gate.pilot_fire=function(m) fire_arms[#fire_arms+1]=m;return 1 end
              gate.pilot_fire_frame=function() return 'PFIRE_1:'..state.issued..':2:00000000000000ff' end
              gate.pilot_fire_status=function() return 'mode=record armed=1' end
            ''' % mode)
            lines=self.run_admitted([30,31])
            self.assertEqual(list(self.lua.globals().fire_arms.values()),[expected,expected])
            self.assertTrue(any('PILOT_FIRE_ARMED mode='+expected in line for line in lines))
            self.assertIn('|PFIRE_1:31:2:00000000000000ff',self.lua.globals().hashed_data)
            self.assertFalse(any('ERROR' in line for line in lines))
        for case in ("X4LAN_CONFIG.native_gate_pilot_fire='always'","X4LAN_CONFIG.native_gate_pilot_fire=true;X4LAN_CONFIG.native_gate_actor_rng_all=nil",
                     "X4LAN_CONFIG.native_gate_pilot_fire=true;X4LAN_CONFIG.native_gate_bullet_cadence=nil",
                     "X4LAN_CONFIG.native_gate_pilot_fire=true;package.loadlib('gate')().pilot_fire=function() return 0 end"):
            self.setUp()
            self.lua.execute(self.PILOT_SANDBOX+'''
              X4LAN_CONFIG.native_gate_actor_rng_all=true
              X4LAN_CONFIG.native_gate_agent_rng=true
              X4LAN_CONFIG.native_gate_ship_rng=true
              X4LAN_CONFIG.native_gate_bullet_rng=true
              X4LAN_CONFIG.native_gate_bullet_cadence='every'
              local gate=package.loadlib('gate')()
              gate.actor_rng_all=function() return 1 end
              gate.actor_rng_all_status=function() return 'armed=1' end
              gate.bullet_cadence=function() return 1 end
              gate.bullet_cadence_status=function() return 'mode=every' end
              gate.pilot_fire=function() return 1 end
              gate.pilot_fire_frame=function() return 'PFIRE_1:0:0:0' end
            '''+case)
            self.lua.execute(SOURCE);self.lua.execute('update()')
            lines=list(self.lua.globals().logs.values())
            self.assertTrue(any('pilot fire refused' in line for line in lines),case)
            self.assertEqual(self.lua.globals().loads,0,case)

    def test_hit_claims_refuse_invalid_settings_before_loading(self):
        cases=["X4LAN_CONFIG.native_gate_hit_claims='always'",
               "X4LAN_CONFIG.native_gate_hit_claims=1",
               "X4LAN_CONFIG.native_gate_bullet_cadence=nil",
               "X4LAN_CONFIG.native_gate_bullet_cadence='observe'",
               "package.loadlib('gate')().hit_claims=function() return 0 end",
               "package.loadlib('gate')().hit_claims_admit=nil",
               "X4LAN_CONFIG.native_gate_pilot_source='script';X4LAN_CONFIG.native_gate_pilot_delay=nil"]
        for case in cases:
            self.setUp()
            self.lua.execute(self.PILOT_PREREQUISITES+self.HIT_CLAIMS+case)
            self.lua.execute(MUX);self.lua.execute(SOURCE);self.lua.execute('update()')
            lines=list(self.lua.globals().logs.values())
            self.assertTrue(any('hit claims refused' in line for line in lines),case)
            self.assertEqual(self.lua.globals().loads,0,case)
        # Native claims without the pilot slots are refused; 'carry' (the Lua carrier) is not.
        self.setUp()
        self.lua.execute(self.HIT_CLAIMS)
        self.lua.execute(MUX);self.lua.execute(SOURCE);self.lua.execute('update()')
        self.assertTrue(any('hit claims refused: native claims need native_gate_pilot_inputs' in line for line in self.lua.globals().logs.values()))
        self.assertEqual(self.lua.globals().loads,0)

    def test_pilot_ship_state_enters_the_declared_state_every_tick(self):
        self.lua.execute(self.PILOT_SANDBOX+'''
          X4LAN_CONFIG.native_gate_pilot_ship_state=true
          local gate=package.loadlib('gate')()
          ships={[1]={41472,'00000000000ae79c'},[2]={41728,'0000000000176ad5'}}
          gate.pilot_ship=function(slot) local s=ships[slot];if s then return s[1],s[2] end end
          local ffi=require('ffi')
          ffi.new=function(kind,value) assert(kind=='UniverseID');return value end
          dead={}
          ffi.C.IsComponentOperational=function(id) return not dead[id] end
          ffi.C.GetObjectPositionInSector=function(id) return {x=id,y=0.5,z=-1,yaw=1,pitch=2,roll=3} end
          ConvertStringToLuaID=function(text) return 'lua'..text end
          GetComponentData=function(id,a,b) assert(a=='hullpercent' and b=='shieldpercent');return id=='lua41472' and 100 or 50,25 end
        ''')
        lines=self.run_admitted([1])
        row='PILOT_SHIPS_1:1/00000000000ae79c/41472/0.5/-1/1/2/3/100/25,2/0000000000176ad5/41728/0.5/-1/1/2/3/50/25'
        self.assertIn('|PILOT_1:1:0:0000000000000000|'+row,self.lua.globals().hashed_data)
        self.assertTrue(any('PILOT_SHIPS tick=1 1/00000000000ae79c/41472/' in line for line in lines))
        self.lua.execute("dead[41728]=true;ships[1]=nil;state.issued=2;update()")
        self.assertIn('PILOT_SHIPS_1:1/0000000000176a13/unresolved,2/0000000000176ad5/dead',self.lua.globals().hashed_data)
        self.assertFalse(any('ERROR' in line for line in self.lua.globals().logs.values()))
        # Without the option the declared state is unchanged.
        self.setUp()
        self.lua.execute(self.PILOT_SANDBOX)
        self.run_admitted([1])
        self.assertNotIn('PILOT_SHIPS',self.lua.globals().hashed_data)

    def test_pilot_block_orders_wraps_stock_order_entries_and_map_hotkeys_once(self):
        self.lua.execute(self.PILOT_SANDBOX+'''
          X4LAN_CONFIG.native_gate_pilot_block_orders=true
          stock={}
          local function entry(name) return function() stock[#stock+1]=name;return 1 end end
          interact={name='InteractMenu',orderAttack=entry('orderAttack'),orderMoveWait=entry('orderMoveWait'),
            buttonTravelMode=entry('buttonTravelMode'),buttonExternal=entry('buttonExternal'),ordering=entry('ordering')}
          map={name='MapMenu',orderMoveWait=entry('map.orderMoveWait'),buttonNewOrder=entry('buttonNewOrder'),
            hotkey=function(action) stock[#stock+1]='hotkey:'..action end}
          Menus={interact,map,{name='OptionsMenu',orderAttack=entry('options')}}
        ''')
        lines=self.run_admitted([1,2])
        self.lua.execute('''
          results={interact.orderAttack(1,2),interact.orderMoveWait(),interact.buttonTravelMode(true),interact.buttonExternal(),
            interact.ordering(),map.orderMoveWait(),map.buttonNewOrder()}
          map.hotkey('INPUT_ACTION_ADDON_DETAILMONITOR_A_SHIFT');map.hotkey('INPUT_ACTION_ADDON_DETAILMONITOR_TRAVELMODE')
          map.hotkey('INPUT_ACTION_ADDON_DETAILMONITOR_SETA');map.hotkey('INPUT_ACTION_ADDON_DETAILMONITOR_T')
          Menus[3].orderAttack()
        ''')
        # Blocked entries return 0 and never reach the stock code; others still run.
        self.assertEqual(list(self.lua.globals().stock.values()),['buttonExternal','ordering','hotkey:INPUT_ACTION_ADDON_DETAILMONITOR_T','options'])
        self.assertEqual(list(self.lua.globals().results.values()),[0,0,0,1,1,0,0])
        lines=list(self.lua.globals().logs.values())
        self.assertEqual(sum('PILOT_ORDERS_BLOCKED menu=InteractMenu entries=3' in line for line in lines),1)  # wrapped once
        self.assertEqual(sum('PILOT_ORDERS_BLOCKED menu=MapMenu entries=3' in line for line in lines),1)
        for entry in ('menu=InteractMenu entry=orderAttack','menu=MapMenu entry=hotkey:INPUT_ACTION_ADDON_DETAILMONITOR_SETA'):
            self.assertTrue(any('PILOT_ORDER_BLOCKED '+entry in line for line in lines),entry)
        self.assertFalse(any('ERROR' in line for line in lines))
        # Without the option the menus stay untouched.
        self.setUp()
        self.lua.execute(self.PILOT_SANDBOX+"called=0;Menus={{name='MapMenu',orderMoveWait=function() called=called+1;return 1 end}}")
        self.run_admitted([1]);self.lua.execute('Menus[1].orderMoveWait()')
        self.assertEqual(self.lua.globals().called,1)

    def test_pilot_native_observation_logs_the_native_record_next_to_the_capture(self):
        self.lua.execute(self.PILOT_SANDBOX+'''
          X4LAN_CONFIG.native_gate_pilot_capture=true
          X4LAN_CONFIG.native_gate_pilot_observe_native=true
          local gate=package.loadlib('gate')()
          gate.pilot_capture=function() return string.rep('1',56) end
          native=0;gate.pilot_native_capture=function() native=native+1;if native==1 then return string.rep('2',56) end;return nil,'no_player_controller' end
        ''')
        lines=self.run_admitted([1,2])
        self.assertTrue(any('PILOT_CAPTURE tick=1 record='+'1'*56+' native='+'2'*56 in line for line in lines))
        self.assertTrue(any('PILOT_CAPTURE tick=2 record='+'1'*56+' native_refused=no_player_controller' in line for line in lines))
        self.assertNotIn('2'*56,self.lua.globals().hashed_data)
        for case in ("X4LAN_CONFIG.native_gate_pilot_capture=nil","package.loadlib('gate')().pilot_native_capture=nil",
                     "X4LAN_CONFIG.native_gate_pilot_observe_native='yes'"):
            self.setUp()
            self.lua.execute(self.PILOT_SANDBOX+"""
              X4LAN_CONFIG.native_gate_pilot_capture=true
              X4LAN_CONFIG.native_gate_pilot_observe_native=true
              package.loadlib('gate')().pilot_native_capture=function() return string.rep('2',56) end
            """+case)
            self.lua.execute(SOURCE);self.lua.execute('update()')
            self.assertTrue(any('pilot native observation' in line for line in self.lua.globals().logs.values()),case)
            self.assertEqual(self.lua.globals().loads,0,case)

    PILOT_NATIVE = '''
      X4LAN_CONFIG.native_gate_pilot_suppress_local=true
      X4LAN_CONFIG.native_gate_pilot_capture=true
      local gate=package.loadlib('gate')();native_calls={};refusal_calls=0
      gate.pilot_capture=function() return string.rep('1',56) end
      gate.pilot_native=function(requests) native_calls[#native_calls+1]=requests;return 1 end
      gate.pilot_native_refusals=function()
        refusal_calls=refusal_calls+1
        if refusal_calls==1 then return '1:action:0x12f:TOGGLE_TRAVEL_MODE:1,1:range:0x11:FIRE_PRIMARY_WEAPON:-1' end
      end
    '''

    def test_pilot_native_controls_arm_per_client_and_log_refusals(self):
        self.lua.execute(self.PILOT_SANDBOX+self.PILOT_NATIVE+"X4LAN_CONFIG.native_gate_pilot_capture_source={'native','device'}"
                         +";X4LAN_CONFIG.native_gate_pilot_device={nil,'xinput'}")
        lines=self.run_admitted([1,2])
        self.assertEqual(list(self.lua.globals().native_calls.values()),[1,1])  # armed again after the UI reload
        self.assertEqual(len(self.lua.globals().device_calls),0)
        self.assertTrue(any('PILOT_NATIVE_ARMED slot=1 requests=1' in line for line in lines))
        refused=[line for line in lines if 'PILOT_NATIVE_REFUSED' in line]
        self.assertEqual(len(refused),2)
        self.assertIn('PILOT_NATIVE_REFUSED tick=1 kind=action id=0x12f input=TOGGLE_TRAVEL_MODE value=1',refused[0])
        self.assertIn('PILOT_NATIVE_REFUSED tick=1 kind=range id=0x11 input=FIRE_PRIMARY_WEAPON value=-1',refused[1])
        self.assertEqual(self.lua.globals().refusal_calls,2)
        self.assertNotIn('TOGGLE_TRAVEL_MODE',self.lua.globals().hashed_data)
        self.assertFalse(any('ERROR' in line for line in lines))
        # The other client of the same table arms the device sampler; requests=false keeps the channels.
        self.setUp()
        self.lua.execute(self.PILOT_SANDBOX+self.PILOT_NATIVE+"X4LAN_CONFIG.slot='p2';X4LAN_CONFIG.native_gate_pilot_capture_source={'native','device'}"
                         +";X4LAN_CONFIG.native_gate_pilot_device={nil,'xinput'}")
        lines=self.run_admitted([1])
        self.assertEqual(len(self.lua.globals().native_calls),0)
        self.assertEqual(list(self.lua.globals().device_calls.values()),['xinput:0:0','xinput:0:0'])
        self.setUp()
        self.lua.execute(self.PILOT_SANDBOX+self.PILOT_NATIVE+"X4LAN_CONFIG.native_gate_pilot_capture_source='native'"
                         +";X4LAN_CONFIG.native_gate_pilot_native_requests=false")
        lines=self.run_admitted([1])
        self.assertEqual(list(self.lua.globals().native_calls.values()),[0,0])
        self.assertTrue(any('PILOT_NATIVE_ARMED slot=1 requests=0' in line for line in lines))
        # Without the option nothing is armed and no refusal is drained.
        self.setUp()
        self.lua.execute(self.PILOT_SANDBOX+self.PILOT_NATIVE)
        lines=self.run_admitted([1])
        self.assertEqual(len(self.lua.globals().native_calls),0);self.assertEqual(self.lua.globals().refusal_calls,0)

    def test_pilot_native_controls_refuse_invalid_settings_before_loading(self):
        cases=["X4LAN_CONFIG.native_gate_pilot_capture_source='keys'",
               "X4LAN_CONFIG.native_gate_pilot_capture_source='device'",
               "X4LAN_CONFIG.native_gate_pilot_capture_source='native';X4LAN_CONFIG.native_gate_pilot_device='kbm'",
               "X4LAN_CONFIG.native_gate_pilot_capture_source='native';X4LAN_CONFIG.native_gate_pilot_suppress_local=nil",
               "X4LAN_CONFIG.native_gate_pilot_capture_source='native';X4LAN_CONFIG.native_gate_pilot_capture=nil",
               "X4LAN_CONFIG.native_gate_pilot_capture_source='native';X4LAN_CONFIG.native_gate_pilot_native_requests='yes'",
               "X4LAN_CONFIG.native_gate_pilot_capture_source='native';package.loadlib('gate')().pilot_native=nil",
               "X4LAN_CONFIG.native_gate_pilot_capture_source='native';package.loadlib('gate')().pilot_native=function() return 0 end"]
        for case in cases:
            self.setUp()
            self.lua.execute(self.PILOT_SANDBOX+self.PILOT_NATIVE+case)
            self.lua.execute(SOURCE);self.lua.execute('update()')
            lines=list(self.lua.globals().logs.values())
            self.assertTrue(any('pilot capture source' in line or 'pilot native controls refused' in line or 'pilot inputs refused' in line
                                for line in lines),case)
            self.assertEqual(self.lua.globals().loads,0,case)

    def test_battle_engage_false_keeps_the_fixture_idle(self):
        for setting,expected in (('nil',['Engage']),('false',[])):
            self.setUp()
            self.lua.execute(f'''
              X4LAN_CONFIG.native_gate_hash='battle'
              X4LAN_CONFIG.native_gate_battle_engage={setting}
              events={{}};AddUITriggeredEvent=function(screen,control) events[#events+1]=control end
            ''')
            self.run_admitted([10])
            self.assertEqual(list(self.lua.globals().events.values()),expected)
        self.setUp()
        self.lua.execute("X4LAN_CONFIG.native_gate_battle_engage=true")
        with self.assertRaises(LuaError):self.lua.execute(SOURCE)

    def test_travel_snapshot_excludes_inline_mode_and_requires_its_prerequisites(self):
        for serial,mask,inline,value in (('false','3','nil','true'),('true','1','nil','true'),('true','3','true','true'),('true','3','nil',"'1'")):
            self.setUp()
            self.lua.execute('''
              X4LAN_CONFIG.native_gate_serial_pathing=%s
              X4LAN_CONFIG.native_gate_path_determinism=%s
              X4LAN_CONFIG.native_gate_travel_determinism=%s
              X4LAN_CONFIG.native_gate_travel_snapshot=%s
              local gate=package.loadlib('gate')()
              gate.path_determinism=function() return 1 end
              gate.travel_determinism=function() return 1 end
              gate.travel_snapshot=function() snapshot_calls=(snapshot_calls or 0)+1;return 1 end
            ''' % (serial,mask,inline,value))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertIsNone(self.lua.globals().snapshot_calls);self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any('refused' in line for line in self.lua.globals().logs.values()))

    def test_render_rng_split_arms_before_loading_and_logs_every_admitted_tick(self):
        for value,mode in (('true','split'),("'observe'",'observe')):
            self.setUp()
            self.lua.execute('''
              X4LAN_CONFIG.native_gate_render_rng_split=%s
              local gate=package.loadlib('gate')();modes={}
              gate.render_rng_split=function(mode) modes[#modes+1]=mode;return 1 end
              gate.render_rng_split_status=function() return 'mode=%s armed=1 installed=1 frame=0 calls=3 failures=0' end
              local load=LoadGame;LoadGame=function(slot) assert(#modes==1);load(slot) end
            ''' % (value,mode))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(list(self.lua.globals().modes.values()),[mode]);self.assertEqual(self.lua.globals().loads,1)
            self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
            self.lua.execute(SOURCE);self.lua.execute('update()')
            self.assertEqual(list(self.lua.globals().modes.values()),[mode,mode]) # identical rerun after the UI reload
            self.lua.execute('%sstate.active=0;state.reason=1;update()' % STOP_FIELDS)
            lines=list(self.lua.globals().logs.values())
            self.assertTrue(any('RENDER_RNG_SPLIT_ARMED mode='+mode in line for line in lines))
            self.assertTrue(any('RENDER_RNG_SPLIT tick=1 mode='+mode+' armed=1' in line for line in lines))
            self.assertTrue(any('RENDER_RNG_SPLIT_STOP mode='+mode in line for line in lines))
            self.assertFalse(any('ERROR' in line for line in lines))

    def test_render_rng_split_refuses_unknown_modes_and_native_refusal(self):
        for value,native in (("'yes'",1),('1',1),("'SPLIT'",1),('true',0)):
            self.setUp()
            self.lua.execute('''
              X4LAN_CONFIG.native_gate_render_rng_split=%s
              local gate=package.loadlib('gate')()
              gate.render_rng_split=function(mode) split_calls=(split_calls or 0)+1;return %d end
              gate.render_rng_split_status=function() return 'mode=off' end
            ''' % (value,native))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any('render RNG split refused' in line for line in self.lua.globals().logs.values()))
            self.assertEqual(self.lua.globals().split_calls,1 if value=='true' else None)

    def test_bullet_cadence_arms_before_loading_and_logs_every_admitted_tick(self):
        for value,mode,module in (('true','every','rng'),("'every'",'every','diag'),("'observe'",'observe','rng')):
            self.setUp()
            self.lua.execute('''
              X4LAN_CONFIG.native_gate_bullet_%s=true
              X4LAN_CONFIG.native_gate_bullet_cadence=%s
              local gate=package.loadlib('gate')();modes={}
              gate.bullet_cadence=function(mode,...) assert(select('#',...)==0);modes[#modes+1]=mode;return 1 end
              gate.bullet_cadence_status=function() return 'mode=%s armed=1 installed=1 site=33eb frame='..state.issued..' failures=0' end
              gate.bullet=function() return 0 end;gate.bullet_clear=function() return 1 end
              local load=LoadGame;LoadGame=function(slot) assert(#modes==1);load(slot) end
            ''' % (module,value,mode))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(list(self.lua.globals().modes.values()),[mode]);self.assertEqual(self.lua.globals().loads,1)
            self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
            self.lua.execute(SOURCE);self.lua.execute('update()')
            self.assertEqual(list(self.lua.globals().modes.values()),[mode,mode]) # identical rerun after the UI reload
            bullet_stop='state.bullet_calls=0;state.bullet_created=0;state.bullet_restored=1;state.bullet_failures=0;state.bullet_unknown=0;state.bullet_outside=0;state.bullet_unsupported=0;'
            self.lua.execute('%s%sstate.active=0;state.reason=1;update()' % (STOP_FIELDS,bullet_stop))
            lines=list(self.lua.globals().logs.values())
            self.assertTrue(any('BULLET_CADENCE_ARMED mode='+mode in line for line in lines))
            self.assertTrue(any('BULLET_CADENCE tick=1 mode='+mode+' armed=1 installed=1 site=33eb frame=1' in line for line in lines))
            self.assertTrue(any('BULLET_CADENCE_STOP mode='+mode in line for line in lines))
            self.assertFalse(any('ERROR' in line for line in lines))

    def test_bullet_cadence_requires_a_known_mode_the_bullet_module_and_native_acceptance(self):
        for value,module,native,called in (("'yes'",'rng',1,False),('1',"rng",1,False),("'EVERY'",'rng',1,False),
                                           ('true',None,1,False),("'observe'",None,1,False),('true','rng',0,True)):
            self.setUp()
            self.lua.execute('''
              %s
              X4LAN_CONFIG.native_gate_bullet_cadence=%s
              local gate=package.loadlib('gate')()
              gate.bullet_cadence=function(mode) cadence_calls=(cadence_calls or 0)+1;return %d end
              gate.bullet_cadence_status=function() return 'mode=off' end
            ''' % ('X4LAN_CONFIG.native_gate_bullet_%s=true' % module if module else '',value,native))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any('bullet cadence refused' in line for line in self.lua.globals().logs.values()))
            self.assertEqual(self.lua.globals().cadence_calls,1 if called else None)

    def test_nav_cadence_arms_before_loading_and_logs_every_admitted_tick(self):
        for value,mode,module in (('true','far','rng'),("'every'",'every','initial_refresh'),("'observe'",'observe','rng')):
            self.setUp()
            self.lua.execute('''
              X4LAN_CONFIG.native_gate_nav_%s=true
              X4LAN_CONFIG.native_gate_nav_cadence=%s
              local gate=package.loadlib('gate')();modes={}
              gate.nav_cadence=function(mode,...) assert(select('#',...)==0);modes[#modes+1]=mode;return 1 end
              gate.nav_cadence_status=function() return 'mode=%s armed=1 installed=1 sites=05eb:14eb:9066:17eb frame='..state.issued..' failures=0' end
              local load=LoadGame;LoadGame=function(slot) assert(#modes==1);load(slot) end
            ''' % (module,value,mode))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(list(self.lua.globals().modes.values()),[mode]);self.assertEqual(self.lua.globals().loads,1)
            self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
            self.lua.execute(SOURCE);self.lua.execute('update()')
            self.assertEqual(list(self.lua.globals().modes.values()),[mode,mode]) # identical rerun after the UI reload
            self.lua.execute('%sstate.active=0;state.reason=1;update()' % STOP_FIELDS)
            lines=list(self.lua.globals().logs.values())
            self.assertTrue(any('NAV_CADENCE_ARMED mode='+mode in line for line in lines))
            self.assertTrue(any('NAV_CADENCE tick=1 mode='+mode+' armed=1 installed=1 sites=05eb:14eb:9066:17eb frame=1' in line for line in lines))
            self.assertTrue(any('NAV_CADENCE_STOP mode='+mode in line for line in lines))
            self.assertFalse(any('ERROR' in line for line in lines))

    def test_actor_rng_all_arms_before_loading_and_logs_every_admitted_tick(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_world_identities=true
          X4LAN_CONFIG.native_gate_agent_rng=true
          X4LAN_CONFIG.native_gate_ship_rng=true
          X4LAN_CONFIG.native_gate_actor_rng_all=true
          local gate=package.loadlib('gate')();arms=0
          gate.actor_rng_all=function(...) assert(select('#',...)==0);arms=arms+1;return 1 end
          gate.actor_rng_all_status=function() return 'armed=1 frames='..state.issued..' last_tick='..state.issued..' table_full=0 failures=0' end
          local load=LoadGame;LoadGame=function(slot) assert(arms==1);load(slot) end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual((self.lua.globals().arms, self.lua.globals().loads), (1, 1))
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update()')
        self.assertEqual(self.lua.globals().arms, 2)  # identical rerun after the UI reload
        identity = ('state.world_identity_calls=0;state.world_identity_count=0;state.world_identity_late=0;'
                    'state.world_identity_restored=1;state.world_identity_failures=0;')
        self.lua.execute('%s%sstate.active=0;state.reason=1;update()' % (STOP_FIELDS, identity))
        lines = list(self.lua.globals().logs.values())
        self.assertTrue(any('ACTOR_RNG_ALL_ARMED' in line for line in lines))
        self.assertTrue(any('ACTOR_RNG_ALL tick=1 armed=1 frames=1 last_tick=1' in line for line in lines))
        self.assertTrue(any('ACTOR_RNG_ALL_STOP armed=1' in line for line in lines))
        self.assertFalse(any('ERROR' in line for line in lines))

    def test_ship_sound_rng_arms_before_loading_and_logs_every_admitted_tick(self):
        for value,mode in (('true','split'),("'split'",'split'),("'observe'",'observe')):
            self.setUp()
            self.lua.execute('''
              X4LAN_CONFIG.native_gate_ship_sound_rng=%s
              local gate=package.loadlib('gate')();modes={}
              gate.ship_sound_rng=function(mode,...) assert(select('#',...)==0);modes[#modes+1]=mode;return 1 end
              gate.ship_sound_rng_status=function() return 'mode=%s installed=1 frame=1 calls=2 drew=1 draws=3' end
            ''' % (value,mode))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(list(self.lua.globals().modes.values()),[mode]);self.assertEqual(self.lua.globals().loads,1)
            self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false;update()')
            lines=list(self.lua.globals().logs.values())
            self.assertTrue(any('SHIP_SOUND_RNG_ARMED mode='+mode in line for line in lines))
            self.assertTrue(any('SHIP_SOUND_RNG tick=1 mode=%s installed=1' % mode in line for line in lines))
        for config in ("X4LAN_CONFIG.native_gate_ship_sound_rng='drop'","X4LAN_CONFIG.native_gate_ship_sound_rng='split';native=0"):
            self.setUp()
            self.lua.execute(config+'''
              local gate=package.loadlib('gate')()
              gate.ship_sound_rng=function() return native or 1 end
              gate.ship_sound_rng_status=function() return 'mode=split' end
            ''')
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any('ship sound RNG refused' in line for line in self.lua.globals().logs.values()))

    def test_player_timer_arms_before_loading_and_logs_every_admitted_tick(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_player_timer=true
          local gate=package.loadlib('gate')();calls=0
          gate.player_timer=function(...) assert(select('#',...)==0);calls=calls+1;return 1 end
          gate.player_timer_status=function() return 'installed=1 calls=3 admitted=1 rebased=1 first=1' end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().calls,1);self.assertEqual(self.lua.globals().loads,1)
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false;update()')
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('PLAYER_TIMER_ARMED' in line for line in lines))
        self.assertTrue(any('PLAYER_TIMER tick=1 installed=1 calls=3 admitted=1 rebased=1 first=1' in line for line in lines))
        self.lua.execute('state.active=0;state.reason=1;update()')
        self.assertTrue(any('PLAYER_TIMER_STOP installed=1' in line for line in self.lua.globals().logs.values()))
        for config in ("X4LAN_CONFIG.native_gate_player_timer='rebase'","X4LAN_CONFIG.native_gate_player_timer=true;native=0"):
            self.setUp()
            self.lua.execute(config+'''
              local gate=package.loadlib('gate')()
              gate.player_timer=function() return native or 1 end
              gate.player_timer_status=function() return 'stage=pins' end
            ''')
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any('player timer refused' in line for line in self.lua.globals().logs.values()))

    def test_warp_rng_arms_before_loading_and_drains_rows_every_admitted_tick(self):
        for value,mode in (('true','split'),("'split'",'split'),("'observe'",'observe')):
            self.setUp()
            self.lua.execute('''
              X4LAN_CONFIG.native_gate_world_identities=true
              X4LAN_CONFIG.native_gate_warp_rng=%s
              local gate=package.loadlib('gate')();modes={}
              gate.warp_rng=function(mode,...) assert(select('#',...)==0);modes[#modes+1]=mode;return 1 end
              gate.warp_rng_status=function() return 'mode=%s installed=1 calls=1 admitted=1' end
              rows={'index=0 tick=1 key=s:00000000000add39 ordinal=1 space=000000000013e75f flag=1 direction=0 keyed=1 natural=1 word=2 draws=2'}
              gate.warp_rng_row=function(i) return rows[i+1] end
            ''' % (value,mode))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(list(self.lua.globals().modes.values()),[mode]);self.assertEqual(self.lua.globals().loads,1)
            self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false;update();update()')
            lines=list(self.lua.globals().logs.values())
            self.assertTrue(any('WARP_RNG_ARMED mode='+mode in line for line in lines))
            self.assertTrue(any('WARP_RNG tick=1 mode=%s installed=1' % mode in line for line in lines))
            self.assertEqual(sum('WARP_RNG_ROW index=0 tick=1 key=s:00000000000add39 ordinal=1' in line for line in lines),1)
        for config in ("X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_warp_rng='key'",
                       "X4LAN_CONFIG.native_gate_warp_rng=true",
                       "X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_warp_rng=true;native=0"):
            self.setUp()
            self.lua.execute(config+'''
              local gate=package.loadlib('gate')()
              gate.warp_rng=function() return native or 1 end
              gate.warp_rng_status=function() return 'stage=pins' end
              gate.warp_rng_row=function() return nil end
            ''')
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any('warp RNG refused' in line for line in self.lua.globals().logs.values()),config)

    def test_physics_trace_children_arm_after_the_trace_and_drain_child_rows(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_world_identities=true
          X4LAN_CONFIG.native_gate_physics_trace=0
          X4LAN_CONFIG.native_gate_physics_trace_children=3
          local gate=package.loadlib('gate')();order={}
          gate.physics_trace=function(rows) order[#order+1]='trace:'..rows;return 1 end
          gate.physics_trace_children=function(t,from,...) assert(select('#',...)==0);order[#order+1]='children:'..t..':'..from;return 1 end
          gate.physics_trace_status=function() return 'installed=1 child_ticks=3' end
          rows={[3]={'index=0 tick=1 step=1 scene=0 key=0000000000074f4a klass=0 which=0 child=0/2 class=02a2ad00'}}
          gate.physics_trace_row=function(kind,i,f) return rows[kind] and rows[kind][i+1] end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,1)
        self.assertEqual(list(self.lua.globals().order.values()),['trace:0','children:3:1'])
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false;update();state.issued=2;update()')
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('PHYSICS_TRACE_CHILDREN_ARMED ticks=3 from=1' in line for line in lines))
        self.assertEqual(sum('PHYSICS_CHILD index=0 tick=1 step=1 scene=0 key=0000000000074f4a' in line for line in lines),1)
        for config in ("X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_physics_trace_children=3",
                       "X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_physics_trace=0;X4LAN_CONFIG.native_gate_physics_trace_children=0",
                       "X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_physics_trace=0;X4LAN_CONFIG.native_gate_physics_trace_children=3601",
                       "X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_physics_trace=0;X4LAN_CONFIG.native_gate_physics_trace_children=3;X4LAN_CONFIG.native_gate_physics_trace_children_from=4",
                       "X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_physics_trace=0;X4LAN_CONFIG.native_gate_physics_trace_children_from=1",
                       "X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_physics_trace=0;X4LAN_CONFIG.native_gate_physics_trace_children=true",
                       "X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_physics_trace=0;X4LAN_CONFIG.native_gate_physics_trace_children=3;native=0"):
            self.setUp()
            self.lua.execute(config+'''
              local gate=package.loadlib('gate')()
              gate.physics_trace=function() return 1 end
              gate.physics_trace_children=function() return native or 1 end
              gate.physics_trace_status=function() return 'stage=armed' end
              gate.physics_trace_row=function() return nil end
            ''')
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0,config)
            self.assertTrue(any('physics trace children refused' in line for line in self.lua.globals().logs.values()),config)

    def test_physics_trace_children_window_and_saved_body_rows(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_world_identities=true
          X4LAN_CONFIG.native_gate_physics_trace=900
          X4LAN_CONFIG.native_gate_physics_trace_children=700
          X4LAN_CONFIG.native_gate_physics_trace_children_from=600
          X4LAN_CONFIG.native_gate_physics_trace_saved=true
          local gate=package.loadlib('gate')();order={}
          gate.physics_trace=function(rows) order[#order+1]='trace:'..rows;return 1 end
          gate.physics_trace_children=function(t,from) order[#order+1]='children:'..t..':'..from;return 1 end
          gate.physics_trace_saved=function(...) assert(select('#',...)==0);order[#order+1]='saved';return 1 end
          gate.physics_trace_status=function() return 'installed=1' end
          gate.physics_trace_row=function() return nil end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,1)
        self.assertEqual(list(self.lua.globals().order.values()),['trace:900','children:700:600','saved'])
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('PHYSICS_TRACE_CHILDREN_ARMED ticks=700 from=600' in line for line in lines))
        self.assertTrue(any('PHYSICS_TRACE_SAVED_ARMED rows=900' in line for line in lines))
        for config in ("X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_physics_trace=0;X4LAN_CONFIG.native_gate_physics_trace_saved=true",
                       "X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_physics_trace_saved=true",
                       "X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_physics_trace=900;X4LAN_CONFIG.native_gate_physics_trace_saved=1",
                       "X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_physics_trace=900;X4LAN_CONFIG.native_gate_physics_trace_saved=true;native=0"):
            self.setUp()
            self.lua.execute(config+'''
              local gate=package.loadlib('gate')()
              gate.physics_trace=function() return 1 end
              gate.physics_trace_saved=function() return native or 1 end
              gate.physics_trace_status=function() return 'stage=armed' end
              gate.physics_trace_row=function() return nil end
            ''')
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0,config)
            self.assertTrue(any('physics trace saved rows refused' in line for line in self.lua.globals().logs.values()),config)

    def test_transition_rows_arm_before_loading_and_drain_every_admitted_tick(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_world_identities=true
          X4LAN_CONFIG.native_gate_transition_rows=true
          local gate=package.loadlib('gate')();calls=0
          gate.transition_rows=function(...) assert(select('#',...)==0);calls=calls+1;return 1 end
          gate.transition_rows_status=function() return 'installed=1 calls=2 rows=1' end
          rows={'index=0 tick=1 ordinal=1 gate=s:000000000001fd20 class=02c34050 object=s:000000000008bddc flag=1 time=0 now=0 ui=1'}
          gate.transition_row=function(i) return rows[i+1] end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().calls,1);self.assertEqual(self.lua.globals().loads,1)
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false;update();update()')
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('TRANSITION_ROWS_ARMED' in line for line in lines))
        self.assertEqual(sum('TRANSITION_ROW index=0 tick=1 ordinal=1 gate=s:000000000001fd20' in line for line in lines),1)
        self.lua.execute('state.active=0;state.reason=1;update()')
        self.assertTrue(any('TRANSITION_ROWS_STOP installed=1' in line for line in self.lua.globals().logs.values()))
        for config in ("X4LAN_CONFIG.native_gate_transition_rows=true",
                       "X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_transition_rows='all'",
                       "X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_transition_rows=true;native=0"):
            self.setUp()
            self.lua.execute(config+'''
              local gate=package.loadlib('gate')()
              gate.transition_rows=function() return native or 1 end
              gate.transition_rows_status=function() return 'stage=pins' end
              gate.transition_row=function() return nil end
            ''')
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any('transition rows refused' in line for line in self.lua.globals().logs.values()),config)

    def test_tempzone_order_arms_before_loading_and_logs_status_and_rows_every_admitted_tick(self):
        for value,mode,tsnap in (('true','order',True),("'order'",'order',True),("'observe'",'observe',False)):
            self.setUp()
            self.lua.execute('''
              X4LAN_CONFIG.native_gate_world_identities=true
              X4LAN_CONFIG.native_gate_serial_pathing=true
              X4LAN_CONFIG.native_gate_path_determinism=3
              X4LAN_CONFIG.native_gate_travel_snapshot=%s
              X4LAN_CONFIG.native_gate_tempzone_order=%s
              local gate=package.loadlib('gate')();modes={}
              gate.path_determinism=function() return 1 end
              gate.path_determinism_status=function() return 'armed=1 mask=3' end
              gate.travel_snapshot=function() return 1 end
              gate.travel_snapshot_status=function() return 'armed=1 installed=1 read=1 tick=1 failures=0' end
              gate.tempzone_order=function(mode,...) assert(select('#',...)==0);modes[#modes+1]=mode;return 1 end
              gate.tempzone_order_status=function() return 'mode=%s installed=1 passes=1 blocks=1' end
              rows={'index=0 tick=1 kind=grant site=e91241 thread=moveworker sector=s:0000000000000010 pos=1,2,3 zone=88001 active=0 n=3 set=1 draws=0'}
              gate.tempzone_order_row=function(i) return rows[i+1] end
            ''' % (tsnap and 'true' or 'nil',value,mode))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(list(self.lua.globals().modes.values()),[mode]);self.assertEqual(self.lua.globals().loads,1)
            self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false;update();update()')
            lines=list(self.lua.globals().logs.values())
            self.assertTrue(any('TEMPZONE_ORDER_ARMED mode='+mode in line for line in lines))
            self.assertTrue(any('TEMPZONE_ORDER tick=1 mode=%s installed=1' % mode in line for line in lines))
            self.assertEqual(sum('TEMPZONE_ROW index=0 tick=1 kind=grant site=e91241' in line for line in lines),1)
            self.lua.execute('state.active=0;state.reason=144;update()')
            self.assertTrue(any('TEMPZONE_ORDER_STOP mode='+mode in line for line in self.lua.globals().logs.values()))
        for config in ("X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_serial_pathing=true;X4LAN_CONFIG.native_gate_tempzone_order='sort'",
                       "X4LAN_CONFIG.native_gate_serial_pathing=true;X4LAN_CONFIG.native_gate_tempzone_order='observe'",
                       "X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_tempzone_order='observe'",
                       "X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_serial_pathing=true;X4LAN_CONFIG.native_gate_tempzone_order=true",
                       "X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_serial_pathing=true;X4LAN_CONFIG.native_gate_tempzone_order='observe';native=0"):
            self.setUp()
            self.lua.execute(config+'''
              local gate=package.loadlib('gate')()
              gate.tempzone_order=function() return native or 1 end
              gate.tempzone_order_status=function() return 'stage=pins' end
              gate.tempzone_order_row=function() return nil end
            ''')
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any('tempzone order refused' in line for line in self.lua.globals().logs.values()),config)

    def test_frame_watch_arms_before_the_load_ends_before_the_frame_and_drains_rows(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_seed_rng=true
          X4LAN_CONFIG.native_gate_draw_watch_frame=true
          local gate=package.loadlib('gate')();order={};rows={'index=0 tick=777 id=0000000000000000 store=1 rip=i014ddcc2 chain=i014eefe4 word=0000000000000001'}
          gate.draw_watch_frame=function(...) assert(select('#',...)==0);order[#order+1]='arm';return 1 end
          gate.draw_watch_frame_end=function() order[#order+1]='end';return 0 end
          gate.draw_watch_status=function() return 'frame=1' end
          gate.draw_watch_row=function(i) return rows[i+1] end
          gate.draw_watch_stray=function() return nil end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,1)
        calls=list(self.lua.globals().order.values())
        self.assertEqual(calls.count('arm'),1)
        self.assertEqual(calls[calls.index('arm')+1:].count('arm'),0)
        self.assertIn('end',calls)
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false;update()')
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('DRAW_WATCH_FRAME_ARMED' in line for line in lines))
        self.assertTrue(any('DRAW_WATCH_ROW index=0 tick=777 id=0000000000000000 store=1 rip=i014ddcc2' in line for line in lines))

    def test_frame_watch_refused_without_the_seed_or_with_the_list(self):
        block=SOURCE[SOURCE.index("  if cfg.native_gate_draw_watch_frame then"):SOURCE.index("   log('DRAW_WATCH_FRAME_ARMED')")]
        self.assertIn('cfg.native_gate_seed_rng and not cfg.native_gate_draw_watch',block)
        self.assertIn("'draw watch frame refused'",block)

    def test_draw_watch_arms_its_ids_after_the_all_ship_rng_and_drains_rows(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_world_identities=true
          X4LAN_CONFIG.native_gate_agent_rng=true
          X4LAN_CONFIG.native_gate_ship_rng=true
          X4LAN_CONFIG.native_gate_actor_rng_all=true
          X4LAN_CONFIG.native_gate_draw_watch={'00000000000ae79c'}
          local gate=package.loadlib('gate')();order={};rows={'index=0 tick=777 id=00000000000ae79c store=1 rip=i014ddcc2 chain=i014eefe4,-00000000,-00000000,-00000000 word=1'}
          gate.actor_rng_all=function() order[#order+1]='all';return 1 end
          gate.actor_rng_all_status=function() return 'armed=1' end
          gate.draw_watch_add=function(id) order[#order+1]='add '..id;return 1 end
          gate.draw_watch=function(...) assert(select('#',...)==0);order[#order+1]='arm';return 1 end
          gate.draw_watch_status=function() return 'armed=1 ids=1' end
          gate.draw_watch_row=function(i) return rows[i+1] end
          strays={'row=0 stray=1 tick=777 mine=1 active=0 armed=1 admitted=1 dr6=ffff0ff1 rip=g0009b64b chain=g0007d301'}
          gate.draw_watch_stray=function(i) return strays[i+1] end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(list(self.lua.globals().order.values()),['all','add 00000000000ae79c','arm']);self.assertEqual(self.lua.globals().loads,1)
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false;update()')
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('DRAW_WATCH_ARMED ids=1' in line for line in lines))
        self.assertTrue(any('DRAW_WATCH_ROW index=0 tick=777 id=00000000000ae79c store=1 rip=i014ddcc2' in line for line in lines))
        self.assertEqual(sum('DRAW_WATCH_STRAY row=0 stray=1 tick=777 mine=1' in line for line in lines),1)
        for config,why in (("X4LAN_CONFIG.native_gate_draw_watch={'00000000000ae79c'}",'draw watch needs native_gate_actor_rng_all'),
                           ("X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_agent_rng=true;X4LAN_CONFIG.native_gate_ship_rng=true;"
                            "X4LAN_CONFIG.native_gate_actor_rng_all=true;X4LAN_CONFIG.native_gate_draw_watch={'ae79c'}",'draw watch refused'),
                           ("X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_agent_rng=true;X4LAN_CONFIG.native_gate_ship_rng=true;"
                            "X4LAN_CONFIG.native_gate_actor_rng_all=true;X4LAN_CONFIG.native_gate_draw_watch={'00000000000ae79c'};native=0",'draw watch refused')):
            self.setUp()
            self.lua.execute(config+'''
              local gate=package.loadlib('gate')()
              gate.actor_rng_all=function() return 1 end
              gate.actor_rng_all_status=function() return 'armed=1' end
              gate.draw_watch_add=function() return 1 end
              gate.draw_watch=function() return native or 1 end
              gate.draw_watch_status=function() return 'stage=self_test' end
              gate.draw_watch_row=function() return nil end
              gate.draw_watch_stray=function() return nil end
            ''')
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any(why in line for line in self.lua.globals().logs.values()),why)

    def test_actor_rng_rows_arm_after_the_all_ship_rng_and_drain_every_admitted_tick(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_world_identities=true
          X4LAN_CONFIG.native_gate_agent_rng=true
          X4LAN_CONFIG.native_gate_ship_rng=true
          X4LAN_CONFIG.native_gate_actor_rng_all=true
          X4LAN_CONFIG.native_gate_actor_rng_rows=true
          local gate=package.loadlib('gate')();order={};rows={'index=0 tick=1 path=ship id=00000000000ae79c draws=1'}
          gate.actor_rng_all=function() order[#order+1]='all';return 1 end
          gate.actor_rng_all_status=function() return 'armed=1 frames=1' end
          gate.actor_rng_rows=function(...) assert(select('#',...)==0);order[#order+1]='rows';return 1 end
          gate.actor_rng_row=function(i) return rows[i+1] end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(list(self.lua.globals().order.values()),['all','rows']);self.assertEqual(self.lua.globals().loads,1)
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false;update()')
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('ACTOR_RNG_ROWS_ARMED' in line for line in lines))
        self.assertTrue(any('ACTOR_RNG_ROW index=0 tick=1 path=ship id=00000000000ae79c draws=1' in line for line in lines))
        for config,why in (("X4LAN_CONFIG.native_gate_actor_rng_rows=true",'actor RNG rows need native_gate_actor_rng_all'),
                           ("X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_agent_rng=true;X4LAN_CONFIG.native_gate_ship_rng=true;"
                            "X4LAN_CONFIG.native_gate_actor_rng_all=true;X4LAN_CONFIG.native_gate_actor_rng_rows=true;native=0",'actor RNG rows refused')):
            self.setUp()
            self.lua.execute(config+'''
              local gate=package.loadlib('gate')()
              gate.actor_rng_all=function() return 1 end
              gate.actor_rng_all_status=function() return 'armed=1' end
              gate.actor_rng_rows=function() return native or 1 end
              gate.actor_rng_row=function() return nil end
            ''')
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any(why in line for line in self.lua.globals().logs.values()),why)

    def test_actor_rng_all_requires_true_identities_seeded_modules_and_native_acceptance(self):
        full = {'native_gate_world_identities': 'true', 'native_gate_agent_rng': 'true', 'native_gate_ship_rng': 'true'}
        cases = [('1', full, 1, False), ("'on'", full, 1, False), ('true', full, 0, True)]
        for missing in full:
            cases.append(('true', {k: v for k, v in full.items() if k != missing}, 1, False))
        for value, flags, native, called in cases:
            self.setUp()
            self.lua.execute('\n'.join(f'X4LAN_CONFIG.{k}={v}' for k, v in flags.items())+'''
              X4LAN_CONFIG.native_gate_actor_rng_all=%s
              local gate=package.loadlib('gate')()
              gate.actor_rng_all=function() arm_calls=(arm_calls or 0)+1;return %d end
              gate.actor_rng_all_status=function() return 'armed=0' end
            ''' % (value, native))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            case = (value, sorted(flags), native)
            self.assertEqual(self.lua.globals().loads, 0, case)
            self.assertTrue(any('all-ship actor RNG refused' in line for line in self.lua.globals().logs.values()), case)
            self.assertEqual(self.lua.globals().arm_calls, 1 if called else None, case)

    def test_nav_cadence_requires_a_known_mode_the_nav_module_and_native_acceptance(self):
        for value,module,native,called in (("'fixed'",'rng',1,False),("'FAR'",'rng',1,False),('1','rng',1,False),
                                           ('true',None,1,False),("'every'",None,1,False),("'far'",'diag',0,True)):
            self.setUp()
            self.lua.execute('''
              %s
              X4LAN_CONFIG.native_gate_nav_cadence=%s
              local gate=package.loadlib('gate')()
              gate.nav_cadence=function(mode) cadence_calls=(cadence_calls or 0)+1;return %d end
              gate.nav_cadence_status=function() return 'mode=off' end
            ''' % ('X4LAN_CONFIG.native_gate_nav_%s=true' % module if module else '',value,native))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any('nav cadence refused' in line for line in self.lua.globals().logs.values()))
            self.assertEqual(self.lua.globals().cadence_calls,1 if called else None)

    def test_view_points_arm_before_loading_and_latch_every_admitted_tick(self):
        for value,mode in (('true','players'),("'observe'",'observe')):
            self.setUp()
            self.lua.execute('''
              X4LAN_CONFIG.native_gate_view_points=%s
              local gate=package.loadlib('gate')();modes={};frames={}
              gate.view_points=function(mode,...) assert(select('#',...)==0);modes[#modes+1]=mode;return 1 end
              gate.view_points_frame=function(tick) frames[#frames+1]=tick;return 1 end
              gate.view_points_status=function() return 'mode=%s armed=1 installed=1 frame='..state.issued..' source=player viewpoints=1 failures=0' end
              local load=LoadGame;LoadGame=function(slot) assert(#modes==1 and #frames==0);load(slot) end
            ''' % (value,mode))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(list(self.lua.globals().modes.values()),[mode]);self.assertEqual(self.lua.globals().loads,1)
            self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
            self.lua.execute(SOURCE);self.lua.execute('update();state.issued=2;update()')
            self.assertEqual(list(self.lua.globals().modes.values()),[mode,mode]) # identical rerun after the UI reload
            self.assertEqual(list(self.lua.globals().frames.values()),[1,2])
            self.lua.execute('%sstate.active=0;state.reason=1;update()' % STOP_FIELDS)
            lines=list(self.lua.globals().logs.values())
            self.assertTrue(any('VIEW_POINTS_ARMED mode='+mode in line for line in lines))
            self.assertTrue(any('VIEW_POINTS tick=2 mode='+mode+' armed=1 installed=1 frame=2 source=player' in line for line in lines))
            self.assertTrue(any('VIEW_POINTS_STOP mode='+mode in line for line in lines))
            self.assertFalse(any('ERROR' in line for line in lines))

    def test_gravidar_digest_arms_and_logs_every_admitted_tick(self):
        self.lua.execute("""
          X4LAN_CONFIG.native_gate_gravidar_digest=true
          local gate=package.loadlib('gate')();armed=0
          gate.gravidar_digest=function(...) assert(select('#',...)==0);armed=armed+1;return 1 end
          gate.gravidar_digest_frame=function() return 'tick='..state.issued..' units=3 unmapped=0 stage=ok' end
        """)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().armed,1);self.assertEqual(self.lua.globals().loads,1)
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update();state.issued=2;update()')
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('GRAVIDAR_DIGEST_ARMED' in line for line in lines))
        self.assertTrue(any('GRAVIDAR_DIGEST tick=2 tick=2 units=3' in line for line in lines))
        self.assertFalse(any('ERROR' in line for line in lines))
        self.setUp()
        self.lua.execute("X4LAN_CONFIG.native_gate_gravidar_digest=1;local gate=package.loadlib('gate')();"
                         "gate.gravidar_digest=function() return 1 end;gate.gravidar_digest_frame=function() return 'x' end")
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,0)
        self.assertTrue(any('Gravidar digest refused' in line for line in self.lua.globals().logs.values()))

    def test_gravidar_digest_rows_arm_with_the_tick_count_and_drain_after_each_digest(self):
        self.lua.execute("""
          X4LAN_CONFIG.native_gate_gravidar_digest=true
          X4LAN_CONFIG.native_gate_gravidar_digest_rows=8
          local gate=package.loadlib('gate')();armed={}
          gate.gravidar_digest=function(...) assert(select('#',...)==1);armed[#armed+1]=select(1,...);return 1 end
          gate.gravidar_digest_frame=function() return 'tick='..state.issued..' units=3 unmapped=0 stage=ok' end
          gate.gravidar_digest_row=function(index) if index<state.issued*2 then return 'index='..index..' tick='..state.issued..' kind=a' end end
        """)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(list(self.lua.globals().armed.values()),[8]);self.assertEqual(self.lua.globals().loads,1)
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update();state.issued=2;update()')
        self.assertEqual(list(self.lua.globals().armed.values()),[8,8]) # identical rerun after the UI reload
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('GRAVIDAR_DIGEST_ARMED rows=8' in line for line in lines))
        rows=[line.split('GRAVIDAR_DIGEST_ROW ')[1] for line in lines if 'GRAVIDAR_DIGEST_ROW ' in line]
        self.assertEqual(rows,['index=0 tick=1 kind=a','index=1 tick=1 kind=a','index=2 tick=2 kind=a','index=3 tick=2 kind=a'])
        digest=next(i for i,line in enumerate(lines) if 'GRAVIDAR_DIGEST tick=2 tick=2' in line)
        self.assertLess(digest,next(i for i,line in enumerate(lines) if 'GRAVIDAR_DIGEST_ROW index=2 ' in line))
        self.assertFalse(any('ERROR' in line for line in lines))

    def test_gravidar_digest_rows_need_the_digest_a_tick_count_and_the_row_reader(self):
        for digest,rows,reader,native,refusal in (
                ('nil','8',True,1,'Gravidar digest rows need native_gate_gravidar_digest'),
                ('true','0',True,1,'Gravidar digest rows refused'),('true','1.5',True,1,'Gravidar digest rows refused'),
                ('true',"'8'",True,1,'Gravidar digest rows refused'),('true','1000001',True,1,'Gravidar digest rows refused'),
                ('true','8',False,1,'Gravidar digest rows refused'),('true','8',True,0,'Gravidar digest refused')):
            self.setUp()
            self.lua.execute('''
              X4LAN_CONFIG.native_gate_gravidar_digest=%s
              X4LAN_CONFIG.native_gate_gravidar_digest_rows=%s
              local gate=package.loadlib('gate')()
              gate.gravidar_digest=function() return %d end
              gate.gravidar_digest_frame=function() return 'x' end
              if %s then gate.gravidar_digest_row=function() end end
            ''' % (digest,rows,native,'true' if reader else 'false'))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            case=(digest,rows,reader,native)
            self.assertEqual(self.lua.globals().loads,0,case)
            self.assertTrue(any(refusal in line for line in self.lua.globals().logs.values()),case)

    def test_view_triggers_arm_after_view_points(self):
        for mode in ('block','observe'):
            self.setUp()
            self.lua.execute("""
              X4LAN_CONFIG.native_gate_view_points='players'
              X4LAN_CONFIG.native_gate_view_triggers='%s'
              local gate=package.loadlib('gate')();order={}
              gate.view_points=function(mode) order[#order+1]='view';return 1 end
              gate.view_points_frame=function() return 1 end
              gate.view_points_status=function() return 'mode=players' end
              gate.view_points_triggers=function(m,...) assert(select('#',...)==0);order[#order+1]='triggers:'..m;return 1 end
            """ % mode)
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(list(self.lua.globals().order.values()),['view','triggers:'+mode]);self.assertEqual(self.lua.globals().loads,1)
            self.assertTrue(any('VIEW_TRIGGERS_ARMED mode='+mode in line for line in self.lua.globals().logs.values()))
        for config,why in (("X4LAN_CONFIG.native_gate_view_triggers='block'",'view triggers need native_gate_view_points'),
                           ("X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_view_triggers='drop'",'view triggers refused')):
            self.setUp()
            self.lua.execute(config+"""
              local gate=package.loadlib('gate')()
              gate.view_points=function() return 1 end
              gate.view_points_frame=function() return 1 end
              gate.view_points_status=function() return 'mode=players' end
              gate.view_points_triggers=function() return 1 end
            """)
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any(why in line for line in self.lua.globals().logs.values()),why)

    def test_view_points_source_arms_after_view_points(self):
        for src in ('ship','player'):
            self.setUp()
            self.lua.execute("""
              X4LAN_CONFIG.native_gate_view_points='players'
              X4LAN_CONFIG.native_gate_view_points_source='%s'
              local gate=package.loadlib('gate')();order={}
              gate.view_points=function(mode) order[#order+1]='view';return 1 end
              gate.view_points_frame=function() return 1 end
              gate.view_points_status=function() return 'mode=players' end
              gate.view_points_source=function(m,...) assert(select('#',...)==0);order[#order+1]='source:'..m;return 1 end
            """ % src)
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(list(self.lua.globals().order.values()),['view','source:'+src]);self.assertEqual(self.lua.globals().loads,1)
            self.assertTrue(any('VIEW_POINTS_SOURCE_ARMED source='+src in line for line in self.lua.globals().logs.values()))
        for config,why in (("X4LAN_CONFIG.native_gate_view_points_source='ship'",'view points source needs native_gate_view_points'),
                           ("X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_view_points_source='camera'",'view points source refused'),
                           ("X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_view_points_source='ship';native=0",'view points source refused')):
            self.setUp()
            self.lua.execute(config+"""
              local gate=package.loadlib('gate')()
              gate.view_points=function() return 1 end
              gate.view_points_frame=function() return 1 end
              gate.view_points_status=function() return 'mode=players' end
              gate.view_points_source=function() return native or 1 end
            """)
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any(why in line for line in self.lua.globals().logs.values()),why)

    def test_region_camera_arms_after_the_synced_region_pass_and_updates_every_tick(self):
        self.lua.execute("""
          X4LAN_CONFIG.native_gate_view_points='players'
          X4LAN_CONFIG.native_gate_region_pass='sync'
          X4LAN_CONFIG.native_gate_region_camera='view'
          local gate=package.loadlib('gate')();order={}
          gate.view_points=function(mode) order[#order+1]='view';return 1 end
          gate.view_points_frame=function(t) order[#order+1]='latch:'..t;return 1 end
          gate.view_points_status=function() return 'mode=players' end
          gate.region_pass=function(mode) order[#order+1]='region:'..mode;return 1 end
          gate.region_pass_status=function() return 'mode=sync' end
          gate.region_camera=function(m,...) assert(select('#',...)==0);order[#order+1]='camera:'..m;return 1 end
          gate.region_camera_frame=function(t) order[#order+1]='camera_frame:'..t;return 1 end
          gate.region_camera_status=function() return 'mode=view viewpoint=5ca73' end
        """)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,1)
        order=list(self.lua.globals().order.values())
        self.assertEqual(order[:3],['view','region:sync','camera:view'])
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('REGION_CAMERA_ARMED mode=view' in line for line in lines))
        for config,why in (("X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_region_pass='observe';X4LAN_CONFIG.native_gate_region_camera='view'",'region camera refused'),
                           ("X4LAN_CONFIG.native_gate_region_pass='sync';X4LAN_CONFIG.native_gate_region_camera='view'",'region camera refused'),
                           ("X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_region_pass='sync';X4LAN_CONFIG.native_gate_region_camera=true",'region camera refused'),
                           ("X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_region_pass='sync';X4LAN_CONFIG.native_gate_region_camera='view';native=0",'region camera refused')):
            self.setUp()
            self.lua.execute(config+"""
              local gate=package.loadlib('gate')()
              gate.view_points=function() return 1 end
              gate.view_points_frame=function() return 1 end
              gate.view_points_status=function() return 'mode=players' end
              gate.region_pass=function() return 1 end
              gate.region_pass_status=function() return 'mode=sync' end
              gate.region_camera=function() return native or 1 end
              gate.region_camera_frame=function() return 1 end
              gate.region_camera_status=function() return 'stage=needs' end
            """)
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any(why in line for line in self.lua.globals().logs.values()),why)

    def test_region_fields_and_traffic_camera_arm_their_groups_and_share_the_frame(self):
        self.lua.execute("""
          X4LAN_CONFIG.native_gate_view_points='players'
          X4LAN_CONFIG.native_gate_mass_traffic='sync'
          X4LAN_CONFIG.native_gate_region_fields='view'
          X4LAN_CONFIG.native_gate_mass_traffic_camera='view'
          local gate=package.loadlib('gate')();order={}
          gate.view_points=function(mode) order[#order+1]='view';return 1 end
          gate.view_points_frame=function(t) return 1 end
          gate.view_points_status=function() return 'mode=players' end
          gate.mass_traffic=function(mode) order[#order+1]='traffic:'..mode;return 1 end
          gate.mass_traffic_status=function() return 'mode=sync' end
          gate.region_camera=function(m,...) assert(select('#',...)==0);order[#order+1]='group:'..m;return 1 end
          gate.region_camera_frame=function(t) order[#order+1]='frame';return 1 end
          gate.region_camera_status=function() return 'mode=view groups=fields,traffic' end
        """)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,1)
        order=list(self.lua.globals().order.values())
        self.assertEqual(order[:4],['view','traffic:sync','group:fields','group:traffic'])
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('REGION_FIELDS_ARMED mode=view' in line for line in lines))
        self.assertTrue(any('MASS_TRAFFIC_CAMERA_ARMED mode=view' in line for line in lines))
        for config,why in (("X4LAN_CONFIG.native_gate_region_fields='view'",'region fields refused'),
                           ("X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_region_fields='view'",'region fields refused'),
                           ("X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_mass_traffic='sync';X4LAN_CONFIG.native_gate_region_fields=true",'region fields refused'),
                           ("X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_mass_traffic_camera='view'",'mass traffic camera refused'),
                           ("X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_mass_traffic='sync';X4LAN_CONFIG.native_gate_mass_traffic_camera='view';native=0",'mass traffic camera refused')):
            self.setUp()
            self.lua.execute(config+"""
              local gate=package.loadlib('gate')()
              gate.view_points=function() return 1 end
              gate.view_points_frame=function() return 1 end
              gate.view_points_status=function() return 'mode=players' end
              gate.mass_traffic=function() return 1 end
              gate.mass_traffic_status=function() return 'mode=sync' end
              gate.region_camera=function() return native or 1 end
              gate.region_camera_frame=function() return 1 end
              gate.region_camera_status=function() return 'stage=needs' end
            """)
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any(why in line for line in self.lua.globals().logs.values()),why)

    def test_effect_camera_arms_its_group_and_drives_the_frame(self):
        self.lua.execute("""
          X4LAN_CONFIG.native_gate_view_points='players'
          X4LAN_CONFIG.native_gate_region_pass='sync'
          X4LAN_CONFIG.native_gate_effect_camera='view'
          local gate=package.loadlib('gate')();order={}
          gate.view_points=function(mode) order[#order+1]='view';return 1 end
          gate.view_points_frame=function(t) return 1 end
          gate.view_points_status=function() return 'mode=players' end
          gate.region_pass=function(mode) order[#order+1]='region:'..mode;return 1 end
          gate.region_pass_status=function() return 'mode=sync' end
          gate.region_camera=function(m,...) assert(select('#',...)==0);order[#order+1]='group:'..m;return 1 end
          gate.region_camera_frame=function(t) order[#order+1]='frame';return 1 end
          gate.region_camera_status=function() return 'mode=view groups=effects' end
        """)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,1)
        order=list(self.lua.globals().order.values())
        self.assertEqual(order[:3],['view','region:sync','group:effects'])
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('EFFECT_CAMERA_ARMED mode=view' in line for line in lines))
        for config,why in (("X4LAN_CONFIG.native_gate_effect_camera='view'",'effect camera refused'),
                           ("X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_effect_camera='view'",'effect camera refused'),
                           ("X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_region_pass='sync';X4LAN_CONFIG.native_gate_effect_camera='view';native=0",'effect camera refused')):
            self.setUp()
            self.lua.execute(config+"""
              local gate=package.loadlib('gate')()
              gate.view_points=function() return 1 end
              gate.view_points_frame=function() return 1 end
              gate.view_points_status=function() return 'mode=players' end
              gate.region_pass=function() return 1 end
              gate.region_pass_status=function() return 'mode=sync' end
              gate.region_camera=function() return native or 1 end
              gate.region_camera_frame=function() return 1 end
              gate.region_camera_status=function() return 'stage=needs' end
            """)
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any(why in line for line in self.lua.globals().logs.values()),why)

    def test_highway_camera_arms_its_group_and_drives_the_frame(self):
        self.lua.execute("""
          X4LAN_CONFIG.native_gate_view_points='players'
          X4LAN_CONFIG.native_gate_mass_traffic='sync'
          X4LAN_CONFIG.native_gate_highway_camera='view'
          local gate=package.loadlib('gate')();order={}
          gate.view_points=function(mode) order[#order+1]='view';return 1 end
          gate.view_points_frame=function(t) return 1 end
          gate.view_points_status=function() return 'mode=players' end
          gate.mass_traffic=function(mode) order[#order+1]='traffic:'..mode;return 1 end
          gate.mass_traffic_status=function() return 'mode=sync' end
          gate.region_camera=function(m,...) assert(select('#',...)==0);order[#order+1]='group:'..m;return 1 end
          gate.region_camera_frame=function(t) order[#order+1]='frame:'..t;return 1 end
          gate.region_camera_status=function() return 'mode=view groups=highway' end
        """)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,1)
        order=list(self.lua.globals().order.values())
        self.assertIn('group:highway',order)
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('HIGHWAY_CAMERA_ARMED mode=view' in line for line in lines))
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update();state.issued=2;update()')
        order=list(self.lua.globals().order.values())
        self.assertTrue(any(o.startswith('frame:') for o in order))
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('REGION_CAMERA tick=' in line and 'groups=highway' in line for line in lines))
        for config in ("X4LAN_CONFIG.native_gate_highway_camera='view'",
                       "X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_highway_camera='view'",
                       "X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_mass_traffic='sync';X4LAN_CONFIG.native_gate_highway_camera=true",
                       "X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_mass_traffic='sync';X4LAN_CONFIG.native_gate_highway_camera='view';native=0"):
            self.setUp()
            self.lua.execute(config+"""
              local gate=package.loadlib('gate')()
              gate.view_points=function() return 1 end
              gate.view_points_frame=function() return 1 end
              gate.view_points_status=function() return 'mode=players' end
              gate.mass_traffic=function() return 1 end
              gate.mass_traffic_status=function() return 'mode=sync' end
              gate.region_camera=function() return native or 1 end
              gate.region_camera_frame=function() return 1 end
              gate.region_camera_status=function() return 'stage=needs' end
            """)
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any('highway camera refused' in line for line in self.lua.globals().logs.values()),config)

    def test_physics_scenes_arm_their_group_and_drive_the_frame(self):
        self.lua.execute("""
          X4LAN_CONFIG.native_gate_view_points='players'
          X4LAN_CONFIG.native_gate_region_pass='sync'
          X4LAN_CONFIG.native_gate_physics_scenes='viewpoints'
          local gate=package.loadlib('gate')();order={}
          gate.view_points=function(mode) order[#order+1]='view';return 1 end
          gate.view_points_frame=function(t) return 1 end
          gate.view_points_status=function() return 'mode=players' end
          gate.region_pass=function(mode) order[#order+1]='region:'..mode;return 1 end
          gate.region_pass_status=function() return 'mode=sync' end
          gate.region_camera=function(m,...) assert(select('#',...)==0);order[#order+1]='group:'..m;return 1 end
          gate.region_camera_frame=function(t) order[#order+1]='frame:'..t;return 1 end
          gate.region_camera_status=function() return 'mode=view groups=scenes key2=0000000000000000' end
        """)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,1)
        self.assertIn('group:scenes',list(self.lua.globals().order.values()))
        self.assertTrue(any('PHYSICS_SCENES_ARMED mode=viewpoints' in line for line in self.lua.globals().logs.values()))
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update();state.issued=2;update()')
        self.assertTrue(any(o.startswith('frame:') for o in self.lua.globals().order.values()))
        for config in ("X4LAN_CONFIG.native_gate_physics_scenes='viewpoints'",
                       "X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_physics_scenes='viewpoints'",
                       "X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_region_pass='sync';X4LAN_CONFIG.native_gate_physics_scenes='view'",
                       "X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_region_pass='sync';X4LAN_CONFIG.native_gate_physics_scenes='viewpoints';native=0"):
            self.setUp()
            self.lua.execute(config+"""
              local gate=package.loadlib('gate')()
              gate.view_points=function() return 1 end
              gate.view_points_frame=function() return 1 end
              gate.view_points_status=function() return 'mode=players' end
              gate.region_pass=function() return 1 end
              gate.region_pass_status=function() return 'mode=sync' end
              gate.region_camera=function() return native or 1 end
              gate.region_camera_frame=function() return 1 end
              gate.region_camera_status=function() return 'stage=needs' end
            """)
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any('physics scenes refused' in line for line in self.lua.globals().logs.values()),config)

    def test_mining_xs_and_probe_arm_their_groups_and_drive_the_frame(self):
        for key,value,group,armed in (('native_gate_mining_camera',"'view'",'mining','MINING_CAMERA_ARMED mode=view'),
                                      ('native_gate_xs_camera',"'view'",'xs_detail','XS_CAMERA_ARMED mode=view'),
                                      ('native_gate_camera_probe','true','camera_probe','CAMERA_PROBE_ARMED mode=true')):
            self.setUp()
            self.lua.execute("""
              X4LAN_CONFIG.native_gate_view_points='players'
              X4LAN_CONFIG.native_gate_region_pass='sync'
              X4LAN_CONFIG.%s=%s
              local gate=package.loadlib('gate')();order={}
              gate.view_points=function(mode) return 1 end
              gate.view_points_frame=function(t) return 1 end
              gate.view_points_status=function() return 'mode=players' end
              gate.region_pass=function(mode) return 1 end
              gate.region_pass_status=function() return 'mode=sync' end
              gate.region_camera=function(m,...) assert(select('#',...)==0);order[#order+1]='group:'..m;return 1 end
              gate.region_camera_frame=function(t) order[#order+1]='frame:'..t;return 1 end
              gate.region_camera_status=function() return 'mode=view groups=x' end
            """ % (key,value))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,1,key)
            self.assertIn('group:'+group,list(self.lua.globals().order.values()))
            self.assertTrue(any(armed in line for line in self.lua.globals().logs.values()),key)
            self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
            self.lua.execute(SOURCE);self.lua.execute('update();state.issued=2;update()')
            self.assertTrue(any(o.startswith('frame:') for o in self.lua.globals().order.values()),key)
            for config in ("X4LAN_CONFIG.%s=%s" % (key,value),
                           "X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.%s=%s" % (key,value),
                           "X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_region_pass='sync';X4LAN_CONFIG.%s=%s" %
                               (key,"'sector'" if value=="'view'" else "'view'"),
                           "X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_region_pass='sync';X4LAN_CONFIG.%s=%s;native=0" % (key,value)):
                self.setUp()
                self.lua.execute(config+"""
                  local gate=package.loadlib('gate')()
                  gate.view_points=function() return 1 end
                  gate.view_points_frame=function() return 1 end
                  gate.view_points_status=function() return 'mode=players' end
                  gate.region_pass=function() return 1 end
                  gate.region_pass_status=function() return 'mode=sync' end
                  gate.region_camera=function() return native or 1 end
                  gate.region_camera_frame=function() return 1 end
                  gate.region_camera_status=function() return 'mode=off' end
                """)
                self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
                self.assertEqual(self.lua.globals().loads,0,config)
                self.assertTrue(any(' refused' in line for line in self.lua.globals().logs.values()),config)

    def test_shape_refresh_arms_its_group_and_drives_the_frame(self):
        self.lua.execute("""
          X4LAN_CONFIG.native_gate_view_points='players'
          X4LAN_CONFIG.native_gate_region_pass='sync'
          X4LAN_CONFIG.native_gate_shape_refresh='view'
          local gate=package.loadlib('gate')();order={}
          gate.view_points=function(mode) order[#order+1]='view';return 1 end
          gate.view_points_frame=function(t) return 1 end
          gate.view_points_status=function() return 'mode=players' end
          gate.region_pass=function(mode) order[#order+1]='pass:'..mode;return 1 end
          gate.region_pass_status=function() return 'mode=sync' end
          gate.region_camera=function(m,...) assert(select('#',...)==0);order[#order+1]='group:'..m;return 1 end
          gate.region_camera_frame=function(t) order[#order+1]='frame:'..t;return 1 end
          gate.region_camera_status=function() return 'mode=view groups=shape_refresh shape_refresh=0/0' end
        """)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,1)
        self.assertIn('group:shape_refresh',list(self.lua.globals().order.values()))
        self.assertTrue(any('SHAPE_REFRESH_ARMED mode=view' in line for line in self.lua.globals().logs.values()))
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update();state.issued=2;update()')
        self.assertTrue(any(o.startswith('frame:') for o in self.lua.globals().order.values()))
        self.assertTrue(any('REGION_CAMERA tick=' in line for line in self.lua.globals().logs.values()))
        for config in ("X4LAN_CONFIG.native_gate_shape_refresh='view'",
                       "X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_shape_refresh='view'",
                       "X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_region_pass='sync';X4LAN_CONFIG.native_gate_shape_refresh=true",
                       "X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_region_pass='sync';X4LAN_CONFIG.native_gate_shape_refresh='view';native=0"):
            self.setUp()
            self.lua.execute(config+"""
              local gate=package.loadlib('gate')()
              gate.view_points=function() return 1 end
              gate.view_points_frame=function() return 1 end
              gate.view_points_status=function() return 'mode=players' end
              gate.region_pass=function() return 1 end
              gate.region_pass_status=function() return 'mode=sync' end
              gate.region_camera=function() return native or 1 end
              gate.region_camera_frame=function() return 1 end
              gate.region_camera_status=function() return 'mode=off' end
            """)
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0,config)
            self.assertTrue(any('shape refresh refused' in line for line in self.lua.globals().logs.values()),config)

    def test_camera_context_arms_its_group_and_drives_the_frame(self):
        self.lua.execute("""
          X4LAN_CONFIG.native_gate_view_points='players'
          X4LAN_CONFIG.native_gate_mass_traffic='sync'
          X4LAN_CONFIG.native_gate_camera_context='view'
          local gate=package.loadlib('gate')();order={}
          gate.view_points=function(mode) order[#order+1]='view';return 1 end
          gate.view_points_frame=function(t) return 1 end
          gate.view_points_status=function() return 'mode=players' end
          gate.mass_traffic=function(mode) order[#order+1]='traffic:'..mode;return 1 end
          gate.mass_traffic_status=function() return 'mode=sync' end
          gate.region_camera=function(m,...) assert(select('#',...)==0);order[#order+1]='group:'..m;return 1 end
          gate.region_camera_frame=function(t) order[#order+1]='frame:'..t;return 1 end
          gate.region_camera_status=function() return 'mode=view groups=context context_calls=0' end
        """)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,1)
        self.assertIn('group:context',list(self.lua.globals().order.values()))
        self.assertTrue(any('CAMERA_CONTEXT_ARMED mode=view' in line for line in self.lua.globals().logs.values()))
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update();state.issued=2;update()')
        self.assertTrue(any(o.startswith('frame:') for o in self.lua.globals().order.values()))
        self.assertTrue(any('REGION_CAMERA tick=' in line for line in self.lua.globals().logs.values()))
        for config in ("X4LAN_CONFIG.native_gate_camera_context='view'",
                       "X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_camera_context='view'",
                       "X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_mass_traffic='sync';X4LAN_CONFIG.native_gate_camera_context=true",
                       "X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_mass_traffic='sync';X4LAN_CONFIG.native_gate_camera_context='view';native=0"):
            self.setUp()
            self.lua.execute(config+"""
              local gate=package.loadlib('gate')()
              gate.view_points=function() return 1 end
              gate.view_points_frame=function() return 1 end
              gate.view_points_status=function() return 'mode=players' end
              gate.mass_traffic=function() return 1 end
              gate.mass_traffic_status=function() return 'mode=sync' end
              gate.region_camera=function() return native or 1 end
              gate.region_camera_frame=function() return 1 end
              gate.region_camera_status=function() return 'stage=needs' end
            """)
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any('camera context refused' in line for line in self.lua.globals().logs.values()),config)

    def test_viewpoint_policy_arms_after_its_groups(self):
        self.lua.execute("""
          X4LAN_CONFIG.native_gate_view_points='players'
          X4LAN_CONFIG.native_gate_region_pass='sync'
          X4LAN_CONFIG.native_gate_highway_camera='view'
          X4LAN_CONFIG.native_gate_viewpoint_policy='sector'
          local gate=package.loadlib('gate')();order={}
          gate.view_points=function(mode) order[#order+1]='view';return 1 end
          gate.view_points_frame=function(t) return 1 end
          gate.view_points_status=function() return 'mode=players' end
          gate.region_pass=function(mode) order[#order+1]='region:'..mode;return 1 end
          gate.region_pass_status=function() return 'mode=sync' end
          gate.region_camera=function(m,...) assert(select('#',...)==0);order[#order+1]='group:'..m;return 1 end
          gate.region_camera_frame=function(t) order[#order+1]='frame:'..t;return 1 end
          gate.region_camera_status=function() return 'mode=view groups=highway,policy policy=sector picks=0/0 alternations=0' end
        """)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,1)
        order=list(self.lua.globals().order.values())
        self.assertIn('group:policy',order)
        self.assertLess(order.index('group:highway'),order.index('group:policy'))
        self.assertTrue(any('VIEWPOINT_POLICY_ARMED mode=sector' in line for line in self.lua.globals().logs.values()))
        for config in ("X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_region_pass='sync';X4LAN_CONFIG.native_gate_viewpoint_policy='sector'",
                       "X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_region_pass='sync';X4LAN_CONFIG.native_gate_highway_camera='view';X4LAN_CONFIG.native_gate_viewpoint_policy='two'",
                       "X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_region_pass='sync';X4LAN_CONFIG.native_gate_highway_camera='view';X4LAN_CONFIG.native_gate_viewpoint_policy='sector';native=0"):
            self.setUp()
            self.lua.execute(config+"""
              local gate=package.loadlib('gate')()
              gate.view_points=function() return 1 end
              gate.view_points_frame=function() return 1 end
              gate.view_points_status=function() return 'mode=players' end
              gate.region_pass=function() return 1 end
              gate.region_pass_status=function() return 'mode=sync' end
              gate.region_camera=function(m) if m=='policy' then return native or 1 end return 1 end
              gate.region_camera_frame=function() return 1 end
              gate.region_camera_status=function() return 'stage=needs' end
            """)
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any('viewpoint policy refused' in line for line in self.lua.globals().logs.values()),config)

    def test_viewpoint_policy_rows_arm_and_drain_every_tick(self):
        self.lua.execute("""
          X4LAN_CONFIG.native_gate_view_points='players'
          X4LAN_CONFIG.native_gate_region_pass='sync'
          X4LAN_CONFIG.native_gate_highway_camera='view'
          X4LAN_CONFIG.native_gate_viewpoint_policy='sector'
          X4LAN_CONFIG.native_gate_viewpoint_policy_rows=120
          local gate=package.loadlib('gate')();armed={}
          gate.view_points=function(mode) return 1 end
          gate.view_points_frame=function(t) return 1 end
          gate.view_points_status=function() return 'mode=players' end
          gate.region_pass=function(mode) return 1 end
          gate.region_pass_status=function() return 'mode=sync' end
          gate.region_camera=function(m) return 1 end
          gate.region_camera_frame=function(t) return 1 end
          gate.region_camera_status=function() return 'mode=view policy=sector' end
          gate.region_camera_policy_rows=function(t,...) assert(select('#',...)==0);armed[#armed+1]=t;return 1 end
          local rows={'index=0 tick=1 site=7 two=1 key=0000000000000000 runtime=777 mover=1','index=1 tick=1 site=102 two=0 key=0000000000000000 runtime=888 mover=0'}
          gate.region_camera_policy_row=function(i) return rows[i+1] end
        """)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,1)
        self.assertEqual(list(self.lua.globals().armed.values()),[120])
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update();state.issued=2;update()')
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('VIEWPOINT_POLICY_ROWS_ARMED ticks=120' in line for line in lines))
        self.assertEqual(sum('POLICY_ROW index=' in line for line in lines),2)
        # A status stride holds tick 70's lines; the rows are written anyway.
        self.lua.execute('''X4LAN_CONFIG.native_gate_status_stride=60
          local gate=package.loadlib('gate')();local before=gate.region_camera_policy_row
          gate.region_camera_policy_row=function(i)
            if i==2 then return 'index=2 tick=70 site=10a two=0 key=0000000000000000 runtime=777 mover=1 a=1 b=1 pend=0' end
            return before(i)
          end''')
        self.lua.execute(SOURCE);self.lua.execute('state.issued=70;update()')
        self.assertTrue(any('POLICY_ROW index=2 tick=70 site=10a' in line for line in self.lua.globals().logs.values()))
        for config in ("X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_region_pass='sync';X4LAN_CONFIG.native_gate_viewpoint_policy_rows=10",
                       "X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_region_pass='sync';X4LAN_CONFIG.native_gate_highway_camera='view';X4LAN_CONFIG.native_gate_viewpoint_policy='sector';X4LAN_CONFIG.native_gate_viewpoint_policy_rows=601",
                       "X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_region_pass='sync';X4LAN_CONFIG.native_gate_highway_camera='view';X4LAN_CONFIG.native_gate_viewpoint_policy='sector';X4LAN_CONFIG.native_gate_viewpoint_policy_rows=true"):
            self.setUp()
            self.lua.execute(config+"""
              local gate=package.loadlib('gate')()
              gate.view_points=function() return 1 end
              gate.view_points_frame=function() return 1 end
              gate.view_points_status=function() return 'mode=players' end
              gate.region_pass=function() return 1 end
              gate.region_pass_status=function() return 'mode=sync' end
              gate.region_camera=function() return 1 end
              gate.region_camera_frame=function() return 1 end
              gate.region_camera_status=function() return 'stage=needs' end
              gate.region_camera_policy_rows=function() return 1 end
              gate.region_camera_policy_row=function() return nil end
            """)
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any('viewpoint policy rows refused' in line for line in self.lua.globals().logs.values()),config)

    def test_runtime_ids_config_validation_refuses_bad_values(self):
        for config,refused in (("X4LAN_CONFIG.native_gate_runtime_ids=true",'runtime ids refused'),
                               ("X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_runtime_ids=0",'runtime ids refused'),
                               ("X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_runtime_ids=524289",'runtime ids refused'),
                               ("X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_runtime_ids='set'",'runtime ids refused'),
                               ("X4LAN_CONFIG.native_gate_runtime_ids_barrier=true",'runtime ids barrier refused')):
            self.assertIn(refused,self._runtime_ids_probe(config),config)

    def test_runtime_ids_arm_right_before_the_load(self):
        for value,base in (('true',65536),("'observe'",0),('70000',70000)):
            lines=self._runtime_ids_probe("X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_runtime_ids="+value)
            self.assertIn('ORDER runtime_ids '+str(base)+' load_requested',lines,value)
            self.assertIn('RUNTIME_IDS_ARMED mode=x',lines,value)

    def test_runtime_ids_local_arms_after_the_set_and_before_the_load(self):
        ids="X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_created_identities=true;"
        lines=self._runtime_ids_probe(ids+"X4LAN_CONFIG.native_gate_runtime_ids=true;X4LAN_CONFIG.native_gate_runtime_ids_local=true")
        self.assertIn('ORDER runtime_ids 65536 runtime_ids_local load_requested',lines)
        self.assertIn('RUNTIME_IDS_LOCAL_ARMED',lines)
        for config in ("X4LAN_CONFIG.native_gate_runtime_ids_local=true",
                       ids+"X4LAN_CONFIG.native_gate_runtime_ids='observe';X4LAN_CONFIG.native_gate_runtime_ids_local=true",
                       "X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_runtime_ids=true;X4LAN_CONFIG.native_gate_runtime_ids_local=true",
                       ids+"X4LAN_CONFIG.native_gate_runtime_ids=true;X4LAN_CONFIG.native_gate_runtime_ids_local=1"):
            lines=self._runtime_ids_probe(config)
            self.assertIn('runtime ids local refused',lines,config)
            self.assertNotIn('load_requested',lines,config)

    def test_runtime_ids_caller_stride_is_checked_and_written_at_once(self):
        for value in ("0","1.5","'x'","3601"):
            self.setUp();self.lua.execute("X4LAN_CONFIG.native_gate_runtime_ids=65536;X4LAN_CONFIG.native_gate_runtime_ids_caller_stride="+value)
            with self.assertRaises(Exception) as caught: self.lua.execute(SOURCE)
            self.assertIn('native_gate_runtime_ids_caller_stride',str(caught.exception))
        self.setUp();self.lua.execute("X4LAN_CONFIG.native_gate_runtime_ids_caller_stride=5")
        with self.assertRaises(Exception) as caught: self.lua.execute(SOURCE)
        self.assertIn('native_gate_runtime_ids_caller_stride',str(caught.exception))
        # On a tick the status stride holds, log() would drop the row: it goes to the log directly.
        text=SOURCE.replace(chr(13),'')
        self.assertIn("if every then ticklog.write('RUNTIME_IDS_CALLER tick='..tick..' '..row) else log('RUNTIME_IDS_CALLER tick='..tick..' '..row) end",text)
        self.assertIn('if tick%(every or 300)==0',text)

    def test_runtime_ids_probe_mark_is_around_the_frame_and_caller_rows_reach_the_table(self):
        start=SOURCE.index(" local paused=gate and gate.pilot_present_pause")
        block=SOURCE[start:SOURCE.index(" if paused==1 then gate.pilot_present_resume() end",start)]
        self.assertLess(block.index('probe_mark(1)'),block.index('pcall(frame)'))
        self.assertLess(block.index('pcall(frame)'),block.index('probe_mark(0)'))
        self.assertIn('for i=0,2047 do\n    local row=gate.runtime_ids_caller_row(i)',SOURCE.replace(chr(13),''))

    def _runtime_ids_probe(self,config):
        # The arming block of frame(), extracted: the snapshot boot's start-menu path it sits in needs the whole game.
        block=SOURCE[SOURCE.index("  -- Runtime IDs: the same slots on every client"):SOURCE.index("  log('LOAD_REQUEST snapshot=")]
        lua=LuaRuntime(unpack_returned_tuples=True)
        lua.execute("""
          X4LAN_CONFIG={};order={};lines={}
          local function log(text) lines[#lines+1]=text end
          gate={runtime_ids=function(b) order[#order+1]='runtime_ids '..b;return 1 end,runtime_ids_status=function() return 'mode=x' end,
                runtime_ids_local=function() order[#order+1]='runtime_ids_local';return 1 end,runtime_ids_probe=function() end,
                runtime_ids_word=function() return 'a' end,runtime_ids_map_row=function() return 'b' end,runtime_ids_caller_row=function() return nil end,
                load_requested=function() order[#order+1]='load_requested';return 1 end}
          function run_block(text)
            local cfg=X4LAN_CONFIG
            local fn=assert(loadstring('local cfg,gate,log=...;'..text))
            return pcall(fn,cfg,gate,log)
          end
        """)
        lua.execute(config)
        result=lua.globals().run_block(block)
        ok,message=result if isinstance(result,tuple) else (result,None)
        return 'ORDER '+' '.join(lua.globals().order.values())+' | '+' | '.join(lua.globals().lines.values())+' | '+str(message)

    def test_highway_speed_arms_with_view_points_a_pass_join_and_identities(self):
        setup = """
          local gate=package.loadlib('gate')();groups={}
          gate.view_points=function() return 1 end
          gate.view_points_frame=function() return 1 end
          gate.view_points_status=function() return 'mode=players' end
          gate.region_pass=function() return 1 end
          gate.region_pass_status=function() return 'mode=sync' end
          gate.region_camera=function(m) groups[#groups+1]=m;return 1 end
          gate.region_camera_frame=function() return 1 end
          gate.region_camera_status=function() return 'mode=view highway_speed=0/0' end
        """
        base="X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_region_pass='sync';"
        for extra,refused in (("X4LAN_CONFIG.native_gate_highway_speed='view';X4LAN_CONFIG.native_gate_world_identities=true",'highway speed refused'),
                              ("X4LAN_CONFIG.native_gate_highway_speed='key'",'highway speed needs native_gate_world_identities')):
            self.setUp()
            self.lua.execute(base+extra+setup)
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any(refused in line for line in self.lua.globals().logs.values()),extra)
        self.setUp()
        self.lua.execute(base+"X4LAN_CONFIG.native_gate_highway_speed='key';X4LAN_CONFIG.native_gate_world_identities=true"+setup)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('HIGHWAY_SPEED_ARMED mode=key' in line for line in lines),lines[-8:])
        self.assertIn('highway_speed',list(self.lua.globals().groups.values()))

    def test_uncovered_view_points_are_logged_when_the_count_changes(self):
        text=SOURCE.replace(chr(13),'')
        start=text.index("  if gate.region_camera_uncovered then")
        block=text[start:text.index("   end\n  end\n",start)+len("   end\n  end\n")]
        lua=LuaRuntime(unpack_returned_tuples=True)
        lua.execute("""
          lines={};counts={2,2,0,0,1}
          gate={region_camera_uncovered=function() return table.remove(counts,1) end}
          function run_block(text,tick)
            local fn=assert(loadstring('local gate,log,row_cursors,tick,cfg=...;'..text))
            fn(gate,function(t) lines[#lines+1]=t end,cursors,tick,{})
          end
          cursors={}
        """)
        for tick in range(1,6):
            lua.globals().run_block(block,tick)
        lines=list(lua.globals().lines.values())
        self.assertEqual([line.split(' no physics')[0] for line in lines],['VIEW_POINTS_UNCOVERED tick=1 count=2','VIEW_POINTS_UNCOVERED tick=3 count=0','VIEW_POINTS_UNCOVERED tick=5 count=1'])
        self.assertIn('no physics scene',lines[0])
        self.assertNotIn('no physics scene',lines[1])

    def test_view_scenes_arm_after_the_scenes_group(self):
        setup = """
          local gate=package.loadlib('gate')();groups={}
          gate.view_points=function() return 1 end
          gate.view_points_frame=function() return 1 end
          gate.view_points_status=function() return 'mode=players' end
          gate.region_pass=function() return 1 end
          gate.region_pass_status=function() return 'mode=sync' end
          gate.region_camera=function(m) groups[#groups+1]=m;return 1 end
          gate.region_camera_frame=function() return 1 end
          gate.region_camera_status=function() return 'mode=view view_scenes=1' end
        """
        base="X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_region_pass='sync';"
        self.lua.execute(base+"X4LAN_CONFIG.native_gate_view_scenes=true"+setup)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,0)
        self.assertTrue(any('view scenes need native_gate_physics_scenes' in line for line in self.lua.globals().logs.values()))
        self.setUp()
        self.lua.execute(base+"X4LAN_CONFIG.native_gate_physics_scenes='viewpoints';X4LAN_CONFIG.native_gate_view_scenes=true"+setup)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('VIEW_SCENES_ARMED mode=true' in line for line in lines),lines[-8:])
        groups=list(self.lua.globals().groups.values())
        self.assertLess(groups.index('scenes'),groups.index('view_scenes'))

    def test_field_fill_arms_after_the_fields_group(self):
        setup = """
          local gate=package.loadlib('gate')();groups={}
          gate.view_points=function() return 1 end
          gate.view_points_frame=function() return 1 end
          gate.view_points_status=function() return 'mode=players' end
          gate.region_pass=function() return 1 end
          gate.region_pass_status=function() return 'mode=sync' end
          gate.region_camera=function(m) groups[#groups+1]=m;return 1 end
          gate.region_camera_frame=function() return 1 end
          gate.region_camera_status=function() return 'mode=view field_fill=view' end
        """
        base="X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_region_pass='sync';"
        fields="X4LAN_CONFIG.native_gate_region_fields='view';"
        self.lua.execute(base+"X4LAN_CONFIG.native_gate_field_fill='view'"+setup)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,0)
        self.assertTrue(any('field fill refused' in line for line in self.lua.globals().logs.values()))
        self.setUp()
        self.lua.execute(base+fields+"X4LAN_CONFIG.native_gate_field_fill='everywhere'"+setup)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,0)
        for mode,group in (('view','field_fill'),('block','field_fill_block')):
            self.setUp()
            self.lua.execute(base+fields+"X4LAN_CONFIG.native_gate_field_fill='%s'" % mode+setup)
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            lines=list(self.lua.globals().logs.values())
            self.assertTrue(any('FIELD_FILL_ARMED mode='+mode in line for line in lines),lines[-8:])
            groups=list(self.lua.globals().groups.values())
            self.assertLess(groups.index('fields'),groups.index(group))

    def test_world_hash_shields_arm_with_the_world_hash(self):
        setup = '''
          local gate=package.loadlib('gate')();shield_modes={}
          gate.world_capture=function() return 'objects=1 state=WORLD_IMPORTED_1:1:0:a:b:c:d' end
          gate.world_banks=function() return 'read=0 write=1' end
          gate.world_page=function() return 0 end
          gate.world_detail=function() return 0 end
          gate.world_hash_shields=function(mode,...) assert(select('#',...)==0);shield_modes[#shield_modes+1]=mode;return native or 1 end
          gate.world_hash_shields_status=function() return 'mode=canonical canonical=3' end
        '''
        for config in ("X4LAN_CONFIG.native_gate_world_hash_shields='canonical'",
                       "X4LAN_CONFIG.native_gate_world_hash_stride=300;X4LAN_CONFIG.native_gate_world_hash_shields='raw'",
                       "X4LAN_CONFIG.native_gate_world_hash_stride=300;X4LAN_CONFIG.native_gate_world_hash_shields='canonical';native=0"):
            self.setUp();self.lua.execute(config+setup);self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any('world hash shields refused' in line for line in self.lua.globals().logs.values()),config)
        self.setUp()
        self.lua.execute("X4LAN_CONFIG.native_gate_world_hash_stride=300;X4LAN_CONFIG.native_gate_world_hash_shields='canonical'"+setup)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,1)
        self.assertEqual(list(self.lua.globals().shield_modes.values()),['canonical'])
        self.assertTrue(any('WORLD_HASH_SHIELDS_ARMED mode=canonical' in line for line in self.lua.globals().logs.values()))

    def test_speak_timer_arms_before_loading_and_logs_every_admitted_tick(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_speak_timer=true
          local gate=package.loadlib('gate')();arms=0
          gate.speak_timer=function(...) assert(select('#',...)==0);arms=arms+1;return 1 end
          gate.speak_timer_status=function() return 'installed=1 site=9090 restored=0 stage=armed failures=0' end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().arms,1);self.assertEqual(self.lua.globals().loads,1)
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false;update()')
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('SPEAK_TIMER_ARMED' in line for line in lines))
        self.assertTrue(any('SPEAK_TIMER tick=1 installed=1 site=9090' in line for line in lines))
        for config in ("X4LAN_CONFIG.native_gate_speak_timer='audio'","X4LAN_CONFIG.native_gate_speak_timer=true;native=0"):
            self.setUp()
            self.lua.execute(config+'''
              local gate=package.loadlib('gate')()
              gate.speak_timer=function() return native or 1 end
              gate.speak_timer_status=function() return 'stage=pins' end
            ''')
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any('speak timer refused' in line for line in self.lua.globals().logs.values()))

    def test_viewpoint_standins_arm_after_the_policy(self):
        setup = """
          local gate=package.loadlib('gate')();standins={}
          gate.view_points=function() return 1 end
          gate.view_points_frame=function() return 1 end
          gate.view_points_status=function() return 'mode=players' end
          gate.region_pass=function() return 1 end
          gate.region_pass_status=function() return 'mode=sync' end
          gate.region_camera=function(m) return 1 end
          gate.region_camera_frame=function() return 1 end
          gate.region_camera_status=function() return 'mode=view standins=3/3' end
          gate.region_camera_standins=function(n,...) assert(select('#',...)==0);standins[#standins+1]=n;return 1 end
        """
        base=("X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_region_pass='sync';"
              "X4LAN_CONFIG.native_gate_highway_camera='view';")
        for config in ("X4LAN_CONFIG.native_gate_viewpoint_standins=3",
                       "X4LAN_CONFIG.native_gate_viewpoint_policy='sector';X4LAN_CONFIG.native_gate_viewpoint_standins=5"):
            self.setUp();self.lua.execute(base+config+setup);self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any('viewpoint standins refused' in line for line in self.lua.globals().logs.values()))
        self.setUp()
        self.lua.execute(base+"X4LAN_CONFIG.native_gate_viewpoint_policy='sector';X4LAN_CONFIG.native_gate_viewpoint_standins=3"+setup)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(list(self.lua.globals().standins.values()),[3])
        self.assertTrue(any('VIEWPOINT_STANDINS_ARMED n=3' in line for line in self.lua.globals().logs.values()))

    def test_traffic_rows_arm_with_view_points_and_a_pass_join(self):
        setup = """
          local gate=package.loadlib('gate')();groups={}
          gate.view_points=function() return 1 end
          gate.view_points_frame=function() return 1 end
          gate.view_points_status=function() return 'mode=players' end
          gate.region_pass=function() return 1 end
          gate.region_pass_status=function() return 'mode=sync' end
          gate.region_camera=function(m) groups[#groups+1]=m;return 1 end
          gate.region_camera_frame=function() return 1 end
          gate.region_camera_status=function() return 'mode=view traffic_rows=early:0' end
        """
        base="X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_region_pass='sync';"
        self.lua.execute(base+"X4LAN_CONFIG.native_gate_traffic_rows='yes'"+setup)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,0)
        self.setUp()
        self.lua.execute(base+"X4LAN_CONFIG.native_gate_traffic_rows=true"+setup)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('TRAFFIC_ROWS_ARMED mode=true' in line for line in lines),lines[-8:])
        self.assertIn('traffic_rows',list(self.lua.globals().groups.values()))
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false;update()')
        self.assertTrue(any('REGION_CAMERA tick=1 mode=view traffic_rows=early:0' in line for line in self.lua.globals().logs.values()))

    def test_traffic_views_arm_with_view_points_and_a_pass_join(self):
        setup = """
          local gate=package.loadlib('gate')();groups={}
          gate.view_points=function() return 1 end
          gate.view_points_frame=function() return 1 end
          gate.view_points_status=function() return 'mode=players' end
          gate.region_pass=function() return 1 end
          gate.region_pass_status=function() return 'mode=sync' end
          gate.region_camera=function(m) groups[#groups+1]=m;return 1 end
          gate.region_camera_frame=function() return 1 end
          gate.region_camera_status=function() return 'mode=view groups=traffic_views' end
        """
        base="X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_region_pass='sync';"
        self.lua.execute(base+"X4LAN_CONFIG.native_gate_traffic_views='all'"+setup)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,0)
        self.assertTrue(any('traffic views refused' in line for line in self.lua.globals().logs.values()))
        self.setUp()
        self.lua.execute(base+"X4LAN_CONFIG.native_gate_traffic_views='block'"+setup)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('TRAFFIC_VIEWS_ARMED mode=block' in line for line in lines),lines[-8:])
        self.assertIn('traffic_views',list(self.lua.globals().groups.values()))
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false;update()')
        self.assertTrue(any('REGION_CAMERA tick=1 mode=view groups=traffic_views' in line for line in self.lua.globals().logs.values()))
        for mode,rows,expected in (('observe','',['traffic_rows','traffic_pass_observe']),('view','',['traffic_views','traffic_rows','traffic_pass_view']),
                                   ('observe','X4LAN_CONFIG.native_gate_traffic_rows=true;',['traffic_rows','traffic_pass_observe'])):
            self.setUp()
            self.lua.execute(base+rows+"X4LAN_CONFIG.native_gate_traffic_views='%s'" % mode+setup)
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertTrue(any('TRAFFIC_VIEWS_ARMED mode='+mode in line for line in self.lua.globals().logs.values()))
            self.assertEqual(list(self.lua.globals().groups.values()),expected)

    def test_sound_rng_arms_with_view_points_and_a_pass_join(self):
        setup = """
          local gate=package.loadlib('gate')();groups={}
          gate.view_points=function() return 1 end
          gate.view_points_frame=function() return 1 end
          gate.view_points_status=function() return 'mode=players' end
          gate.region_pass=function() return 1 end
          gate.region_pass_status=function() return 'mode=sync' end
          gate.region_camera=function(m) groups[#groups+1]=m;return 1 end
          gate.region_camera_frame=function() return 1 end
          gate.region_camera_status=function() return 'mode=view sound=0/0' end
        """
        base="X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_region_pass='sync';"
        self.lua.execute(base+"X4LAN_CONFIG.native_gate_sound_rng='key'"+setup)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,0)
        self.assertTrue(any('sound rng refused' in line for line in self.lua.globals().logs.values()))
        self.setUp()
        self.lua.execute(base+"X4LAN_CONFIG.native_gate_sound_rng='split'"+setup)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('SOUND_RNG_ARMED mode=split' in line for line in lines),lines[-8:])
        self.assertIn('sound_rng',list(self.lua.globals().groups.values()))

    def test_highway_rows_arm_with_window_and_drain_every_tick(self):
        self.lua.execute("""
          X4LAN_CONFIG.native_gate_view_points='players'
          X4LAN_CONFIG.native_gate_region_pass='sync'
          X4LAN_CONFIG.native_gate_highway_camera='view'
          X4LAN_CONFIG.native_gate_highway_rows=600
          X4LAN_CONFIG.native_gate_highway_rows_from=1090
          local gate=package.loadlib('gate')();armed={};groups={};frames=0
          gate.view_points=function(mode) return 1 end
          gate.view_points_frame=function(t) return 1 end
          gate.view_points_status=function() return 'mode=players' end
          gate.region_pass=function(mode) return 1 end
          gate.region_pass_status=function() return 'mode=sync' end
          gate.region_camera=function(m) groups[#groups+1]=m;return 1 end
          gate.region_camera_frame=function(t) frames=frames+1;return 1 end
          gate.region_camera_status=function() return 'mode=view highway_rows=0/0/0,rows:0' end
          gate.region_camera_highway_rows=function(t,f,...) assert(select('#',...)==0);armed[#armed+1]=t..'/'..f;return 1 end
          local rows={'index=0 tick=1 site=frame key=000000000000072e attention=8 mover=0 before=3e800000 v=3f000000,0,0,0',
                      'index=1 tick=1 site=offset key=000000000000072a attention=7 mover=0 before=3e800000 v=42c80000,0,0,0'}
          gate.region_camera_highway_row=function(i) return rows[i+1] end
        """)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,1)
        self.assertEqual(list(self.lua.globals().armed.values()),['600/1090'])
        self.assertIn('highway_rows',list(self.lua.globals().groups.values()))
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update();state.issued=2;update()')
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('HIGHWAY_ROWS_ARMED ticks=600 from=1090' in line for line in lines))
        self.assertEqual(sum('HIGHWAY_PARAM_ROW index=' in line for line in lines),2)
        self.assertGreater(self.lua.globals().frames,0)
        # Without the from tick the window starts at tick 1; without a pass join, out of range or not a number: refused.
        self.setUp()
        self.lua.execute("""
          X4LAN_CONFIG.native_gate_view_points='players'
          X4LAN_CONFIG.native_gate_region_pass='sync'
          X4LAN_CONFIG.native_gate_highway_rows=5
          local gate=package.loadlib('gate')();armed={}
          gate.view_points=function() return 1 end
          gate.view_points_frame=function() return 1 end
          gate.view_points_status=function() return 'mode=players' end
          gate.region_pass=function() return 1 end
          gate.region_pass_status=function() return 'mode=sync' end
          gate.region_camera=function() return 1 end
          gate.region_camera_frame=function() return 1 end
          gate.region_camera_status=function() return 'mode=view' end
          gate.region_camera_highway_rows=function(t,f) armed[#armed+1]=t..'/'..f;return 1 end
          gate.region_camera_highway_row=function() return nil end
        """)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,1)
        self.assertEqual(list(self.lua.globals().armed.values()),['5/1'])
        for config in ("X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_highway_rows=10",
                       "X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_region_pass='sync';X4LAN_CONFIG.native_gate_highway_rows=3601",
                       "X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_region_pass='sync';X4LAN_CONFIG.native_gate_highway_rows=true",
                       "X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_region_pass='sync';X4LAN_CONFIG.native_gate_highway_rows=10;X4LAN_CONFIG.native_gate_highway_rows_from=0",
                       "X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_region_pass='sync';X4LAN_CONFIG.native_gate_highway_rows_from=10"):
            self.setUp()
            self.lua.execute(config+"""
              local gate=package.loadlib('gate')()
              gate.view_points=function() return 1 end
              gate.view_points_frame=function() return 1 end
              gate.view_points_status=function() return 'mode=players' end
              gate.region_pass=function() return 1 end
              gate.region_pass_status=function() return 'mode=sync' end
              gate.region_camera=function() return 1 end
              gate.region_camera_frame=function() return 1 end
              gate.region_camera_status=function() return 'stage=needs' end
              gate.region_camera_highway_rows=function() return 1 end
              gate.region_camera_highway_row=function() return nil end
            """)
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any('highway rows' in line for line in self.lua.globals().logs.values()),config)

    def test_effect_rows_arm_and_drain_every_tick(self):
        self.lua.execute("""
          X4LAN_CONFIG.native_gate_view_points='players'
          X4LAN_CONFIG.native_gate_region_pass='sync'
          X4LAN_CONFIG.native_gate_effect_rows=true
          local gate=package.loadlib('gate')();order={}
          gate.view_points=function(mode) return 1 end
          gate.view_points_frame=function(t) return 1 end
          gate.view_points_status=function() return 'mode=players' end
          gate.region_pass=function(mode) return 1 end
          gate.region_pass_status=function() return 'mode=sync' end
          gate.region_camera=function(m) order[#order+1]='group:'..m;return 1 end
          gate.region_camera_frame=function(t) return 1 end
          gate.region_camera_status=function() return 'mode=view' end
          local rows={'index=0 tick=1 key=0 deferred=1','index=1 tick=1 key=0 deferred=0'}
          gate.region_camera_effect_row=function(i) return rows[i+1] end
        """)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,1)
        self.assertIn('group:effect_rows',list(self.lua.globals().order.values()))
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('EFFECT_ROWS_ARMED' in line for line in lines))
        for config,why in (("X4LAN_CONFIG.native_gate_effect_rows=true",'effect rows refused'),
                           ("X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_region_pass='sync';X4LAN_CONFIG.native_gate_effect_rows='yes'",'effect rows refused')):
            self.setUp()
            self.lua.execute(config+"""
              local gate=package.loadlib('gate')()
              gate.view_points=function() return 1 end
              gate.view_points_frame=function() return 1 end
              gate.view_points_status=function() return 'mode=players' end
              gate.region_pass=function() return 1 end
              gate.region_pass_status=function() return 'mode=sync' end
              gate.region_camera=function() return 1 end
              gate.region_camera_frame=function() return 1 end
              gate.region_camera_status=function() return 'stage=needs' end
              gate.region_camera_effect_row=function() return nil end
            """)
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any(why in line for line in self.lua.globals().logs.values()),why)

    def test_weapon_fire_rng_arms_and_logs_every_tick(self):
        self.lua.execute("""
          X4LAN_CONFIG.native_gate_world_identities=true
          X4LAN_CONFIG.native_gate_weapon_fire_rng=true
          local gate=package.loadlib('gate')();order={}
          gate.weapon_fire_rng=function(...) assert(select('#',...)==0);order[#order+1]='arm';return 1 end
          gate.weapon_fire_rng_status=function() order[#order+1]='status';return 'installed=1 keyed=2' end
        """)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,1)
        self.assertEqual(list(self.lua.globals().order.values())[0],'arm')
        self.assertTrue(any('WEAPON_FIRE_RNG_ARMED' in line for line in self.lua.globals().logs.values()))
        for config,why in (("X4LAN_CONFIG.native_gate_weapon_fire_rng=true",'weapon fire rng refused'),
                           ("X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_weapon_fire_rng=1",'weapon fire rng refused'),
                           ("X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_weapon_fire_rng=true;native=0",'weapon fire rng refused')):
            self.setUp()
            self.lua.execute(config+"""
              local gate=package.loadlib('gate')()
              gate.weapon_fire_rng=function() return native or 1 end
              gate.weapon_fire_rng_status=function() return 'stage=pins' end
            """)
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any(why in line for line in self.lua.globals().logs.values()),why)

    def test_engine_refresh_rng_arms_with_the_world_identities_and_logs_at_stop(self):
        self.lua.execute("""
          X4LAN_CONFIG.native_gate_world_identities=true
          X4LAN_CONFIG.native_gate_engine_refresh_rng=true
          local gate=package.loadlib('gate')();order={}
          gate.engine_refresh_rng=function(...) assert(select('#',...)==0);order[#order+1]='arm';return 1 end
          gate.engine_refresh_rng_status=function() order[#order+1]='status';return 'installed=1 keyed=2' end
        """)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,1)
        self.assertEqual(list(self.lua.globals().order.values())[0],'arm')
        self.assertTrue(any('ENGINE_REFRESH_RNG_ARMED' in line for line in self.lua.globals().logs.values()))
        for config in ("X4LAN_CONFIG.native_gate_engine_refresh_rng=true",
                       "X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_engine_refresh_rng=1",
                       "X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_engine_refresh_rng=true;native=0"):
            self.setUp()
            self.lua.execute(config+"""
              local gate=package.loadlib('gate')()
              gate.engine_refresh_rng=function() return native or 1 end
              gate.engine_refresh_rng_status=function() return 'stage=pins' end
            """)
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any('engine refresh rng refused' in line for line in self.lua.globals().logs.values()),config)

    def test_portable_math_arms_before_the_load_and_logs_at_stop(self):
        self.lua.execute("""
          X4LAN_CONFIG.native_gate_portable_math=true
          local gate=package.loadlib('gate')();order={}
          gate.portable_math=function(...) assert(select('#',...)==0);order[#order+1]='arm';return 1 end
          gate.portable_math_status=function() order[#order+1]='status';return 'installed=1 sites=29' end
        """)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,1)
        self.assertEqual(list(self.lua.globals().order.values())[0],'arm')
        self.assertTrue(any('PORTABLE_MATH_ARMED installed=1 sites=29' in line for line in self.lua.globals().logs.values()))
        for config in ("X4LAN_CONFIG.native_gate_portable_math=1",
                       "X4LAN_CONFIG.native_gate_portable_math='true'",
                       "X4LAN_CONFIG.native_gate_portable_math=true;native=0"):
            self.setUp()
            self.lua.execute(config+"""
              local gate=package.loadlib('gate')()
              gate.portable_math=function() return native or 1 end
              gate.portable_math_status=function() return 'stage=pins' end
            """)
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any('portable math refused stage=pins' in line for line in self.lua.globals().logs.values()),config)

    def test_exact_reciprocal_arms_before_the_load_and_logs_at_stop(self):
        self.lua.execute("""
          X4LAN_CONFIG.native_gate_exact_reciprocal=true
          local gate=package.loadlib('gate')();order={}
          gate.exact_reciprocal=function(...) assert(select('#',...)==0);order[#order+1]='arm';return 1 end
          gate.exact_reciprocal_status=function() order[#order+1]='status';return 'installed=1 windows=819' end
        """)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,1)
        self.assertEqual(list(self.lua.globals().order.values())[0],'arm')
        self.assertTrue(any('EXACT_RECIPROCAL_ARMED installed=1 windows=819' in line for line in self.lua.globals().logs.values()))
        for config in ("X4LAN_CONFIG.native_gate_exact_reciprocal=1",
                       "X4LAN_CONFIG.native_gate_exact_reciprocal=true;native=0"):
            self.setUp()
            self.lua.execute(config+"""
              local gate=package.loadlib('gate')()
              gate.exact_reciprocal=function() return native or 1 end
              gate.exact_reciprocal_status=function() return 'stage=pins' end
            """)
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any('exact reciprocal refused stage=pins' in line for line in self.lua.globals().logs.values()),config)

    def test_cost_cuts_arm_only_when_asked(self):
        # Without the keys the gate never hears of them.
        self.lua.execute("""
          local gate=package.loadlib('gate')()
          gate.fast_reads=function() error('not configured') end
          gate.path_budget=function() error('not configured') end
          gate.pass_stride=function() error('not configured') end
        """)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,1)
        self.assertFalse(any('PATH_BUDGET' in line or 'FAST_READS' in line or 'STRIDE' in line for line in self.lua.globals().logs.values()))
        # fast reads: armed before the gate starts, a refusal never starts it
        self.setUp()
        self.lua.execute("""
          X4LAN_CONFIG.native_gate_fast_reads=true
          local gate=package.loadlib('gate')();gate.fast_reads=function() return 1 end
        """)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,1)
        self.assertTrue(any('FAST_READS_ARMED' in line for line in self.lua.globals().logs.values()))
        self.setUp()
        self.lua.execute("""
          X4LAN_CONFIG.native_gate_fast_reads=true
          local gate=package.loadlib('gate')();gate.fast_reads=function() return 0 end
        """)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,0)
        self.assertTrue(any('REFUSED fast_reads' in line for line in self.lua.globals().logs.values()))

    def test_tick_rate_is_set_before_the_gate_starts(self):
        self.lua.execute("""
          X4LAN_CONFIG.native_gate_tick_hz=30
          local gate=package.loadlib('gate')();order={}
          local boot=gate.boot
          gate.tick_rate=function(hz,pace) order[#order+1]='rate:'..hz..':'..pace;return 1 end
          gate.boot=function(...) order[#order+1]='boot';return boot(...) end
        """)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,1)
        self.assertEqual(list(self.lua.globals().order.values())[:2],['rate:30:1','boot'])
        self.assertTrue(any('TICK_RATE_ARMED hz=30 pace=true' in line for line in self.lua.globals().logs.values()))
        # pace off, and a refused rate never starts the gate
        self.setUp()
        self.lua.execute("""
          X4LAN_CONFIG.native_gate_tick_hz=30;X4LAN_CONFIG.native_gate_tick_pace=false
          local gate=package.loadlib('gate')();order={}
          gate.tick_rate=function(hz,pace) order[#order+1]='rate:'..hz..':'..pace;return 1 end
        """)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(list(self.lua.globals().order.values())[0],'rate:30:0')
        self.setUp()
        self.lua.execute("""
          X4LAN_CONFIG.native_gate_tick_hz=25
          local gate=package.loadlib('gate')();gate.tick_rate=function() return 0 end
        """)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,0)
        self.assertTrue(any('REFUSED tick_hz=25' in line for line in self.lua.globals().logs.values()))
        # no key: the gate never hears of it (old gates keep working)
        self.setUp()
        self.lua.execute("""
          local gate=package.loadlib('gate')();gate.tick_rate=function() error('not configured') end
        """)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,1)

    def test_fp_env_tick_is_not_tied_to_the_ui_thread(self):
        # The gate hook runs on the main thread and the UI on the MoveWorker: a tick gated on
        # ui_thread never ran live (FP_ENV_STOP samples=0 after 6003 ticks, play22-soak).
        gate = (Path(__file__).resolve().parents[1]/'src/native/engine/lockstep_gate.c').read_text()
        self.assertIn('if(fpenv_installed && !fpenv_tick(stats.issued))', gate)
        self.assertNotIn('ui_thread && !fpenv_tick', gate)

    def test_fp_env_arms_with_its_mode_and_logs_at_stop(self):
        self.lua.execute("""
          X4LAN_CONFIG.native_gate_fp_env='default'
          local gate=package.loadlib('gate')();order={}
          gate.fp_env=function(mode,...) assert(select('#',...)==0);order[#order+1]='arm:'..mode;return 1 end
          gate.fp_env_status=function() order[#order+1]='status';return 'installed=1 mode=default' end
        """)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,1)
        self.assertEqual(list(self.lua.globals().order.values())[0],'arm:default')
        self.assertTrue(any('FP_ENV_ARMED installed=1 mode=default' in line for line in self.lua.globals().logs.values()))
        for config in ("X4LAN_CONFIG.native_gate_fp_env=true",
                       "X4LAN_CONFIG.native_gate_fp_env='ftz'",
                       "X4LAN_CONFIG.native_gate_fp_env='observe';native=0"):
            self.setUp()
            self.lua.execute(config+"""
              local gate=package.loadlib('gate')()
              gate.fp_env=function() return native or 1 end
              gate.fp_env_status=function() return 'stage=pins' end
            """)
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any('fp env refused stage=pins' in line for line in self.lua.globals().logs.values()),config)

    def test_decimal_arms_before_the_load_and_logs_at_stop(self):
        self.lua.execute("""
          X4LAN_CONFIG.native_gate_decimal=true
          local gate=package.loadlib('gate')();order={}
          gate.decimal=function(...) assert(select('#',...)==0);order[#order+1]='arm';return 1 end
          gate.decimal_status=function() order[#order+1]='status';return 'installed=1 readers=2' end
        """)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,1)
        self.assertEqual(list(self.lua.globals().order.values())[0],'arm')
        self.assertTrue(any('DECIMAL_ARMED installed=1 readers=2' in line for line in self.lua.globals().logs.values()))
        for config in ("X4LAN_CONFIG.native_gate_decimal='on'",
                       "X4LAN_CONFIG.native_gate_decimal=true;native=0"):
            self.setUp()
            self.lua.execute(config+"""
              local gate=package.loadlib('gate')()
              gate.decimal=function() return native or 1 end
              gate.decimal_status=function() return 'stage=pins' end
            """)
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any('decimal refused stage=pins' in line for line in self.lua.globals().logs.values()),config)

    def test_motion_watch_arms_each_id_and_drains_rows(self):
        self.lua.execute("""
          X4LAN_CONFIG.native_gate_motion_watch={'00000000000a214c','000000000006dd87'}
          local gate=package.loadlib('gate')();order={}
          gate.motion_watch=function(id,...) assert(select('#',...)==0);order[#order+1]='watch:'..id;return 1 end
          gate.motion_watch_frame=function(t) order[#order+1]='frame';return 1 end
          local rows={'index=0 tick=1 saved=00000000000a214c attention=5'}
          gate.motion_watch_row=function(i) return rows[i+1] end
          gate.motion_watch_status=function() return 'watched=2' end
        """)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,1)
        order=list(self.lua.globals().order.values())
        self.assertEqual(order[:2],['watch:00000000000a214c','watch:000000000006dd87'])
        self.assertTrue(any('MOTION_WATCH_ARMED watched=2' in line for line in self.lua.globals().logs.values()))
        for config,why in (("X4LAN_CONFIG.native_gate_motion_watch=true",'motion watch refused'),
                           ("X4LAN_CONFIG.native_gate_motion_watch={'a214c'}",'motion watch refused'),
                           ("X4LAN_CONFIG.native_gate_motion_watch={'00000000000a214c'};native=0",'motion watch refused')):
            self.setUp()
            self.lua.execute(config+"""
              local gate=package.loadlib('gate')()
              gate.motion_watch=function() return native or 1 end
              gate.motion_watch_frame=function() return 1 end
              gate.motion_watch_row=function() return nil end
              gate.motion_watch_status=function() return 'watched=0' end
            """)
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any(why in line for line in self.lua.globals().logs.values()),why)

    def test_motion_watch_orders_arm_after_the_watch(self):
        self.lua.execute("""
          X4LAN_CONFIG.native_gate_motion_watch={'00000000000a214c'}
          X4LAN_CONFIG.native_gate_motion_watch_orders=true
          local gate=package.loadlib('gate')();order={}
          gate.motion_watch=function(id) order[#order+1]='watch';return 1 end
          gate.motion_watch_frame=function(t) return 1 end
          gate.motion_watch_row=function() return nil end
          gate.motion_watch_status=function() return 'watched=1' end
          gate.motion_watch_orders=function(...) assert(select('#',...)==0);order[#order+1]='orders';return 1 end
          local rows={'index=0 tick=193 saved=00000000000a214c site=006e0962'}
          gate.motion_watch_order_row=function(i) return rows[i+1] end
          local targets={'index=0 tick=194 saved=00000000000a214c model=02c4f610'}
          gate.motion_watch_target_row=function(i) return targets[i+1] end
          local events={'index=0 tick=574 saved=0000000000078855 event=start_travel'}
          gate.motion_watch_event_row=function(i) return events[i+1] end
          local refreshes={'index=0 tick=574 saved=0000000000078855 site=00ea1d9a'}
          gate.motion_watch_refresh_row=function(i) return refreshes[i+1] end
        """)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,1)
        self.assertEqual(list(self.lua.globals().order.values())[:2],['watch','orders'])
        logs=list(self.lua.globals().logs.values())
        self.assertTrue(any('MOTION_ORDERS_ARMED' in line for line in logs))
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update();state.issued=2;update()')
        logs=list(self.lua.globals().logs.values())
        self.assertEqual(sum('MOTION_ORDER index=0 tick=193 saved=00000000000a214c site=006e0962' in line for line in logs),1)
        self.assertEqual(sum('MOTION_TARGET index=0 tick=194 saved=00000000000a214c model=02c4f610' in line for line in logs),1)
        self.assertEqual(sum('MOTION_EVENT index=0 tick=574 saved=0000000000078855 event=start_travel' in line for line in logs),1)
        self.assertEqual(sum('MOTION_REFRESH index=0 tick=574 saved=0000000000078855 site=00ea1d9a' in line for line in logs),1)
        for config,why in (("X4LAN_CONFIG.native_gate_motion_watch_orders=true",'motion watch orders need native_gate_motion_watch'),
                           ("X4LAN_CONFIG.native_gate_motion_watch={'00000000000a214c'};X4LAN_CONFIG.native_gate_motion_watch_orders=true;native=0",'motion watch orders refused')):
            self.setUp()
            self.lua.execute(config+"""
              local gate=package.loadlib('gate')()
              gate.motion_watch=function() return 1 end
              gate.motion_watch_frame=function() return 1 end
              gate.motion_watch_row=function() return nil end
              gate.motion_watch_status=function() return 'watched=1' end
              gate.motion_watch_orders=function() return native or 1 end
              gate.motion_watch_order_row=function() return nil end
              gate.motion_watch_target_row=function() return nil end
              gate.motion_watch_event_row=function() return nil end
              gate.motion_watch_refresh_row=function() return nil end
            """)
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any(why in line for line in self.lua.globals().logs.values()),why)

    def test_view_rows_arm_after_view_points_and_drain_every_tick(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_view_points='players'
          X4LAN_CONFIG.native_gate_view_rows=true
          local gate=package.loadlib('gate')();order={};rows={}
          gate.view_points=function(mode) order[#order+1]='view';return 1 end
          gate.view_points_frame=function() return 1 end
          gate.view_points_status=function() return 'mode=players frame='..state.issued end
          gate.view_points_rows=function(...) assert(select('#',...)==0);order[#order+1]='rows';return 1 end
          gate.view_points_row=function(i) if i<3 then return 'index='..i..' tick='..state.issued end end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(list(self.lua.globals().order.values()),['view','rows']);self.assertEqual(self.lua.globals().loads,1)
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update();state.issued=2;update()')
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('VIEW_ROWS_ARMED' in line for line in lines))
        self.assertEqual(sum('VIEW_POINTS_ROW index=' in line for line in lines),3) # drained once, cursor kept
        self.assertFalse(any('ERROR' in line for line in lines))
        for config,why in (("X4LAN_CONFIG.native_gate_view_rows=true",'view rows need native_gate_view_points'),
                           ("X4LAN_CONFIG.native_gate_view_points='players';X4LAN_CONFIG.native_gate_view_rows=1",'view rows refused')):
            self.setUp()
            self.lua.execute(config+'''
              local gate=package.loadlib('gate')()
              gate.view_points=function() return 1 end
              gate.view_points_frame=function() return 1 end
              gate.view_points_status=function() return 'mode=players' end
              gate.view_points_rows=function() return 1 end
            ''')
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any(why in line for line in self.lua.globals().logs.values()),why)

    def test_view_points_refuse_unknown_modes_native_refusal_and_a_failed_latch(self):
        for value,native,called in (("'camera'",1,False),("'PLAYERS'",1,False),('1',1,False),("'players'",0,True)):
            self.setUp()
            self.lua.execute('''
              X4LAN_CONFIG.native_gate_view_points=%s
              local gate=package.loadlib('gate')()
              gate.view_points=function(mode) view_calls=(view_calls or 0)+1;return %d end
              gate.view_points_frame=function() return 1 end
              gate.view_points_status=function() return 'mode=off' end
            ''' % (value,native))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any('view points refused' in line for line in self.lua.globals().logs.values()))
            self.assertEqual(self.lua.globals().view_calls,1 if called else None)
        self.setUp()
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_view_points='players'
          local gate=package.loadlib('gate')()
          gate.view_points=function() return 1 end
          gate.view_points_frame=function() return 0 end
          gate.view_points_status=function() return 'stage=frame_guard failures=1' end
          state.boot_phase=3;state.issued=1;is_menu=false
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update()')
        lines=list(self.lua.globals().logs.values())
        self.assertEqual(self.lua.globals().state.active,0);self.assertEqual(self.lua.globals().reports,0)
        self.assertTrue(any('ERROR' in line and 'view points frame failed stage=frame_guard' in line for line in lines))

    def test_nav_probe_init_arms_after_the_cadence_and_needs_it(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_nav_rng=true
          X4LAN_CONFIG.native_gate_nav_cadence='far'
          X4LAN_CONFIG.native_gate_nav_probe_init=true
          local gate=package.loadlib('gate')();order={}
          gate.nav_cadence=function(mode) order[#order+1]='cadence';return 1 end
          gate.nav_cadence_status=function() return 'mode=far' end
          gate.nav_probe_init=function(...) assert(select('#',...)==0);order[#order+1]='probe';return 1 end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(list(self.lua.globals().order.values()),['cadence','probe']);self.assertEqual(self.lua.globals().loads,1)
        self.assertTrue(any('NAV_PROBE_INIT_ARMED' in line for line in self.lua.globals().logs.values()))
        for config,why in (("X4LAN_CONFIG.native_gate_nav_probe_init=true",'needs native_gate_nav_cadence'),
                           ("X4LAN_CONFIG.native_gate_nav_rng=true;X4LAN_CONFIG.native_gate_nav_cadence='far';X4LAN_CONFIG.native_gate_nav_probe_init=1",'nav probe init refused'),
                           ("X4LAN_CONFIG.native_gate_nav_rng=true;X4LAN_CONFIG.native_gate_nav_cadence='far';X4LAN_CONFIG.native_gate_nav_probe_init=true;native=0",'nav probe init refused')):
            self.setUp()
            self.lua.execute(config+'''
              local gate=package.loadlib('gate')()
              gate.nav_cadence=function() return 1 end
              gate.nav_cadence_status=function() return 'mode=far' end
              gate.nav_probe_init=function() return native or 1 end
            ''')
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any(why in line for line in self.lua.globals().logs.values()),why)

    def test_command_order_arms_and_logs_status_and_rows_every_admitted_tick(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_serial_pathing=true
          X4LAN_CONFIG.native_gate_path_determinism=3
          X4LAN_CONFIG.native_gate_travel_snapshot=true
          X4LAN_CONFIG.native_gate_command_order='sort'
          X4LAN_CONFIG.native_gate_command_order_rows=true
          local gate=package.loadlib('gate')();armed={};rows={'index=0 tick=1 source=saved:0000000000092d05 n=2 reordered=1 seq=x'}
          gate.command_order_rows=function(...) assert(select('#',...)==0);armed[#armed+1]='rows';return 1 end
          gate.path_determinism=function() return 1 end
          gate.path_determinism_status=function() return 'armed=1 mask=3' end
          gate.travel_snapshot=function() return 1 end
          gate.travel_snapshot_status=function() return 'armed=1 installed=1 read=1 tick=1 failures=0' end
          gate.command_order=function(mode,...) assert(select('#',...)==0);armed[#armed+1]=mode;return 1 end
          gate.command_order_status=function() return 'mode=sort armed=1 frame=1 multi=1' end
          gate.command_order_row=function(i) return rows[i+1] end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(list(self.lua.globals().armed.values()),['sort','rows']);self.assertEqual(self.lua.globals().loads,1)
        self.assertTrue(any('COMMAND_ORDER_ARMED mode=sort' in line for line in self.lua.globals().logs.values()))
        self.assertTrue(any('COMMAND_ORDER_ROWS_ARMED' in line for line in self.lua.globals().logs.values()))
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false;update()')
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('COMMAND_ORDER tick=1 mode=sort armed=1 frame=1 multi=1' in line for line in lines))
        self.assertTrue(any('COMMAND_ORDER_ROW index=0 tick=1 source=saved:0000000000092d05' in line for line in lines))
        for config,why in (("X4LAN_CONFIG.native_gate_command_order='observe'",'command order refused'),
                           ("X4LAN_CONFIG.native_gate_serial_pathing=true;X4LAN_CONFIG.native_gate_command_order='sort'",'command order refused'),
                           ("X4LAN_CONFIG.native_gate_serial_pathing=true;X4LAN_CONFIG.native_gate_command_order='order'",'command order refused'),
                           ("X4LAN_CONFIG.native_gate_serial_pathing=true;X4LAN_CONFIG.native_gate_command_order='observe';native=0",'command order refused'),
                           ("X4LAN_CONFIG.native_gate_command_order_rows=true",'command order rows need native_gate_command_order'),
                           ("X4LAN_CONFIG.native_gate_serial_pathing=true;X4LAN_CONFIG.native_gate_command_order='observe';X4LAN_CONFIG.native_gate_command_order_rows=1",'command order rows refused')):
            self.setUp()
            self.lua.execute(config+'''
              local gate=package.loadlib('gate')()
              gate.command_order=function() return native or 1 end
              gate.command_order_status=function() return 'mode=observe' end
              gate.command_order_row=function() return nil end
              gate.command_order_rows=function() return 1 end
            ''')
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any(why in line for line in self.lua.globals().logs.values()),why)

    def test_nav_params_init_arms_after_the_probe_init_before_the_player(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_nav_rng=true
          X4LAN_CONFIG.native_gate_nav_cadence='far'
          X4LAN_CONFIG.native_gate_nav_probe_init=true
          X4LAN_CONFIG.native_gate_nav_params_init=true
          X4LAN_CONFIG.native_gate_nav_player=true
          local gate=package.loadlib('gate')();order={}
          gate.nav_cadence=function(mode) order[#order+1]='cadence';return 1 end
          gate.nav_cadence_status=function() return 'mode=far' end
          gate.nav_probe_init=function() order[#order+1]='probe';return 1 end
          gate.nav_params_init=function(...) assert(select('#',...)==0);order[#order+1]='params';return 1 end
          gate.nav_player=function() order[#order+1]='player';return 1 end
          gate.nav_player_status=function() return 'installed=1' end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(list(self.lua.globals().order.values()),['cadence','probe','params','player']);self.assertEqual(self.lua.globals().loads,1)
        self.assertTrue(any('NAV_PARAMS_INIT_ARMED' in line for line in self.lua.globals().logs.values()))
        for config,why in (("X4LAN_CONFIG.native_gate_nav_params_init=true",'nav params init needs native_gate_nav_cadence'),
                           ("X4LAN_CONFIG.native_gate_nav_rng=true;X4LAN_CONFIG.native_gate_nav_cadence='far';X4LAN_CONFIG.native_gate_nav_params_init=1",'nav params init refused'),
                           ("X4LAN_CONFIG.native_gate_nav_rng=true;X4LAN_CONFIG.native_gate_nav_cadence='far';X4LAN_CONFIG.native_gate_nav_params_init=true;native=0",'nav params init refused')):
            self.setUp()
            self.lua.execute(config+'''
              local gate=package.loadlib('gate')()
              gate.nav_cadence=function() return 1 end
              gate.nav_cadence_status=function() return 'mode=far' end
              gate.nav_params_init=function() return native or 1 end
            ''')
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any(why in line for line in self.lua.globals().logs.values()),why)

    def test_nav_player_arms_after_the_probe_init_and_logs_every_admitted_tick(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_nav_rng=true
          X4LAN_CONFIG.native_gate_nav_cadence='far'
          X4LAN_CONFIG.native_gate_nav_probe_init=true
          X4LAN_CONFIG.native_gate_nav_player=true
          local gate=package.loadlib('gate')();order={}
          gate.nav_cadence=function(mode) order[#order+1]='cadence';return 1 end
          gate.nav_cadence_status=function() return 'mode=far' end
          gate.nav_probe_init=function() order[#order+1]='probe';return 1 end
          gate.nav_player=function(...) assert(select('#',...)==0);order[#order+1]='player';return 1 end
          gate.nav_player_status=function() return 'installed=1 frame=1 rows=0' end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(list(self.lua.globals().order.values()),['cadence','probe','player']);self.assertEqual(self.lua.globals().loads,1)
        self.assertTrue(any('NAV_PLAYER_ARMED' in line for line in self.lua.globals().logs.values()))
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false;update()')
        self.assertTrue(any('NAV_PLAYER tick=1 installed=1 frame=1 rows=0' in line for line in self.lua.globals().logs.values()))
        for config,why in (("X4LAN_CONFIG.native_gate_nav_player=true",'nav player needs native_gate_nav_cadence'),
                           ("X4LAN_CONFIG.native_gate_nav_rng=true;X4LAN_CONFIG.native_gate_nav_cadence='far';X4LAN_CONFIG.native_gate_nav_player=1",'nav player refused'),
                           ("X4LAN_CONFIG.native_gate_nav_rng=true;X4LAN_CONFIG.native_gate_nav_cadence='far';X4LAN_CONFIG.native_gate_nav_player=true;X4LAN_CONFIG.native_gate_player_observer=true",'nav player refused'),
                           ("X4LAN_CONFIG.native_gate_nav_rng=true;X4LAN_CONFIG.native_gate_nav_cadence='far';X4LAN_CONFIG.native_gate_nav_player=true;native=0",'nav player refused')):
            self.setUp()
            self.lua.execute(config+'''
              local gate=package.loadlib('gate')()
              gate.nav_cadence=function() return 1 end
              gate.nav_cadence_status=function() return 'mode=far' end
              gate.nav_player=function() return native or 1 end
              gate.nav_player_status=function() return 'installed=1' end
            ''')
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any(why in line for line in self.lua.globals().logs.values()),why)

    def test_attention_all_arms_after_boot_and_logs_every_admitted_tick(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_attention_rng=true
          X4LAN_CONFIG.native_gate_attention_all=true
          local gate=package.loadlib('gate')();order={}
          gate.attention_all=function(...) assert(select('#',...)==0);order[#order+1]='arm';return 1 end
          gate.attention_all_status=function() order[#order+1]='status';return 'armed=1 calls=3' end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,1)
        self.assertEqual(list(self.lua.globals().order.values())[0],'arm')
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('ATTENTION_ALL_ARMED' in line for line in lines))
        for config,why in (("X4LAN_CONFIG.native_gate_attention_all=true",'attention all refused'),
                           ("X4LAN_CONFIG.native_gate_attention_rng=true;X4LAN_CONFIG.native_gate_attention_all='yes'",'attention all refused'),
                           ("X4LAN_CONFIG.native_gate_attention_initial_refresh=true;X4LAN_CONFIG.native_gate_attention_all=true;native=0",'attention all refused')):
            self.setUp()
            self.lua.execute(config+'''
              local gate=package.loadlib('gate')()
              gate.attention_all=function() return native or 1 end
              gate.attention_all_status=function() return 'armed=0' end
            ''')
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any(why in line for line in self.lua.globals().logs.values()),why)

    def test_mass_traffic_arms_before_loading_and_logs_every_admitted_tick(self):
        for value,mode in (('true','sync'),("'observe'",'observe')):
            self.setUp()
            self.lua.execute('''
              X4LAN_CONFIG.native_gate_mass_traffic=%s
              local gate=package.loadlib('gate')();modes={}
              gate.mass_traffic=function(mode,...) assert(select('#',...)==0);modes[#modes+1]=mode;return 1 end
              gate.mass_traffic_status=function() return 'mode=%s armed=1 installed=1 frame='..state.issued..' tries=3 skips=0 failures=0' end
              local load=LoadGame;LoadGame=function(slot) assert(#modes==1);load(slot) end
            ''' % (value,mode))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(list(self.lua.globals().modes.values()),[mode]);self.assertEqual(self.lua.globals().loads,1)
            self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
            self.lua.execute(SOURCE);self.lua.execute('update();state.issued=2;update()')
            self.assertEqual(list(self.lua.globals().modes.values()),[mode,mode]) # identical rerun after the UI reload
            self.lua.execute('%sstate.active=0;state.reason=1;update()' % STOP_FIELDS)
            lines=list(self.lua.globals().logs.values())
            self.assertTrue(any('MASS_TRAFFIC_ARMED mode='+mode in line for line in lines))
            self.assertTrue(any('MASS_TRAFFIC tick=1 mode='+mode+' armed=1 installed=1 frame=1 tries=3' in line for line in lines))
            self.assertTrue(any('MASS_TRAFFIC tick=2 mode='+mode+' armed=1 installed=1 frame=2' in line for line in lines))
            self.assertTrue(any('MASS_TRAFFIC_STOP mode='+mode in line for line in lines))
            self.assertFalse(any('ERROR' in line for line in lines))

    def test_region_pass_arms_before_loading_and_logs_every_admitted_tick(self):
        for value,mode in (('true','sync'),("'observe'",'observe')):
            self.setUp()
            self.lua.execute('''
              X4LAN_CONFIG.native_gate_mass_traffic='sync'
              X4LAN_CONFIG.native_gate_region_pass=%s
              local gate=package.loadlib('gate')();modes={}
              gate.mass_traffic=function(mode) modes[#modes+1]='traffic:'..mode;return 1 end
              gate.mass_traffic_status=function() return 'mode=sync frame='..state.issued end
              gate.region_pass=function(mode,...) assert(select('#',...)==0);modes[#modes+1]='region:'..mode;return 1 end
              gate.region_pass_status=function() return 'mode=%s armed=1 installed=1 joins='..state.issued..' failures=0' end
            ''' % (value,mode))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(list(self.lua.globals().modes.values()),['traffic:sync','region:'+mode]);self.assertEqual(self.lua.globals().loads,1)
            self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
            self.lua.execute(SOURCE);self.lua.execute('update();state.issued=2;update();%sstate.active=0;state.reason=1;update()' % STOP_FIELDS)
            lines=list(self.lua.globals().logs.values())
            self.assertTrue(any('REGION_PASS_ARMED mode='+mode in line for line in lines))
            self.assertTrue(any('REGION_PASS tick=2 mode='+mode+' armed=1 installed=1 joins=2' in line for line in lines))
            self.assertTrue(any('REGION_PASS_STOP mode='+mode in line for line in lines))
            self.assertFalse(any('ERROR' in line for line in lines))
        for value in ("'off'",'1'):
            self.setUp()
            self.lua.execute('X4LAN_CONFIG.native_gate_region_pass=%s;local gate=package.loadlib("gate")();'
                             'gate.region_pass=function() return 1 end;gate.region_pass_status=function() return "x" end' % value)
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any('region pass refused' in line for line in self.lua.globals().logs.values()))

    def test_mass_traffic_refuses_unknown_modes_and_a_native_refusal(self):
        for value,native,called in (("'off'",1,False),("'SYNC'",1,False),('1',1,False),("'sync'",0,True)):
            self.setUp()
            self.lua.execute('''
              X4LAN_CONFIG.native_gate_mass_traffic=%s
              local gate=package.loadlib('gate')()
              gate.mass_traffic=function(mode) mass_calls=(mass_calls or 0)+1;return %d end
              gate.mass_traffic_status=function() return 'mode=off stage=pins' end
            ''' % (value,native))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any('mass traffic refused' in line for line in self.lua.globals().logs.values()))
            self.assertEqual(self.lua.globals().mass_calls,1 if called else None)

    def test_camera_offset_runs_only_on_the_named_client_every_admitted_tick(self):
        for client,slot,local in (('2','p2',True),("'p2'",'p2',True),('2','p1',False),("'p1'",'p1',True)):
            self.setUp()
            self.lua.execute('''
              X4LAN_CONFIG.slot='%s'
              X4LAN_CONFIG.native_gate_camera_offset={client=%s,y=70000}
              local gate=package.loadlib('gate')();applied={}
              gate.camera_offset=function(tick,x,y,z) assert(x==0 and y==70000 and z==0);applied[#applied+1]=tick;return 1 end
              gate.camera_offset_status=function() return 'tick='..(applied[#applied] or 0)..' calls='..#applied..' failures=0' end
            ''' % (slot,client))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,1);self.assertEqual(len(self.lua.globals().applied),0)
            self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
            self.lua.execute(SOURCE);self.lua.execute('update();state.issued=2;update();%sstate.active=0;state.reason=1;update()' % STOP_FIELDS)
            lines=list(self.lua.globals().logs.values())
            self.assertEqual(list(self.lua.globals().applied.values()),[1,2] if local else [])
            self.assertTrue(any('CAMERA_OFFSET_ARMED slot=p' in line and 'local='+str(local).lower()+' offset=0,70000,0' in line for line in lines))
            self.assertEqual(any('CAMERA_OFFSET tick=2 tick=2 calls=2' in line for line in lines),local)
            self.assertEqual(any('CAMERA_OFFSET_STOP' in line for line in lines),local)
            self.assertFalse(any('ERROR' in line for line in lines))

    def test_camera_anchor_runs_only_on_the_named_client_with_the_slot(self):
        for client,slot,local,chase,expect in (('2','p2',True,'','40,10'),("'p2'",'p2',True,',back=0,up=0','0,0'),('2','p1',False,'','40,10'),
                                               ('2','p2',True,',back=-25.5,up=4','-25.5,4')):
            self.setUp()
            self.lua.execute('''
              X4LAN_CONFIG.slot='%s'
              X4LAN_CONFIG.native_gate_view_points='players'
              X4LAN_CONFIG.native_gate_camera_anchor={client=%s,slot=2%s}
              local gate=package.loadlib('gate')();applied={};chase={}
              gate.view_points=function() return 1 end
              gate.view_points_frame=function() return 1 end
              gate.view_points_status=function() return 'mode=players' end
              gate.camera_anchor=function(tick,slot,back,up,orbit,...) assert(slot==2 and orbit==0 and select('#',...)==0);applied[#applied+1]=tick;chase[#chase+1]=back..','..up;return 1 end
              gate.camera_anchor_status=function() return 'tick='..(applied[#applied] or 0)..' calls='..#applied..' failures=0' end
            ''' % (slot,client,chase))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,1);self.assertEqual(len(self.lua.globals().applied),0)
            self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
            self.lua.execute(SOURCE);self.lua.execute('update();state.issued=2;update();%sstate.active=0;state.reason=1;update()' % STOP_FIELDS)
            lines=list(self.lua.globals().logs.values())
            self.assertEqual(list(self.lua.globals().applied.values()),[1,2] if local else [])
            self.assertEqual(set(self.lua.globals().chase.values()),{expect} if local else set())
            self.assertTrue(any('CAMERA_ANCHOR_ARMED client=p2 local='+str(local).lower()+' slot=2 chase='+expect in line for line in lines))
            self.assertEqual(any('CAMERA_ANCHOR tick=2 tick=2 calls=2' in line for line in lines),local)
            self.assertEqual(any('CAMERA_ANCHOR_STOP' in line for line in lines),local)
            self.assertFalse(any('CAMERA_OFFSET' in line for line in lines))
            self.assertFalse(any('ERROR' in line for line in lines))
        for value in ("{client=2,slot=3}","{client=3,slot=2}","{client=2}","true","{client=2,slot=2,back=6000}","{client=2,slot=2,up=0/0}",
                      "{client=2,slot=2,back='far'}",
                      "{client=2,slot=2};X4LAN_CONFIG.native_gate_camera_offset={client=2,z=1}",
                      "{client=2,slot=2};X4LAN_CONFIG.native_gate_view_points=nil",
                      "{client=2,slot=2};X4LAN_CONFIG.native_gate_pilot_camera_follow=true"):
            self.setUp()
            self.lua.execute('X4LAN_CONFIG.native_gate_view_points="players";X4LAN_CONFIG.native_gate_camera_anchor=%s;local gate=package.loadlib("gate")();'
                             'gate.view_points=function() return 1 end;gate.view_points_frame=function() return 1 end;'
                             'gate.view_points_status=function() return "x" end;gate.camera_offset=function() return 1 end;'
                             'gate.camera_offset_status=function() return "x" end;'
                             'gate.camera_anchor=function() return 1 end;gate.camera_anchor_status=function() return "x" end' % value)
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0,value)
            self.assertTrue(any('camera anchor refused' in line for line in self.lua.globals().logs.values()),value)

    def test_camera_anchor_own_follows_the_local_slot_except_slot_one(self):
        for slot,value,local,expect in (('p2',"'own'",True,'slot=2 chase=40,10'),('p1',"'own'",False,'slot=1 chase=40,10'),
                                       ('p2',"{own=true,back=60,up=12}",True,'slot=2 chase=60,12')):
            self.setUp()
            self.lua.execute('''
              X4LAN_CONFIG.slot='%s'
              X4LAN_CONFIG.native_gate_view_points='players'
              X4LAN_CONFIG.native_gate_camera_anchor=%s
              local gate=package.loadlib('gate')();applied={}
              gate.view_points=function() return 1 end
              gate.view_points_frame=function() return 1 end
              gate.view_points_status=function() return 'mode=players' end
              gate.camera_anchor=function(tick,slot,back,up) applied[#applied+1]=slot..':'..back..','..up;return 1 end
              gate.camera_anchor_status=function() return 'tick=1 calls='..#applied..' failures=0' end
            ''' % (slot,value))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,1)
            self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
            self.lua.execute(SOURCE);self.lua.execute('update();state.issued=2;update()')
            lines=list(self.lua.globals().logs.values())
            self.assertTrue(any('CAMERA_ANCHOR_ARMED client=own local='+str(local).lower()+' '+expect in line for line in lines),(slot,value))
            applied=list(self.lua.globals().applied.values())
            self.assertEqual(bool(applied),local)
            if local: self.assertTrue(all(a.startswith('2:') for a in applied))
        # orbit=true passes 1 to the gate (the keypad turns the chase rig on that client)
        self.setUp()
        self.lua.execute('''
          X4LAN_CONFIG.slot='p2'
          X4LAN_CONFIG.native_gate_view_points='players'
          X4LAN_CONFIG.native_gate_camera_anchor={own=true,orbit=true}
          local gate=package.loadlib('gate')();orbits={}
          gate.view_points=function() return 1 end
          gate.view_points_frame=function() return 1 end
          gate.view_points_status=function() return 'mode=players' end
          gate.camera_anchor=function(tick,slot,back,up,orbit) orbits[#orbits+1]=orbit;return 1 end
          gate.camera_anchor_keys=function(...) anchor_keys={...};return 1 end
          gate.camera_anchor_status=function() return 'tick=1 failures=0' end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update();state.issued=2;update()')
        self.assertEqual(set(self.lua.globals().orbits.values()),{1})
        self.assertTrue(any('CAMERA_ANCHOR_ARMED client=own local=true slot=2 chase=40,10 orbit=1 keys=modifier=CONTROL,yaw_left=J,yaw_right=L,'
                            'pitch_up=I,pitch_down=K,zoom_in=U,zoom_out=O,reset=P' in line for line in self.lua.globals().logs.values()))
        self.assertEqual(list(self.lua.globals().anchor_keys.values()),[0x11,ord('J'),ord('L'),ord('I'),ord('K'),ord('U'),ord('O'),ord('P')])
        # 'numpad' and a named map pass their virtual keys
        for spec,expect in (("'numpad'",[0,0x64,0x66,0x68,0x62,0x6b,0x6d,0x65]),
                            ("{modifier='SHIFT',yaw_left='left',yaw_right='RIGHT',reset='HOME'}",[0x10,0x25,0x27,ord('I'),ord('K'),ord('U'),ord('O'),0x24])):
            self.setUp()
            self.lua.execute('''
              X4LAN_CONFIG.slot='p2'
              X4LAN_CONFIG.native_gate_view_points='players'
              X4LAN_CONFIG.native_gate_camera_anchor={own=true,orbit=%s}
              local gate=package.loadlib('gate')()
              gate.view_points=function() return 1 end
              gate.view_points_frame=function() return 1 end
              gate.view_points_status=function() return 'mode=players' end
              gate.camera_anchor=function() return 1 end
              gate.camera_anchor_keys=function(...) anchor_keys={...};return 1 end
              gate.camera_anchor_status=function() return 'tick=1 failures=0' end
            ''' % spec)
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,1,spec)
            self.assertEqual(list(self.lua.globals().anchor_keys.values()),expect,spec)
        for value in ("{own=false}","'mine'","{own=true,orbit='yes'}","{own=true,orbit={yaw_left='NOPE'}}","{own=true,orbit={modifier='ALT'}}"):
            self.setUp()
            self.lua.execute('X4LAN_CONFIG.slot="p2";X4LAN_CONFIG.native_gate_view_points="players";X4LAN_CONFIG.native_gate_camera_anchor=%s;'
                             'local gate=package.loadlib("gate")();gate.view_points=function() return 1 end;gate.view_points_frame=function() return 1 end;'
                             'gate.view_points_status=function() return "x" end;gate.camera_anchor=function() return 1 end;gate.camera_anchor_status=function() return "x" end' % value)
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0,value)
            self.assertTrue(any('camera anchor refused' in line for line in self.lua.globals().logs.values()),value)

    def test_camera_offset_refuses_invalid_settings_and_fails_closed(self):
        for value in ('{client=3,x=1}',"{client='p3',x=1}",'{client=1}','{client=1,x=2e7}','{client=1,x=0/0}','true',"'p1'"):
            self.setUp()
            self.lua.execute('X4LAN_CONFIG.native_gate_camera_offset=%s;local gate=package.loadlib("gate")();'
                             'gate.camera_offset=function() return 1 end;gate.camera_offset_status=function() return "x" end' % value)
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0,value)
            self.assertTrue(any('camera offset refused' in line for line in self.lua.globals().logs.values()),value)
        self.setUp()
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_camera_offset={client=1,z=5000}
          local gate=package.loadlib('gate')()
          gate.camera_offset=function() return 0 end
          gate.camera_offset_status=function() return 'stage=overwritten failures=1' end
          state.boot_phase=3;state.issued=1;is_menu=false
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update()')
        lines=list(self.lua.globals().logs.values())
        self.assertEqual(self.lua.globals().state.active,0);self.assertEqual(self.lua.globals().reports,0)
        self.assertTrue(any('ERROR' in line and 'camera offset failed stage=overwritten' in line for line in lines))

    def test_camera_perturbation_runs_only_on_the_named_client_every_admitted_tick(self):
        for target,slot,local in (('2','p2',True),("'p2'",'p2',True),('2','p1',False),("'p1'",'p1',True)):
            self.setUp()
            self.lua.execute('''
              X4LAN_CONFIG.slot='%s'
              X4LAN_CONFIG.native_gate_camera_perturb=%s
              local gate=package.loadlib('gate')();perturbed={}
              gate.camera_perturb=function(tick) perturbed[#perturbed+1]=tick;return 1 end
              gate.camera_perturb_status=function() return 'tick='..(perturbed[#perturbed] or 0)..' calls='..#perturbed..' failures=0' end
            ''' % (slot,target))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,1);self.assertEqual(len(self.lua.globals().perturbed),0)
            self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
            self.lua.execute(SOURCE);self.lua.execute('update();state.issued=2;update();%sstate.active=0;state.reason=1;update()' % STOP_FIELDS)
            lines=list(self.lua.globals().logs.values())
            self.assertEqual(list(self.lua.globals().perturbed.values()),[1,2] if local else [])
            self.assertTrue(any('CAMERA_PERTURB_ARMED slot=p%s local=%s' % (slot[1] if local else '2',str(local).lower()) in line for line in lines))
            self.assertEqual(any('CAMERA_PERTURB tick=2 tick=2 calls=2' in line for line in lines),local)
            self.assertEqual(any('CAMERA_PERTURB_STOP' in line for line in lines),local)
            self.assertFalse(any('ERROR' in line for line in lines))

    def test_camera_perturbation_refuses_invalid_targets_and_fails_closed(self):
        for target in ('3',"'p3'",'true','0',"'2'"):
            self.setUp()
            self.lua.execute('X4LAN_CONFIG.native_gate_camera_perturb=%s;local gate=package.loadlib("gate")();'
                             'gate.camera_perturb=function() return 1 end;gate.camera_perturb_status=function() return "x" end' % target)
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any('camera perturbation refused' in line for line in self.lua.globals().logs.values()))
        self.setUp()
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_camera_perturb=1
          local gate=package.loadlib('gate')()
          gate.camera_perturb=function() return 0 end
          gate.camera_perturb_status=function() return 'stage=fov_changed failures=1' end
          state.boot_phase=3;state.issued=1;is_menu=false
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update()')
        lines=list(self.lua.globals().logs.values())
        self.assertEqual(self.lua.globals().state.active,0);self.assertEqual(self.lua.globals().reports,0)
        self.assertTrue(any('ERROR' in line and 'camera perturbation failed stage=fov_changed' in line for line in lines))
        self.assertTrue(any('CAMERA_PERTURB_STOP stage=fov_changed' in line for line in lines))

    def test_travel_determinism_requires_serial_pathing_and_the_lifted_budget(self):
        for serial,mask,value in (('false','3','true'),('true','1','true'),('true','nil','true'),('true','3',"'1'")):
            self.setUp()
            self.lua.execute('''
              X4LAN_CONFIG.native_gate_serial_pathing=%s
              X4LAN_CONFIG.native_gate_path_determinism=%s
              X4LAN_CONFIG.native_gate_travel_determinism=%s
              local gate=package.loadlib('gate')()
              gate.path_determinism=function() return 1 end
              gate.travel_determinism=function() travel_calls=(travel_calls or 0)+1;return 1 end
            ''' % (serial,mask,value))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertIsNone(self.lua.globals().travel_calls);self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any('refused' in line for line in self.lua.globals().logs.values()))

    def test_path_determinism_refresh_requires_serial_pathing_and_an_exact_mask(self):
        for mask,serial in (('1','false'),('3','false'),('0','true'),('4','true'),('1.5','true'),("'1'",'true')):
            self.setUp()
            self.lua.execute('''
              X4LAN_CONFIG.native_gate_serial_pathing=%s
              X4LAN_CONFIG.native_gate_path_determinism=%s
              package.loadlib('gate')().path_determinism=function() arm_calls=(arm_calls or 0)+1;return 1 end
            ''' % (serial,mask))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertIsNone(self.lua.globals().arm_calls);self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any('path determinism refused' in line for line in self.lua.globals().logs.values()))

    AI_SCHEDULE_PREREQUISITES = """
      X4LAN_CONFIG.native_gate_serial_pathing=true
      X4LAN_CONFIG.native_gate_radar_serial=true
      X4LAN_CONFIG.native_gate_gravidar_time_sync=true
      local gate=package.loadlib('gate')();time_calls=0
      gate.gravidar_time=function() time_calls=time_calls+1;return 1 end
      gate.gravidar_key_status=function() return 'armed=0 time_sync=1 failures=0' end
      gate.gravidar=function() return 'tick=0 calls=0' end
    """

    def test_ai_schedule_arms_after_gravidar_time_and_logs_ticks_and_stop(self):
        self.lua.execute(self.AI_SCHEDULE_PREREQUISITES+'''
          X4LAN_CONFIG.native_gate_ai_schedule=true
          local gate=package.loadlib('gate')();arm_calls=0
          gate.ai_schedule=function(...)
            assert(select('#',...)==1 and select(1,...)==1 and time_calls==arm_calls+1);arm_calls=arm_calls+1;return 1
          end
          gate.ai_schedule_status=function() return 'armed=1 mask=1 admitted=1 cursor=2 count=25 stage=frame failures=0' end
          local load=LoadGame;LoadGame=function(slot) assert(arm_calls==1);load(slot) end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().arm_calls,1);self.assertEqual(self.lua.globals().loads,1)
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update()')
        self.assertEqual(self.lua.globals().arm_calls,2)
        self.lua.execute("state.active=0;state.completed=1;setmetatable(state,{__index=function() return 0 end})")
        self.lua.execute('update()')
        lines=list(self.lua.globals().logs.values())
        armed=next(i for i,line in enumerate(lines) if 'AI_SCHEDULE_ARMED mask=1' in line)
        self.assertLess(next(i for i,line in enumerate(lines) if 'GRAVIDAR_TIME_ARMED' in line),armed)
        tick=next(i for i,line in enumerate(lines) if 'AI_SCHEDULE tick=1 armed=1 mask=1 admitted=1' in line)
        stop=next(i for i,line in enumerate(lines) if 'AI_SCHEDULE_STOP armed=1' in line)
        self.assertLess(armed,tick);self.assertLess(tick,stop)
        self.assertFalse(any('ERROR' in line for line in lines))

    def test_gravidar_deadlines_arm_after_the_time_sync_and_logs_ticks_and_stop(self):
        self.lua.execute(self.AI_SCHEDULE_PREREQUISITES+'''
          X4LAN_CONFIG.native_gate_world_identities=true
          X4LAN_CONFIG.native_gate_gravidar_deadlines=true
          local gate=package.loadlib('gate')();arm_calls=0
          gate.gravidar_deadlines=function(...) assert(select('#',...)==0 and time_calls==arm_calls+1);arm_calls=arm_calls+1;return 1 end
          gate.gravidar_deadlines_status=function() return 'armed=1 installed=1 active=1 restored=0 calls=3 rebased=2 failures=0 stage=admitted' end
          local load=LoadGame;LoadGame=function(slot) assert(arm_calls==1);load(slot) end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().arm_calls,1);self.assertEqual(self.lua.globals().loads,1)
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update()')
        self.assertEqual(self.lua.globals().arm_calls,2)
        self.lua.execute("state.active=0;state.completed=1;setmetatable(state,{__index=function() return 0 end})")
        self.lua.execute('update()')
        lines=list(self.lua.globals().logs.values())
        armed=next(i for i,line in enumerate(lines) if 'GRAVIDAR_DEADLINES_ARMED' in line)
        self.assertLess(next(i for i,line in enumerate(lines) if 'GRAVIDAR_TIME_ARMED' in line),armed)
        tick=next(i for i,line in enumerate(lines) if 'GRAVIDAR_DEADLINES tick=1 armed=1 installed=1' in line)
        stop=next(i for i,line in enumerate(lines) if 'GRAVIDAR_DEADLINES_STOP armed=1' in line)
        self.assertLess(armed,tick);self.assertLess(tick,stop)
        self.assertFalse(any('ERROR' in line for line in lines))

    def test_gravidar_deadlines_require_an_exact_flag_the_time_sync_identities_and_native_acceptance(self):
        cases=(('true','',True),('1','',False),("'on'",'',False),
               ('true','X4LAN_CONFIG.native_gate_gravidar_time_sync=nil',False),
               ('true','X4LAN_CONFIG.native_gate_world_identities=nil',False),
               ('true','gate.gravidar_deadlines_status=nil',False),
               ('true','gate.gravidar_deadlines=function() arm_calls=(arm_calls or 0)+1;return 0 end',False))
        for value,extra,accepted in cases:
            self.setUp()
            self.lua.execute(self.AI_SCHEDULE_PREREQUISITES+'''
              X4LAN_CONFIG.native_gate_world_identities=true
              X4LAN_CONFIG.native_gate_gravidar_deadlines=%s
              local gate=package.loadlib('gate')()
              gate.gravidar_deadlines=function() arm_calls=(arm_calls or 0)+1;return 1 end
              gate.gravidar_deadlines_status=function() return 'armed=1' end
              %s
            ''' % (value,extra))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            refused=any('Gravidar deadlines refused' in line for line in self.lua.globals().logs.values())
            self.assertEqual(self.lua.globals().loads,int(accepted),(value,extra));self.assertEqual(refused,not accepted,(value,extra))

    def test_ai_schedule_logs_sector_fallbacks_when_they_change_and_at_stop(self):
        self.lua.execute(self.AI_SCHEDULE_PREREQUISITES+'''
          X4LAN_CONFIG.native_gate_ai_schedule=true
          X4LAN_CONFIG.native_gate_gravidar_sector_order=true
          X4LAN_CONFIG.native_gate_world_identities=true
          local gate=package.loadlib('gate')()
          gate.ai_schedule=function(mask) assert(mask==5);return 1 end
          gate.ai_schedule_status=function() return 'armed=1 mask=5 stage=frame failures=0' end
          fallback='fallback=1 unmapped=0 collisions=0 entries=[runtime=3e6 vt=2b1ea48 macro=cluster_99_sector001_macro parent=c8/7 key=8000000000000001]'
          gate.ai_schedule_fallbacks=function() return fallback end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update();state.issued=2;update()')
        self.lua.execute("fallback=fallback:gsub('fallback=1','fallback=2');state.issued=3;update()")
        self.lua.execute("state.active=0;state.completed=3;setmetatable(state,{__index=function() return 0 end})")
        self.lua.execute('update()')
        lines=list(self.lua.globals().logs.values())
        logged=[line for line in lines if 'AI_SCHEDULE_FALLBACK' in line]
        self.assertEqual(len(logged),3,logged)
        self.assertIn('AI_SCHEDULE_FALLBACK tick=1 fallback=1 unmapped=0 collisions=0 entries=[runtime=3e6',logged[0])
        self.assertIn('AI_SCHEDULE_FALLBACK tick=3 fallback=2',logged[1])
        self.assertIn('AI_SCHEDULE_FALLBACK stop=1 fallback=2',logged[2])
        self.assertLess(lines.index(logged[2]),next(i for i,line in enumerate(lines) if 'AI_SCHEDULE_STOP' in line))
        self.assertFalse(any('ERROR' in line for line in lines))

    def test_gravidar_load_pass_and_sector_order_extend_the_ai_schedule_mask(self):
        cases=(('','',1),('X4LAN_CONFIG.native_gate_gravidar_load_pass=true','',3),
               ('X4LAN_CONFIG.native_gate_gravidar_sector_order=true','X4LAN_CONFIG.native_gate_world_identities=true',5),
               ('X4LAN_CONFIG.native_gate_gravidar_load_pass=true;X4LAN_CONFIG.native_gate_gravidar_sector_order=true',
                'X4LAN_CONFIG.native_gate_world_identities=true',7),
               ('X4LAN_CONFIG.native_gate_gravidar_load_pass=1','',None),
               ('X4LAN_CONFIG.native_gate_gravidar_sector_order=true','',None), # needs saved identities
               ('X4LAN_CONFIG.native_gate_ai_schedule=nil;X4LAN_CONFIG.native_gate_gravidar_load_pass=true','',None),
               ('X4LAN_CONFIG.native_gate_ai_schedule=nil;X4LAN_CONFIG.native_gate_gravidar_sector_order=true',
                'X4LAN_CONFIG.native_gate_world_identities=true',None))
        for flags,extra,mask in cases:
            self.setUp()
            self.lua.execute(self.AI_SCHEDULE_PREREQUISITES+'''
              X4LAN_CONFIG.native_gate_ai_schedule=true
              %s
              %s
              local gate=package.loadlib('gate')()
              gate.ai_schedule=function(m) armed_mask=m;return 1 end
            ''' % (flags,extra))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            lines=list(self.lua.globals().logs.values())
            self.assertEqual(self.lua.globals().armed_mask,mask,(flags,extra))
            self.assertEqual(self.lua.globals().loads,int(mask is not None),(flags,extra))
            self.assertEqual(any('AI_SCHEDULE_ARMED mask=%s' % mask in line for line in lines),mask is not None,(flags,extra))
            self.assertEqual(any(' refused' in line for line in lines),mask is None,(flags,extra))

    def test_ai_schedule_requires_held_gravidar_publishes_and_an_exact_flag(self):
        cases=(('true','',True),('1','',False),("'on'",'',False),
               ('true','X4LAN_CONFIG.native_gate_gravidar_time_sync=nil',False),
               ('true','X4LAN_CONFIG.native_gate_radar_serial=nil',False), # the time sync refuses first
               ('true','X4LAN_CONFIG.native_gate_serial_pathing=nil',False),
               ('true','gate.ai_schedule=function() arm_calls=(arm_calls or 0)+1;return 0 end',False))
        for value,extra,accepted in cases:
            self.setUp()
            self.lua.execute(self.AI_SCHEDULE_PREREQUISITES+'''
              X4LAN_CONFIG.native_gate_ai_schedule=%s
              local gate=package.loadlib('gate')()
              gate.ai_schedule=function() arm_calls=(arm_calls or 0)+1;return 1 end
              %s
            ''' % (value,extra))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            refused=any(' refused' in line for line in self.lua.globals().logs.values())
            self.assertEqual(self.lua.globals().loads,int(accepted),(value,extra));self.assertEqual(refused,not accepted,(value,extra))

    SCRIPT_RNG_PREREQUISITES = """
      X4LAN_CONFIG.native_gate_interrupt_diag=true
      X4LAN_CONFIG.native_gate_nested_actions=true
      X4LAN_CONFIG.native_gate_world_identities=true
      local gate=package.loadlib('gate')()
      gate.interrupt=function() return 0 end;gate.interrupt_clear=function() return 1 end
      gate.interrupt_step=function() return 0 end;gate.interrupt_step_clear=function() return 1 end
      gate.nested_action=function() return 0 end;gate.nested_action_clear=function() return 1 end
    """

    def test_script_rng_arms_before_loading_and_logs_status_rows_and_stop(self):
        self.lua.execute(self.SCRIPT_RNG_PREREQUISITES+'''
          X4LAN_CONFIG.native_gate_script_rng='key'
          X4LAN_CONFIG.native_gate_script_rng_rows=true
          local gate=package.loadlib('gate')();arm_calls=0;rows={[0]='index=0 tick=1 src=1 state=1',[1]='index=1 evicted=1'}
          gate.script_rng=function(mode,mask,rows) assert(mode=='key' and mask==3 and rows==1);arm_calls=arm_calls+1;return 1 end
          gate.script_rng_status=function() return 'installed=1 mode=1 mask=3 failures=0' end
          gate.script_rng_row=function(i) return rows[i] end
          local load=LoadGame;LoadGame=function(slot) assert(arm_calls==1);load(slot) end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().arm_calls,1);self.assertEqual(self.lua.globals().loads,1)
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update()')
        self.assertEqual(self.lua.globals().arm_calls,2)
        self.lua.execute("state.active=0;state.completed=1;setmetatable(state,{__index=function() return 0 end})")
        self.lua.execute('update()')
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('SCRIPT_RNG_ARMED mode=key mask=3' in line for line in lines))
        tick=next(i for i,line in enumerate(lines) if 'SCRIPT_RNG tick=1 installed=1 mode=1 mask=3' in line)
        row=next(i for i,line in enumerate(lines) if 'SCRIPT_RNG_ROW index=0 tick=1' in line)
        stop=next(i for i,line in enumerate(lines) if 'SCRIPT_RNG_STOP installed=1' in line)
        self.assertLess(tick,row);self.assertLess(row,stop)
        self.assertEqual(sum('SCRIPT_RNG_ROW' in line for line in lines),2)
        self.assertFalse(any('ERROR' in line for line in lines))

    def test_script_rng_rows_window_drains_only_its_ticks_at_once(self):
        self.lua.execute(self.SCRIPT_RNG_PREREQUISITES+'''
          X4LAN_CONFIG.native_gate_script_rng='key'
          X4LAN_CONFIG.native_gate_script_rng_rows=true
          X4LAN_CONFIG.native_gate_script_rng_rows_window={5,6}
          X4LAN_CONFIG.native_gate_status_stride=3600
          local gate=package.loadlib('gate')();asked=0
          -- 40000 rows: ticks 1..8, 5000 a tick; the ring holds the newest 32768, older ones say evicted
          gate.script_rng=function() return 1 end
          gate.script_rng_status=function() return 'installed=1 mode=1 mask=3 failures=0' end
          gate.script_rng_row=function(i) asked=asked+1
            if i>=40000 then return nil end
            if i<40000-32768 then return 'index='..i..' evicted=1' end
            return 'index='..i..' tick='..(math.floor(i/5000)+1)..' src=1' end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.lua.execute('state.boot_phase=3;is_menu=false')
        for tick in (1,4,5,6,7):
            self.lua.execute('state.issued=%d' % tick);self.lua.execute('update()')
        lines=list(self.lua.globals().logs.values())
        rows=[row for line in lines for row in line.split(chr(10)) if 'SCRIPT_RNG_ROW ' in row]
        ticks={int(r.split(' tick=')[1].split(' ')[0]) for r in rows}
        self.assertEqual(ticks,{5,6})
        self.assertEqual(len(rows),10000)
        self.assertFalse(any('evicted' in r for r in rows))
        self.assertLess(self.lua.globals().asked,40000)
        self.assertLess(len(lines),len(rows)/50) # batched, not a call per row
        for value in ('{6,5}','{1,700}','{0,5}','5'):
            self.setUp()
            self.lua.execute(self.SCRIPT_RNG_PREREQUISITES+"X4LAN_CONFIG.native_gate_script_rng='key';X4LAN_CONFIG.native_gate_script_rng_rows=true;"
                             "X4LAN_CONFIG.native_gate_script_rng_rows_window="+value)
            with self.assertRaises(Exception) as caught: self.lua.execute(SOURCE)
            self.assertIn('native_gate_script_rng_rows_window',str(caught.exception))

    def test_script_rng_requires_hooks_identities_and_exclusive_rng_writers(self):
        cases=(("'key'",'3','',True),("'observe'",'3','X4LAN_CONFIG.native_gate_interrupt_diag=nil',False),
               ("'key'",'3','X4LAN_CONFIG.native_gate_nested_actions=nil',False),
               ("'key'",'3','X4LAN_CONFIG.native_gate_world_identities=nil',False),
               ("'on'",'3','',False),("'key'",'4','',False),("'key'",'8','',False),("'key'",'1.5','',False),("'key'","'3'",'',False),
               ("'key'",'3','X4LAN_CONFIG.native_gate_script_rng_rows=1',False),
               ("'key'",'3','X4LAN_CONFIG.native_gate_ai_rng=true',False),("'key'",'3','X4LAN_CONFIG.native_gate_interrupt_rng=true',False),
               ("'key'",'3','X4LAN_CONFIG.native_gate_npc_rng=true',False),("'observe'",'3','X4LAN_CONFIG.native_gate_npc_rng=true;gate.npc_exec=function() return 0 end;gate.npc_exec_clear=function() return 1 end',True),
               ("'observe'",'7',"X4LAN_CONFIG.native_gate_boot_rng_anchor='0123456789abcdef'",False),
               ("'observe'",'3',"X4LAN_CONFIG.native_gate_boot_rng_anchor='0123456789abcdef'",True))
        for mode,mask,extra,accepted in cases:
            self.setUp()
            self.lua.execute(self.SCRIPT_RNG_PREREQUISITES+'''
              X4LAN_CONFIG.native_gate_script_rng=%s
              X4LAN_CONFIG.native_gate_script_rng_mask=%s
              %s
              local gate=package.loadlib('gate')()
              gate.boot_rng_anchor=function() return 1 end
              gate.script_rng=function(mode,mask,rows) assert(rows==0);arm_calls=(arm_calls or 0)+1;return 1 end
            ''' % (mode,mask,extra))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            refused=any('script RNG hook refused' in line for line in self.lua.globals().logs.values())
            self.assertEqual(self.lua.globals().arm_calls,1 if accepted else None,(mode,mask,extra))
            self.assertEqual(self.lua.globals().loads,int(accepted),(mode,mask,extra));self.assertEqual(refused,not accepted)

    def test_script_rng_options_natural_rows_and_unmapped_keying(self):
        cases=(('','',0,True),('X4LAN_CONFIG.native_gate_script_rng_natural=true','',1,True),
               ("X4LAN_CONFIG.native_gate_script_rng_unmapped='key'",'',2,True),
               ("X4LAN_CONFIG.native_gate_script_rng_natural=true;X4LAN_CONFIG.native_gate_script_rng_unmapped='key'",'',3,True),
               ('X4LAN_CONFIG.native_gate_script_rng_natural=true','X4LAN_CONFIG.native_gate_script_rng_rows=nil',None,False),
               ('X4LAN_CONFIG.native_gate_script_rng_natural=1','',None,False),
               ('X4LAN_CONFIG.native_gate_script_rng_unmapped=true','',None,False),
               ("X4LAN_CONFIG.native_gate_script_rng_unmapped='key'",'X4LAN_CONFIG.native_gate_script_rng_mask=2',None,False))
        for option,extra,expected,accepted in cases:
            self.setUp()
            self.lua.execute(self.SCRIPT_RNG_PREREQUISITES+'''
              X4LAN_CONFIG.native_gate_script_rng='key'
              X4LAN_CONFIG.native_gate_script_rng_rows=true
              %s
              %s
              local gate=package.loadlib('gate')()
              gate.script_rng=function(mode,mask,rows,options) seen_options=options;arm_calls=(arm_calls or 0)+1;return 1 end
            ''' % (option,extra))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            lines=list(self.lua.globals().logs.values())
            refused=any('script RNG hook refused' in line for line in lines)
            self.assertEqual(refused,not accepted,(option,extra));self.assertEqual(self.lua.globals().seen_options,expected,(option,extra))
            if accepted: self.assertTrue(any('SCRIPT_RNG_ARMED mode=key mask=' in line and f' options={expected}' in line for line in lines))

    def test_md_timed_rng_arms_after_script_rng_and_logs_ticks_and_stop(self):
        self.lua.execute(self.SCRIPT_RNG_PREREQUISITES+'''
          X4LAN_CONFIG.native_gate_script_rng='key'
          X4LAN_CONFIG.native_gate_md_timed_rng=true
          local gate=package.loadlib('gate')();arm_calls=0;script_calls=0
          gate.script_rng=function() script_calls=script_calls+1;return 1 end
          gate.script_rng_status=function() return 'installed=1 mode=1 mask=3 timed=4 failures=0' end
          gate.script_rng_row=function() end
          gate.md_timed_rng=function(...)
            assert(select('#',...)==0 and script_calls==arm_calls+1);arm_calls=arm_calls+1;return 1
          end
          gate.md_timed_rng_status=function() return 'armed=1 sites=3 timed_calls=4 wait_calls=1 raise_calls=2 foreign=0 restored=0 failures=0' end
          local load=LoadGame;LoadGame=function(slot) assert(arm_calls==1);load(slot) end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().arm_calls,1);self.assertEqual(self.lua.globals().loads,1)
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update()')
        self.assertEqual(self.lua.globals().arm_calls,2)
        self.lua.execute("state.active=0;state.completed=1;setmetatable(state,{__index=function() return 0 end})")
        self.lua.execute('update()')
        lines=list(self.lua.globals().logs.values())
        script=next(i for i,line in enumerate(lines) if 'SCRIPT_RNG_ARMED' in line)
        armed=next(i for i,line in enumerate(lines) if 'MD_TIMED_RNG_ARMED' in line)
        tick=next(i for i,line in enumerate(lines) if 'MD_TIMED_RNG tick=1 armed=1 sites=3' in line)
        stop=next(i for i,line in enumerate(lines) if 'MD_TIMED_RNG_STOP armed=1' in line)
        self.assertLess(script,armed);self.assertLess(armed,tick);self.assertLess(tick,stop)
        self.assertFalse(any('ERROR' in line for line in lines))

    def test_md_timed_rng_requires_script_rng_md_bit_and_an_exact_flag(self):
        cases=(('true',"X4LAN_CONFIG.native_gate_script_rng='key'",True),
               ('true',"X4LAN_CONFIG.native_gate_script_rng='observe';X4LAN_CONFIG.native_gate_script_rng_mask=6",True),
               ('true','',False),('1',"X4LAN_CONFIG.native_gate_script_rng='key'",False),
               ("'on'","X4LAN_CONFIG.native_gate_script_rng='key'",False),
               ('true',"X4LAN_CONFIG.native_gate_script_rng='key';X4LAN_CONFIG.native_gate_script_rng_mask=1",False),
               ('true',"X4LAN_CONFIG.native_gate_script_rng='key';X4LAN_CONFIG.native_gate_script_rng_mask=5",False),
               ('true',"X4LAN_CONFIG.native_gate_script_rng='key';gate.md_timed_rng=function() arm_calls=(arm_calls or 0)+1;return 0 end",False))
        for value,extra,accepted in cases:
            self.setUp()
            self.lua.execute(self.SCRIPT_RNG_PREREQUISITES+'''
              local gate=package.loadlib('gate')()
              gate.script_rng=function() return 1 end
              gate.md_timed_rng=function() arm_calls=(arm_calls or 0)+1;return 1 end
              X4LAN_CONFIG.native_gate_md_timed_rng=%s
              %s
            ''' % (value,extra))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            refused=any(' refused' in line for line in self.lua.globals().logs.values())
            self.assertEqual(self.lua.globals().loads,int(accepted),(value,extra));self.assertEqual(refused,not accepted,(value,extra))

    def test_boot_hold_arms_before_loading_and_logs_ticks_and_stop(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_boot_hold=true
          local gate=package.loadlib('gate')();arm_calls=0
          gate.boot_hold=function(...) assert(select('#',...)==0);arm_calls=arm_calls+1;return 1 end
          gate.boot_hold_status=function() return 'armed=1 installed=1 active=1 held=1 moves=2 admitted=1 released=1 stage=released failures=0' end
          local load=LoadGame;LoadGame=function(slot) assert(arm_calls==1);load(slot) end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().arm_calls,1);self.assertEqual(self.lua.globals().loads,1)
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update()')
        self.assertEqual(self.lua.globals().arm_calls,2)
        self.lua.execute("state.active=0;state.completed=1;setmetatable(state,{__index=function() return 0 end})")
        self.lua.execute('update()')
        lines=list(self.lua.globals().logs.values())
        armed=next(i for i,line in enumerate(lines) if 'BOOT_HOLD_ARMED' in line)
        tick=next(i for i,line in enumerate(lines) if 'BOOT_HOLD tick=1 armed=1 installed=1 active=1 held=1' in line)
        stop=next(i for i,line in enumerate(lines) if 'BOOT_HOLD_STOP armed=1' in line)
        self.assertLess(armed,tick);self.assertLess(tick,stop)
        self.assertFalse(any('ERROR' in line for line in lines))

    def test_boot_hold_requires_an_exact_flag_and_native_acceptance(self):
        for value,result,accepted in (('true','1',True),('1','1',False),("'on'",'1',False),('true','0',False)):
            self.setUp()
            self.lua.execute('''
              X4LAN_CONFIG.native_gate_boot_hold=%s
              package.loadlib('gate')().boot_hold=function() arm_calls=(arm_calls or 0)+1;return %s end
            ''' % (value,result))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            refused=any('boot hold refused' in line for line in self.lua.globals().logs.values())
            self.assertEqual(self.lua.globals().loads,int(accepted),value);self.assertEqual(refused,not accepted,value)

    def test_md_resume_rng_arms_before_loading_and_logs_ticks_rows_and_stop(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_md_resume_rng=true
          local gate=package.loadlib('gate')();arm_calls=0;row_calls=0
          gate.md_resume_rng=function(...) assert(select('#',...)==0);arm_calls=arm_calls+1;return 1 end
          gate.md_resume_rng_status=function() return 'armed=1 installed=1 active=1 restored=0 calls=2 keyed=2 passed=0 failures=0' end
          gate.md_resume_rng_row=function(index)
            row_calls=row_calls+1
            if index<2 then return 'index='..index..' tick=0 owner=00000000000000a1 ord=1 draws=1 name=Other.Cue1' end
          end
          local load=LoadGame;LoadGame=function(slot) assert(arm_calls==1);load(slot) end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().arm_calls,1);self.assertEqual(self.lua.globals().loads,1)
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update()')
        self.assertEqual(self.lua.globals().arm_calls,2)
        self.lua.execute("state.active=0;state.completed=1;setmetatable(state,{__index=function() return 0 end})")
        self.lua.execute('update()')
        lines=list(self.lua.globals().logs.values())
        armed=next(i for i,line in enumerate(lines) if 'MD_RESUME_RNG_ARMED' in line)
        tick=next(i for i,line in enumerate(lines) if 'MD_RESUME_RNG tick=1 armed=1 installed=1 active=1' in line)
        rows=[i for i,line in enumerate(lines) if 'MD_RESUME_RNG_ROW index=' in line]
        stop=next(i for i,line in enumerate(lines) if 'MD_RESUME_RNG_STOP armed=1' in line)
        self.assertEqual(len(rows),2);self.assertLess(armed,tick);self.assertLess(tick,rows[0]);self.assertLess(rows[-1],stop)
        self.assertFalse(any('ERROR' in line for line in lines))

    def test_md_resume_rng_requires_an_exact_flag_and_native_acceptance(self):
        for value,result,rows,accepted in (('true','1',True,True),('1','1',True,False),("'on'",'1',True,False),
                                           ('true','0',True,False),('true','1',False,False)):
            self.setUp()
            self.lua.execute('''
              X4LAN_CONFIG.native_gate_md_resume_rng=%s
              local gate=package.loadlib('gate')()
              gate.md_resume_rng=function() arm_calls=(arm_calls or 0)+1;return %s end
              if %s then gate.md_resume_rng_row=function() end end
            ''' % (value,result,'true' if rows else 'false'))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            refused=any('MD resume RNG refused' in line for line in self.lua.globals().logs.values())
            self.assertEqual(self.lua.globals().loads,int(accepted),(value,result,rows));self.assertEqual(refused,not accepted,(value,result,rows))

    def test_ai_wait_rng_arms_before_loading_and_logs_ticks_rows_and_stop(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_world_identities=true
          X4LAN_CONFIG.native_gate_ai_wait_rng=true
          local gate=package.loadlib('gate')();arm_calls=0
          gate.ai_wait_rng=function(...) assert(select('#',...)==0);arm_calls=arm_calls+1;return 1 end
          gate.ai_wait_rng_status=function() return 'armed=1 installed=1 shared=0 active=1 restored=0 calls=3 keyed=3 passed=0 failures=0' end
          gate.ai_wait_rng_row=function(index)
            if index<3 then return 'index='..index..' tick=0 owner=0000000000080d54 ord=1 draws=1 pre=0 thread=0 name=move.idle' end
          end
          local load=LoadGame;LoadGame=function(slot) assert(arm_calls==1);load(slot) end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().arm_calls,1);self.assertEqual(self.lua.globals().loads,1)
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update()')
        self.assertEqual(self.lua.globals().arm_calls,2)
        self.lua.execute("state.active=0;state.completed=1;setmetatable(state,{__index=function() return 0 end})")
        self.lua.execute('update()')
        lines=list(self.lua.globals().logs.values())
        armed=next(i for i,line in enumerate(lines) if 'AI_WAIT_RNG_ARMED' in line)
        tick=next(i for i,line in enumerate(lines) if 'AI_WAIT_RNG tick=1 armed=1 installed=1 shared=0 active=1' in line)
        rows=[i for i,line in enumerate(lines) if 'AI_WAIT_RNG_ROW index=' in line]
        stop=next(i for i,line in enumerate(lines) if 'AI_WAIT_RNG_STOP armed=1' in line)
        self.assertEqual(len(rows),3);self.assertLess(armed,tick);self.assertLess(tick,rows[0]);self.assertLess(rows[-1],stop)
        self.assertFalse(any('ERROR' in line for line in lines))

    def test_economy_load_rng_arms_before_loading_and_logs_ticks_and_stop(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_world_identities=true
          X4LAN_CONFIG.native_gate_economy_load_rng=true
          local gate=package.loadlib('gate')();arm_calls=0
          gate.economy_load_rng=function(...) assert(select('#',...)==0);arm_calls=arm_calls+1;return 1 end
          gate.economy_load_rng_status=function() return 'installed=1 calls=5 keyed=5 unmapped=0 failures=0' end
          local load=LoadGame;LoadGame=function(slot) assert(arm_calls==1);load(slot) end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().arm_calls,1);self.assertEqual(self.lua.globals().loads,1)
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update()')
        self.lua.execute("state.active=0;state.completed=1;setmetatable(state,{__index=function() return 0 end})")
        self.lua.execute('update()')
        lines=list(self.lua.globals().logs.values())
        armed=next(i for i,line in enumerate(lines) if 'ECONOMY_LOAD_RNG_ARMED' in line)
        tick=next(i for i,line in enumerate(lines) if 'ECONOMY_LOAD_RNG tick=1 installed=1 calls=5' in line)
        stop=next(i for i,line in enumerate(lines) if 'ECONOMY_LOAD_RNG_STOP installed=1' in line)
        self.assertLess(armed,tick);self.assertLess(tick,stop)
        self.assertFalse(any('ERROR' in line for line in lines))
        for config in ('X4LAN_CONFIG.native_gate_economy_load_rng=1','X4LAN_CONFIG.native_gate_economy_load_rng=true;native=0',
                       'X4LAN_CONFIG.native_gate_economy_load_rng=true;X4LAN_CONFIG.native_gate_world_identities=nil'):
            self.setUp()
            self.lua.execute('X4LAN_CONFIG.native_gate_world_identities=true;'+config+'''
              local gate=package.loadlib('gate')()
              gate.economy_load_rng=function() return native or 1 end
              gate.economy_load_rng_status=function() return 'stage=pins' end
            ''')
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0,config)
            self.assertTrue(any('economy load RNG refused' in line for line in self.lua.globals().logs.values()),config)

    def test_region_rng_arms_before_loading_and_logs_ticks_rows_and_stop(self):
        for mode,number in (('key',1),('observe',0)):
            self.setUp()
            self.lua.execute('''
              X4LAN_CONFIG.native_gate_world_identities=true
              X4LAN_CONFIG.native_gate_region_rng='%s'
              local gate=package.loadlib('gate')();arm_calls=0
              gate.region_rng=function(...) assert(select('#',...)==1 and select(1,...)==%d);arm_calls=arm_calls+1;return 1 end
              gate.region_rng_status=function() return 'armed=1 installed=1 active=1 mode=%d restored=0 calls=3 failures=0' end
              gate.region_rng_row=function(index)
                if index<2 then return 'index='..index..' tick=1 region=00000000000007ce field=0 ord=1 mapped=1 seeded=1' end
              end
              local load=LoadGame;LoadGame=function(slot) assert(arm_calls==1);load(slot) end
            ''' % (mode,number,number))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().arm_calls,1);self.assertEqual(self.lua.globals().loads,1)
            self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
            self.lua.execute(SOURCE);self.lua.execute('update()')
            self.assertEqual(self.lua.globals().arm_calls,2)
            self.lua.execute("state.active=0;state.completed=1;setmetatable(state,{__index=function() return 0 end})")
            self.lua.execute('update()')
            lines=list(self.lua.globals().logs.values())
            armed=next(i for i,line in enumerate(lines) if 'REGION_RNG_ARMED mode='+mode in line)
            tick=next(i for i,line in enumerate(lines) if f'REGION_RNG tick=1 armed=1 installed=1 active=1 mode={number}' in line)
            rows=[i for i,line in enumerate(lines) if 'REGION_RNG_ROW index=' in line]
            stop=next(i for i,line in enumerate(lines) if 'REGION_RNG_STOP armed=1' in line)
            self.assertEqual(len(rows),2);self.assertLess(armed,tick);self.assertLess(tick,rows[0]);self.assertLess(rows[-1],stop)
            self.assertFalse(any('ERROR' in line for line in lines))

    def test_region_rng_requires_a_known_mode_identities_and_native_acceptance(self):
        for value,result,rows,identities,accepted in (
                ("'key'",'1',True,True,True),("'observe'",'1',True,True,True),('true','1',True,True,False),("'Key'",'1',True,True,False),
                ('1','1',True,True,False),("'key'",'0',True,True,False),("'key'",'1',False,True,False),("'key'",'1',True,False,False)):
            self.setUp()
            self.lua.execute('''
              X4LAN_CONFIG.native_gate_region_rng=%s
              X4LAN_CONFIG.native_gate_world_identities=%s
              local gate=package.loadlib('gate')()
              gate.region_rng=function() arm_calls=(arm_calls or 0)+1;return %s end
              if %s then gate.region_rng_row=function() end end
            ''' % (value,'true' if identities else 'nil',result,'true' if rows else 'false'))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            refused=any('region RNG refused' in line for line in self.lua.globals().logs.values())
            case=(value,result,rows,identities)
            self.assertEqual(self.lua.globals().loads,int(accepted),case);self.assertEqual(refused,not accepted,case)

    def test_spawn_rng_arms_before_loading_and_logs_ticks_rows_and_stop(self):
        for mode,number in (('key',1),('observe',0)):
            self.setUp()
            self.lua.execute("""
              X4LAN_CONFIG.native_gate_spawn_rng='%s'
              local gate=package.loadlib('gate')();arm_calls=0
              gate.spawn_rng=function(...) assert(select('#',...)==1 and select(1,...)==%d);arm_calls=arm_calls+1;return 1 end
              gate.spawn_rng_status=function() return 'armed=1 installed=1 active=1 mode=%d restored=0 calls=3 failures=0' end
              gate.spawn_rng_row=function(index)
                if index<2 then return 'index='..index..' tick=0 phase=2 ui=1 mode=%d normal=1 state=0000000000000001' end
              end
              local load=LoadGame;LoadGame=function(slot) assert(arm_calls==1);load(slot) end
            """ % (mode,number,number,number))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().arm_calls,1);self.assertEqual(self.lua.globals().loads,1)
            self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
            self.lua.execute(SOURCE);self.lua.execute('update()')
            self.assertEqual(self.lua.globals().arm_calls,2)
            self.lua.execute("state.active=0;state.completed=1;setmetatable(state,{__index=function() return 0 end})")
            self.lua.execute('update()')
            lines=list(self.lua.globals().logs.values())
            armed=next(i for i,line in enumerate(lines) if 'SPAWN_RNG_ARMED mode='+mode in line)
            tick=next(i for i,line in enumerate(lines) if f'SPAWN_RNG tick=1 armed=1 installed=1 active=1 mode={number}' in line)
            rows=[i for i,line in enumerate(lines) if 'SPAWN_RNG_ROW index=' in line]
            stop=next(i for i,line in enumerate(lines) if 'SPAWN_RNG_STOP armed=1' in line)
            self.assertEqual(len(rows),2);self.assertLess(armed,tick);self.assertLess(tick,rows[0]);self.assertLess(rows[-1],stop)
            self.assertFalse(any('ERROR' in line for line in lines))

    def test_spawn_rng_requires_a_known_mode_and_native_acceptance(self):
        for value,result,rows,accepted in (("'key'",'1',True,True),("'observe'",'1',True,True),('true','1',True,False),
                                           ("'Key'",'1',True,False),('1','1',True,False),("'key'",'0',True,False),("'key'",'1',False,False)):
            self.setUp()
            self.lua.execute("""
              X4LAN_CONFIG.native_gate_spawn_rng=%s
              local gate=package.loadlib('gate')()
              gate.spawn_rng=function() arm_calls=(arm_calls or 0)+1;return %s end
              gate.spawn_rng_status=function() return 'armed=1' end
              if %s then gate.spawn_rng_row=function() end end
            """ % (value,result,'true' if rows else 'false'))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            refused=any('spawn RNG refused' in line for line in self.lua.globals().logs.values())
            case=(value,result,rows)
            self.assertEqual(self.lua.globals().loads,int(accepted),case);self.assertEqual(refused,not accepted,case)

    def test_ai_wait_rng_requires_exact_flag_identities_and_no_selected_wait_experiment(self):
        for value,result,rows,identities,selected,accepted in (
                ('true','1',True,True,False,True),('1','1',True,True,False,False),("'on'",'1',True,True,False,False),
                ('true','0',True,True,False,False),('true','1',False,True,False,False),('true','1',True,False,False,False),
                ('true','1',True,True,True,False)):
            self.setUp()
            self.lua.execute('''
              X4LAN_CONFIG.native_gate_ai_wait_rng=%s
              X4LAN_CONFIG.native_gate_world_identities=%s
              if %s then X4LAN_CONFIG.native_gate_world_wait_rng_manifest='WAIT_POLICY_2;x' end
              local gate=package.loadlib('gate')()
              gate.ai_wait_rng=function() arm_calls=(arm_calls or 0)+1;return %s end
              if %s then gate.ai_wait_rng_row=function() end end
            ''' % (value,'true' if identities else 'nil','true' if selected else 'false',result,'true' if rows else 'false'))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            refused=any('AI wait RNG refused' in line for line in self.lua.globals().logs.values())
            case=(value,result,rows,identities,selected)
            self.assertEqual(self.lua.globals().loads,int(accepted),case);self.assertEqual(refused,not accepted,case)

    def test_find_order_arms_before_loading_and_logs_ticks_rows_and_stop(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_world_identities=true
          X4LAN_CONFIG.native_gate_find_order='sort'
          local gate=package.loadlib('gate')();arm_calls=0;arm_mode=nil
          gate.find_order=function(mode,...) assert(select('#',...)==0);arm_mode=mode;arm_calls=arm_calls+1;return 1 end
          gate.find_order_status=function() return 'armed=1 installed=1 active=1 mode=2 restored=0 calls=3 applied=3 failures=0' end
          gate.find_order_row=function(index)
            if index<2 then return 'index='..index..' tick=1 scope=420342c014014929 ord=1 mode=2 raw=2 n=2 ids=100a,1014' end
          end
          local load=LoadGame;LoadGame=function(slot) assert(arm_calls==1);load(slot) end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().arm_calls,1);self.assertEqual(self.lua.globals().arm_mode,2);self.assertEqual(self.lua.globals().loads,1)
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update()')
        self.assertEqual(self.lua.globals().arm_calls,2)
        self.lua.execute("state.active=0;state.completed=1;setmetatable(state,{__index=function() return 0 end})")
        self.lua.execute('update()')
        lines=list(self.lua.globals().logs.values())
        armed=next(i for i,line in enumerate(lines) if 'FIND_ORDER_ARMED mode=sort' in line)
        tick=next(i for i,line in enumerate(lines) if 'FIND_ORDER tick=1 armed=1 installed=1 active=1 mode=2' in line)
        rows=[i for i,line in enumerate(lines) if 'FIND_ORDER_ROW index=' in line]
        stop=next(i for i,line in enumerate(lines) if 'FIND_ORDER_STOP armed=1' in line)
        self.assertEqual(len(rows),2);self.assertLess(armed,tick);self.assertLess(tick,rows[0]);self.assertLess(rows[-1],stop)
        self.assertFalse(any('ERROR' in line for line in lines))

    def test_find_order_requires_a_known_mode_saved_ids_and_native_acceptance(self):
        for value,identities,result,accepted,mode in (("'observe'",'true','1',True,1),("'sort'",'true','1',True,2),
                                                      ("'sort'",'false','1',False,None),('true','true','1',False,None),
                                                      ("'key'",'true','1',False,None),("'sort'",'true','0',False,2)):
            self.setUp()
            self.lua.execute('''
              X4LAN_CONFIG.native_gate_world_identities=%s
              X4LAN_CONFIG.native_gate_find_order=%s
              local gate=package.loadlib('gate')()
              gate.find_order=function(mode) arm_mode=mode;return %s end
              gate.find_order_row=function() end
            ''' % (identities,value,result))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            refused=any('find order refused' in line for line in self.lua.globals().logs.values())
            self.assertEqual(self.lua.globals().loads,int(accepted),(value,identities,result))
            self.assertEqual(refused,not accepted,(value,identities,result));self.assertEqual(self.lua.globals().arm_mode,mode,(value,identities,result))

    def test_event_order_arms_before_loading_and_logs_every_admitted_tick(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_world_identities=true
          X4LAN_CONFIG.native_gate_event_order=true
          local gate=package.loadlib('gate')();arm_calls=0
          gate.event_order=function(...) assert(select('#',...)==0);arm_calls=arm_calls+1;return 1 end
          gate.event_order_status=function() return 'armed=1 installed=1 passes=1 failures=0' end
          local load=LoadGame;LoadGame=function(slot) assert(arm_calls==1);load(slot) end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().arm_calls,1);self.assertEqual(self.lua.globals().loads,1)
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update()')
        lines=list(self.lua.globals().logs.values())
        self.assertEqual(self.lua.globals().arm_calls,2)
        self.assertTrue(any('EVENT_ORDER_ARMED' in line for line in lines))
        self.assertTrue(any('EVENT_ORDER tick=1 armed=1 installed=1' in line for line in lines))
        self.assertFalse(any('ERROR' in line for line in lines))

    def test_event_order_requires_saved_identities_and_an_exact_flag(self):
        for value,identities in (('true','false'),('1','true'),("'true'",'true'),('false','true')):
            self.setUp()
            self.lua.execute('''
              X4LAN_CONFIG.native_gate_world_identities=%s
              X4LAN_CONFIG.native_gate_event_order=%s
              package.loadlib('gate')().event_order=function() arm_calls=(arm_calls or 0)+1;return 1 end
            ''' % (identities,value))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertIsNone(self.lua.globals().arm_calls)
            if value=='false':
                self.assertEqual(self.lua.globals().loads,1)
            else:
                self.assertEqual(self.lua.globals().loads,0)
                self.assertTrue(any('event order refused' in line for line in self.lua.globals().logs.values()))

    def test_event_rng_arms_after_event_order_logs_status_and_drains_rows(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_world_identities=true
          X4LAN_CONFIG.native_gate_event_order=true
          X4LAN_CONFIG.native_gate_event_rng='key'
          local gate=package.loadlib('gate')();order_calls=0;rng_arms={}
          gate.event_order=function() order_calls=order_calls+1;return 1 end
          gate.event_order_status=function() return 'armed=1 installed=1' end
          gate.event_rng=function(mode) assert(order_calls>#rng_arms);rng_arms[#rng_arms+1]=mode;return 1 end
          gate.event_rng_status=function() return 'armed=1 mode=key frame=1 calls=4 keyset=00000000000000aa drawset=00000000000000bb' end
          gate.event_rng_row=function(i) if i<2 then return 'index='..i..' tick=1 class=saved key=0000000000043cc2 stype=2b9bff8 etype=2ac17b8 due=404c000000000000 entry=0000000000001111 draws=3 keyed=1' end end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(list(self.lua.globals().rng_arms.values()),['key']);self.assertEqual(self.lua.globals().loads,1)
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update()')
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('EVENT_RNG_ARMED mode=key' in line for line in lines))
        self.assertTrue(any('EVENT_RNG tick=1 armed=1 mode=key' in line for line in lines))
        rows=[line for line in lines if 'NATIVE_GATE EVENT_RNG_ROW ' in line]
        self.assertEqual([row.split('EVENT_RNG_ROW ')[1].split(' ')[0] for row in rows],['index=0','index=1'])
        self.assertFalse(any('ERROR' in line for line in lines))

    def test_event_rng_rows_window_is_passed_to_the_arm(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_world_identities=true
          X4LAN_CONFIG.native_gate_event_order=true
          X4LAN_CONFIG.native_gate_event_rng='observe'
          X4LAN_CONFIG.native_gate_event_rng_rows={1400,1420}
          local gate=package.loadlib('gate')();args={}
          gate.event_order=function() return 1 end
          gate.event_order_status=function() return 'armed=1' end
          gate.event_rng=function(...) args={...};return 1 end
          gate.event_rng_status=function() return 'armed=1 mode=observe' end
          gate.event_rng_row=function() end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(list(self.lua.globals().args.values()),['observe',1400,1420]);self.assertEqual(self.lua.globals().loads,1)
        self.assertTrue(any('EVENT_RNG_ARMED mode=observe rows=1400-1420' in line for line in self.lua.globals().logs.values()))
        self.setUp()
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_world_identities=true
          X4LAN_CONFIG.native_gate_event_order=true
          X4LAN_CONFIG.native_gate_event_rng='observe'
          X4LAN_CONFIG.native_gate_event_rng_rows=1400
          local gate=package.loadlib('gate')()
          gate.event_order=function() return 1 end
          gate.event_rng=function() return 1 end
          gate.event_rng_status=function() return 'x' end
          gate.event_rng_row=function() end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,0)
        self.assertTrue(any('event rng refused' in line for line in self.lua.globals().logs.values()))

    def test_event_rng_requires_event_order_identities_and_a_mode(self):
        for mode,order,identities,native in (("'key'",'false','true',1),("'key'",'true','false',1),('true','true','true',1),
                                             ("'sort'",'true','true',1),("'observe'",'true','true',0)):
            self.setUp()
            self.lua.execute('''
              X4LAN_CONFIG.native_gate_world_identities=%s
              X4LAN_CONFIG.native_gate_event_order=%s
              X4LAN_CONFIG.native_gate_event_rng=%s
              local gate=package.loadlib('gate')()
              gate.event_order=function() return 1 end
              gate.event_rng=function() rng_arms=(rng_arms or 0)+1;return %d end
              gate.event_rng_status=function() return 'stage=pins' end
              gate.event_rng_row=function() end
            ''' % (identities,order,mode,native))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0,(mode,order,identities,native))
            lines=list(self.lua.globals().logs.values())
            self.assertTrue(any('refused' in line for line in lines),(mode,order,identities,native))

    def test_build_rng_arms_before_the_load_and_logs_at_stop(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_world_identities=true
          X4LAN_CONFIG.native_gate_build_rng=true
          local gate=package.loadlib('gate')();order={}
          gate.build_rng=function(...) assert(select('#',...)==0);order[#order+1]='arm';return 1 end
          gate.build_rng_status=function() return 'installed=1 keyed=2' end
          local load=LoadGame;LoadGame=function(slot) assert(#order==1);load(slot) end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,1)
        self.assertTrue(any('BUILD_RNG_ARMED' in line for line in self.lua.globals().logs.values()))
        for config in ("X4LAN_CONFIG.native_gate_build_rng=true",
                       "X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_build_rng='key'",
                       "X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_build_rng=true;native=0"):
            self.setUp()
            self.lua.execute(config+'''
              local gate=package.loadlib('gate')()
              gate.build_rng=function() return native or 1 end
              gate.build_rng_status=function() return 'stage=pins' end
            ''')
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any('build rng refused' in line for line in self.lua.globals().logs.values()),config)

    def test_ui_cargo_arms_logs_status_and_drains_rows(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_world_identities=true
          X4LAN_CONFIG.native_gate_ui_cargo='block'
          local gate=package.loadlib('gate')();modes={}
          gate.ui_cargo=function(mode) modes[#modes+1]=mode;return 1 end
          gate.ui_cargo_status=function() return 'installed=1 mode=block frame=1 blocked=2' end
          gate.ui_cargo_row=function(i) if i<2 then return 'index='..i..' tick=1 site=GetStorageData class=saved key=0000000000043cc2 blocked=1' end end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(list(self.lua.globals().modes.values()),['block']);self.assertEqual(self.lua.globals().loads,1)
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update()')
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('UI_CARGO_ARMED mode=block' in line for line in lines))
        self.assertTrue(any('UI_CARGO tick=1 installed=1 mode=block' in line for line in lines))
        rows=[line for line in lines if 'NATIVE_GATE UI_CARGO_ROW ' in line]
        self.assertEqual([row.split('UI_CARGO_ROW ')[1].split(' ')[0] for row in rows],['index=0','index=1'])
        self.assertFalse(any('ERROR' in line for line in lines))
        for mode,identities,native in (("'key'",'true',1),("'block'",'false',1),('true','true',1),("'observe'",'true',0)):
            self.setUp()
            self.lua.execute('''
              X4LAN_CONFIG.native_gate_world_identities=%s
              X4LAN_CONFIG.native_gate_ui_cargo=%s
              local gate=package.loadlib('gate')()
              gate.ui_cargo=function() return %d end
              gate.ui_cargo_status=function() return 'stage=pins' end
              gate.ui_cargo_row=function() end
            ''' % (identities,mode,native))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0,(mode,identities,native))
            self.assertTrue(any('ui cargo refused' in line for line in self.lua.globals().logs.values()),(mode,identities,native))

    def test_supply_rows_arm_and_drain_rows(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_world_identities=true
          X4LAN_CONFIG.native_gate_supply_rows=true
          local gate=package.loadlib('gate')();arms=0
          gate.supply_rows=function(...) assert(select('#',...)==0);arms=arms+1;return 1 end
          gate.supply_rows_status=function() return 'installed=1 posts=1' end
          gate.supply_row=function(i) if i<1 then return 'index=0 tick=1 class=saved key=00000000000340cf due=4038f33333333333 thread=ui chain=0042cb19' end end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().arms,1);self.assertEqual(self.lua.globals().loads,1)
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update()')
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('SUPPLY_ROWS_ARMED' in line for line in lines))
        self.assertTrue(any('NATIVE_GATE SUPPLY_POST_ROW index=0 tick=1' in line for line in lines))
        self.assertFalse(any('ERROR' in line for line in lines))
        for config in ("X4LAN_CONFIG.native_gate_supply_rows=true",
                       "X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_supply_rows=1",
                       "X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_supply_rows=true;native=0"):
            self.setUp()
            self.lua.execute(config+'''
              local gate=package.loadlib('gate')()
              gate.supply_rows=function() return native or 1 end
              gate.supply_rows_status=function() return 'stage=pins' end
              gate.supply_row=function() end
            ''')
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any('supply rows refused' in line for line in self.lua.globals().logs.values()),config)

    def test_ui_lazy_rows_window_is_passed_to_the_arm(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_world_identities=true
          X4LAN_CONFIG.native_gate_ui_lazy='observe'
          X4LAN_CONFIG.native_gate_ui_lazy_rows={150,200}
          local gate=package.loadlib('gate')();args={}
          gate.ui_lazy=function(...) args={...};return 1 end
          gate.ui_lazy_status=function() return 'installed=1 mode=observe' end
          gate.ui_lazy_row=function() end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(list(self.lua.globals().args.values()),['observe',150,200]);self.assertEqual(self.lua.globals().loads,1)
        self.assertTrue(any('UI_LAZY_ARMED mode=observe rows=150-200' in line for line in self.lua.globals().logs.values()))
        self.setUp()
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_world_identities=true
          X4LAN_CONFIG.native_gate_ui_lazy='observe'
          X4LAN_CONFIG.native_gate_ui_lazy_rows=150
          local gate=package.loadlib('gate')()
          gate.ui_lazy=function() return 1 end
          gate.ui_lazy_status=function() return 'x' end
          gate.ui_lazy_row=function() end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,0)
        self.assertTrue(any('ui lazy refused' in line for line in self.lua.globals().logs.values()))

    def test_loadout_order_arms_with_or_without_rows_and_drains_them(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_world_identities=true
          X4LAN_CONFIG.native_gate_loadout_order='observe'
          X4LAN_CONFIG.native_gate_loadout_order_rows={150,200}
          local gate=package.loadlib('gate')();args={}
          gate.loadout_order=function(...) args={...};return 1 end
          gate.loadout_order_status=function() return 'installed=1 mode=observe' end
          gate.loadout_order_row=function(i) if i<1 then return 'index=0 tick=187 kind=out site=unit class=saved key=000000000003bccb count=2 todo=0 rem=0 moved=0 macros=a:1/1@10,b:2/3@20' end end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(list(self.lua.globals().args.values()),['observe',150,200]);self.assertEqual(self.lua.globals().loads,1)
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update()')
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('LOADOUT_ORDER_ARMED mode=observe rows=150-200' in line for line in lines))
        self.assertTrue(any('NATIVE_GATE LOADOUT_ROW index=0 tick=187 kind=out site=unit' in line for line in lines))
        self.assertFalse(any('ERROR' in line for line in lines))
        # No window: one argument, no world identities needed.
        self.setUp()
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_loadout_order='canonical'
          local gate=package.loadlib('gate')();args={}
          gate.loadout_order=function(...) args={...};return 1 end
          gate.loadout_order_status=function() return 'installed=1 mode=canonical' end
          gate.loadout_order_row=function() end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(list(self.lua.globals().args.values()),['canonical']);self.assertEqual(self.lua.globals().loads,1)
        self.assertTrue(any('LOADOUT_ORDER_ARMED mode=canonical' in line for line in self.lua.globals().logs.values()))
        for config,why in (("X4LAN_CONFIG.native_gate_loadout_order='sort'",'loadout order refused'),
                           ("X4LAN_CONFIG.native_gate_loadout_order='observe';X4LAN_CONFIG.native_gate_loadout_order_rows={150,200}",'loadout order refused (rows)'),
                           ("X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_loadout_order='observe';X4LAN_CONFIG.native_gate_loadout_order_rows=150",'loadout order refused (rows)'),
                           ("X4LAN_CONFIG.native_gate_loadout_order='canonical';native=0",'loadout order refused stage=pins')):
            self.setUp()
            self.lua.execute(config+'''
              local gate=package.loadlib('gate')()
              gate.loadout_order=function() return native or 1 end
              gate.loadout_order_status=function() return 'stage=pins' end
              gate.loadout_order_row=function() end
            ''')
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any(why in line for line in self.lua.globals().logs.values()),config)

    def test_event_and_ui_lazy_rows_go_out_at_once(self):
        text=SOURCE.replace(chr(13),'')
        self.assertIn("ticklog.write('UI_LAZY_ROW '..row)",text)
        self.assertIn("ticklog.write('EVENT_RNG_ROW '..row)",text)
        self.assertNotIn("log('UI_LAZY_ROW '..row)",text.replace("ticklog.write('UI_LAZY_ROW '..row)",''))
        self.assertNotIn("log('EVENT_RNG_ROW '..row)",text.replace("ticklog.write('EVENT_RNG_ROW '..row)",''))

    def test_build_rows_arm_and_go_out_at_once(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_build_rows=true
          X4LAN_CONFIG.native_gate_status_stride=3600
          local gate=package.loadlib('gate')();calls=0
          gate.build_rows=function() calls=calls+1;return 1 end
          gate.build_rows_status=function() return 'installed=1' end
          gate.build_rows_row=function(i) if i<2 then return 'index='..i..' tick=16127 kind=add task=55 ship=1010 rng=0' end end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,1)
        self.assertGreaterEqual(self.lua.globals().calls,1)
        self.lua.execute('state.boot_phase=3;state.issued=7;is_menu=false')
        self.lua.execute('update();update()')
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('BUILD_ROWS_ARMED' in line for line in lines))
        # On a tick the status stride holds: still written, and each row once.
        self.assertEqual(sum('NATIVE_GATE BUILD_ROW index=' in line for line in lines),2)
        for config in ("X4LAN_CONFIG.native_gate_build_rows=1","X4LAN_CONFIG.native_gate_build_rows=true;native=0"):
            self.setUp()
            self.lua.execute(config+'''
              local gate=package.loadlib('gate')()
              gate.build_rows=function() return native or 1 end
              gate.build_rows_status=function() return 'stage=pins' end
              gate.build_rows_row=function() end
            ''')
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any('build rows refused' in line for line in self.lua.globals().logs.values()),config)

    def test_flight_fault_rows_are_drained_and_named_at_the_stop(self):
        text=SOURCE.replace(chr(13),'')
        self.assertIn("log('FLIGHT_FAULT_ROW '..row)",text)
        self.assertIn('drain_shape_lifetimes();drain_flight_faults()',text)
        self.assertIn("log('FLIGHT_FAULTS_STOP '..tostring(gate.flight_faults()))",text)
        self.lua.execute('''
          local gate=package.loadlib('gate')();asked=0
          gate.flight_fault_row=function(i) asked=asked+1;if i<2 then return 'index='..i..' tick=642 site=stale_agent actor=7 other=0000000000000384 fatal=0' end end
          state.boot_phase=3;state.issued=1;is_menu=false
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update()')
        lines=list(self.lua.globals().logs.values())
        self.assertEqual(sum('NATIVE_GATE FLIGHT_FAULT_ROW index=' in line for line in lines),2)

    def test_play_mode_arms_and_skips_rows(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_play_mode=true
          local gate=package.loadlib('gate')();calls=0
          gate.play_mode=function() calls=calls+1;return 1 end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,1)
        self.assertGreaterEqual(self.lua.globals().calls,1)
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('PLAY_MODE_ARMED' in line for line in lines))
        self.setUp()
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_play_mode=true
          local gate=package.loadlib('gate')()
          gate.play_mode=nil
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,0)
        self.assertTrue(any('REFUSED play_mode' in line for line in self.lua.globals().logs.values()))

    def test_macro_arena_arms_and_drains_rows(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_macro_arena=true
          local gate=package.loadlib('gate')();calls=0
          gate.macro_arena=function() calls=calls+1;return 1 end
          gate.macro_arena_status=function() return 'installed=1 created=0' end
          gate.macro_arena_row=function(i) if i<1 then return 'index=0 tick=3 kind=duplicate id=0000000012345678 slot=12345679 other=0000000012345678' end end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,1)
        self.assertGreaterEqual(self.lua.globals().calls,1)
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update()')
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('MACRO_ARENA_ARMED' in line for line in lines))
        self.assertTrue(any('NATIVE_GATE MACRO_ARENA_ROW index=0 tick=3 kind=duplicate' in line for line in lines))
        self.assertFalse(any('ERROR' in line for line in lines))
        for config in ("X4LAN_CONFIG.native_gate_macro_arena=1","X4LAN_CONFIG.native_gate_macro_arena=true;native=0"):
            self.setUp()
            self.lua.execute(config+'''
              local gate=package.loadlib('gate')()
              gate.macro_arena=function() return native or 1 end
              gate.macro_arena_status=function() return 'stage=pins' end
              gate.macro_arena_row=function() end
            ''')
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any('macro arena refused' in line for line in self.lua.globals().logs.values()),config)

    def test_pick_order_arms_and_drains_rows(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_pick_order='observe'
          X4LAN_CONFIG.native_gate_pick_order_rows={150,200}
          local gate=package.loadlib('gate')();args={}
          gate.pick_order=function(...) args={...};return 1 end
          gate.pick_order_status=function() return 'installed=1 mode=observe' end
          gate.pick_order_row=function(i) if i<1 then return 'index=0 tick=187 fn=476cf0 caller=c7d3f6 size=3 mode=4 total=7 draw=2 native=2 canon=0 moved=1' end end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(list(self.lua.globals().args.values()),['observe',150,200]);self.assertEqual(self.lua.globals().loads,1)
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update()')
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('PICK_ORDER_ARMED mode=observe rows=150-200' in line for line in lines))
        self.assertTrue(any('NATIVE_GATE PICK_ORDER_ROW index=0 tick=187 fn=476cf0' in line for line in lines))
        self.assertFalse(any('ERROR' in line for line in lines))
        for config,why in (("X4LAN_CONFIG.native_gate_pick_order='sort'",'pick order refused'),
                           ("X4LAN_CONFIG.native_gate_pick_order='observe';X4LAN_CONFIG.native_gate_pick_order_rows=150",'pick order refused (rows)'),
                           ("X4LAN_CONFIG.native_gate_pick_order='canonical';native=0",'pick order refused stage=pins')):
            self.setUp()
            self.lua.execute(config+'''
              local gate=package.loadlib('gate')()
              gate.pick_order=function() return native or 1 end
              gate.pick_order_status=function() return 'stage=pins' end
              gate.pick_order_row=function() end
            ''')
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any(why in line for line in self.lua.globals().logs.values()),config)

    def test_order_canon_arms_and_drains_rows(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_order_canon='observe'
          X4LAN_CONFIG.native_gate_order_canon_rows={150,200}
          local gate=package.loadlib('gate')();args={}
          gate.order_canon=function(...) args={...};return 1 end
          gate.order_canon_status=function() return 'installed=1 mode=observe' end
          gate.order_canon_row=function(i) if i<1 then return 'index=0 tick=187 site=5065fe count=2 moved=1 macros=a@10,b@20' end end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(list(self.lua.globals().args.values()),['observe',150,200]);self.assertEqual(self.lua.globals().loads,1)
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update()')
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('ORDER_CANON_ARMED mode=observe rows=150-200' in line for line in lines))
        self.assertTrue(any('NATIVE_GATE ORDER_CANON_ROW index=0 tick=187 site=5065fe' in line for line in lines))
        self.assertFalse(any('ERROR' in line for line in lines))
        for config,why in (("X4LAN_CONFIG.native_gate_order_canon='sort'",'order canon refused'),
                           ("X4LAN_CONFIG.native_gate_order_canon='observe';X4LAN_CONFIG.native_gate_order_canon_rows=150",'order canon refused (rows)'),
                           ("X4LAN_CONFIG.native_gate_order_canon='canonical';native=0",'order canon refused stage=pins')):
            self.setUp()
            self.lua.execute(config+'''
              local gate=package.loadlib('gate')()
              gate.order_canon=function() return native or 1 end
              gate.order_canon_status=function() return 'stage=pins' end
              gate.order_canon_row=function() end
            ''')
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any(why in line for line in self.lua.globals().logs.values()),config)

    def test_address_order_arms_with_a_tick_and_drains_rows(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_address_order=150
          local gate=package.loadlib('gate')();args={}
          gate.address_order=function(...) args={...};return 1 end
          gate.address_order_status=function() return 'armed=1 tick=150' end
          gate.address_order_row=function(i) if i<1 then return 'index=0 when=arm kind=macro total=1 part=0 partial=0 ids=0000aaaa' end end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(list(self.lua.globals().args.values()),[150]);self.assertEqual(self.lua.globals().loads,1)
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update()')
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('ADDRESS_ORDER_ARMED tick=150' in line for line in lines))
        self.assertTrue(any('NATIVE_GATE ADDRESS_ORDER_ROW index=0 when=arm kind=macro' in line for line in lines))
        self.assertFalse(any('ERROR' in line for line in lines))
        for config in ("X4LAN_CONFIG.native_gate_address_order='soon'","X4LAN_CONFIG.native_gate_address_order=150;native=0"):
            self.setUp()
            self.lua.execute(config+'''
              local gate=package.loadlib('gate')()
              gate.address_order=function() return native or 1 end
              gate.address_order_status=function() return 'stage=tick' end
              gate.address_order_row=function() end
            ''')
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any('address order refused' in line for line in self.lua.globals().logs.values()),config)

    def test_supply_wants_arms_with_its_window_and_drains_rows(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_world_identities=true
          X4LAN_CONFIG.native_gate_supply_wants={150,200}
          local gate=package.loadlib('gate')();args={}
          gate.supply_wants=function(...) args={...};return 1 end
          gate.supply_wants_status=function() return 'installed=1 calls=0' end
          gate.supply_wants_row=function(i) if i<1 then return 'index=0 tick=187 site=trade class=saved key=000000000003bccb a3=0 a4=1 count=1 wants=x:1,-:0,-:0' end end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(list(self.lua.globals().args.values()),[150,200]);self.assertEqual(self.lua.globals().loads,1)
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update()')
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('SUPPLY_WANTS_ARMED rows=150-200' in line for line in lines))
        self.assertTrue(any('NATIVE_GATE SUPPLY_WANTS_ROW index=0 tick=187 site=trade' in line for line in lines))
        self.assertFalse(any('ERROR' in line for line in lines))
        for config in ("X4LAN_CONFIG.native_gate_supply_wants={150,200}",
                       "X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_supply_wants=150",
                       "X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_supply_wants={150,200};native=0"):
            self.setUp()
            self.lua.execute(config+'''
              local gate=package.loadlib('gate')()
              gate.supply_wants=function() return native or 1 end
              gate.supply_wants_status=function() return 'stage=pins' end
              gate.supply_wants_row=function() end
            ''')
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any('supply wants refused' in line for line in self.lua.globals().logs.values()),config)

    def test_ui_lazy_arms_logs_status_and_drains_rows(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_world_identities=true
          X4LAN_CONFIG.native_gate_ui_lazy='observe'
          local gate=package.loadlib('gate')();modes={}
          gate.ui_lazy=function(mode) modes[#modes+1]=mode;return 1 end
          gate.ui_lazy_status=function() return 'installed=1 mode=observe frame=1 lua=1/0/2' end
          gate.ui_lazy_row=function(i) if i<1 then return 'index=0 tick=1 target=units class=saved key=00000000000340cf blocked=0 chain=0028c24b' end end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(list(self.lua.globals().modes.values()),['observe']);self.assertEqual(self.lua.globals().loads,1)
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update()')
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('UI_LAZY_ARMED mode=observe' in line for line in lines))
        self.assertTrue(any('UI_LAZY tick=1 installed=1 mode=observe' in line for line in lines))
        self.assertTrue(any('NATIVE_GATE UI_LAZY_ROW index=0 tick=1 target=units' in line for line in lines))
        self.assertFalse(any('ERROR' in line for line in lines))
        for mode,identities,native in (("'key'",'true',1),("'block'",'false',1),('true','true',1),("'block'",'true',0)):
            self.setUp()
            self.lua.execute('''
              X4LAN_CONFIG.native_gate_world_identities=%s
              X4LAN_CONFIG.native_gate_ui_lazy=%s
              local gate=package.loadlib('gate')()
              gate.ui_lazy=function() return %d end
              gate.ui_lazy_status=function() return 'stage=pins' end
              gate.ui_lazy_row=function() end
            ''' % (identities,mode,native))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertEqual(self.lua.globals().loads,0,(mode,identities,native))
            self.assertTrue(any('ui lazy refused' in line for line in self.lua.globals().logs.values()),(mode,identities,native))

    def test_event_rows_arm_after_event_order_and_drain_one_row_per_line(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_world_identities=true
          X4LAN_CONFIG.native_gate_event_order=true
          X4LAN_CONFIG.native_gate_event_rows=3
          local gate=package.loadlib('gate')();order_calls=0;row_arms={}
          gate.event_order=function() order_calls=order_calls+1;return 1 end
          gate.event_rows=function(ticks) assert(order_calls>#row_arms);row_arms[#row_arms+1]=ticks;return 1 end
          gate.event_order_status=function() return 'armed=1 installed=1 rows=3' end
          gate.event_order_row=function(i) if i<3 then return 'index='..i..' pass=1 phase=2 tick=0 slot='..i end end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(list(self.lua.globals().row_arms.values()),[3])
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update()')
        lines=list(self.lua.globals().logs.values())
        self.assertEqual(list(self.lua.globals().row_arms.values()),[3,3])
        self.assertTrue(any('EVENT_ROWS_ARMED ticks=3' in line for line in lines))
        rows=[line for line in lines if 'NATIVE_GATE EVENT_ROW ' in line]
        self.assertEqual([row.split('EVENT_ROW ')[1].split(' ')[0] for row in rows],['index=0','index=1','index=2'])
        self.assertFalse(any('ERROR' in line for line in lines))

    def test_event_rows_require_event_order_and_a_bounded_tick_count(self):
        for rows,order in (('3','false'),('-1','true'),('601','true'),('1.5','true'),("'3'",'true')):
            self.setUp()
            self.lua.execute('''
              X4LAN_CONFIG.native_gate_world_identities=true
              X4LAN_CONFIG.native_gate_event_order=%s
              X4LAN_CONFIG.native_gate_event_rows=%s
              local gate=package.loadlib('gate')()
              gate.event_order=function() return 1 end
              gate.event_rows=function() row_arms=(row_arms or 0)+1;return 1 end
            ''' % (order,rows))
            self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
            self.assertIsNone(self.lua.globals().row_arms);self.assertEqual(self.lua.globals().loads,0)
            self.assertTrue(any('event rows refused' in line for line in self.lua.globals().logs.values()))

    def test_queue_config_follows_early_manifest_before_loading_and_retries(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_world_trace_early=true
          X4LAN_CONFIG.native_gate_world_queue_trace=true
          X4LAN_CONFIG.native_gate_world_trace_ticks=12
          X4LAN_CONFIG.native_gate_world_hash_watch={'0000000000000063'}
          local gate=package.loadlib('gate')();manifest_calls=0;queue_config_calls=0
          gate.world_trace_config=function(ids,last) manifest_calls=manifest_calls+1;return 1 end
          gate.world_queue_trace_config=function() assert(manifest_calls==queue_config_calls+1);queue_config_calls=queue_config_calls+1;return 1 end
          local load=LoadGame;LoadGame=function(slot) assert(queue_config_calls==1);load(slot) end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().queue_config_calls,1)
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update()')
        self.assertEqual(self.lua.globals().queue_config_calls,2)
        self.assertEqual(self.lua.globals().loads,1)
        self.assertFalse(any('ERROR' in line for line in self.lua.globals().logs.values()))

    def test_world_trace_arms_after_capture_and_drains_append_only(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_world_hash_stride=1
          X4LAN_CONFIG.native_gate_world_hash_watch={'0000000000000063'}
          X4LAN_CONFIG.native_gate_world_trace_ticks=12
          X4LAN_CONFIG.native_gate_world_queue_trace=true
          local gate=package.loadlib('gate')();trace_available=0;trace_reads=0;queue_reads=0
          gate.world_watch=function(saved) assert(saved=='0000000000000063');watch=true;return 1 end
          gate.world_capture=function(tick) assert(watch);captured=tick;return 'tick='..tick..' state=WORLD_IMPORTED_1:'..tick end
          gate.world_banks=function() return 'test' end
          gate.world_page=function(n) assert(n==0);return 0 end
          gate.world_detail=function(n) assert(n==0);return 0 end
          gate.world_trace=function(last) assert(captured==1 and last==12);trace_available=2;return 1 end
          gate.world_trace_row=function(row)
            if row==0 then return trace_available end
            assert(row==trace_reads+1);trace_reads=row;return 'WORLD_TRACE_TEST row='..row
          end
          gate.world_queue_trace_row=function(row)
            if row==0 then return trace_available end
            assert(row==queue_reads+1);queue_reads=row;return 'WORLD_QUEUE_TEST row='..row
          end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update();update()')
        self.assertEqual(self.lua.globals().trace_reads,2)
        self.assertEqual(self.lua.globals().queue_reads,2)
        self.lua.execute('state.issued=2;trace_available=3;update();update()')
        self.assertEqual(self.lua.globals().trace_reads,3)
        self.assertEqual(self.lua.globals().queue_reads,3)
        self.assertFalse(any('ERROR' in line for line in self.lua.globals().logs.values()))

    def test_speech_observer_is_independent_and_both_npc_masks_are_exact(self):
        for scoped,deadline in ((False,False),(True,False),(False,True),(True,True)):
            self.setUp()
            self.lua.execute('''
              X4LAN_CONFIG.native_gate_npc_diag=true
              X4LAN_CONFIG.native_gate_npc_finish=true
              X4LAN_CONFIG.native_gate_npc_trigger=true
              X4LAN_CONFIG.native_gate_npc_provenance=true
              X4LAN_CONFIG.native_gate_interrupt_diag=true
              local gate=package.loadlib('gate')()
              gate.interrupt=function() return 0 end;gate.interrupt_clear=function() return 1 end
              gate.interrupt_step=function() return 0 end;gate.interrupt_step_clear=function() return 1 end
              gate.boot=function(a,b,c,d,e,options) captured_options=options;return 0 end
              gate.npc_speak=function(i) if i==0 then return 1 end return 'tick=10 ordinal=1' end
              gate.npc_speak_clear=function() speech_cleared=true;return 1 end
              gate.npc_enqueue=function(i) if i==0 then return 1 end return 'tick=10 ordinal=1' end
              gate.npc_enqueue_clear=function() enqueue_cleared=true;return 1 end
              gate.npc_finish=function(i) if i==0 then return 1 end return 'tick=10 ordinal=1' end
              gate.npc_finish_clear=function() finish_cleared=true;return 1 end
              gate.npc_ready=function(i) if i==0 then return 1 end return 'tick=10 ordinal=1' end
              gate.npc_ready_clear=function() ready_cleared=true;return 1 end
              state.npc_ready_calls=1;state.npc_ready_restored=1;state.npc_ready_seen=1
              gate.npc_deadline=function(i) if i==0 then return 1 end return 'tick=10 ordinal=1' end
              gate.npc_deadline_clear=function() deadline_cleared=true;return 1 end
              state.npc_deadline_calls=1;state.npc_deadline_restored=1;state.npc_deadline_seen=1
              gate.npc_cause=function(i) if i==0 then return 1 end return 'tick=10 irq=1' end
              gate.npc_trigger=function(i) if i==0 then return 1 end return 'tick=10 ordinal=1' end
              gate.npc_provenance=function(i) assert(not trigger_cleared);if i==0 then return 1 end return 'tick=10 trigger=1' end
              gate.npc_trigger_clear=function() trigger_cleared=true;return 1 end
              state.npc_trigger_calls=1;state.npc_cause_calls=1;state.npc_trigger_seen=1
              state.npc_provenance_calls=1
              gate.npc_exec=function(i) if i==0 then return 1 end return 'tick=10 ordinal=1' end
              gate.npc_exec_clear=function() execution_cleared=true;return 1 end
              state.active=0;state.boot_phase=3;state.completed=10;is_menu=false
              state.npc_speak_calls=1;state.npc_speak_restored=1
              state.npc_enqueue_calls=1;state.npc_enqueue_restored=1;state.npc_speak_seen=1
              state.npc_finish_calls=1;state.npc_finish_restored=1;state.npc_finish_seen=1
              state.npc_exec_calls=1;state.npc_exec_restored=1;state.interrupt_failures=0
              setmetatable(state,{__index=function() return 0 end})
            ''')
            self.lua.globals().X4LAN_CONFIG.native_gate_npc_rng=scoped
            self.lua.globals().X4LAN_CONFIG.native_gate_npc_deadline=deadline
            self.lua.globals().X4LAN_CONFIG.native_gate_npc_provenance_origin=deadline
            self.lua.execute(SOURCE);self.lua.execute('update();update()')
            self.assertEqual(self.lua.globals().captured_options,281474976710656+2199023255552+(140737488355328 if scoped else 0)+(562949953421312 if deadline else 0))
            lines=list(self.lua.globals().logs.values())
            event=next(i for i,line in enumerate(lines) if 'NPC_SPEAK tick=10' in line)
            stop=next(i for i,line in enumerate(lines) if 'NPC_SPEAK_STOP calls=1 restored=1 failures=0' in line)
            self.assertLess(event,stop);self.assertTrue(self.lua.globals().speech_cleared)
            self.assertTrue(self.lua.globals().enqueue_cleared)
            self.assertTrue(self.lua.globals().finish_cleared)
            self.assertTrue(self.lua.globals().trigger_cleared)
            self.assertTrue(any('identity=fixture_trigger_v1' in line for line in lines))
            provenance=next(i for i,line in enumerate(lines) if 'NPC_PROVENANCE tick=10 trigger=1' in line)
            provenance_version=2 if deadline else 1
            provenance_stop=next(i for i,line in enumerate(lines) if f'NPC_PROVENANCE_STOP calls=1 restored=1 failures=0 overflows=0 scope=windows_local_observation_v{provenance_version}' in line)
            self.assertLess(provenance,provenance_stop)
            self.assertEqual(bool(self.lua.globals().deadline_cleared),deadline)
            self.assertEqual(bool(self.lua.globals().ready_cleared),deadline)
            self.assertEqual(any('NPC_DEADLINE_STOP calls=1 restored=1 failures=0' in line for line in lines),deadline)
            self.assertTrue(any('NPC_SPEAK_COVERAGE seen=1 foreign_thread=0 unowned=0' in line for line in lines))
            self.assertEqual(any('NPC_EXEC tick=10' in line for line in lines),scoped)
            self.assertEqual(any('identity=ordered_invocation_v1' in line for line in lines),scoped)
            self.assertFalse(any('ERROR' in line for line in lines))

    def test_unknown_radar_object_metadata_preserves_64_bit_identity_and_reads_once(self):
        lua=LuaRuntime(unpack_returned_tuples=True)
        lua.execute('''
          local native=require('ffi');native.cdef('typedef uint64_t UniverseID;')
          local wanted=native.new('uint64_t',0xffffffff)*4294967296+0xfffffffe
          ffi={new=native.new,string=function(s) return s end,C={GetPlayerContainerID=function() return wanted end,
            GetPlayerOccupiedShipID=function() return wanted end,GetPlayerControlledShipID=function() return 0 end,
            GetObjectIDCode=function(object) assert(object==wanted);return 'TST-001' end}}
          cfg={};logs={};reads=0
          log=function(s) logs[#logs+1]=s end
          ConvertStringToLuaID=function(...)
            assert(select('#',...)==1);local text=...
            assert(text=='18446744073709551614');return 'lua:'..text
          end
          GetComponentData=function(object,a,b)
            assert(object=='lua:18446744073709551614' and a=='macro' and b=='owner')
            reads=reads+1;return 'fixture_macro','argon pilot'
          end
        ''')
        helper=SOURCE[SOURCE.index('local radar_objects={}'):SOURCE.index('local function registered_ships(')]
        lua.execute(helper+'''
          trace_radar_objects('ids=fffffffffffffffe/0,0000000000000001/1',22)
          trace_radar_objects('ids=fffffffffffffffe/0',23)
        ''')
        self.assertEqual(lua.globals().reads,1)
        self.assertEqual(list(lua.globals().logs.values()),[
            'RADAR_OBJECT tick=22 id=fffffffffffffffe macro=fixture_macro owner=argon_pilot player=3 code=TST-001'])
        lua.execute('''
          cfg.native_gate_world_identities=true
          gate={saved_identity=function(...)
            assert(select('#',...)==1 and (...)=='18446744073709551614')
            return '123456789abcdef0'
          end}
        ''')
        lua.execute(helper+"trace_radar_objects('ids=fffffffffffffffe/0',24)")
        self.assertTrue(lua.globals().logs[2].endswith(' saved=123456789abcdef0'))

    def setUp(self):
        self.lua = LuaRuntime(unpack_returned_tuples=True)
        self.lua.execute('''
          X4LAN_CONFIG={native_gate_test=true,test_profile='45841',slot='p1',
            native_gate_hash='clock',native_gate_snapshot_sha=string.rep('a',64),
            port=12345,token=string.rep('c',32)}
          X4LAN_SHA256={digest=function() return string.rep('b',64) end}
          logs={};loads=0;reports=0;boots=0;is_menu=true;poll_missing=false
          state={active=1,boot_phase=0,issued=0,report_tick=0,rng='0'}
          local gate={boot=function() boots=boots+1;return 0 end,
            poll=function() if not poll_missing then return state end end,
            load_requested=function() state.boot_phase=1;return 1 end,
            report=function(tick) reports=reports+1;state.report_tick=tick;return 1 end,
            stop=function() state.active=0;return 1 end}
          package.loadlib=function(path) assert(not path:find('fixedtick'));return function() return gate end end
          local ffi={cdef=function() end,typeof=function() return true end,string=function(s) return s end,
            C={GetSaveFolderPath=function() return 'fake/45841/save' end,
              GetCurrentGameTime=function() return 7 end,IsStartmenu=function() return is_menu end,
              ReloadSaveList=function() end,IsSaveListLoadingComplete=function() return true end}}
          require=function(name) assert(name=='ffi');return ffi end
          DebugError=function(s) logs[#logs+1]=s end
          RegisterEvent=function() end
          SetScript=function(name,fn) update=fn end
          GetSaveList=function() return {{filename='save_009'}} end
          LoadGame=function(slot) assert(slot=='save_009');loads=loads+1 end
        ''')

    def test_ui_reload_reports_first_admitted_frame_and_loads_only_once(self):
        self.lua.execute(SOURCE)
        self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads, 1)
        self.assertEqual(self.lua.globals().reports, 0)
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE)  # New UI state reloads the extension.
        self.lua.execute('update();update()')
        self.assertEqual(self.lua.globals().loads, 1)
        self.assertEqual(self.lua.globals().reports, 1)
        self.assertEqual(self.lua.globals().boots, 2)

    def test_loading_ui_cannot_report_for_simulation_owner(self):
        self.lua.execute(SOURCE)
        self.lua.execute('update();update();poll_missing=true;update();update()')
        self.assertEqual(self.lua.globals().reports, 0)
        self.assertFalse(any('ERROR' in line for line in self.lua.globals().logs.values()))

    def test_snapshot_manifest_precedes_load_and_md_enrollment_is_checked(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_hash='battle'
          X4LAN_CONFIG.native_gate_snapshot_fixture={}
          for i=1,12 do X4LAN_CONFIG.native_gate_snapshot_fixture[i]=string.format('%016x',i) end
          events={};RegisterEvent=function(name,callback) events[name]=callback end
          ConvertIDTo64Bit=function(object) return object end
          AddUITriggeredEvent=function(screen,control) assert(control=='RestoreActors');restore_queued=true end
          local gate=package.loadlib('gate')()
          gate.snapshot_fixture=function(manifest)
            assert(#manifest==203);manifest_calls=(manifest_calls or 0)+1;return 1
          end
          local load=LoadGame;LoadGame=function(slot) assert(manifest_calls==1);load(slot) end
          gate.snapshot_actor=function(i) assert(state.issued==1);return 'id='..i end
          gate.snapshot_enroll=function(i,runtime)
            assert(runtime==tostring(100+i));enrolled=(enrolled or 0)+1;return reject_enrollment and 0 or 1
          end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads,1)
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE);self.lua.execute('update()')
        self.assertEqual(self.lua.globals().reports,1);self.assertTrue(self.lua.globals().restore_queued)
        self.lua.execute("events['X4LAN_NativeGate.Actor1'](nil,101)")
        self.assertEqual(self.lua.globals().enrolled,1)
        self.lua.execute("reject_enrollment=true;enrollment_ok=pcall(events['X4LAN_NativeGate.Actor2'],nil,102)")
        self.assertFalse(self.lua.globals().enrollment_ok)
        self.assertFalse(any('ERROR' in line for line in self.lua.globals().logs.values()))

    def test_snapshot_actor_callbacks_after_native_stop_are_outside_session(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_hash='battle'
          X4LAN_CONFIG.native_gate_snapshot_fixture={}
          events={};RegisterEvent=function(name,callback) events[name]=callback end
          ConvertIDTo64Bit=function() error('stopped event must not resolve object') end
          local gate=package.loadlib('gate')()
          gate.snapshot_enroll=function() error('stopped event must not enroll') end
          state.active=0;state.reason=1;state.issued=1
        ''')
        self.lua.execute(SOURCE)
        self.lua.execute('''
          for i=1,12 do events['X4LAN_NativeGate.Actor'..i](nil,'queued') end
          events['X4LAN_NativeGate.Ready'](nil,12)
        ''')
        logs=list(self.lua.globals().logs.values())
        self.assertEqual(sum('ACTOR_OUTSIDE_SESSION' in line for line in logs),12)
        self.assertTrue(any('READY_OUTSIDE_SESSION' in line for line in logs))
        self.lua.execute("state.active=1;still_guarded=not pcall(events['X4LAN_NativeGate.Actor1'],nil,'bad')")
        self.assertTrue(self.lua.globals().still_guarded)

    def test_pre_arm_fixture_callbacks_are_accepted_before_native_gate_starts(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_hash='battle'
          events={};RegisterEvent=function(name,callback) events[name]=callback end
          resolved=0;ConvertIDTo64Bit=function(object) resolved=resolved+1;return object end
          state.active=0;state.issued=0;state.reason=0
        ''')
        self.lua.execute(SOURCE)
        self.lua.execute('''
          for i=1,12 do events['X4LAN_NativeGate.Actor'..i](nil,100+i) end
          events['X4LAN_NativeGate.Ready'](nil,12)
          bad_count_rejected=not pcall(events['X4LAN_NativeGate.Ready'],nil,11)
        ''')
        self.assertEqual(self.lua.globals().resolved,12)
        self.assertTrue(self.lua.globals().bad_count_rejected)
        self.assertFalse(any('OUTSIDE_SESSION' in line for line in self.lua.globals().logs.values()))

    def pilot_watch_roundtrip(self, origin):
        self.lua.execute('X4LAN_CONFIG.native_gate_npc_pilot_origin='+('true' if origin else 'false'))
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_npc_pilot=620203
          local gate=package.loadlib('gate')()
          watches=0;pilot_clears=0;pending={}
          gate.npc_pilot_watch=function(saved) assert(saved==620203);assert(boots>0);assert(watches>0 or state.boot_phase==0);watches=watches+1;return 1 end
          gate.npc_pilot=function(i) if i==0 then return #pending end return pending[i] end
          gate.npc_pilot_clear=function() pending={};pilot_clears=pilot_clears+1;return 1 end
          setmetatable(state,{__index=function() return 0 end})
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update()')
        self.assertEqual(self.lua.globals().watches,1)
        self.lua.execute("state.boot_phase=3;state.issued=1;state.input1='00000000';state.input2='00000000';is_menu=false")
        self.lua.execute(SOURCE);self.lua.execute('update()')
        self.assertEqual(self.lua.globals().watches,2)
        self.lua.execute("pending={'tick=1 ordinal=1'};state.active=0;state.completed=1;state.npc_pilot_calls=1;state.interrupt_restored=1;update()")
        lines=list(self.lua.globals().logs.values())
        row=next(i for i,line in enumerate(lines) if 'NPC_PILOT tick=1 ordinal=1' in line)
        version=2 if origin else 1
        stop=next(i for i,line in enumerate(lines) if f'NPC_PILOT_STOP calls=1 restored=1 failures=0 overflows=0 scope=selected_saved_pilot_v{version}' in line)
        self.assertLess(row,stop);self.assertFalse(any('ERROR' in line for line in lines))

    def test_pilot_watch_arms_before_loading_and_survives_ui_reload(self):
        self.pilot_watch_roundtrip(False)

    def test_pilot_origin_stop_uses_explicit_v2(self):
        self.pilot_watch_roundtrip(True)

    def test_interrupt_trace_drains_before_final_stop(self):
        self.lua.execute("""
          X4LAN_CONFIG.native_gate_interrupt_diag=true
          state.active=0;state.boot_phase=3;state.completed=10;is_menu=false
          state.interrupt_calls=1;state.interrupt_restored=1;state.interrupt_failures=0
          state.interrupt_step_calls=1;state.interrupt_step_restored=1
          setmetatable(state,{__index=function() return 0 end})
          local gate=package.loadlib('gate')()
          gate.interrupt=function(i) if i==0 then return 1 end return 'tick=10 ordinal=1' end
          gate.interrupt_clear=function() cleared=true;return 1 end
          gate.interrupt_step=function(i) if i==0 then return 1 end return 'tick=10 ordinal=1 parent=1' end
          gate.interrupt_step_clear=function() steps_cleared=true;return 1 end
        """)
        self.lua.execute(SOURCE);self.lua.execute('update();update()')
        lines=list(self.lua.globals().logs.values())
        event=next(i for i,line in enumerate(lines) if 'INTERRUPT tick=10' in line)
        stop=next(i for i,line in enumerate(lines) if 'INTERRUPT_STOP calls=1 restored=1 failures=0' in line)
        self.assertLess(event,stop);self.assertTrue(self.lua.globals().cleared)
        step=next(i for i,line in enumerate(lines) if 'INTERRUPT_STEP tick=10' in line)
        step_stop=next(i for i,line in enumerate(lines) if 'INTERRUPT_STEP_STOP calls=1 restored=1 failures=0' in line)
        self.assertLess(step,step_stop);self.assertTrue(self.lua.globals().steps_cleared)
        self.assertFalse(any('ERROR' in line for line in lines))

    def test_final_spline_trace_is_drained_after_owned_restoration(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_spline_diag=true
          state.active=0;state.boot_phase=3;state.completed=100;is_menu=false
          setmetatable(state,{__index=function() return 0 end})
          package.loadlib('gate')().spline=function(actor) return 'tick=100 calls=1 model=fixture' end
        ''')
        self.lua.execute(SOURCE);self.lua.execute('update();update()')
        rows=[line for line in self.lua.globals().logs.values() if ' final=1 ' in line]
        self.assertEqual(len(rows),12)
        self.assertTrue(all('tick=100' in row for row in rows))
        self.assertFalse(any('ERROR' in line for line in self.lua.globals().logs.values()))

    def test_mismatched_native_rng_schema_cannot_report(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_rng_schema=2
          state.rng_schema=1;state.boot_phase=3;state.issued=1;is_menu=false
        ''')
        self.lua.execute(SOURCE)
        self.lua.execute('update()')
        self.assertEqual(self.lua.globals().reports, 0)
        self.assertEqual(self.lua.globals().state.active, 0)

    def test_bullet_trace_drains_events_after_report_without_reporting_twice(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_bullet_diag=true
          local gate=package.loadlib('gate')()
          pending={};clears=0
          gate.bullet=function(i) if i==0 then return #pending end;return pending[i] end
          gate.bullet_clear=function() pending={};clears=clears+1;return 1 end
          state.boot_phase=3;state.issued=1;is_menu=false
        ''')
        self.lua.execute(SOURCE)
        self.lua.execute("update();pending={'id=2 slot=1 tick=1 spawn=1 call=1 created=0'};update();update()")
        self.assertEqual(self.lua.globals().reports, 1)
        self.assertEqual(self.lua.globals().clears, 3)
        rows = [line for line in self.lua.globals().logs.values() if 'BULLET id=' in line]
        self.assertEqual(len(rows), 1)
        self.assertFalse(any('ERROR' in line for line in self.lua.globals().logs.values()))

    def _run_weapon_fixture(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_hash='battle'
          X4LAN_CONFIG.native_gate_actor_diag=true
          X4LAN_CONFIG.native_gate_weapon_diag=true
          local gate=package.loadlib('gate')()
          registrations={};events={}
          RegisterEvent=function(name,fn) events[name]=fn end
          ConvertIDTo64Bit=function(id) return id end
          ConvertStringToLuaID=function(id) return tonumber(id) end
          GetComponentData=function(id,key)
            if key=='macro' then return 'fixture_gun' end
            return 100,100
          end
          local C=require('ffi').C
          C.IsComponentOperational=function() return true end
          C.GetObjectPositionInSector=function() return {x=0,y=0,z=0,yaw=0,pitch=0,roll=0} end
          C.GetNumUpgradeSlots=function() return 4 end
          C.GetUpgradeSlotCurrentComponent=function(id,kind,slot)
            assert(kind=='weapon');if slot%2==0 then return id*100+slot end;return 0
          end
          gate.actor=function() return 'fixture' end
          gate.register_weapon=function(id,actor,slot)
            assert(id==actor*100+slot and (slot==2 or slot==4))
            registrations[#registrations+1]={actor,slot};return 1
          end
          gate.weapon=function() return 'fixture' end
          state.boot_phase=3;state.issued=2;is_menu=false
        ''')
        self.lua.execute(SOURCE)
        self.lua.execute('''
          for i=1,12 do events['X4LAN_NativeGate.Actor'..i](nil,i) end
          events['X4LAN_NativeGate.Ready'](nil,12)
          update();state.issued=3;update()
        ''')
        self.assertEqual(len(self.lua.globals().registrations), 24)
        self.assertEqual(self.lua.globals().reports, 2)
        self.assertFalse(any('ERROR' in line for line in self.lua.globals().logs.values()))

    def test_weapon_registration_preserves_empty_physical_slots(self):
        self._run_weapon_fixture()

    def test_trace_window_includes_boundaries_without_dropping_state_reports(self):
        self._run_weapon_fixture()
        self.lua.execute('''
          logs={};X4LAN_CONFIG.native_gate_trace_stride=30
          X4LAN_CONFIG.native_gate_trace_window={5,6}
          for tick=4,7 do state.issued=tick;update() end
        ''')
        logs=list(self.lua.globals().logs.values())
        self.assertEqual(sum('WEAPON id=' in line for line in logs),48)
        self.assertEqual(sum('STATE tick=' in line for line in logs),4)
        self.assertEqual(self.lua.globals().reports,6)
        self.assertFalse(any('ERROR' in line for line in logs))

    def test_held_state_rows_are_written_only_with_a_flush(self):
        self.lua.execute('X4LAN_CONFIG.native_gate_status_stride=4')
        self._run_weapon_fixture()  # ticks 2 and 3: among the first four, written
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_strict_state=true
          X4LAN_CONFIG.native_gate_strict_physics=true
          local gate=package.loadlib('gate')()
          gate.actor=function()
            local vectors=string.rep('3f8000004000000040400000deadbeef',6)
            return 'attention=9;current='..vectors..';previous='..vectors..';hull_bits=4004000000000000;shield=none'
          end
          gate.body=function() return 'none' end
          X4LAN_SHA256.digest=function(data) hashed=data;return string.rep('b',64) end
          for tick=4,7 do state.issued=tick;update() end
        ''')
        lines=[line for call in self.lua.globals().logs.values() for line in call.split('\n')]
        self.assertEqual(sum('STATE_ROW tick=4 ' in line for line in lines),12)
        self.assertFalse(any('STATE_ROW tick=5 ' in line or 'STATE_END tick=7 ' in line for line in lines))
        self.assertEqual(self.lua.globals().hashed.count('/raw:'),12)  # held ticks are sampled and hashed as before
        self.lua.execute("state.active=0;"+STOP_FIELDS+"setmetatable(state,{__index=function() return 0 end});update()")
        lines=[line for call in self.lua.globals().logs.values() for line in call.split('\n')]
        held=next(i for i,line in enumerate(lines) if 'STATUS_HELD first=5 last=7 ticks=3 stride=4' in line)
        for tick in (5,6,7):
            rows=[line.split(' data=',1)[1] for line in lines[held:] if f'STATE_ROW tick={tick} ' in line]
            self.assertEqual(len(rows),12)
            self.assertTrue(rows[0].startswith('1/'))
            self.assertTrue(any(f'STATE_END tick={tick} entities=12 scope=BATTLE_SUBSET_3' in line for line in lines[held:]))
        self.assertFalse(any('ERROR' in line for line in lines))

    def test_spline_speed_limit_changes_hash_before_pose_changes(self):
        self._run_weapon_fixture()
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_strict_state=true
          X4LAN_CONFIG.native_gate_strict_physics=true
          X4LAN_CONFIG.native_gate_steering_schema=2
          X4LAN_CONFIG.native_gate_order_state=true
          X4LAN_CONFIG.native_gate_spline_state=true
          X4LAN_NATIVE_ORDERS=function() return 'ORDERS_1;0;0;default,none' end
          spline_bits=string.rep('3f800000',4)
          local gate=package.loadlib('gate')()
          gate.actor=function()
            local vectors=string.rep('3f800000400000004040000000000000',6)
            return 'attention=9;current='..vectors..';previous='..vectors..
              ';hull_bits=4004000000000000;shield=none;steering=spline:0;spline_state='..spline_bits
          end
          gate.body=function() return 'none' end
          X4LAN_SHA256.digest=function(data) hashed=data;return string.rep('b',64) end
          state.issued=4;update()
        ''')
        first=self.lua.globals().hashed
        self.assertTrue(first.startswith('BATTLE_SUBSET_6|'))
        self.lua.execute("spline_bits=string.rep('3f800000',3)..'3f4f262e';state.issued=5;update()")
        self.assertNotEqual(first,self.lua.globals().hashed)
        self.lua.execute("spline_bits='unavailable';state.issued=6;update()")
        self.assertEqual(self.lua.globals().state.active,0)
        self.assertEqual(self.lua.globals().reports,4)

    def test_docking_and_anchor_changes_enter_hash_even_when_anchor_leader_is_dead(self):
        self._run_weapon_fixture()
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_docking_state=true
          dock='DOCK_1;0;0;0;0';anchor='ANCHOR_1;1;station;argon;100;0;0,0,0,0,0,0;0;none'
          X4LAN_NATIVE_DOCKING=function()
            local ships={};for i=1,12 do ships[i]=dock end
            return ships,{anchor,anchor}
          end
          X4LAN_SHA256.digest=function(data) hashed=data;return string.rep('b',64) end
          AddUITriggeredEvent=function() end
          state.issued=4;update()
        ''')
        first=self.lua.globals().hashed
        self.assertTrue(first.startswith('BATTLE_SUBSET_8|'))
        self.assertEqual(first.count('/dock:'),12);self.assertEqual(first.count('/anchor:'),2)
        self.lua.execute("dock='DOCK_1;1;0;0;1';state.issued=5;update()")
        self.assertNotEqual(first,self.lua.globals().hashed)
        self.lua.execute("require('ffi').C.IsComponentOperational=function(id) return id~=1 end;state.issued=6;update()")
        before=self.lua.globals().hashed
        self.assertIn('1/dead/dock:',before)
        self.lua.execute("anchor=anchor:gsub(';100;',';99;');state.issued=7;update()")
        self.assertNotEqual(before,self.lua.globals().hashed)
        self.assertFalse(any('ERROR' in line for line in self.lua.globals().logs.values()))

    def test_dock_controller_inputs_replace_absent_navigation_buffers_and_change_hash(self):
        self._run_weapon_fixture()
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_strict_state=true
          X4LAN_CONFIG.native_gate_docking_state=true
          phase='00000001';undocking=false
          package.loadlib('gate')().actor=function()
            return 'attention=9;hull_bits=4004000000000000;shield=none;controller=dock;dock_state='..
              (undocking and 'UNDOCKCTL_1:' or 'DOCKCTL_1:')..
              string.rep('0',16)..':'..phase..(undocking and ':01:' or ':0101:')..string.rep('0',undocking and 192 or 336)
          end
          X4LAN_NATIVE_DOCKING=function()
            local ships={};for i=1,12 do ships[i]='DOCK_1;0;1;0;0' end
            return ships,{'pending','pending'}
          end
          AddUITriggeredEvent=function() end
          X4LAN_SHA256.digest=function(data) hashed=data;return string.rep('b',64) end
          state.issued=4;update()
        ''')
        before=self.lua.globals().hashed
        self.assertEqual(before.count(':dockctl=DOCKCTL_1:'),12)
        self.lua.execute("phase='00000002';state.issued=5;update()")
        self.assertNotEqual(before,self.lua.globals().hashed)
        self.lua.execute('undocking=true;state.issued=6;update()')
        self.assertEqual(self.lua.globals().hashed.count(':dockctl=UNDOCKCTL_1:'),12)
        self.lua.execute('X4LAN_CONFIG.native_gate_docking_state=false;state.issued=7;update()')
        self.assertEqual(self.lua.globals().state.active,0)
        self.assertTrue(any('requires docking state capture' in line for line in self.lua.globals().logs.values()))

    def test_order_queue_state_enters_canonical_hash(self):
        self._run_weapon_fixture()
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_order_state=true
          order_state='ORDERS_1;1;1;1,MoveWait,active,4,1,0,0,0,0,0;default,none'
          X4LAN_NATIVE_ORDERS=function() return order_state end
          X4LAN_SHA256.digest=function(data) hashed=data;return string.rep('b',64) end
          state.issued=4;update()
        ''')
        first=self.lua.globals().hashed
        self.assertTrue(first.startswith('BATTLE_SUBSET_5|'))
        self.assertEqual(first.count('/orders:ORDERS_1;'),12)
        self.lua.execute("order_state=order_state:gsub('MoveWait','ProtectShip');state.issued=5;update()")
        self.assertNotEqual(first,self.lua.globals().hashed)

    def test_shield_refresh_is_explicit_and_requested_for_every_actor_sample(self):
        self._run_weapon_fixture()
        self.lua.execute('''
          refresh_flags={};package.loadlib('gate')().actor=function(object,actor,refresh)
            refresh_flags[#refresh_flags+1]=refresh;return 'fixture'
          end
          state.issued=4;update()
          X4LAN_CONFIG.native_gate_shield_refresh=true;state.issued=5;update()
        ''')
        self.assertEqual(list(self.lua.globals().refresh_flags.values()), [0]*12+[1]*12)

    def test_admitted_commands_enter_state_hash_and_next_proposal(self):
        self._run_weapon_fixture()
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_command_test='pvp'
          state.wire_version=2;state.input1='02010000';state.input2='00000000'
          state.issued=60
          applied={};AddUITriggeredEvent=function(screen,control,action) applied[#applied+1]=action end
          X4LAN_SHA256.digest=function(data) hashed=data;return string.rep('b',64) end
          package.loadlib('gate')().report=function(tick,hash,command)
            submitted=command;state.report_tick=tick;reports=reports+1;return 1
          end
        ''')
        commands = (Path(__file__).resolve().parents[1]/'tools/dev/native_gate_commands.lua').read_text()
        self.lua.execute(commands)
        self.lua.execute('update();update()')
        self.assertEqual(len(self.lua.globals().applied), 1)
        self.assertEqual(self.lua.globals().submitted, '02010000')
        self.assertTrue(self.lua.globals().hashed.endswith('|INPUTS_1:pvp:02010000:00000000'))
        self.assertEqual(self.lua.globals().reports, 3)

    def test_strict_physics_includes_wreck_motion_but_excludes_storage_padding(self):
        self._run_weapon_fixture()
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_strict_state=true
          X4LAN_CONFIG.native_gate_strict_physics=true
          local gate=package.loadlib('gate')()
          local C=require('ffi').C
          C.IsComponentOperational=function(id) return id~=8 end
          gate.actor=function()
            local vectors=string.rep('3f8000004000000040400000deadbeef',6)
            return 'attention=9;current='..vectors..';previous='..vectors..';hull_bits=4004000000000000;shield=none'
          end
          padding='deadbeef';wreck_velocity='3f800000'
          gate.body=function(actor,which)
            if which==1 then return 'none' end
            local pose='3f8000004000000040400000'..padding..string.rep('3f800000',4)
            local motion={}
            for i=0,31 do motion[i+1]=padding end
            for _,i in ipairs({0,1,2,4,5,6,8,9,10,12,13,14,15,22}) do motion[i+1]='3f800000' end
            motion[31]='ab3fcd00'
            if actor==8 then motion[1]=wreck_velocity end
            local vectors=string.rep('3f8000004000000040400000'..padding,2)
            return 'pose='..pose..' motion='..table.concat(motion)..' pre='..vectors..' delta='..vectors
          end
          state.issued=4;update();padding='feedface';X4LAN_CONFIG.native_gate_trace_stride=30;state.issued=5;update()
          wreck_velocity='40000000';state.issued=6;update()
        ''')
        states = []
        for tick in (4, 5, 6):
            rows = [line.split(' data=', 1)[1] for line in self.lua.globals().logs.values()
                    if 'STATE_ROW tick='+str(tick)+' ' in line]
            self.assertEqual(len(rows), 12)
            states.append('BATTLE_SUBSET_3|'+'|'.join(rows))
        self.assertEqual(len(states), 3)
        self.assertEqual(states[0], states[1])
        self.assertNotEqual(states[1], states[2])
        self.assertIn('BATTLE_SUBSET_3|', states[0])
        self.assertIn('|8/dead/body0:', states[0])
        self.assertIn('/body1:none', states[0])
        self.assertNotIn('deadbeef', states[0])
        self.assertNotIn('feedface', states[1])
        self.lua.execute('''
          package.loadlib('gate')().body=function() return nil end
          state.issued=7;update()
        ''')
        self.assertEqual(self.lua.globals().reports, 5)
        self.assertEqual(self.lua.globals().state.active, 0)

    def test_strict_state_includes_native_velocity_and_health_without_vector_padding(self):
        self._run_weapon_fixture()
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_strict_state=true
          local gate=package.loadlib('gate')()
          gate.actor=function()
            local vectors=string.rep('3f8000004000000040400000deadbeef',6)
            return ';current='..vectors..';previous='..vectors..';hull_bits=4004000000000000;shield=none'
          end
          state.issued=4;update()
        ''')
        states=[line for line in self.lua.globals().logs.values() if 'STATE tick=4 ' in line]
        self.assertEqual(len(states), 1)
        self.assertIn('data=BATTLE_SUBSET_2|', states[0])
        self.assertIn('/raw:4004000000000000:none:3f8000004000000040400000:', states[0])
        self.assertNotIn('deadbeef', states[0])
        self.assertEqual(self.lua.globals().reports, 3)
        self.lua.execute('''
          local gate=package.loadlib('gate')()
          gate.actor=function() return ';hull_bits=4004000000000000;shield=unavailable' end
          state.issued=5;update()
        ''')
        self.assertEqual(self.lua.globals().reports, 3)
        self.assertEqual(self.lua.globals().state.active, 0)

    def test_steering_lifetimes_are_hashed_and_unknown_model_is_refused(self):
        self._run_weapon_fixture()
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_strict_state=true
          X4LAN_CONFIG.native_gate_strict_physics=true
          X4LAN_CONFIG.native_gate_steering_schema=2
          local gate=package.loadlib('gate')()
          steering='spline:0';hashed={}
          gate.actor=function()
            local vectors=string.rep('3f800000400000004040000000000000',6)
            return 'attention=9;current='..vectors..';previous='..vectors..';hull_bits=4004000000000000;shield=none;steering='..steering
          end
          gate.body=function() return 'none' end
          X4LAN_SHA256.digest=function(data) hashed[#hashed+1]=data;return string.rep('b',64) end
          state.issued=4;update()
          steering='1:3f800000:40000000';state.issued=5;update()
          steering='2:3f800000:40000000';state.issued=6;update()
          steering='slot:0:2c4f610';state.issued=7;update()
          steering='unsupported:123';state.issued=8;update()
        ''')
        hashes=list(self.lua.globals().hashed.values())
        self.assertEqual(len(hashes), 4)
        self.assertIn(':steering=slot:0:2c4f610',hashes[3])
        self.assertTrue(hashes[0].startswith('BATTLE_SUBSET_4|'))
        self.assertIn(':steering=spline:0',hashes[0])
        self.assertNotEqual(hashes[1], hashes[2])
        self.assertEqual(self.lua.globals().state.active, 0)


class StatusStrideTests(unittest.TestCase):
    """native_gate_status_stride: held ticks change the log only, never the sample, digest or report."""
    setUp = SnapshotProbeTests.setUp
    MODULES = '''
      X4LAN_CONFIG.native_gate_serial_pathing=true
      X4LAN_CONFIG.native_gate_path_determinism=3
      X4LAN_CONFIG.native_gate_bullet_diag=true
      X4LAN_CONFIG.native_gate_frame_timing=true
      local gate=package.loadlib('gate')();clears=0;hashes={};digests={};status_calls=0
      gate.path_determinism=function() return 1 end
      gate.path_determinism_status=function() status_calls=status_calls+1;return 'armed=1 frames='..state.issued end
      gate.bullet=function(i) if i==0 then return 1 end return 'id=1 slot=1 tick='..state.issued end
      gate.bullet_clear=function() clears=clears+1;return 1 end
      gate.frame_timing=function() return 'frames='..state.issued end
      gate.qpc_us=function() return 0 end
      gate.report=function(tick,hash)
        if tick==fail_at then return 0 end
        reports=reports+1;state.report_tick=tick;digests[#digests+1]=tick..':'..hash;return 1
      end
      X4LAN_SHA256.digest=function(data) hashes[#hashes+1]=data;return string.rep('b',64) end
    '''

    def run_ticks(self, last, extra=''):
        self.lua.execute(self.MODULES+extra)
        self.lua.execute(SOURCE);self.lua.execute('update();update();update()')
        self.lua.execute('state.boot_phase=3;state.issued=0;is_menu=false')
        self.lua.execute(SOURCE)  # the loading UI reload arms with the first admitted tick
        self.lua.execute(f'for tick=1,{last} do state.issued=tick;update() end')
        return self.lines()

    def lines(self):
        """Every logged line; a written tick's lines arrive in multi-line DebugError calls."""
        return [line for call in self.lua.globals().logs.values() for line in call.split('\n')]

    def stop(self):
        self.lua.execute('state.active=0;'+STOP_FIELDS+'setmetatable(state,{__index=function() return 0 end});update()')
        return self.lines()

    @staticmethod
    def ticks(lines, prefix):
        return [int(line.split(prefix+' tick=')[1].split()[0]) for line in lines if prefix+' tick=' in line]

    def test_first_and_every_nth_tick_are_written_and_reports_are_unchanged(self):
        plain=self.run_ticks(8)
        plain_state=[list(self.lua.globals()[name].values()) for name in ('hashes','digests')]
        plain_clears=self.lua.globals().clears
        self.assertEqual(self.lua.globals().status_calls,8)
        self.setUp()
        lines=self.run_ticks(8,'X4LAN_CONFIG.native_gate_status_stride=3')
        self.assertEqual([list(self.lua.globals()[name].values()) for name in ('hashes','digests')],plain_state)
        self.assertEqual(self.lua.globals().reports,8)
        self.assertEqual(self.lua.globals().clears,plain_clears)  # trace buffers are emptied on held ticks too
        self.assertEqual(self.ticks(plain,'RNG'),list(range(1,9)))
        self.assertEqual(self.ticks(lines,'RNG'),[1,2,3,6])
        self.assertEqual(self.ticks(lines,'PATH_DETERMINISM'),[1,2,3,6])
        self.assertEqual(self.lua.globals().status_calls,4)  # module statuses are built on written ticks only
        self.assertEqual([line for line in lines if 'BULLET id=' in line and not line.endswith(' tick=0')],
                         ['[X4LAN] NATIVE_GATE BULLET id=1 slot=1 tick=%d' % t for t in (1,2,3,6)])
        self.assertEqual(self.ticks(lines,'FRAME_TIMING'),list(range(1,9)))  # always written
        self.assertFalse(any('STATUS_HELD' in line or 'ERROR' in line for line in lines))
        # A stop writes the ticks held since the last written one, in order, before its STOP lines;
        # they keep their rows and RNG/INPUTS lines, the module statuses come from the STOP lines.
        lines=self.stop()
        held=next(i for i,line in enumerate(lines) if 'STATUS_HELD first=7 last=8 ticks=2 stride=3' in line)
        stop=next(i for i,line in enumerate(lines) if 'NATIVE_GATE STOP slot=p1' in line)
        self.assertEqual(self.ticks(lines[held:],'RNG'),[7,8])
        self.assertIn('[X4LAN] NATIVE_GATE BULLET id=1 slot=1 tick=8',lines[held:stop])
        self.assertEqual(self.ticks(lines,'PATH_DETERMINISM'),[1,2,3,6])
        self.assertTrue(any('PATH_DETERMINISM_STOP armed=1 frames=8' in line for line in lines[stop:]))
        self.assertLess(held,stop)
        self.assertEqual(self.ticks(lines,'RNG'),[1,2,3,6,7,8])  # 4 and 5 preceded the written tick 6

    def test_written_ticks_are_batched_in_bounded_calls(self):
        self.run_ticks(3,'''
          X4LAN_CONFIG.native_gate_status_stride=3
          local gate=package.loadlib('gate')()
          gate.bullet=function(i) if i==0 then return 40 end return 'id=1 slot=1 tick='..state.issued..' row='..i..' '..string.rep('f',200) end
        ''')
        calls=list(self.lua.globals().logs.values())
        self.assertEqual(sum('NATIVE_GATE RNG tick=3 ' in call for call in calls),1)
        chunk=[call for call in calls if 'tick=3 row=' in call]
        self.assertGreater(len(chunk),1)
        self.assertTrue(all(len(call)<=6100 for call in chunk))
        self.assertTrue(all(line.startswith('[X4LAN] NATIVE_GATE ') for call in chunk for line in call.split('\n')))
        self.assertEqual(sum(call.count(' row=') for call in chunk),40)
        self.assertLess(len(chunk),40)

    def test_failure_writes_the_held_ticks_and_the_failing_tick_before_the_error(self):
        lines=self.run_ticks(5,'X4LAN_CONFIG.native_gate_status_stride=3;fail_at=5')
        self.assertEqual(self.lua.globals().reports,4)
        held=next(i for i,line in enumerate(lines) if 'STATUS_HELD first=4 last=5 ticks=2 stride=3' in line)
        error=next(i for i,line in enumerate(lines) if 'NATIVE_GATE ERROR' in line)
        self.assertIn('native report rejected',lines[error])
        self.assertLess(held,error)
        self.assertEqual(self.ticks(lines[held:error],'RNG'),[4,5])
        self.assertIn('[X4LAN] NATIVE_GATE BULLET id=1 slot=1 tick=5',lines[held:error])
        self.assertTrue(any('ABORT stopped=1' in line for line in lines[error:]))

    def test_only_the_last_held_ticks_are_kept(self):
        self.run_ticks(130,'X4LAN_CONFIG.native_gate_status_stride=100')
        lines=self.stop()
        self.assertTrue(any('STATUS_HELD first=115 last=130 ticks=16 stride=100' in line for line in lines))
        self.assertEqual(self.ticks(lines,'RNG'),list(range(1,101))+list(range(115,131)))
        self.assertEqual(self.ticks(lines,'PATH_DETERMINISM'),list(range(1,101)))

    def test_world_capture_ticks_are_written_with_their_pages_in_order(self):
        lines=self.run_ticks(9,'''
          X4LAN_CONFIG.native_gate_status_stride=3;X4LAN_CONFIG.native_gate_world_hash_stride=4
          local gate=package.loadlib('gate')()
          gate.world_capture=function(tick) return 'tick='..tick..' state=WORLD_IMPORTED_1:'..tick end
          gate.world_banks=function() return 'tick='..state.issued end
          gate.world_page=function(n) if n==0 then return 1 end return '[X4LAN] NATIVE_GATE WORLD_OBJECT tick='..state.issued..' id=1'..string.char(10) end
          gate.world_detail=function(n) assert(n==0);return 0 end
        ''')
        self.assertEqual(self.ticks(lines,'RNG'),[1,2,3,4,6,8,9])
        self.assertEqual([line.split('WORLD_HASH ')[1] for line in lines if 'WORLD_HASH ' in line],
                         ['tick=%d state=WORLD_IMPORTED_1:%d' % (t,t) for t in (1,4,8)])
        for tick in (4,8):
            kinds=[line.split('NATIVE_GATE ')[1].split()[0] for line in lines
                   if f'WORLD_BANKS tick={tick}' in line or f'WORLD_OBJECT tick={tick} ' in line or f'WORLD_HASH tick={tick} ' in line]
            self.assertEqual(kinds,['WORLD_BANKS','WORLD_OBJECT','WORLD_HASH'])
        self.assertEqual(sum('|WORLD_IMPORTED_1:' in data for data in self.lua.globals().hashes.values()),9)

    def test_probe_profile_reports_sections_every_60_ticks(self):
        lines=self.run_ticks(125,'''
          X4LAN_CONFIG.native_gate_status_stride=60;X4LAN_CONFIG.native_gate_probe_profile=true
          local clock=0;package.loadlib('gate')().qpc_us=function() clock=clock+1;return clock end
        ''')
        rows=[line.split('PROBE_PROFILE ')[1] for line in lines if 'PROBE_PROFILE ' in line]
        self.assertEqual([row.split()[0] for row in rows],['tick=60','tick=120'])
        fields=dict(item.split('=') for item in rows[1].split()[1:])
        self.assertEqual(list(fields),['ticks']+[f'{name}_us' for name in ('drains','inputs','pilot','modules','sample','capture',
                         'digest','log','actor','health','orders','fleet','bodies','total')]+['max_total_us'])
        self.assertEqual(fields['ticks'],'60')
        self.assertTrue(all(value.isdigit() for value in fields.values()))
        self.assertGreater(int(fields['total_us']),0)
        self.setUp()
        self.assertFalse(any('PROBE_PROFILE' in line for line in self.run_ticks(61)))
        for value in ('1',"'on'"):
            self.setUp();self.lua.execute('X4LAN_CONFIG.native_gate_probe_profile='+value)
            with self.assertRaises(LuaError):self.lua.execute(SOURCE)

    def test_stride_one_writes_every_tick_and_off_is_the_default(self):
        lines=self.run_ticks(4,'X4LAN_CONFIG.native_gate_status_stride=1')
        self.assertEqual(self.ticks(lines,'RNG'),[1,2,3,4])
        self.assertFalse(any('STATUS_HELD' in line for line in self.stop()))

    def test_invalid_stride_fails_before_loading_or_booting(self):
        for value in ('0','-1','1.5','3601','true',"'60'",'0/0','{60}'):
            with self.subTest(value=value):
                self.setUp();self.lua.execute('X4LAN_CONFIG.native_gate_status_stride='+value)
                with self.assertRaises(LuaError):self.lua.execute(SOURCE)
                self.assertEqual(self.lua.globals().loads,0);self.assertEqual(self.lua.globals().boots,0)


class NativeDigestTests(unittest.TestCase):
    """gate.sha256: the per-tick digest in the gate when it has one, checked against X4LAN_SHA256 at arm."""
    setUp = SnapshotProbeTests.setUp
    MODULES = StatusStrideTests.MODULES
    run_ticks = StatusStrideTests.run_ticks
    lines = StatusStrideTests.lines

    def test_native_digest_is_checked_at_arm_and_hashes_every_tick(self):
        digest='''
          local function sum(s) local h=7;for i=1,#s do h=(h*31+s:byte(i))%4294967296 end;return string.format('%064x',h) end
          lua_calls,native_calls=0,0
          X4LAN_SHA256.digest=function(data) lua_calls=lua_calls+1;hashes[#hashes+1]=data;return sum(data) end
          package.loadlib('gate')().sha256=function(data) native_calls=native_calls+1;return (skew or '')..sum(data) end
        '''
        lines=self.run_ticks(3,digest)
        reported=list(self.lua.globals().digests.values())
        self.assertEqual(self.lua.globals().lua_calls,2)      # the boot digest of each arm (initial and loading UI)
        self.assertEqual(self.lua.globals().native_calls,2+3) # its arm check, then every admitted tick
        self.assertEqual(len(reported),3)
        self.assertEqual(sum('LUA_RUNTIME jit=on digest=native' in line for line in lines),2)
        self.setUp();self.run_ticks(3,digest+'package.loadlib("gate")().sha256=nil')
        self.assertEqual(list(self.lua.globals().digests.values()),reported)  # the same SHA-256 either way
        self.assertTrue(any('digest=lua' in line for line in self.lua.globals().logs.values()))
        # A native digest that differs from X4LAN_SHA256 is refused before the boot.
        self.setUp();self.lua.execute(self.MODULES+digest+"skew='f'")
        self.lua.execute(SOURCE);self.lua.execute('update()')
        lines=list(self.lua.globals().logs.values())
        self.assertTrue(any('native SHA-256 differs from X4LAN_SHA256' in line for line in lines))
        self.assertEqual(self.lua.globals().boots,0);self.assertEqual(self.lua.globals().reports,0)

class TraceRowsTests(unittest.TestCase):
    def test_trace_rows_are_written_at_once_not_held_by_the_status_stride(self):
        # s2-victim6: 3116 NPC actions recorded, none in the log (held with ticks the stride does not print).
        body = SOURCE.split('local function drain_interrupts()', 1)[1].split('local function drain_damage()', 1)[0]
        self.assertNotIn(" log('", body)
        for name in ('NPC_ACTION', 'NPC_TRIGGER', 'NPC_CAUSE', 'NPC_PROVENANCE', 'NPC_EXEC', 'NPC_SPEAK', 'NESTED_ACTION', 'INTERRUPT '):
            self.assertIn("ticklog.write('" + name, body)


if __name__ == '__main__':
    unittest.main()
