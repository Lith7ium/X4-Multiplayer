"""V2 is opt-in; complete physical relabels preserve semantics, partial ones do not."""
import hashlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/dev'))
from native_world_evidence import DOMAINS,configured_schema,decode_detail,evidence,frames,roots
from compare_native_world import compare
from validate_native_gate import validate

V1='WORLD_IMPORTED_1'
V2='WORLD_IMPORTED_2'


def row(read=0,cursor=None,transpose_pending=True,schema=V2,identical_banks=False):
    cursor=read if cursor is None else cursor
    pending=[(101,1001,3),(102,1002,5)]
    banks=[(11.5,0,1),(12.5,1,0)]
    if identical_banks:banks[1]=banks[0]
    if read:
        banks.reverse()
        if transpose_pending:pending.reverse()
    relation=2 if cursor==2 else 0 if cursor==read else 1
    timing=struct.pack('<IdIdB',2,10.5,relation if schema==V2 else cursor,17.25,1)
    timing+=b''.join(struct.pack('<dBB',*banks[p]) for p in (read,1-read))
    if schema==V2:
        timing+=b''.join(struct.pack('<BQQ',int(pending[p][0]!=0),pending[p][1],pending[p][2]) for p in (read,1-read))
    raw=(struct.pack('<II',read,1-read)+struct.pack('<dIdB',10.5,cursor,17.25,1)+
         b''.join(struct.pack('<QQ',runtime,counter) for runtime,saved,counter in pending)+
         b''.join(struct.pack('<Q',saved) for runtime,saved,counter in pending)+
         bytes(int(runtime!=0) for runtime,saved,counter in pending)+b''.join(struct.pack('<dBB',*bank) for bank in banks))
    footer=struct.pack('<IIQ',0x4000,5,0)
    domains={'topology':b'\0'*5+footer,'health':footer,
             'motion':struct.pack('<I',0x2c51158)+b'\0'*186+footer,'timing':timing+footer}
    return {'type':0x4000,'flags':5,'parent':0,'hashes':tuple(hashlib.sha256(domains[d]).digest() for d in DOMAINS),
            'timing_data':domains['timing'],'nav_raw':raw.hex(),'domains':domains,'phase':(read,1-read)}


def object_line(tick,record,schema=V2):
    hashes=' '.join(name+'='+value.hex() for name,value in zip(DOMAINS,record['hashes']))
    text=f'NATIVE_GATE WORLD_OBJECT tick={tick} id=000000000000004d type=4000 flags=5 parent=0000000000000000 {hashes} timing_fields={record["timing_data"][:-16].hex()}'
    return text+(f' nav_raw={record["nav_raw"]}' if schema==V2 else '')+'\n'


def summary(tick,record,changed=1,schema=V2):
    return f'NATIVE_GATE WORLD_HASH tick={tick} objects=1 present=1 changed={changed} unknown_class=0 unknown_motion=0 unknown_references=0 state={schema}:{tick}:1:'+':'.join(roots({77:record}))+'\n'


def banks(tick,record):
    r,w=record['phase']
    return f'NATIVE_GATE WORLD_BANKS tick={tick} read_before={r} write_before={w} read_after={r} write_after={w}\n'


def log(record=None,schema=V2,tick=1):
    record=record or row(schema=schema)
    return banks(tick,record)+object_line(tick,record,schema)+summary(tick,record,schema=schema)


