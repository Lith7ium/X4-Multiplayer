"""Native solver evidence retains input/output/publication and coverage limits."""
from pathlib import Path
import sys
import unittest
import subprocess
from lupa.luajit21 import LuaRuntime

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/dev'))
from native_solver_evidence import evidence, SIZES
from analyze_native_solver import records, compare


def event(index,actor,kind):
    owner = f'{123 if actor == 0 else 456:016x}'
    row = dict(index=index,tick=2,kind=kind,actor=actor,owner=owner,
               ordinal=1 if kind==1 else 0,role=1 if actor==0 else 3,
               bracket=1 if actor==0 else 2,caller=('00d93559' if actor==0 else '00ec92c5') if kind==1 else '00000000',
               result=1 if kind==1 else 0,valid=1,solve_tick=2 if kind==2 else 0,solve_ordinal=1 if kind==2 else 0)
    row.update({k:'0'*(size*2) for k,size in SIZES.items()})
    if kind==1:row['m1']='0'*128+'01'*96
    else:row['published']='01'*96
    return ' '.join(f'{k}={v}' for k,v in row.items())


def log():
    rows = [event(1,0,1),event(2,1,1),event(3,1,2),event(4,0,2)]
    return ('[X4LAN] NATIVE_GATE PLAYER_SOLVER_ENABLED tick=1 schema=1 early_coverage=0 input_changes=0\n'
            '[X4LAN] NATIVE_GATE WORLD_ACTOR id=1 saved=00000000000001c8\n'+
            '\n'.join('[X4LAN] NATIVE_GATE PLAYER_SOLVER_EVENT '+row for row in rows)+
            '\n[X4LAN] NATIVE_GATE PLAYER_SOLVER_STOP schema=1 count=4 failures=0 excluded=99 open=0 restored=1 active=0 early_coverage=0 emitted=4')


class SolverEvidenceTests(unittest.TestCase):
    def test_interpretation_retains_raw_failure_and_distinguishes_adjacent_fields(self):
        original=records(log());key=next(key for key in original if key[2]==1)
        for offset,unwritten in ((0x25,True),(0x35,True),(0x3c,True),(0x24,False),(0x34,False),(0x3b,False),(0x78,False)):
            with self.subTest(offset=offset):
                changed={k:dict(row) for k,row in original.items()}
                raw=bytearray.fromhex(changed[key]['c0']);raw[offset]=1;changed[key]['c0']=raw.hex()
                report=compare(original,changed)
                self.assertFalse(report['raw_equal'])
                self.assertEqual(report['first_raw_difference']['only_constructor_unwritten_header_bytes'],unwritten)
                self.assertEqual(bool(report['differences_outside_constructor_unwritten_ranges']),not unwritten)
                self.assertTrue(report['unwritten_ranges_are_not_a_session_acceptance_rule'])

    def test_invalid_context_is_a_diagnostic_failure(self):
        for context in (None,(),[],['bad']*4,'000000000000007b',(None,)*4):
            with self.subTest(context=context):
                errors=[];_,summary=evidence(log(),1,context,2,errors)
                self.assertTrue(errors);self.assertFalse(summary['matching_published_owners'])

    def test_cli_requires_serial_pathing_before_building(self):
        result=subprocess.run([sys.executable,str(ROOT/'tools/dev/build_native_gate_overlay.py'),
            '--gate-dll','missing.dll','--snapshot','missing.xml.gz','--mode','battle',
            '--player-probe','observe','--player-observer','--flight-diag','--player-solver-observer',
            '--out','unused-output'],capture_output=True,text=True)
        self.assertEqual(result.returncode,2)
        self.assertIn('requires --serial-pathing',result.stderr)

    def validate(self,text):
        errors=[];trace,summary=evidence(text,1,('000000000000007b',)*4,2,errors)
        return errors,trace,summary

    def test_distinct_existing_owners_and_native_publication(self):
        errors,trace,summary=self.validate(log())
        self.assertEqual(errors,[]);self.assertEqual(len(trace),4)
        self.assertEqual(summary['matching_published_owners'],['000000000000007b','00000000000001c8'])
        self.assertEqual(summary['excluded_calls'],99)
        self.assertFalse(summary['independent_player_contexts'])
        self.assertFalse(summary['early_coverage'])

    def test_failures_missing_lifecycle_and_ownership_are_rejected(self):
        changes=[('restored=1','restored=0'),('open=0','open=1'),('failures=0','failures=1'),
                 ('index=1','index=2'),('valid=1','valid=0'),('caller=00d93559','caller=00d93558'),
                 ('actor=1 owner=00000000000001c8','actor=1 owner=000000000000007b'),
                 ('solve_tick=2','solve_tick=1'),('emitted=4','emitted=3'),
                 ('bracket=2','bracket=0'),('result=1','result=2'),('kind=1','kind=3')]
        for before,after in changes:
            with self.subTest(before=before):self.assertTrue(self.validate(log().replace(before,after))[0])

    def test_actual_publication_tamper_is_not_acceptance(self):
        self.assertTrue(self.validate(log().replace('published='+'01'*96,'published='+'02'*96))[0])

    def test_native_input_change_remains_in_cross_client_comparison(self):
        before=self.validate(log())[1]
        changed=log().replace('c0='+'00'*504,'c0='+'01'*504,1)
        errors,after,_=self.validate(changed)
        self.assertFalse(errors);self.assertNotEqual(before,after)

    def test_worker_record_allocation_order_is_local(self):
        text=log().replace(event(1,0,1),event(1,1,1)).replace(event(2,1,1),event(2,0,1))
        self.assertEqual(self.validate(log())[1],self.validate(text)[1])

    def test_lua_trace_drains_and_checks_restoration(self):
        lua=LuaRuntime(unpack_returned_tuples=True)
        source=(ROOT/'tools/dev/native_gate_player.lua').read_text()
        start=source.index('if cfg.native_gate_player_observer then\n X4LAN_NATIVE_PLAYER_TRACE')
        end=source.index('local function id(value)',start)
        lua.execute("cfg={native_gate_player_observer=true,native_gate_player_solver_observer=true}; observed=0; solver_observed=0; logs={}; DebugError=function(s) logs[#logs+1]=s end")
        lua.execute(source[start:end])
        lua.globals().row=event(1,0,1)
        lua.execute("""gate={player_observe=function() return 'count=0 failures=0 restored=1 active=0' end,
          player_solver_event=function(index) if index==1 then return row end
          return 'schema=1 count=1 failures=0 excluded=4 open=0 restored=1 active=0 early_coverage=0' end}
          X4LAN_NATIVE_PLAYER_TRACE(gate,true)
          assert(solver_observed==1 and #logs==3)
          gate.player_solver_event=function() return 'schema=1 count=1 failures=0 excluded=4 open=1 restored=1 active=0 early_coverage=0' end
          assert(not pcall(X4LAN_NATIVE_PLAYER_TRACE,gate,true))
        """)


if __name__=='__main__':unittest.main()
