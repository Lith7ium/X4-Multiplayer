"""Actual Lua adapter and independent GetUp receipt mutations; no game."""
from copy import deepcopy
import hashlib
import struct
from pathlib import Path
import sys
import contextlib
import io
import tempfile
import unittest
from unittest.mock import patch
from lupa.luajit21 import LuaRuntime

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/dev'))
from native_player_getup import evidence,bootstrap_plan
from native_player_origin import evidence as origin_evidence
from native_gate_relay import Barrier
import build_native_gate_overlay as builder
from test_native_gate_player_origin import context,receipts,render,CONFIG
from test_native_gate_transition_saved import text as transition_text,ENABLE
import test_native_gate_probe as probe_tests

RUN=hashlib.sha256(b'a'*32).hexdigest()
CONFIG_GETUP=CONFIG.replace('"observe"','"agreed-getup"')+'\nnative_gate_world_hash_stride = 1\ntoken = "'+'a'*32+'"\n'
PLAYER=(ROOT/'tools/dev/native_gate_player.lua').read_text()
ORIGIN=(ROOT/'tools/dev/native_gate_player_origin.lua').read_text()

def fixture():
    rows,watches,model=context()
    for row in rows:row['tick']=31
    q=deepcopy(rows[1]);q.update(event=3,kind=4,enter=5,leave=6,queue=1,enqueue=1,queue_valid=1,transfer=1,publication=2,arg=1,
                                old=0,new=0,result=0,s0=deepcopy(rows[1]['s1']),s1=deepcopy(rows[1]['s1']))
    d=deepcopy(q);d.update(event=4,kind=5,enter=7,leave=8,tick=32,caller=0xeacf66)
    rows.extend([q,d]);raw=struct.pack('<QII',0x140000000+0x2c51c38,1,0).hex()
    model=[dict(schema=2,ticket=1,state=3,saved=0xae79c,runtime=123,command=0x90000,agent=0x30000,enqueue=3,dispatch=4,mode=1,before=raw,after=raw,image=0x140000000,raw_world_coverage=0)]
    tickets,edges,stop=receipts()
    edges.append(dict(schema=3,edge=3,ticket=1,transition=3));stop.update(edges=3,edge_rows=3)
    for row in tickets+edges+[stop]:row['schema']=3
    tickets[0].update(post_tick=30,delivery_tick=31,request=1,post_enter=1,post_leave=2,delivery_enter=3,delivery_leave=4,after_tick=31,transition_before=0,transition_after=6)
    stop.update(native_unwinds=0,post_unwinds=0,delivery_unwinds=0)
    log=ENABLE+transition_text(rows,watches,model)+'\n'+render(tickets,edges,stop)
    states={tick:{str(slot):dict(command='87010000' if tick==29 and slot==1 else '00000000') for slot in (1,2)} for tick in range(121)}
    for tick in range(1,121):
        released=tick>=31
        data=f'PLAYER_GETUP_STATE_1:{RUN}:1:{tick}:{int(tick>=30)}:{int(tick>=30)}:{5 if tick>=32 else 4 if released else 2 if tick==30 else 0}:0000000000011e12:00000000000ae79c:{"2c51158" if released else "2c39488"}:0:{0 if released else 1}:1|PLAYER_GETUP_UI_1:0000000000011e12:00000000000ae79f:00000000000ae79c:{"0000000000000000" if released else "00000000000ae79c"}'
        log+=f'\nNATIVE_GATE PLAYER_GETUP_STATE tick={tick} data={data}\nNATIVE_GATE PLAYER_INPUTS tick={tick} data=PLAYER_INPUTS_1:{"87010000" if tick==30 else "00000000"}:00000000'
    log+='\nNATIVE_GATE PLAYER_GETUP_STOP '+f'schema=1 run={RUN} epoch=1 tick=30 last=120 processed=120 claimed=1 accepted=1 calls=1 returns=1 unwinds=0 open=0 stop_open=0 failures=0 closing=1 player=0000000000011e12 ship=00000000000ae79c command1=87010000 command2=00000000 completed_release=0 independent_player=0 atomic_lease=0 queue_exception_cleanup=0 boundary=admitted_ui'
    return log,states

