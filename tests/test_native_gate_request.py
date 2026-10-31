"""Read-only native request observation requires complete ownership and receipts."""
from pathlib import Path
import subprocess
import sys
import unittest
from lupa.luajit21 import LuaRuntime

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/dev'))
from native_request_evidence import evidence, REQUEST_SIZES, CALLERS


def fixture(actors=(1,2),scope_change=None):
    lines=['NATIVE_GATE PLAYER_REQUEST_ENABLED tick=1 schema=2 input_changes=0 early_coverage=0 generation_coverage=0 native_faction_coverage=0 raw_bank_runtime_refs=1']
    for actor in actors:
        owner=f'{actor+100:016x}'
        lines.append(f'NATIVE_GATE WORLD_ACTOR id={actor} saved={owner}')
        for phase in (0,1):
            row=dict(index=actor,phase=phase,tick=2,actor=actor,owner=owner,ordinal=1,role=3,valid=1,
                     requests=6,other_requests=0,other_first_caller='00000000',model_same=1,solver_same=1,read=0,write=1,model_present=1,
                     model_rva='02c4e9c8',solver_present=1,main=0,main_capacity=0,pending=0,
                     pending_capacity=0,unknown_refs=0,resource_refs=0,banks='00'*0x340,
                     params='00'*0x140,model='00'*16,main_data='',pending_data='')
            if scope_change:scope_change(row)
            text=' '.join(f'{k}={v}' for k,v in row.items());parts=(len(text)+767)//768
            for part in range(1,parts+1):
                lines.append(f'NATIVE_GATE PLAYER_REQUEST_SCOPE_CHUNK index={actor} phase={phase} part={part} parts={parts} bytes={len(text)} data='+text[(part-1)*768:part*768])
        for ordinal in range(1,7):
            row=dict(index=(actor-1)*6+ordinal,tick=2,actor=actor,owner=owner,agent_ordinal=1,
                     ordinal=ordinal,role=3,group=(ordinal-1)//3,axis=(ordinal-1)%3,
                     caller=CALLERS[ordinal-1],value='0000000000000000',epsilon='3f1a36e2eb1c432d',valid=1)
            row.update({k:'00'*n for k,n in REQUEST_SIZES.items()})
            lines.append('NATIVE_GATE PLAYER_REQUEST_EVENT '+' '.join(f'{k}={v}' for k,v in row.items()))
        lines.append(f'NATIVE_GATE PLAYER_SOLVER_EVENT tick=2 actor={actor} kind=1 ordinal=1 solve_tick=0 solve_ordinal=0 owner={owner} m1='+('00'*160))
    lines.append(f'NATIVE_GATE PLAYER_REQUEST_STOP schema=2 count={len(actors)*6} scopes={len(actors)} failures=0 excluded=19 open=0 peak=2 restore_open=0 restored=1 active=0 early_coverage=0 generation_coverage=0 native_faction_coverage=0 raw_bank_runtime_refs=1 emitted={len(actors)*6} emitted_scopes={len(actors)}')
    return '\n'.join(lines)


