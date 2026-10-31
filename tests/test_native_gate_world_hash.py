"""Sparse world evidence must independently reproduce every SHA-256 root."""
import hashlib
from pathlib import Path
import sys
import struct
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/dev'))
from native_world_evidence import DOMAINS,evidence,frames,roots,decode_detail,configured_watches


class WorldHashTests(unittest.TestCase):
    def row(self,identity,value):
        return {'type':123,'flags':1,'parent':0,'hashes':tuple(hashlib.sha256((value+domain).encode()).digest() for domain in DOMAINS)}

    def object(self,tick,identity,row):
        hashes=' '.join(name+'='+value.hex() for name,value in zip(DOMAINS,row['hashes']))
        return f'NATIVE_GATE WORLD_OBJECT tick={tick} id={identity:016x} type=7b flags=1 parent=0000000000000000 {hashes}\n'

    def summary(self,tick,objects,changed):
        return f'NATIVE_GATE WORLD_HASH tick={tick} objects={len(objects)} present={len(objects)} changed={changed} unknown_class=0 unknown_motion=0 unknown_references=0 state=WORLD_IMPORTED_1:{tick}:{len(objects)}:'+':'.join(roots(objects))+'\n'

    def fixture(self):
        first={1:self.row(1,'a'),2:self.row(2,'b')};second={**first,2:self.row(2,'c')}
        return ''.join(self.object(1,k,v) for k,v in first.items())+self.summary(1,first,2)+self.object(60,2,second[2])+self.summary(60,second,1)+'NATIVE_GATE WORLD_HASH_STOP scans=2 failures=0\n'

    def test_sparse_delta_and_stable_sorted_identity(self):
        errors=[];states,coverage=evidence(self.fixture(),1,60,60,errors)
        self.assertEqual(errors,[]);self.assertEqual(set(states),{1,60});self.assertEqual(coverage[-1]['present'],2)
        objects={2:self.row(2,'b'),1:self.row(1,'a')}
        self.assertEqual(roots(objects),roots(dict(reversed(list(objects.items())))))

    def test_missing_tampered_duplicate_and_truncated_rows_fail(self):
        source=self.fixture();first=source.splitlines(keepends=True)[0]
        bad=[source.replace(first,''),source.replace(first,first+first),source.replace('changed=1','changed=2'),
             source.replace('scans=2','scans=1'),source.replace('failures=0','failures=1'),
             source.replace('tick=60 objects','tick=61 objects'),source.replace('topology=','topology=0',1),
             source.rsplit('NATIVE_GATE WORLD_HASH tick=60',1)[0]]
        original=self.row(1,'a')['hashes'][0].hex()
        bad.append(source.replace(original,('0' if original[0]!='0' else '1')+original[1:],1))
        for log in bad:
            with self.subTest(log=log[:100]):
                errors=[];evidence(log,1,60,60,errors);self.assertTrue(errors)

    def test_incomplete_reference_coverage_is_retained(self):
        errors=[];_,coverage=evidence(self.fixture().replace('unknown_references=0','unknown_references=1'),1,60,60,errors)
        self.assertEqual(errors,[]);self.assertEqual(coverage[0]['unknown_references'],1)

    def detail_fixture(self):
        footer=struct.pack('<IIQ',123,1,0)
        domains={name:b'\0'*4+footer for name in DOMAINS}
        row={'type':123,'flags':1,'parent':0,'hashes':tuple(hashlib.sha256(domains[name]).digest() for name in DOMAINS)}
        details=''.join(f'NATIVE_GATE WORLD_DETAIL tick=1 id={1:016x} domain={name} bytes={data.hex()}\n' for name,data in domains.items())
        return self.object(1,1,row)+details+self.summary(1,{1:row},1)

    def test_same_scan_details_reproduce_object_hashes(self):
        source=self.detail_fixture();errors=[];frame=next(frames(source.splitlines(True),errors,1))
        self.assertEqual(errors,[]);self.assertEqual(set(frame['details'][1]),set(DOMAINS))
        detail=next(line for line in source.splitlines(True) if 'WORLD_DETAIL' in line)
        for bad in (source.replace(detail,''),source.replace(detail,detail+detail),source.replace('bytes=00000000','bytes=01000000',1),source.replace('domain=timing','domain=unknown',1)):
            errors=[];list(frames(bad.splitlines(True),errors,1));self.assertTrue(errors)

    def test_navigation_deadlines_decoded_without_losing_bits(self):
        footer=struct.pack('<IIQ',123,7,11)
        motion=struct.pack('<I',0x2c51158)+b'\0'*186+footer
        timing=struct.pack('<IdIdB',2,19.5,7,20.0,1)+struct.pack('<dBB',-1.0,0,1)*2+footer
        decoded=decode_detail({'motion':motion,'timing':timing})
        self.assertEqual(decoded['timing']['nav.refresh_time'],{'hex':struct.pack('<d',19.5).hex(),'value':19.5})
        self.assertEqual(decoded['timing']['nav.parameters_due']['value'],20.0)
        self.assertEqual(decoded['timing']['bank1.notified']['value'],1)
        self.assertEqual(decoded['motion']['bank1.reference1']['value'],0)

    def test_configured_watch_cannot_disappear_from_log(self):
        expected=configured_watches("X4LAN_CONFIG.native_gate_world_hash_watch = {'0000000000000001'}")
        self.assertEqual(expected,{1})
        source=self.detail_fixture();errors=[];list(frames(source.splitlines(True),errors,1,expected));self.assertEqual(errors,[])
        source=''.join(line for line in source.splitlines(True) if 'WORLD_DETAIL' not in line)
        errors=[];list(frames(source.splitlines(True),errors,1,expected));self.assertTrue(errors)

    def test_all_object_timing_bytes_bound_to_existing_hash(self):
        source=self.detail_fixture();line=source.splitlines(True)[0]
        source=source.replace(line,line.rstrip('\n')+' timing_fields=00000000\n',1)
        errors=[];frame=next(frames(source.splitlines(True),errors,1,{1},True));self.assertEqual(errors,[])
        decoded=decode_detail({'timing':frame['objects'][1]['timing_data']})
        self.assertEqual(decoded['timing']['attention']['value'],0)
        for bad in (source.replace(' timing_fields=00000000',''),source.replace('timing_fields=00000000','timing_fields=01000000'),source.replace('timing_fields=00000000','timing_fields=000')):
            errors=[];list(frames(bad.splitlines(True),errors,1,{1},True));self.assertTrue(errors)