class EnrichedNavTests(unittest.TestCase):
    def parse(self,text,schema=V2):
        errors=[];captured=list(frames(io.StringIO(text),errors,1,expected_schema=schema))
        return errors,captured

    def test_complete_permutation_preserves_roots_and_retains_physical_diagnostics(self):
        a,b=row(),row(1)
        self.assertEqual(roots({77:a}),roots({77:b}))
        self.assertNotEqual(a['nav_raw'],b['nav_raw'])
        for value in (a,b):self.assertEqual(self.parse(log(value))[0],[])
        fields=decode_detail({'timing':a['timing_data']},V2)['timing']
        self.assertEqual(fields['nav.refresh_relation']['value'],0)
        self.assertEqual(fields['bank0.pending_reference']['value'],1001)
        self.assertEqual(fields['bank1.reference_transition_count']['value'],5)

    def test_equal_banks_and_cursor_relation_do_not_hide_untransposed_pending_entries(self):
        a=row(identical_banks=True);b=row(1,transpose_pending=False,identical_banks=True)
        self.assertEqual(a['timing_data'][:45],b['timing_data'][:45])
        self.assertEqual(a['hashes'][:3],b['hashes'][:3])
        self.assertNotEqual(a['hashes'][3],b['hashes'][3])
        for value in (a,b):self.assertEqual(self.parse(log(value))[0],[])

    def test_legacy_raw_cursor_failure_survives_relabel(self):
        a,b=row(schema=V1),row(1,schema=V1)
        self.assertNotEqual(a['hashes'][3],b['hashes'][3])
        self.assertEqual(a['hashes'][:3],b['hashes'][:3])
        for value in (a,b):self.assertEqual(self.parse(log(value,V1),V1)[0],[])
        self.assertEqual(decode_detail({'timing':b['timing_data']})['timing']['nav.refresh_cursor']['value'],1)

    def test_current_opposite_and_sentinel_remain_distinct(self):
        digests=[]
        for cursor in range(3):
            a=row(cursor=cursor);b=row(1,cursor=2 if cursor==2 else 1-cursor)
            self.assertEqual(a['hashes'],b['hashes']);digests.append(a['hashes'][3])
            self.assertEqual(self.parse(log(a))[0],[])
        self.assertEqual(len(set(digests)),3)

    def test_each_canonical_timing_byte_is_bound_to_hash(self):
        original=row()
        for offset in range(79):
            value=dict(original);data=bytearray(value['timing_data']);data[offset]^=1;value['timing_data']=bytes(data)
            with self.subTest(offset=offset):self.assertTrue(self.parse(log(value))[0])

    def test_each_counter_saved_reference_and_timing_raw_byte_is_bound(self):
        original=row();raw=bytes.fromhex(original['nav_raw'])
        offsets=list(range(8,29))+list(range(37,45))+list(range(53,61))+list(range(61,99))
        for offset in offsets:
            value=dict(original);changed=bytearray(raw);changed[offset]^=1;value['nav_raw']=changed.hex()
            with self.subTest(offset=offset):self.assertTrue(self.parse(log(value))[0])

    def test_null_unknown_and_invalid_raw_references_are_distinct(self):
        original=row();raw=bytes.fromhex(original['nav_raw'])
        for physical in (0,1):
            for mode in ('runtime_zero','saved_zero','unknown','invalid'):
                changed=bytearray(raw)
                if mode=='runtime_zero':changed[29+physical*16:37+physical*16]=b'\0'*8
                elif mode=='saved_zero':changed[61+physical*8:69+physical*8]=b'\0'*8
                else:changed[77+physical]=2 if mode=='unknown' else 3
                self.assertTrue(self.parse(log({**original,'nav_raw':changed.hex()}))[0])

    def test_invalid_selector_cursor_and_incomplete_capture_cannot_pass(self):
        original=row();raw=bytes.fromhex(original['nav_raw']);source=log(original)
        bad=[source.replace(banks(1,original),''),source.replace(banks(1,original),banks(1,original)*2),
             source.replace('write_after=1','write_after=0'),source.replace('tick=1 read_before','tick=2 read_before'),
             source.replace(' nav_raw='+original['nav_raw'],''),source.replace(original['nav_raw'],original['nav_raw'][:-2]),
             source.replace('nav_raw='+original['nav_raw'],'nav_raw=-'),source.replace('timing_fields='+original['timing_data'][:-16].hex(),''),
             source.replace('WORLD_IMPORTED_2','WORLD_IMPORTED_1')]
        for offset,number in ((0,2),(4,0),(16,3),(16,0xffffffff)):
            changed=bytearray(raw);struct.pack_into('<I',changed,offset,number);bad.append(log({**original,'nav_raw':changed.hex()}))
        for text in bad:self.assertTrue(self.parse(text)[0])

    def test_raw_only_delta_is_required_when_phase_relabels(self):
        a,b=row(),row(1)
        complete=log(a)+banks(2,b)+object_line(2,b)+summary(2,b)
        self.assertEqual(self.parse(complete)[0],[])
        omitted=log(a)+banks(2,b)+summary(2,b,changed=0)
        self.assertTrue(self.parse(omitted)[0])
        self.assertTrue(self.parse(log(a)+banks(2,b))[0])

    def test_versions_cannot_be_mixed_relabelled_or_enabled_implicitly(self):
        self.assertTrue(self.parse(log(),V1)[0])
        self.assertTrue(self.parse(log(row(schema=V1),V1),V2)[0])
        forged=log(row(schema=V1),V1).replace('WORLD_IMPORTED_1','WORLD_IMPORTED_2').replace('\nNATIVE_GATE WORLD_HASH',' nav_raw=-\nNATIVE_GATE WORLD_HASH')
        self.assertTrue(self.parse(forged,V2)[0])
        self.assertTrue(self.parse(log()+log(row(schema=V1),V1,tick=2),V2)[0])

    def test_config_is_explicit_and_rejects_invalid_or_duplicate_values(self):
        prefix='X4LAN_CONFIG.native_gate_world_hash_schema = '
        stride='\nX4LAN_CONFIG.native_gate_world_hash_stride = 1\n'
        self.assertEqual(configured_schema(''),V1)
        self.assertEqual(configured_schema(prefix+repr(V1)),V1)
        self.assertEqual(configured_schema(prefix+repr(V2)+stride),V2)
        for bad in ('false','nil','2',repr('WORLD_IMPORTED_3'),repr(V2),repr(V2)+stride+prefix+repr(V2),repr(V2)+stride+stride):
            with self.assertRaises(ValueError):configured_schema(prefix+bad)

    def test_compare_uses_configured_schema_and_preserves_other_domain_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            for slot,value in ((1,row()),(2,row(1))):
                client=root/f'client{slot}';(client/'installed').mkdir(parents=True)
                (client/'installed/x4_lan_lab-config.lua').write_text("X4LAN_CONFIG.native_gate_world_hash_schema = 'WORLD_IMPORTED_2'\nX4LAN_CONFIG.native_gate_world_hash_stride = 1\n")
                (client/'engine-output.log').write_text(log(value))
            result=compare(root);self.assertEqual(result['errors'],[]);self.assertIsNone(result['first_difference'])
            value=row(1);value['hashes']=(hashlib.sha256(b'changed topology').digest(),*value['hashes'][1:])
            (root/'client2/engine-output.log').write_text(log(value))
            result=compare(root);self.assertEqual(result['first_difference'],1);self.assertEqual(result['domain_object_counts']['topology'],1)

    def test_stop_failures_and_unknown_coverage_remain_visible(self):
        source=log().replace('unknown_motion=0','unknown_motion=1')+'NATIVE_GATE WORLD_HASH_STOP scans=1 failures=0\n'
        errors=[];states,coverage=evidence(source,1,1,1,errors,expected_schema=V2)
        self.assertEqual(errors,[]);self.assertEqual(coverage[0]['unknown_motion'],1);self.assertEqual(set(states),{1})
        for bad in (source.replace('failures=0','failures=1'),source.replace('scans=1','scans=0')):
            errors=[];evidence(bad,1,1,1,errors,expected_schema=V2);self.assertTrue(errors)
        for stride in (0,-1,18001,1.5):
            errors=[];evidence(source,1,stride,1,errors,expected_schema=V2);self.assertTrue(errors)

    def test_production_validator_requires_enriched_receipt_and_never_claims_native_process_proof(self):
        source=log()+'NATIVE_GATE WORLD_HASH_STOP scans=1 failures=0\n'
        state=summary(1,row()).split(' state=',1)[1].strip()
        mutations=[source,source.replace(' nav_raw='+row()['nav_raw'],''),source.replace(banks(1,row()),''),
                   source.replace(row()['nav_raw'],row()['nav_raw'][:-2]),source.replace('failures=0','failures=1'),
                   source.replace('WORLD_IMPORTED_2','WORLD_IMPORTED_1')]
        with tempfile.TemporaryDirectory() as directory:
            run=Path(directory)
            (run/'result.json').write_text(json.dumps({'verdict':'pass','exit_code':0}))
            states={str(tick):{str(slot):{'game':20+tick/60,'real':20+tick/60,
                'digest':hashlib.sha256(('CLOCK_ONLY_1'+('' if tick==0 else '|'+state)).encode()).hexdigest(),
                'command':'00000000'} for slot in (1,2)} for tick in (0,1)}
            (run/'native-barrier.json').write_text(json.dumps({'states':states,'hold_seconds':3,'verdict':'pass','failure':None}))
            for slot in (1,2):
                client=run/f'client{slot}';(client/'installed').mkdir(parents=True)
                (client/'result.json').write_text(json.dumps({'lane_restored':True}))
                (client/'installed/x4_lan_lab-config.lua').write_text("X4LAN_CONFIG.native_gate_world_hash_schema = 'WORLD_IMPORTED_2'\nX4LAN_CONFIG.native_gate_world_hash_stride = 1\n")
            for i,changed in enumerate(mutations):
                for slot in (1,2):
                    (run/f'client{slot}/engine-output.log').write_text(
                        f'NATIVE_GATE STOP slot=p{slot} completed=1 reason=1 restored=1 wait_game_delta=0 max_dt_error=0\n'+(source if slot==1 else changed))
                result=validate(run,'clock')
                self.assertEqual(result['session_pass'],i==0,result['errors'])
                self.assertEqual(result['evidence_valid'],i==0,result['errors'])
                self.assertFalse(result['independent_native_players'])


if __name__=='__main__':unittest.main()
