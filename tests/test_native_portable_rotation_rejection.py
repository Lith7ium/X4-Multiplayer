"""Passive rejection parser/config/Lua tests; no game or native acceptance."""
from pathlib import Path
import re
from types import SimpleNamespace
import tempfile
import unittest

import native_portable_rotation_rejection as rejection
import test_native_portable_rotation as adapter
from test_native_portable_rotation import status, text, SOURCE
from test_portable_import_capability import reviewed_euler_contract
from test_native_portable import TOKEN, participants
import native_portable_lane as lane


def fixture():
    h={key:'0' for key in rejection.HEADER}
    h.update(schema='x4-rotation-reject-v1',enabled='1',present='1',readout_drained='1',
             capture_scope='failing_stage',context_scope='predicate_only',stage='3',
             predicate=str(rejection.PREDICATES.index('helper')),helper_known='1',helper_result='-4',
             serial='1',role='0',**{'class':'2'},owner='123',thread='123',phase='3',scope_open='1',load_open='1',
             selected0='64',selected1='63',load_calls='1',records='1',bytes='4',attempted_reads='1')
    for key in rejection.HEX:h[key]='0'*16
    h['image_base']='0000000140000000'
    r=dict(schema='x4-rotation-reject-read-v1',index='1',address='000000010000000c',requested_bytes='4',copied_bytes='4',ok='1',data='00000000')
    c=dict(schema='x4-rotation-reject-completion-v1',return_valid='1',raw_return='8877665544332211',unwind='0',primary_failure_after='6',scope_open='0',load_open='1',role0_returns='64',role1_returns='63')
    return [h,r,c]


def lines(rows):return [' '.join(f'{k}={v}' for k,v in row.items()) for row in rows]


