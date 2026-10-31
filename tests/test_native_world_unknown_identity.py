from copy import deepcopy
import hashlib
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools/dev'))
import native_world_unknown_identity as identity


def fixture():
    base = 0x140000000; entity = 0x30000; context = 0x40000; exec_ = 0x50000; node = 0x60000
    bits = int.from_bytes(struct.pack('<d', 7.083), 'little')
    old = dict(index=1, reason=6, reads=31, owned=1, errors=0, ui=1, boot=2, tick=0, saved=0, eligible=1, thread=1, phase_status=1, elapsed=0, raw_tag=0x66,
               exec=exec_, current=exec_, context=context, lifetime=0x44, entity=entity, runtime=0x2000001,
               tag=0x66, raw_value=context+0xa8, app=0x20, vbi=0, view=1, main=0,
               tls=bits, game=bits, real=bits, saved_time=bits)
    q = [base,node,exec_,exec_+0x38,exec_,context+0xa8,context,0x44,entity,0x2000001,
         base+0x2c6a270,0x70000,base+0x2c69030,0x80000,0x90000,0x90008,1,base+0x80000,base+0x81000,node]
    storage = b'move.idle\0'.ljust(16,b'\0')+struct.pack('<QQ',9,15)
    script = struct.pack('<20Q7I',*q,0x66,4,9,7,0,1,1)+b'move.idle'.ljust(96,b'\0')+storage+struct.pack('<4I4Q',0x20,0,1,0,*([bits]*4))
    code = bytearray(range(36));code[6:10] = bytes(4); sha = hashlib.sha256(code).hexdigest()
    struct.pack_into('<i', code, 6, 0x70000-0x6000a)
    bitmap = bytearray(32);struct.pack_into('<I',bitmap,4,2<<(2*(22%16)));struct.pack_into('<I',bitmap,8,2<<(2*(44%16)))
    block = bytearray(32);struct.pack_into('<QQ',block,0,entity,entity+0x100);struct.pack_into('<HHI',block,24,1,1,0x550000)
    def object_(pointer,runtime,saved,parent):
        q = [pointer,runtime,saved,parent,0x90000,0xa0000,0xa0000,runtime,base+0x30000,base+0x40000,base+0x50000,base+0x60000,base+0x70000]
        return struct.pack('<13Q5I',*q,4095,1,1,1,14)+b'.?AVEntity@U@@'.ljust(128,b'\0')+code+bitmap+block+struct.pack('<6I',1,0,0,0x50000,0x50080,0x40000)
    raw = [script,object_(entity,0x2000001,0,entity+0x100),object_(entity+0x100,0x2000002,0x1234,0)]
    rows = [dict(schema=1,sample=1,part=i,reads=100,bytes=2000,error=0,stopped=0,boundary=3,before=x,after=x) for i,x in enumerate(raw)]
    stop = dict(schema=1,samples=1,complete=1,rows=3,exported=3,open=0,closed=1,read_limit=512,byte_limit=16384,hop_limit=8)
    return old,rows,stop,sha


def log(rows, stop):
    def body(r):
        return ' '.join(f'{k}={v.hex() if isinstance(v,(bytes,bytearray)) else v}' for k,v in r.items())
    lines = ['NATIVE_GATE WORLD_UNKNOWN_IDENTITY '+body(r) for r in rows]
    if stop is not None:
        lines.append('NATIVE_GATE WORLD_UNKNOWN_IDENTITY_STOP '+body(stop))
    return lines