class RequestEvidenceTests(unittest.TestCase):
    def validate(self,text):
        errors=[];trace,summary=evidence(text,1,2,errors);return errors,trace,summary

    def test_native_requests_link_to_two_saved_agent_publications_without_leases(self):
        errors,trace,summary=self.validate(fixture())
        self.assertEqual(errors,[]);self.assertEqual(len(trace[0]),12)
        self.assertEqual(len(summary['matching_published_owners']),2)
        self.assertEqual(summary['excluded_calls'],19)
        self.assertFalse(summary['pilot_ownership_proved']);self.assertFalse(summary['control_writes'])
        self.assertFalse(summary['complete_generation_coverage'])

    def test_ownership_abi_counts_bounds_and_restore_fail_closed(self):
        for before,after in [('restored=1','restored=0'),('open=0','open=1'),('restore_open=0','restore_open=1'),('failures=0','failures=1'),
                             ('valid=1','valid=0'),('requests=6','requests=3'),('ordinal=6','ordinal=7'),
                             ('00ec91cc','00ec91cd'),('3f1a36e2eb1c432d','3f1a36e2eb1c432e'),
                             ('main=0','main=17'),('resource_refs=0','resource_refs=1'),
                             ('owner=0000000000000065','owner=0000000000000066'),('phase=1','phase=0')]:
            with self.subTest(before=before):self.assertTrue(self.validate(fixture().replace(before,after))[0])

    def test_actual_request_target_bytes_remain_strict_cross_copy_data(self):
        baseline=self.validate(fixture())[1]
        altered=fixture().replace('after='+'00'*48,'after='+'01'+'00'*47,1)
        errors,trace,_=self.validate(altered)
        self.assertEqual(errors,[]);self.assertNotEqual(trace,baseline)

    def test_agent_publication_tamper_is_not_proof(self):
        # Change the write-bank pose for one of the two owners.
        def change(row):row['banks']='00'*0x1a0+'01'+'00'*(0x1a0-1)
        self.assertTrue(self.validate(fixture(scope_change=change))[0])

    def test_two_other_matching_owners_cannot_hide_a_publication_failure(self):
        text=fixture((1,2,3))
        old='m1='+'00'*160
        new='m1='+'00'*64+'01'+'00'*95
        self.assertTrue(self.validate(text.replace(old,new,1))[0])

    def test_scope_chunk_loss_duplicate_reorder_and_length_fail_closed(self):
        text=fixture();lines=text.splitlines()
        chosen=next(i for i,line in enumerate(lines) if 'SCOPE_CHUNK ' in line and ' part=2 ' in line)
        for changed in (lines[:chosen]+lines[chosen+1:], lines[:chosen]+[lines[chosen]]+lines[chosen:],
                        lines[:chosen]+[lines[chosen+1],lines[chosen]]+lines[chosen+2:]):
            self.assertTrue(self.validate('\n'.join(changed))[0])
        self.assertTrue(self.validate(text.replace(' part=2 ',' part=22 ',1))[0])
        self.assertTrue(self.validate(text.replace(' data=',' data=x',1))[0])

    def test_independent_observer_capacity_limits(self):
        text=fixture()
        for old,new in [('index=1 phase=0','index=2049 phase=0'),
                        ('index=1 tick=2','index=16385 tick=2'),
                        ('count=12 scopes=2','count=16385 scopes=2'),
                        ('count=12 scopes=2','count=12 scopes=2049')]:
            with self.subTest(new=new):self.assertTrue(self.validate(text.replace(old,new,1))[0])

    def test_request_cli_requires_both_observer_layers(self):
        result=subprocess.run([sys.executable,str(ROOT/'tools/dev/build_native_gate_overlay.py'),
            '--gate-dll','missing.dll','--snapshot','missing.xml.gz','--mode','battle',
            '--player-request-observer','--out','unused'],capture_output=True,text=True)
        self.assertEqual(result.returncode,2);self.assertIn('requires --player-solver-observer',result.stderr)

    def test_lua_drains_both_scope_phases_and_requires_restoration(self):
        lua=LuaRuntime(unpack_returned_tuples=True)
        source=(ROOT/'tools/dev/native_gate_player.lua').read_text()
        start=source.index('local function request_trace(');end=source.index('local function id(value)',start)
        lua.execute("cfg={native_gate_player_request_observer=true}; request_observed=0;request_scopes=0;logs={};previous_calls=0; DebugError=function(s) logs[#logs+1]=s end; X4LAN_NATIVE_PLAYER_TRACE=function() previous_calls=previous_calls+1 end")
        lua.execute(source[start:end])
        lua.execute("""
          gate={player_request_event=function(index)
            if index>0 then return 'request' end
            return 'schema=2 count=1 scopes=1 failures=0 excluded=19 open=0 peak=2 restore_open=0 restored=1 active=0 early_coverage=0 generation_coverage=0 native_faction_coverage=0 raw_bank_runtime_refs=1'
          end,player_request_scope=function(index,phase) assert(index==1);return 'scope '..phase end}
          X4LAN_NATIVE_PLAYER_TRACE(gate,true)
          assert(request_observed==1 and request_scopes==1 and #logs==4 and previous_calls==1)
          request_observed=0;request_scopes=0;logs={}
          gate.player_request_scope=function() return string.rep('x',12500) end
          X4LAN_NATIVE_PLAYER_TRACE(gate,true)
          assert(#logs==36)
          for _,line in ipairs(logs) do assert(#line<1000) end
          gate.player_request_event=function() return 'schema=2 count=1 scopes=1 failures=0 excluded=19 open=1 peak=2 restore_open=0 restored=1 active=0' end
          assert(not pcall(X4LAN_NATIVE_PLAYER_TRACE,gate,true))
          gate.player_request_event=function() return 'schema=2 count=1 scopes=1 failures=0 excluded=19 open=0 peak=2 restore_open=1 restored=1 active=0' end
          assert(not pcall(X4LAN_NATIVE_PLAYER_TRACE,gate,true))
        """)


if __name__=='__main__':unittest.main()
