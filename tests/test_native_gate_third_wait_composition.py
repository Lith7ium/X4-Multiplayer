"""Exact new composition; legacy Find and its seed remain unchanged."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import sys
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/dev'))
import native_world_find_rng as find
import native_world_wait_rng as rng
from native_world_wait import evidence as wait_evidence
from native_gate_wait_manifest import selected_time_bits, double_bits
import test_native_gate_world_find_rng as old
import test_native_gate_world_wait_policy as policy_tests
import test_native_gate_probe as probe_tests


def config(document):
    policy, observed = find.composition_waits(document)
    waits = ';'.join(','.join([e['id'],str(e['version']),str(e['index']),selected_time_bits(e),
                    double_bits(e['minimum']),double_bits(e['maximum']),e['name']]) for e in observed['entries'])
    return '\n'.join('X4LAN_CONFIG.'+k+' = '+json.dumps(v) for k,v in {
        'native_gate_snapshot_sha':document['snapshot_sha256'],
        'native_gate_world_wait_manifest':waits,
        'native_gate_world_wait_rng_manifest':rng.manifest_wire(policy,observed),
        'native_gate_world_find_rng_manifest':find.manifest_wire(document)}.items())+'\n'


def fixture():
    document=find.selection(2);policy,observed=find.composition_waits(document)
    with mock.patch.object(policy_tests,'EXPERIMENT',policy):parts,receipts,stop=policy_tests.sample()
    stop.update(applied=3,observed=0)
    wait_text=policy_tests.combined(parts,receipts,stop)
    errors=[];waits,_=wait_evidence(wait_text,observed['entries'],errors,rng_configured=True,rng_policies=rng.policies(policy))
    assert not errors,errors
    rows,actions,values,_=old.fixture()
    local=waits[1]['raw'][1]
    keys=('context','lifetime','entity','runtime','script','exec','program')
    for kind,row in rows:
        if kind=='IDENTITY':row.update({k:local[k] for k in keys});row['backlink']=local['context']
    for phase in ('before','after'):
        actions[0][phase].update({k:local[k] for k in keys});actions[0][phase]['frame']=local['exec']+0x38
    values[2]['event']['c']=local['exec']+0x38;values[3]['event']['b']=local['exec']+0x38
    rows[-1][1].update(schema=2,composition=find.THIRD_CONTRACT_SHA)
    return document,rows,actions,values,waits,wait_text


def read(data):
    document,rows,actions,values,waits,text=data;errors=[]
    result=find.evidence(old.log(rows)+'\n'+text,document,actions,values,waits,errors,'composed')
    return result,errors


class ThirdWaitCompositionTests(unittest.TestCase):
    def test_explicit_identity_keeps_legacy_and_key_bytes(self):
        a,b=find.selection(),find.selection(2)
        self.assertEqual(find.key_bytes(a),find.key_bytes(b))
        self.assertEqual(hashlib.sha256(find.key_bytes(b)).hexdigest(),a['entries'][0]['expected_key_sha256'])
        self.assertEqual(len(find.key_bytes(b)),179)
        self.assertTrue(find.manifest_wire(a).startswith(find.PREFIX))
        self.assertTrue(find.manifest_wire(b).startswith(find.THIRD_PREFIX))
        self.assertTrue(find.manifest_wire(b).endswith(find.THIRD_CONTRACT_SHA+';'))
        self.assertEqual(find.configured(config(b)),b)
        p,o=find.composition_waits(b)
        self.assertEqual([rng.seed(e,p['snapshot_sha256']) for e in p['entries']],
                         [0x41aa3ad655ad9a9c,0x8b0f91e8910d9f7c,0x4cf7dda6c67c9349])
        # The independently frozen candidate is a CRLF file; builder pins raw
        # bytes, separately from this descriptor's canonical JSON digest.
        raw=(json.dumps(p,indent=2)+'\n').replace('\n','\r\n').encode('utf8')
        self.assertEqual(hashlib.sha256(raw).hexdigest(),b['existing_wait_policy_sha256'])

    def test_config_requires_exact_composed_wire_and_observation(self):
        text=config(find.selection(2))
        for a,b in [('3,intervene,70,71,0,6.98333','3,observe,6.98333'),
                    ('3,intervene,70,71,0,6.98333','3,intervene,70,72,0,6.98333'),
                    ('401beeee0f3cb3e6','401beeee0f3cb3e5'),
                    (find.THIRD_CONTRACT_SHA,find.CONTRACT_SHA)]:
            with self.subTest(a=a),self.assertRaises(ValueError):find.configured(text.replace(a,b))
        for key in ('native_gate_snapshot_sha','native_gate_world_wait_rng_manifest','native_gate_world_wait_manifest'):
            lines=text.splitlines();line=next(x for x in lines if key+' ' in x)
            for bad in (text.replace(line+'\n',''),text+line+'\n'):
                with self.subTest(key=key),self.assertRaises(ValueError):find.configured(bad)
        for key,value in [('schema',True),('schema',3),('composition','other'),('existing_wait_policy_sha256','0'*64)]:
            d=find.selection(2);d[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):find.manifest_wire(d)
        for key in ('saved_pilot','script','version','ordinal'):
            d=find.selection(2);d['entries'][0][key]=None
            with self.assertRaises(ValueError):find.manifest_wire(d)

    def test_requires_three_actual_scoped_receipts_and_same_pair_composition(self):
        data=fixture();result,errors=read(data);self.assertEqual(errors,[]);self.assertTrue(result['selected_scope_valid'])
        self.assertEqual(result['composed_wait']['stop']['applied'],3)
        for mutation in ('missing','observed','not_restored','deadline_selector','raw_invalid','wrong_stop'):
            d=list(deepcopy(data))
            if mutation=='missing':d[-1]=''
            if mutation=='observed':d[-1]=d[-1].replace('policy=1','policy=2')
            if mutation=='not_restored':d[-1]=d[-1].replace('restored=1','restored=0')
            if mutation=='deadline_selector':d[4][2]['raw'][0]['time']^=1
            if mutation=='raw_invalid':d[4][2]['validation_errors']=['invalid selected Wait']
            if mutation=='wrong_stop':d[1][-1][1]['composition']=find.CONTRACT_SHA
            with self.subTest(mutation=mutation):
                refused,err=read(d);self.assertTrue(err);self.assertFalse(refused['selected_scope_valid'])
        legacy=old.fixture();err=[];other=find.evidence(old.log(legacy[0]),find.selection(),*legacy[1:],err,'legacy')
        self.assertEqual(err,[])
        err=[];paired=find.paired_diagnostics([dict(slot=1,evidence=result,errors=[]),dict(slot=2,evidence=other,errors=[])],True,err)
        self.assertFalse(paired['selected_scope_valid']);self.assertTrue(err)

    def test_luajit_passes_exact_new_wire_in_existing_bootstrap_order(self):
        harness=probe_tests.SnapshotProbeTests();harness.setUp();lua=harness.lua
        document=find.selection(2);wire=find.manifest_wire(document)
        lua.execute(config(document));lua.globals().expected_find_wire=wire
        lua.execute('''
          X4LAN_CONFIG.native_gate_world_trace_early=true;X4LAN_CONFIG.native_gate_world_trace_ticks=12
          X4LAN_CONFIG.native_gate_world_hash_watch={'0000000000000063'}
          X4LAN_CONFIG.native_gate_world_action_manifest='99,4,move.idle';X4LAN_CONFIG.native_gate_world_values=true
          local gate=package.loadlib('gate')();calls=0;order={}
          gate.world_trace_config=function() return 1 end
          gate.world_wait_trace_config=function() order[#order+1]='wait';return 1 end
          gate.world_wait_rng_config=function(s) assert(s:find('3,intervene,70,71,0,6.98333',1,true));order[#order+1]='rng';return 1 end
          gate.world_action_trace_config=function() order[#order+1]='action';return 1 end
          gate.world_values_config=function() order[#order+1]='values';return 1 end
          gate.world_find_rng_config=function(s) assert(s==expected_find_wire and table.concat(order,',')=='wait,rng,action,values');calls=calls+1;order={};return 1 end
          gate.world_trace_row=function() return 0 end;gate.world_action_trace_row=gate.world_trace_row
          gate.world_wait_trace_row=gate.world_trace_row;gate.world_wait_rng_row=gate.world_trace_row
          gate.world_values_row=gate.world_trace_row;gate.world_values_unknown_row=gate.world_trace_row
          gate.world_values_unknown_identity_row=gate.world_trace_row;gate.world_find_rng_row=gate.world_trace_row
          gate.world_values_cursors=function() return 0,0,0 end
          setmetatable(state,{__index=function() return 0 end})
        ''')
        lua.execute(probe_tests.SOURCE);lua.execute('update();update();update()');self.assertEqual(lua.globals().calls,1)
        lua.execute("state.boot_phase=3;state.issued=1;state.input1='00000000';state.input2='00000000';is_menu=false")
        lua.execute(probe_tests.SOURCE);lua.execute('update()');self.assertEqual(lua.globals().calls,2)
        self.assertFalse(any('ERROR' in x for x in lua.globals().logs.values()))


if __name__=='__main__':unittest.main()