class UnknownIdentityTests(unittest.TestCase):
    def evaluate(self, old=None, rows=None, stop=None, **kwargs):
        default, default_rows, default_stop, sha = fixture()
        errors=[]
        with patch.object(identity,'STUB_SHA',sha):
            result=identity.evidence(log(default_rows if rows is None else rows,default_stop if stop is None else stop),[default if old is None else old],errors,**kwargs)
        return result,errors

    def test_exact_copied_evidence_derives_supported_metadata_not_canonical_identity(self):
        result,errors=self.evaluate();self.assertEqual(errors,[])
        self.assertTrue(result['coverage_complete']);sample=result['samples'][0]
        self.assertEqual((sample['script_name'],sample['script_version'],sample['stored_pc'],sample['actual_program_slot']),('move.idle',4,1,0))
        self.assertEqual(sample['saved_ancestor'],0x1234)
        self.assertFalse(sample['canonical_identity']);self.assertFalse(sample['context_registry_supported']);self.assertFalse(result['byte_identical_aba_excluded'])

    def test_every_script_identity_and_type_claim_rechecked(self):
        # Exact scalar identity, generation, RTTI, name, bitmap and code tamper.
        offsets={0:[0,8,16,24,32,40,48,56,64,72,80,96,112,120,128,152,160,168,172,176,180,184,188,204,300,310,316,324,332,340,356],
                 1:[0,8,16,24,56,64,72,80,88,96,104,108,112,116,120,124,252,258,320,344,350,352,356,360,364,372]}
        for part,positions in offsets.items():
            for offset in positions:
                with self.subTest(part=part,offset=offset):
                    old,rows,stop,_=fixture();data=bytearray(rows[part]['before']);data[offset]^=1
                    rows[part]['before']=rows[part]['after']=bytes(data)
                    result,errors=self.evaluate(old,rows,stop)
                    self.assertFalse(result['coverage_complete']);self.assertTrue(errors)

    def test_changed_bytes_stop_and_partial_cannot_be_forged_into_success(self):
        for kind in ('change','stop','partial'):
            old,rows,stop,_=fixture()
            if kind=='change':
                data=bytearray(rows[1]['after']);data[8]^=1;rows[1]['after']=bytes(data)
            elif kind=='stop':
                for row in rows:row['stopped']=1
            else:
                data=bytearray(rows[1]['before']);struct.pack_into('<I',data,104,0);rows[1]['before']=rows[1]['after']=bytes(data)
            result,errors=self.evaluate(old,rows,stop);self.assertTrue(errors);self.assertFalse(result['coverage_complete'])
            for row in rows:row['error']={'change':64,'stop':8,'partial':32}[kind]
            result,errors=self.evaluate(old,rows,stop);self.assertFalse(errors);self.assertFalse(result['coverage_complete']);self.assertFalse(result['samples'][0]['supported'])

    def test_all_original_owner_samples_remain_separate_and_strict(self):
        old,rows,stop,_=fixture();second=deepcopy(old);second['index']=2;second['runtime']+=1
        extra=deepcopy(rows)
        for row in extra:row['sample']=2
        stop.update(samples=2,complete=2,rows=6,exported=6)
        errors=[]
        with patch.object(identity,'STUB_SHA',fixture()[3]):result=identity.evidence(log(rows+extra,stop),[old,second],errors)
        self.assertTrue(result['samples'][0]['supported']);self.assertFalse(result['samples'][1]['supported']);self.assertTrue(errors)

    def test_bounds_shape_duplicates_and_missing_status_refuse(self):
        for key,value in [('schema',2),('sample',29),('part',10),('reads',513),('bytes',16385),('error',256),('stopped',2),('boundary',4)]:
            old,rows,stop,_=fixture();rows[0][key]=value
            result,errors=self.evaluate(old,rows,stop);self.assertTrue(errors);self.assertFalse(result['coverage_complete'])
        for key in fixture()[2]:
            if key=='schema':continue
            old,rows,stop,_=fixture();stop[key]+=1
            result,errors=self.evaluate(old,rows,stop);self.assertTrue(errors);self.assertFalse(result['coverage_complete'])
        old,rows,stop,sha=fixture()
        for lines in [log(rows,None),log(rows+[rows[0]],stop),log(rows[::-1],stop),log(rows,stop)+log([],stop),log(rows,stop)+log(rows,None)]:
            errors=[]
            with patch.object(identity,'STUB_SHA',sha):result=identity.evidence(lines,[old],errors)
            self.assertTrue(errors);self.assertFalse(result['coverage_complete'])
        errors=[];result=identity.evidence([], [old],errors);self.assertFalse(result['available']);self.assertFalse(errors)

    def test_longest_row_has_headroom(self):
        _,rows,_,_=fixture()
        for row in rows:
            for key in ('sample','part','reads','bytes','error','stopped','boundary'):row[key]=(1<<32)-1
        self.assertLess(max(map(len,log(rows,None)))+32,2048)

    def test_ancestry_partial_is_not_a_typed_null_or_canonical_id(self):
        old,rows,stop,_=fixture()
        head=bytearray(rows[0]['before']);struct.pack_into('<I',head,184,5)
        rows[0]['before']=rows[0]['after']=bytes(head)
        ancestor=bytearray(376);struct.pack_into('<Q',ancestor,0,old['entity']+0x100)
        rows[2]['before']=rows[2]['after']=bytes(ancestor)
        result,errors=self.evaluate(old,rows,stop);self.assertFalse(errors)
        self.assertTrue(result['samples'][0]['supported']);self.assertFalse(result['samples'][0]['ancestry_complete']);self.assertFalse(result['coverage_complete'])
        self.assertIsNone(result['samples'][0]['saved_ancestor'])
        for which in ('before','after'):
            head=bytearray(rows[0][which]);struct.pack_into('<I',head,184,2);rows[0][which]=bytes(head)
        result,errors=self.evaluate(old,rows,stop);self.assertTrue(errors);self.assertFalse(result['coverage_complete'])

    def test_native_termination_precedence_and_first_unsupported_are_enforced(self):
        # The exact C fixture's saved ancestor also has a null parent. Native
        # stops on SAVED first, so NULL cannot describe the same raw records.
        old,rows,stop,_=fixture()
        for which in ('before','after'):
            raw=bytearray(rows[0][which]);struct.pack_into('<I',raw,184,2);rows[0][which]=bytes(raw)
        result,errors=self.evaluate(old,rows,stop)
        self.assertFalse(result['coverage_complete']);self.assertTrue(any('termination mismatch' in e for e in errors))
        old,rows,stop,_=fixture();extra=deepcopy(rows[2]);extra['part']=3
        for which in ('before','after'):
            raw=bytearray(rows[0][which]);struct.pack_into('<II',raw,180,2,5);rows[0][which]=bytes(raw)
            raw=bytearray(rows[2][which]);struct.pack_into('<QQ',raw,16,0,old['entity']+0x200);struct.pack_into('<I',raw,108,3);rows[2][which]=bytes(raw)
            raw=bytearray(extra[which]);struct.pack_into('<Q',raw,0,old['entity']+0x200);extra[which]=bytes(raw)
        rows.append(extra);stop.update(rows=4,exported=4)
        result,errors=self.evaluate(old,rows,stop)
        self.assertFalse(result['coverage_complete']);self.assertTrue(any('continues after native terminal' in e for e in errors))


if __name__=='__main__':unittest.main()