class RejectionEvidence(unittest.TestCase):
    def test_closed_schema_and_native_enum_agreement(self):
        parsed=rejection.decode(lines(fixture()))
        self.assertFalse(parsed['acceptance']);self.assertEqual(parsed['header']['selected0'],64)
        header=(Path(__file__).resolve().parents[1]/'src/native/engine/lockstep_rotation_rejection.h').read_text()
        for enum,prefix,expected in [('rotation_reject_stage','RR_',rejection.STAGES),('rotation_reject_predicate','RP_',rejection.PREDICATES)]:
            body=re.search(r'enum '+enum+r'\s*\{([^}]+)\}',header,re.S)[1]
            self.assertEqual([item.strip().removeprefix(prefix).lower() for item in body.split(',')],expected)
        scalar_body=header.split('switch(predicate)',1)[1].split('default:',1)[0]
        self.assertEqual({s.lower() for s in re.findall(r'case RP_([A-Z_]+):',scalar_body)},rejection.SCALAR_PREDICATES)
        for index,row in enumerate(fixture()):
            for key in row:
                changed=fixture();del changed[index][key]
                with self.assertRaises(ValueError):rejection.decode(lines(changed))

    def test_read_failure_overflow_and_completion_are_distinct(self):
        for key,value in [('copied_bytes','0'),('ok','0'),('data','-'),('index','2'),('requested_bytes','5')]:
            rows=fixture();rows[1][key]=value
            with self.assertRaises(ValueError):rejection.decode(lines(rows))
        rows=fixture();rows[1].update(ok='0',copied_bytes='0',data='-');rows[0].update(bytes='0',failed_reads='1')
        self.assertFalse(rejection.decode(lines(rows))['reads'][0]['ok'])
        rows=fixture();rows[1].update(ok='1',requested_bytes='513',copied_bytes='0',data='-');rows[0].update(bytes='0',truncated='1')
        self.assertTrue(rejection.decode(lines(rows))['header']['truncated'])
        rows[0]['truncated']='0'
        with self.assertRaises(ValueError):rejection.decode(lines(rows))
        rows[0]['truncated']='1';rows[1]['requested_bytes']='4'
        with self.assertRaises(ValueError):rejection.decode(lines(rows))
        rows=fixture();rows[2].update(return_valid='0',raw_return='-',unwind='1')
        self.assertFalse(rejection.decode(lines(rows))['completion']['return_valid'])
        for value in ['0','0000000000000000']:
            rows[2]['raw_return']=value
            with self.assertRaises(ValueError):rejection.decode(lines(rows))

    def test_bounds_unknowns_joins_and_no_acceptance(self):
        for key,value in [('records','193'),('bytes','8193'),('stage','99'),('predicate','999'),('role','2'),('class','3'),
                          ('enabled','0'),('helper_result','-11'),('helper_known','0'),('thread','0'),('readout_drained','0'),
                          ('dropped_records','1'),('attempted_reads','0'),('truncated','1'),('helper_result','-0')]:
            rows=fixture();rows[0][key]=value
            with self.assertRaises(ValueError):rejection.decode(lines(rows))
        prefix='[X4LAN] PORTABLE_ROTATION_REJECT slot=1 run=owned agent='+'d'*64+' '
        raw=[prefix+s for s in lines(fixture())]
        self.assertFalse(rejection.evidence(raw,'owned','d'*64)['acceptance'])
        with self.assertRaises(ValueError):rejection.evidence(raw,'foreign','d'*64)
        with self.assertRaises(ValueError):rejection.evidence(raw+raw,'owned','d'*64)
        with self.assertRaises(ValueError):rejection.decode(lines(fixture())[:-1])
        rows=fixture();rows[0].update(stage='2',predicate=str(rejection.PREDICATES.index('configured')),
                                     helper_result='-9',operands_known='1',serial='0',owner='0',phase='0',records='0',bytes='0',attempted_reads='0')
        self.assertFalse(rejection.decode(lines([rows[0],rows[2]]))['acceptance'])

    def test_impossible_attribution_and_expected_identity_refuse(self):
        for mutation in (
            {0:{'operands_known':'1','actual':'0000000000000001','expected':'0000000000000002'}},
            {0:{'helper_result':'0'}},
            {2:{'return_valid':'0','raw_return':'-','unwind':'0'}},
            {0:{'accounted':'1','class':'2'}},
            {0:{'actual':'0000000000000001'}},
            {0:{'primary_failure_at_entry':'7'}}):
            rows=fixture()
            for index,fields in mutation.items():rows[index].update(fields)
            with self.assertRaises(ValueError):rejection.decode(lines(rows))
        for run,agent in [('bad run','bad'),('test','D'*64),('X','d'*64),('', 'd'*64),(None,'d'*64),('test',None)]:
            raw=[f'[X4LAN] PORTABLE_ROTATION_REJECT slot=1 run={run} agent={agent} '+s for s in lines(fixture())]
            with self.assertRaises(ValueError):rejection.evidence(raw,run,agent)
        # The original may return before a later protected operation unwinds.
        rows=fixture();rows[2].update(return_valid='1',unwind='1',primary_failure_after='13')
        self.assertEqual(rejection.decode(lines(rows))['completion']['raw_return'],'8877665544332211')
        # Native helpers can return KEEP_NATIVE (1); it is a nonzero outcome.
        rows[0]['helper_result']='1'
        self.assertFalse(rejection.decode(lines(rows))['acceptance'])

    def test_optional_config_preserves_default_and_requires_euler(self):
        with tempfile.TemporaryDirectory() as temporary:
            run=SimpleNamespace(extra_tokens={'@X4LANE_TOKEN@':TOKEN,'@X4LANE_PORT@':'31001'},
                                lane=SimpleNamespace(profile='45850',profile_dir=Path(temporary)/'45850'),run_id='owned')
            args=(run,reviewed_euler_contract(),participants(),12,120000,600000)
            self.assertNotIn('rotation_rejection_diagnostics',lane.settings_text(*args))
            self.assertIn('rotation_rejection_diagnostics=true',lane.settings_text(*args,rotation_rejection_diagnostics=True))
            with self.assertRaises(ValueError):lane.settings_text(*args,rotation_rejection_diagnostics=1)

    def test_actual_lua_emits_once_and_preserves_primary_failure(self):
        lua=adapter.WindowsEulerAdapter().runtime();g=lua.globals();g.X4MP_PORTABLE_CONFIG.rotation_rejection_diagnostics=True
        g.rejection_rows=lua.table_from(lines(fixture()))
        lua.execute('native.portable_rotation_rejection=function(i)return rejection_rows[i+1]end')
        lua.execute(SOURCE);lua.execute('close_status()')
        value=status();value.update(failure=6,complete=0,phase=5)
        g.math_status=text(value);lua.execute('X4MP_PORTABLE_BACKEND.observe();X4MP_PORTABLE_BACKEND.observe()')
        logs=list(g.logs.values());parsed=rejection.evidence(logs,'test','d'*64)
        self.assertEqual(parsed['completion']['raw_return'],'8877665544332211')
        self.assertEqual(sum('PORTABLE_ROTATION_REJECT slot=' in row for row in logs),3)
        self.assertIn('PORTABLE_NATIVE TERMINAL result=failure',logs[-1])
        for malformed in ('nil',"'bogus'"):
            lua=adapter.WindowsEulerAdapter().runtime();g=lua.globals();g.X4MP_PORTABLE_CONFIG.rotation_rejection_diagnostics=True
            lua.execute('native.portable_rotation_rejection=function(i)return '+malformed+' end')
            lua.execute(SOURCE);lua.execute('close_status()');g.math_status=text(value)
            lua.execute('for i=1,601 do X4MP_PORTABLE_BACKEND.observe() end')
            logs=list(g.logs.values());self.assertEqual(sum('REJECT_ERROR' in row for row in logs),1)
            self.assertTrue(any('TERMINAL result=failure' in row for row in logs))

    def test_fatal_reload_only_attempts_copied_readout_once_without_callback_takeover(self):
        for drained in (False,True):
            lua=adapter.WindowsEulerAdapter().runtime();g=lua.globals()
            g.X4MP_PORTABLE_CONFIG.rotation_rejection_diagnostics=True
            g.rejection_rows=lua.table_from(lines(fixture()));g.drained=drained
            lua.execute('''
              rejection_calls=0;callback_writes=0
              SetScript=function()callback_writes=callback_writes+1;error('unexpected callback takeover')end
              native.portable_rotation_rejection=function(i)
                rejection_calls=rejection_calls+1
                if drained then return rejection_rows[i+1] end
              end
            ''')
            lua.execute(SOURCE);lua.execute('math_ok=0;X4MP_PORTABLE_BACKEND=nil')
            value=status();value.update(failure=6,complete=0,phase=5);g.math_status=text(value)
            with self.assertRaises(Exception):lua.execute(SOURCE)
            logs=list(g.logs.values());self.assertEqual(g.cancels,1);self.assertEqual(g.callback_writes,0)
            self.assertIsNone(g.X4MP_PORTABLE_BACKEND)
            self.assertEqual(g.rejection_calls,3 if drained else 1)
            self.assertTrue(any('STARTUP_REFUSED' in row for row in logs))
            self.assertTrue(any('PORTABLE_BOOT CANCEL' in row for row in logs))
            self.assertFalse(any('TERMINAL result=success' in row for row in logs))
            if drained:self.assertFalse(rejection.evidence(logs,'test','d'*64)['acceptance'])
            else:
                self.assertTrue(any('REJECT_ERROR' in row and 'reason=unavailable' in row for row in logs))
                g.drained=True # No scheduled later retry or automatic acceptance exists.
                self.assertEqual(g.rejection_calls,1)

    def test_normal_observer_can_drain_copied_records_after_primary_terminal(self):
        lua=adapter.WindowsEulerAdapter().runtime();g=lua.globals();g.X4MP_PORTABLE_CONFIG.rotation_rejection_diagnostics=True
        g.rejection_rows=lua.table_from(lines(fixture()))
        lua.execute('drained=false;native.portable_rotation_rejection=function(i)if drained then return rejection_rows[i+1]end end')
        lua.execute(SOURCE);lua.execute('close_status()');value=status();value.update(failure=6,complete=0,phase=5);g.math_status=text(value)
        lua.execute('X4MP_PORTABLE_BACKEND.observe()')
        self.assertFalse(any('PORTABLE_ROTATION_REJECT ' in row for row in g.logs.values()))
        lua.execute('drained=true;X4MP_PORTABLE_BACKEND.observe();X4MP_PORTABLE_BACKEND.observe()')
        logs=list(g.logs.values());self.assertFalse(rejection.evidence(logs,'test','d'*64)['acceptance'])
        self.assertEqual(sum('PORTABLE_NATIVE TERMINAL ' in row for row in logs),1)
