"""Actual generated Lua modules/configuration in addon load order; no game.

Only native-export/engine callbacks are modeled. Transition, origin, player,
diagnostic and probe adapters are the production files emitted by the builder.
"""
import hashlib
import unittest
from lupa.luajit21 import LuaError
from test_native_gate_getup_diagnostic_evidence import generated_overlay,configuration,SNAPSHOT
import test_native_gate_getup as getup
from test_native_gate_player_origin import receipts,render
from test_native_gate_transition_saved import text as transition_text
import test_native_gate_probe as probe


class GetUpStartupTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with generated_overlay() as out:
            cls.config=(out/'ui/config.lua.append').read_text()
            for old,new in {'@X4LANE_TOKEN@':'a'*32,'@X4LANE_PORT@':'42000','@X4LANE_SLOT@':'p1'}.items():cls.config=cls.config.replace(old,new)
            configuration(cls.config)  # Includes required actor_diag and exact closed contract.
            cls.modules=[(name,(out/name).read_text()) for name in (out/'ui.xml.files').read_text().splitlines()]
        cls.expected_order=['ui/native_gate_getup_diagnostic.lua','ui/native_gate_player_origin.lua',
            'ui/native_gate_transition.lua','ui/native_gate_player.lua','ui/native_gate_probe.lua']

    def runtime(self,backend=None,config=None,overrides=None):
        backend=backend if backend is not None else getup.GetUpTests.probe_backend()
        backend.setdefault('events',[]);backend.setdefault('native_actions',0)
        harness=probe.SnapshotProbeTests();harness.setUp();lua=harness.lua
        lua.execute('X4LAN_CONFIG={}')
        lua.execute(self.config if config is None else config)
        _,_,origin=receipts(empty=True)
        origin.update(imports=0,bound=0,native_unwinds=0,post_unwinds=0,delivery_unwinds=0)
        def event(name):backend['events'].append(name)
        def boot(*args):
            event('boot');backend['boots']+=1
            if 'boot_args' in backend:self.assertEqual(args,backend['boot_args'])
            else:backend['boot_args']=args
            return 0
        def trace(ids,last):self.assertEqual((ids,last),('000000000006d940',12));event('world_trace');return 1
        def queue():event('world_queue');return 1
        def watch(wire,last):
            self.assertEqual((wire,last),('2|00000000000ae79c/p/ship_arg_s_scout_01_a_macro/player',120));event('transition_watch');return 1
        def transition():event('transition');return 1
        def transition_status():
            active=backend['state']['active']
            return transition_text([],[],[],bound=0,watch_rows=0,import_calls=0,prebind=0,
                sites=7 if active else 0,enabled=active,exception_schema=1,direct_calls=0,direct_returns=0,direct_unwinds=0,
                queue_exception_cleanup=0).split('PLAYER_TRANSITION_STOP ',1)[1].replace('watches=0','watches=1')
        def origin_config(token):
            self.assertEqual(token,'1|0000000000011e12|00000000000ae79c|1|120');event('origin');backend['origins']+=1;return 1
        def configure_getup(run,contract=None):
            expected='diagnostic-getup-1' if lua.globals().X4LAN_CONFIG.native_gate_player_probe=='diagnostic-getup' else None
            self.assertEqual(contract,expected);self.assertEqual(run,hashlib.sha256(b'a'*32).hexdigest())
            event('getup');backend['getups']+=1
            if backend.get('refuse_getup'):return 0
            backend['getup_installed']=True;return 1
        def origin_status():
            active=backend['state']['active'];backend['status_reads']+=1
            current=dict(origin,schema=3 if backend.get('getup_installed') else 2,sites=3 if active else 0,closing=0 if active else 1)
            return render([],[],current).split('PLAYER_ORIGIN_STOP ',1)[1]
        def stop():event('stop');backend['stops']+=1;backend['state'].update(active=0,restored=1);return 1
        def requested():event('load_requested');backend['state']['boot_phase']=1;return 1
        def load(slot):self.assertEqual(slot,'save_009');event('load');backend['loads']+=1
        def forbidden():backend['native_actions']+=1;raise AssertionError('no native action/capture before admitted gameplay')
        callbacks=dict(nboot=boot,ntrace=trace,nqueue=queue,nwatch=watch,ntransition=transition,
            ntransition_status=transition_status,norigin=origin_config,ngetup=configure_getup,norigin_status=origin_status,
            nstop=stop,nrequested=requested,nload=load,nforbidden=forbidden,npoll=lambda:lua.table_from(backend['state']),
            ndigest=lambda value:hashlib.sha256(value.encode()).hexdigest())
        for name,callback in callbacks.items():lua.globals()[name]=callback
        lua.execute("""
          X4LAN_SHA256.digest=function(s) return ndigest(s) end
          local gate=package.loadlib('gate')()
          gate.boot=function(...) return nboot(...) end;gate.poll=function() return npoll() end
          gate.world_capture=function() return nforbidden() end;gate.world_capture_nav_v2=gate.world_capture
          gate.world_trace_config=function(...) return ntrace(...) end
          gate.world_queue_trace_config=function() return nqueue() end
          gate.player_transition_watch_config=function(...) return nwatch(...) end
          gate.player_transition_config=function() return ntransition() end
          gate.player_transition_status=function() return ntransition_status() end
          gate.player_transition_row=function(i) assert(i==0);return 0 end
          gate.player_origin_config=function(...) return norigin(...) end
          gate.player_getup_config=function(...) return ngetup(...) end
          gate.player_origin_status=function() return norigin_status() end
          gate.player_getup_batch=function() return nforbidden() end
          gate.stop=function() return nstop() end;gate.load_requested=function() return nrequested() end
          gate.world_trace_row=function(i) assert(i==0);return 0 end
          gate.world_queue_trace_row=function(i) assert(i==0);return 0 end
          gate.world_trace_status=function() return 'configured before Load; incomplete' end
          gate.world_queue_trace_status=function() return 'not installed before Load; incomplete' end
          gate.player_getup_diagnostic_status=function() return nil end
          LoadGame=function(slot) return nload(slot) end
        """)
        failures=[]
        for name,source in self.modules:
            try:lua.execute((overrides or {}).get(name,source))
            except LuaError as error:failures.append((name,str(error)))  # X4 continues loading later addon files.
        return lua,backend,failures

    def test_generated_order_and_actual_adapters_reach_load_then_fresh_vm_reattaches(self):
        self.assertEqual([name for name,_ in self.modules],self.expected_order)
        lua,backend,failures=self.runtime();self.assertEqual(failures,[])
        lua.execute('update();update();update()')
        self.assertEqual(backend['events'],['boot','world_trace','world_queue','transition_watch','transition','origin','getup','load_requested','load'])
        self.assertEqual((backend['loads'],backend['stops'],backend['native_actions']),(1,0,0))
        self.assertEqual(backend['boot_args'][5],70377334178981)
        expected='SNAPSHOT_BYTES_1|'+SNAPSHOT+'|'+configuration(self.config)['plan']
        self.assertEqual(backend['boot_args'][4],hashlib.sha256(expected.encode()).hexdigest())
        self.assertEqual(lua.eval('X4LAN_NATIVE_PLAYER_COMMANDS.proposal(29)'),'87010000')
        self.assertEqual(lua.eval('X4LAN_NATIVE_PLAYER_COMMANDS.proposal(30)'),'00000000')
        for phase in (1,2):
            backend['state']['boot_phase']=phase;claim=backend['claim'];second,backend,failures=self.runtime(backend)
            self.assertEqual(failures,[]);second.execute('update();update();update()')
            self.assertEqual((backend['loads'],backend['stops'],backend['native_actions']),(1,0,0));self.assertIs(backend['claim'],claim)
            self.assertFalse(any('ERROR' in row for row in second.globals().logs.values()))

    def test_old_transition_mode_gate_reproduces_native_startup_abort(self):
        name='ui/native_gate_transition.lua';source=dict(self.modules)[name]
        legacy=source.replace(" or cfg.native_gate_player_probe=='diagnostic-getup'",'')
        self.assertNotEqual(source,legacy)
        lua,backend,failures=self.runtime(overrides={name:legacy})
        self.assertEqual(len(failures),1);self.assertIn('transition observer requires explicit player probe',failures[0][1])
        lua.execute('update();update();update()')
        self.assertEqual((backend['boots'],backend['loads'],backend['stops'],backend['getups'],backend['native_actions']),(1,0,1,0,0))
        self.assertTrue(any('transition adapter missing' in row for row in lua.globals().logs.values()))
        self.assertTrue(any('ABORT stopped=1 active=0 restored=1 tick=0' in row for row in lua.globals().logs.values()))

    def test_real_partial_configuration_retains_failed_raw_observer_footers(self):
        backend=getup.GetUpTests.probe_backend();backend['refuse_getup']=True
        lua,backend,failures=self.runtime(backend);self.assertEqual(failures,[]);lua.execute('update();update();update()')
        self.assertEqual((backend['loads'],backend['stops'],backend['native_actions']),(0,1,0))
        rows=list(lua.globals().logs.values())
        self.assertTrue(any('PLAYER_TRANSITION_STOP ' in row and 'bound=0' in row for row in rows))
        self.assertTrue(any('PLAYER_ORIGIN_STOP ' in row and 'schema=2 ' in row for row in rows))
        self.assertFalse(any('ARMED ' in row for row in rows))

    def test_contract_slot_and_dependency_failures_never_load(self):
        mutations=[('diagnostic-getup-1','diagnostic-getup-X'),('slot = "p1"','slot = "p2"'),
            ('native_gate_world_trace_early = true','native_gate_world_trace_early = false'),
            ('native_gate_world_queue_trace = true','native_gate_world_queue_trace = false')]
        for before,after in mutations:
            with self.subTest(before=before):
                self.assertIn(before,self.config);lua,backend,failures=self.runtime(config=self.config.replace(before,after))
                if lua.globals().update is not None:lua.execute('update();update();update()')
                self.assertEqual((backend['loads'],backend['native_actions']),(0,0))
                self.assertTrue(failures or any('ERROR' in row for row in lua.globals().logs.values()))

    def test_paired_mode_and_existing_transition_mode_refusals_are_preserved(self):
        paired=self.config.replace('"diagnostic-getup"','"agreed-getup"')
        paired='\n'.join(row for row in paired.splitlines() if 'native_gate_diagnostic_contract' not in row)
        lua,backend,failures=self.runtime(config=paired);self.assertEqual(failures,[]);lua.execute('update();update();update()')
        self.assertEqual((backend['loads'],backend['stops'],backend['native_actions']),(1,0,0))
        transition=dict(self.modules)['ui/native_gate_transition.lua']
        for mode in ('observe','agreed-getup','diagnostic-getup','native-local-negative','agreed-throttle','authority-negative','bad'):
            vm=probe.SnapshotProbeTests();vm.setUp();vm.lua.execute('X4LAN_CONFIG={native_gate_player_transition_observer=true,native_gate_world_trace_early=true,native_gate_world_queue_trace=true}')
            vm.lua.globals().X4LAN_CONFIG.native_gate_player_probe=mode
            if mode in ('observe','agreed-getup','diagnostic-getup'):vm.lua.execute(transition)
            else:
                with self.subTest(mode=mode),self.assertRaisesRegex(LuaError,'requires explicit player probe'):vm.lua.execute(transition)

if __name__=='__main__':unittest.main()