class GetUpTests(unittest.TestCase):
    def test_modeled_request_delivery_and_publication_still_do_not_complete_release(self):
        log,states=fixture();errors=[];samples,inputs,result=evidence(log,1,CONFIG_GETUP,states,errors)
        self.assertEqual(errors,[]);self.assertEqual(len(samples),120);self.assertEqual(len(inputs),120)
        self.assertTrue(result['request_accepted']);self.assertTrue(result['delivery_observed'])
        self.assertFalse(result['completed_releases']);self.assertFalse(result['independent_player_context'])
        self.assertTrue(origin_evidence(log)['scope_containment_serialized'])

    def test_receipt_mutations_refuse_with_valid_positive_baseline(self):
        log,states=fixture()
        for before,after in [('calls=1 returns=1','calls=2 returns=1'),('stop_open=0 failures=0','stop_open=1 failures=0'),
                             ('completed_release=0','completed_release=1'),('request=1','request=2'),('post_tick=30','post_tick=29'),
                             ('transition_after=6','transition_after=3'),('after_tick=31','after_tick=30'),
                             ('schema=3','schema=2'),('delivery_leave=4','delivery_leave=3'),
                             ('post_leave=2','post_leave=4'),
                             ('command2=00000000','command2=87010000'),('boundary=admitted_ui','boundary=pre_solver'),
                             ('PLAYER_INPUTS tick=1 ','PLAYER_INPUTS malformed=1 '),
                             (':31:1:1:4:',':31:1:1:5:'),('queue_exception_cleanup=0 boundary','queue_exception_cleanup=1 boundary')]:
            with self.subTest(before=before):
                errors=[];evidence(log.replace(before,after),1,CONFIG_GETUP,states,errors);self.assertTrue(errors)
        errors=[];changed=deepcopy(states);changed[29]['2']['command']='87010000';evidence(log,1,CONFIG_GETUP,changed,errors);self.assertTrue(errors)
        for tail in ('\nNATIVE_GATE PLAYER_GETUP_UNKNOWN value=1','\nNATIVE_GATE PLAYER_INPUTS tick=1 data=PLAYER_INPUTS_1:00000000:00000000'):
            errors=[];evidence(log+tail,1,CONFIG_GETUP,states,errors);self.assertTrue(errors)

    def test_publication_history_cannot_disappear_or_regress(self):
        log,states=fixture()
        for tick in (31,33):
            with self.subTest(tick=tick):
                prefix=f'NATIVE_GATE PLAYER_GETUP_STATE tick={tick} data='
                old=next(line for line in log.splitlines() if line.startswith(prefix))
                phase='4' if tick==31 else '5'
                changed=old.replace(f':{tick}:1:1:{phase}:',f':{tick}:1:1:3:').replace(
                    ':2c51158:0:0:1|',':2c39488:0:1:1|')
                changed=changed.rsplit(':',1)[0]+':00000000000ae79c'
                errors=[];evidence(log.replace(old,changed),1,CONFIG_GETUP,states,errors)
                self.assertTrue(any('pre-publication context' in e for e in errors))
        # The modeled dispatch at32 cannot be forgotten at33, even while the
        # public ship/controller type remains Nav and returns to phase5 later.
        errors=[];evidence(log.replace(':33:1:1:5:',':33:1:1:4:'),1,CONFIG_GETUP,states,errors)
        self.assertTrue(any('phase regressed' in e for e in errors))

    def probe_runtime(self,backend,diagnostic=False):
        """Fresh real Lua VM/probe/ORIGIN over one retained synthetic native session.

        Native reattachment/ownership is exercised separately by the actual C
        harness; this covers the production Lua reload and cleanup path.
        """
        harness=probe_tests.SnapshotProbeTests();harness.setUp();lua=harness.lua
        _,_,footer=receipts(empty=True)
        footer.update(imports=0,bound=0,native_unwinds=0,post_unwinds=0,delivery_unwinds=0)
        def boot(*args):
            backend['boots']+=1
            if 'boot_args' in backend:self.assertEqual(args,backend['boot_args'])
            else:backend['boot_args']=args
            return 0
        def configure_origin(token):
            backend['origins']+=1;backend['origin_installed']=True;return 1
        def configure_getup(run,contract=None):
            self.assertEqual(contract,"diagnostic-getup-1" if diagnostic else None)
            backend['getups']+=1
            if backend.get('refuse_getup'):return 0
            if 'run' in backend:self.assertEqual(run,backend['run'])
            else:backend['run']=run
            backend['getup_installed']=True;return 1
        def status():
            backend['status_reads']+=1
            current=dict(footer,schema=3 if backend.get('getup_installed') else 2,
                         sites=3 if backend['state']['active'] else 0,closing=0 if backend['state']['active'] else 1)
            return render([],[],current).split('PLAYER_ORIGIN_STOP ',1)[1]
        def stop():
            backend['stops']+=1;backend['state']['active']=0;backend['origin_installed']=False;return 1
        def load_requested():backend['state']['boot_phase']=1;return 1
        def load(slot):self.assertEqual(slot,'save_009');backend['loads']+=1
        callbacks=dict(native_boot=boot,native_origin=configure_origin,native_getup=configure_getup,
                       native_status=status,native_stop=stop,native_poll=lambda:lua.table_from(backend['state']),
                       native_load_requested=load_requested,native_load=load)
        for key,value in callbacks.items():lua.globals()[key]=value
        lua.execute('''
          X4LAN_CONFIG.native_gate_player_probe='agreed-getup'
          X4LAN_CONFIG.native_gate_player_origin_token='1|0000000000011e12|00000000000ae79c|1|120'
          X4LAN_CONFIG.native_gate_world_hash_stride=1
          X4LAN_CONFIG.native_gate_player_transition_observer=true
          X4LAN_CONFIG.native_gate_world_trace_early=true
          X4LAN_CONFIG.native_gate_world_queue_trace=true
          X4LAN_CONFIG.native_gate_world_trace_ticks=12
          X4LAN_CONFIG.native_gate_world_hash_watch={'0000000000000063'}
          local gate=package.loadlib('gate')()
          gate.world_capture=function() error('world capture before Load completed') end
          gate.boot=function(...) return native_boot(...) end
          gate.poll=function() return native_poll() end
          gate.world_trace_config=function() return 1 end
          gate.world_queue_trace_config=function() return 1 end
          gate.player_origin_config=function(token) return native_origin(token) end
          gate.player_getup_config=function(run,contract) return native_getup(run,contract) end
          gate.player_origin_status=function() return native_status() end
          gate.stop=function() return native_stop() end
          gate.load_requested=function() return native_load_requested() end
          LoadGame=function(slot) return native_load(slot) end
          X4LAN_NATIVE_TRANSITION={configure=function() end,drain=function() end}
        ''')
        if diagnostic:
            lua.execute("""
              X4LAN_CONFIG.native_gate_player_probe='diagnostic-getup'
              X4LAN_CONFIG.native_gate_diagnostic_contract='diagnostic-getup-1'
              X4LAN_CONFIG.native_gate_serial_pathing=true;X4LAN_CONFIG.native_gate_world_identities=true
              X4LAN_CONFIG.native_gate_flight_diag=true;X4LAN_CONFIG.native_gate_ai_diag=true
              X4LAN_CONFIG.native_gate_ship_diag=true;X4LAN_CONFIG.native_gate_nav_diag=true
              X4LAN_CONFIG.native_gate_agent_diag=true;X4LAN_CONFIG.native_gate_spline_diag=true
            """)
            lua.execute((ROOT/'tools/dev/native_gate_getup_diagnostic.lua').read_text())
        lua.execute(ORIGIN);lua.execute(probe_tests.SOURCE);return lua

    @staticmethod
    def probe_backend():
        return dict(boots=0,origins=0,getups=0,status_reads=0,stops=0,loads=0,
                    state=dict(active=1,boot_phase=0,issued=0,report_tick=0,rng='0'),
                    claim=dict(processed=0,calls=0,claimed=0))

    def test_fresh_lua_origin_and_probe_reattach_during_load_without_reset_or_reload(self):
        for phase in (1,2):
            with self.subTest(phase=phase):
                backend=self.probe_backend();first=self.probe_runtime(backend)
                first.execute('update();update();update()')
                self.assertEqual(backend['loads'],1);self.assertEqual(backend['stops'],0)
                original_claim=backend['claim'];backend['state']['boot_phase']=phase
                second=self.probe_runtime(backend);self.assertIsNot(first,second)
                second.execute('update();update();update()')
                self.assertEqual((backend['boots'],backend['origins'],backend['getups']),(2,2,2))
                self.assertEqual(backend['loads'],1);self.assertEqual(backend['stops'],0)
                self.assertIs(backend['claim'],original_claim)
                self.assertEqual(original_claim,dict(processed=0,calls=0,claimed=0))
                for lua in (first,second):self.assertFalse(any('ERROR' in line for line in lua.globals().logs.values()))

    def test_actual_probe_partial_getup_configuration_stops_and_drains_origin(self):
        backend=self.probe_backend();backend['refuse_getup']=True;lua=self.probe_runtime(backend)
        lua.execute('update();update();update()')
        self.assertEqual(backend['stops'],1);self.assertEqual(backend['loads'],0)
        self.assertEqual(backend['state']['active'],0);self.assertFalse(backend['origin_installed'])
        self.assertGreater(backend['status_reads'],0);self.assertEqual(backend['claim']['calls'],0)
        lines=list(lua.globals().logs.values())
        self.assertTrue(any('GetUp configuration refused' in line for line in lines))
        self.assertTrue(any('PLAYER_ORIGIN_STOP ' in line and ' schema=2 ' in line for line in lines))
        self.assertFalse(any('ARMED ' in line for line in lines))

    def test_actual_lua_bootstrap_preimage_matches_shared_getup_helper(self):
        backend=self.probe_backend();lua=self.probe_runtime(backend);preimages=[]
        def digest(value):preimages.append(value);return hashlib.sha256(value.encode()).hexdigest()
        lua.globals().actual_digest=digest
        lua.execute('X4LAN_SHA256.digest=function(value) return actual_digest(value) end;update();update()')
        config=CONFIG_GETUP.replace('token = "'+'a'*32+'"','token = "'+'c'*32+'"')
        expected='SNAPSHOT_BYTES_1|'+'a'*64+'|'+bootstrap_plan(config)
        self.assertIn(expected,preimages);self.assertEqual(backend['boot_args'][4],hashlib.sha256(expected.encode()).hexdigest())
        for old,new in [('agreed-getup','diagnostic-getup'),('ae79c','ae79d'),('|1|120','|1|119')]:
            with self.subTest(old=old),self.assertRaises(ValueError):bootstrap_plan(config.replace(old,new))

    def runtime(self):
        lua=LuaRuntime(unpack_returned_tuples=True)
        lua.execute('''
          X4LAN_CONFIG={slot='p1',native_gate_player_probe='agreed-getup',native_gate_player_transition_observer=true,native_gate_world_hash_stride=1,native_gate_player_origin_token='token'}
          controlled=0xae79c;container=0xae79f;processed=0;calls=0;logs={};phase=0
          DebugError=function(s) logs[#logs+1]=s end
          local api={GetPlayerID=function() return 0x11e12 end,GetPlayerContainerID=function() return container end,
            GetPlayerOccupiedShipID=function() return 0xae79c end,GetPlayerControlledShipID=function() return controlled end}
          require=function() return {typeof=function() return true end,cdef=function() end,C=api} end
          gate={saved_identity=function(value) return string.format('%016x',tonumber(value)) end}
          gate.player_getup_batch=function(tick,run,epoch,a,b)
            if stopped or tick~=processed+1 then return 0 end
            assert(run==X4LAN_NATIVE_GETUP_RUN and epoch==1 and b=='00000000' and a==(tick==30 and '87010000' or '00000000'))
            processed=tick;if tick==30 then calls=calls+1;phase=2 end;return 1
          end
          gate.player_getup_state=function(tick)
            return 'PLAYER_GETUP_STATE_1:'..X4LAN_NATIVE_GETUP_RUN..':1:'..tick..':'..(tick>=30 and '1:1' or '0:0')..':'..phase..':0000000000011e12:00000000000ae79c:'..(phase>=4 and '2c51158:0:0:1' or '2c39488:0:1:1')
          end
          X4LAN_NATIVE_GETUP_GATE=gate
        ''')
        lua.globals().X4LAN_NATIVE_GETUP_RUN=RUN;lua.execute(PLAYER);return lua

    def test_actual_lua_full_batch_then_native_persistent_claim_and_context_guard(self):
        lua=self.runtime()
        lua.execute("for t=1,30 do X4LAN_NATIVE_PLAYER_COMMANDS.apply(t,t==30 and '87010000' or '00000000','00000000');X4LAN_NATIVE_PLAYER(t,gate) end")
        self.assertEqual(lua.globals().calls,1)
        lua.execute('phase=4;controlled=0;X4LAN_NATIVE_PLAYER_COMMANDS.apply(31,"00000000","00000000");X4LAN_NATIVE_PLAYER(31,gate)')
        lua.execute(PLAYER) # Lua locals reset, native processed ledger does not.
        with self.assertRaisesRegex(Exception,'claim/action refused'):
            lua.execute('X4LAN_NATIVE_PLAYER_COMMANDS.apply(30,"87010000","00000000")')
        self.assertEqual(lua.globals().calls,1)
        lua=self.runtime();lua.execute('X4LAN_NATIVE_PLAYER_COMMANDS.apply(1,"00000000","00000000");X4LAN_NATIVE_PLAYER(1,gate);controlled=0')
        with self.assertRaisesRegex(Exception,'without publication'):lua.execute('X4LAN_NATIVE_PLAYER(2,gate)')

    def test_authority_conflicts_stop_and_early_world_failure_never_call_public_action(self):
        for a,b in [('87020000','00000000'),('87010100','00000000'),('87010001','00000000'),('87010000','87010000')]:
            lua=self.runtime()
            with self.assertRaisesRegex(Exception,'authority refused'):lua.globals().X4LAN_NATIVE_PLAYER_COMMANDS.apply(30,a,b)
            self.assertEqual(lua.globals().calls,0)
        lua=self.runtime();lua.execute('X4LAN_NATIVE_PLAYER_COMMANDS.apply(1,"00000000","00000000");stopped=true')
        with self.assertRaisesRegex(Exception,'claim/action refused'):lua.execute('X4LAN_NATIVE_PLAYER_COMMANDS.apply(30,"87010000","00000000")')
        self.assertEqual(lua.globals().calls,0)

    def test_actual_barrier_world_tick1_mismatch_stops_before_action_grant(self):
        token='a'*32;barrier=Barrier(token,120,hold_seconds=0)
        def packet(slot,tick,digest):
            clock=struct.pack('>d',20+tick/60).hex()
            return f'NGSTATE|2|{token}|{slot}|{tick}|{clock}|{clock}|{digest}|00000000'.encode()
        barrier.receive(packet(1,0,'a'*64),('127.0.0.1',1),0)
        replies=barrier.receive(packet(2,0,'a'*64),('127.0.0.1',2),0)
        self.assertEqual(len(replies),2);self.assertTrue(all(b'|1|00000000|00000000' in row for _,row in replies))
        lua=self.runtime();lua.execute('X4LAN_NATIVE_PLAYER_COMMANDS.apply(1,"00000000","00000000");X4LAN_NATIVE_PLAYER(1,gate)')
        barrier.receive(packet(1,1,'a'*64),('127.0.0.1',1),1)
        replies=barrier.receive(packet(2,1,'b'*64),('127.0.0.1',2),1)
        self.assertEqual(barrier.failed,'battle_state_mismatch_tick_1')
        self.assertTrue(all(row.startswith(b'NGSTOP|') for _,row in replies));self.assertEqual(barrier.released,0)
        lua.execute('stopped=true');self.assertEqual(lua.globals().calls,0)
        # Even a stale queued proposal cannot advance the failed coordinator.
        self.assertFalse(any(row.startswith(b'NGGO|') for _,row in barrier.receive(packet(1,1,'a'*64),('127.0.0.1',1),2)))

    def test_builder_refuses_getup_without_exact_snapshot_watch_before_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);dll=root/'gate';save=root/'snapshot';dll.write_bytes(b'fixture');save.write_bytes(b'fixture')
            args=['builder','--gate-dll',str(dll),'--snapshot',str(save),'--out',str(root/'refused'),'--mode','battle',
                  '--player-probe','agreed-getup','--player-transition-observer','--serial-pathing','--world-hash-stride','1',
                  '--world-hash-watch','0000000000000063','--world-trace-ticks','12','--world-trace-early','--world-queue-trace']
            with patch.object(sys,'argv',args),contextlib.redirect_stderr(io.StringIO()),self.assertRaises(SystemExit):builder.main()
            self.assertFalse((root/'refused').exists())

    def test_origin_lua_closed_export_retains_footer_before_failure(self):
        log,_=fixture();footer=log.split('PLAYER_ORIGIN_STOP ',1)[1].splitlines()[0]
        lua=LuaRuntime(unpack_returned_tuples=True);lua.globals().footer=footer
        lua.execute('''X4LAN_CONFIG={native_gate_player_origin_token='token',native_gate_player_probe='agreed-getup',token='token'}
          X4LAN_SHA256={digest=function() return 'run' end};logs={};DebugError=function(s) logs[#logs+1]=s end
          gate={player_origin_config=function() return 1 end,player_getup_config=function() return 1 end,
            player_origin_status=function() return footer end,player_getup_status=function() return 'raw-getup' end}
        ''')
        initial=footer.replace('sites=0','sites=3').replace('closing=1','closing=0')
        lua.globals().footer=initial;lua.execute(ORIGIN);lua.execute('X4LAN_NATIVE_ORIGIN.configure(gate)')
        lua.globals().footer=footer.replace('restore_open=0','restore_open=1')
        with self.assertRaisesRegex(Exception,'coverage incomplete'):lua.execute('X4LAN_NATIVE_ORIGIN.drain(gate,true)')
        self.assertTrue(any('PLAYER_ORIGIN_STOP ' in line and 'restore_open=1' in line for line in lua.globals().logs.values()))

    def test_partial_action_config_still_drains_installed_origin(self):
        _,_,footer=receipts(empty=True);footer.update(schema=2,native_unwinds=0,post_unwinds=0,delivery_unwinds=0)
        raw=render([],[],footer).split('PLAYER_ORIGIN_STOP ',1)[1]
        lua=LuaRuntime(unpack_returned_tuples=True);lua.globals().footer=raw
        lua.execute('''X4LAN_CONFIG={native_gate_player_origin_token='token',native_gate_player_probe='agreed-getup',token='token'}
          X4LAN_SHA256={digest=function() return 'run' end};logs={};DebugError=function(s) logs[#logs+1]=s end
          gate={player_origin_config=function() return 1 end,player_getup_config=function() return 0 end,player_origin_status=function() return footer end}
        ''');lua.execute(ORIGIN)
        with self.assertRaisesRegex(Exception,'configuration refused'):lua.execute('X4LAN_NATIVE_ORIGIN.configure(gate)')
        lua.execute('X4LAN_NATIVE_ORIGIN.drain(gate,true)')
        self.assertTrue(any('PLAYER_ORIGIN_STOP schema=' in line or 'PLAYER_ORIGIN_STOP ' in line for line in lua.globals().logs.values()))

if __name__=='__main__':unittest.main()
