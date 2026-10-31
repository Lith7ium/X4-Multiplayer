from copy import deepcopy
from pathlib import Path
import sys,unittest,struct
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/dev'))
from native_world_queue import KEYS,HEX,PHASE,TYPED,evidence,compare

def fixture():
    e={key:0 for key in KEYS};data=bytearray(0x90);data[:8]=(0x142c4f578).to_bytes(8,'little');data[0x10]=77
    e.update(event=1,kind=0,tick=1,id=99,reference=77,enter=1,leave=2,enqueue=1,thread=7,ui=1,boot=3,view=1,
             caller=0xea1941,order_type=0x2c513d8,payload_type=0x2c4f578,source_status=2,source_tag=0x66,
             name_status=1,source_saved=88,source_pc=12,name='move.generic',command=0x10000,record=0x10010,exec=0x40000,context=0x50000,lifetime=22,payload=bytes(data))
    append=deepcopy(e);append.update(event=2,kind=1,enter=3,leave=4,trace_event=7,source_status=0,source_tag=0,name_status=0,source_saved=0,source_pc=0,name='-')
    return [e,append]

def log(events,outstanding=0,schema=1):
    def value(key,v):return v.hex() if isinstance(v,bytes) else f'{v:x}' if key in HEX else str(v)
    lines=['NATIVE_GATE WORLD_QUEUE_EVENT '+' '.join(f'{k}={value(k,v)}' for k,v in e.items()) for e in events]
    lines.append(f'NATIVE_GATE WORLD_QUEUE_STOP schema={schema} capacity=512 reserved={len(events)} complete={len(events)} rows={len(events)} failures=0 dropped=0 installed=0 restored=1 outstanding={outstanding} preimport_calls=3 open=0 peak=1 restore_open=0')
    return '\n'.join(lines)

def typed_fixture(order_type=0x2c513d8):
    source,append=fixture()
    for e in (source,append):
        e.update({k:0 for k in PHASE|TYPED});e.update(phase_status=1,phase_view=1,table=0x90000,order_size=0xb0,
            raw_caller=0x140eacf66,command_header=(0x140000000+order_type).to_bytes(8,'little')+bytes(8),flags_before=bytes(13),flags_after=bytes(13))
    dispatch=deepcopy(append);dispatch.update(event=2,kind=2,enter=3,leave=6,caller=0xeacf66)
    append.update(event=3,enter=4,leave=5,trace_event=8,dispatch_parent=2,caller_source=1,caller=0xeacf66,raw_caller=0x700001000)
    if order_type==0x2c513d8:return [source,dispatch,append]
    for e in (source,dispatch):
        value=0 if order_type==0x2c513b8 else 1
        e.update(order_type=order_type,order_size=16,order_value=value,reference=0,record=0,payload_type=0,payload=bytes(0x90),
                 command_header=(0x140000000+order_type).to_bytes(8,'little')+bytes([value])+bytes(7))
    dispatch['leave']=4
    return [source,dispatch]

class QueueEvidenceTests(unittest.TestCase):
    def test_model_order_decodes_full_uint32_and_preserves_generation(self):
        for mode in range(7):
            records=typed_fixture(0x2c51c38)
            for e in records:e.update(order_value=mode,command_header=e['command_header'][:8]+mode.to_bytes(4,'little')+bytes(4))
            errors=[];evidence(log(records,schema=3),errors);self.assertEqual(errors,[])
            records[1]['command_header']=records[1]['command_header'][:8]+(mode+256).to_bytes(4,'little')+bytes(4)
            errors=[];evidence(log(records,schema=3),errors);self.assertTrue(errors)

    def test_typed_dispatch_and_nested_append_require_the_exact_generation(self):
        records=typed_fixture();errors=[]
        trace={7:dict(hook='order',id=99,tick=1,errors=0),8:dict(hook='pending',id=99,tick=1,errors=0,parent=7)}
        events,coverage=evidence(log(records,schema=3),errors,trace=trace)
        self.assertEqual(errors,[]);self.assertEqual(coverage['hooks'],dict(enqueue=1,dispatch=1,append=1));self.assertEqual(len(events),3)
        for index,key,value in [(2,'dispatch_parent',1),(2,'caller_source',0),(1,'caller',0xeacf65),(2,'thread',9),(1,'leave',4)]:
            broken=deepcopy(records);broken[index][key]=value;errors=[];evidence(log(broken,schema=3),errors)
            self.assertTrue(errors,(index,key,value))
        broken=deepcopy(records);duplicate=deepcopy(broken[2]);duplicate.update(event=4,enter=6,leave=7);broken[1]['leave']=8;broken.append(duplicate)
        errors=[];evidence(log(broken,schema=3),errors);self.assertTrue(any('duplicate nested' in e for e in errors))

    def test_all_four_small_payloads_preserve_values_and_normalize_padding_only_in_comparison(self):
        for order_type in (0x2c513b8,0x2c51538,0x2c51518,0x2c514f8):
            records=typed_fixture(order_type);errors=[];a,_=evidence(log(records,schema=3),errors)
            self.assertEqual(errors,[],hex(order_type))
            broken=deepcopy(records);broken[1]['order_value']=3;errors=[];evidence(log(broken,schema=3),errors);self.assertTrue(errors)
            if order_type!=0x2c513b8:
                changed=deepcopy(records)
                for e in changed:e['command_header']=e['command_header'][:9]+b'padding'
                errors=[];b,_=evidence(log(changed,schema=3),errors);self.assertEqual(errors,[]);self.assertEqual(compare(a,b),[])
                # A native flag result remains observable independently of inputs.
                changed[1]['flags_after']=bytes([1])+bytes(12);b,_=evidence(log(changed,schema=3),errors)
                self.assertIn('flags_after',compare(a,b)[0]['fields'])

    def test_detached_or_skipped_commands_are_not_complete_until_dispatch(self):
        records=typed_fixture(0x2c513b8)[:1];errors=[];evidence(log(records,outstanding=1,schema=3),errors)
        self.assertTrue(any('undispatched' in e for e in errors))
        errors=[];evidence(log(records,outstanding=0,schema=3),errors)
        self.assertTrue(any('outstanding generation count' in e for e in errors))
        # Reusing an address is valid only after the earlier dispatch returned.
        records=typed_fixture(0x2c513b8);later=deepcopy(records)
        for e in later:e.update(event=e['event']+2,enqueue=3,enter=e['enter']+4,leave=e['leave']+4)
        records+=later;errors=[];evidence(log(records,schema=3),errors);self.assertEqual(errors,[])

    def test_pre_admission_ownership_requires_supported_clock_and_phase_evidence(self):
        records=fixture()
        for e in records:
            e.update({k:0 for k in PHASE});e.update(phase_status=1,phase_view=1,table=0x90000)
        records[0].update(tick=0,boot=2,early_eligible=1,elapsed=1234)
        errors=[];_,coverage=evidence(log(records,schema=2),errors)
        self.assertEqual(errors,[]);self.assertEqual(coverage['provenance_errors'],[])
        for key,value in [('early_eligible',0),('elapsed',120001),('phase_status',3),('phase_view',0),('game',0x7ff8000000000000)]:
            broken=deepcopy(records);broken[0][key]=value;errors=[];evidence(log(broken,schema=2),errors)
            self.assertTrue(errors,(key,value))
        # Native flags may already be clear while the gate still says phase2.
        changed=deepcopy(records);changed[0].update(app_flags=2,vbi_loading=1,table=0x80000,elapsed=3000,main=1)
        errors=[];a,_=evidence(log(records,schema=2),errors);b,_=evidence(log(changed,schema=2),errors)
        self.assertEqual(errors,[]);self.assertIn('vbi_loading',compare(a,b)[0]['fields'])
        changed=deepcopy(records);changed[0].update(table=0xa0000,elapsed=9999,main=1,real=int.from_bytes(struct.pack('<d',12),'little'))
        b,_=evidence(log(changed,schema=2),errors);self.assertEqual(compare(a,b),[])

    def test_queue_generation_links_to_matching_agent_snapshot(self):
        errors=[];events,coverage=evidence(log(fixture()),errors,trace={7:dict(hook='pending',id=99,tick=1,errors=0)})
        self.assertEqual(errors,[]);self.assertEqual(coverage['provenance_errors'],[]);self.assertEqual(len(events),2)
        errors=[];evidence(log(fixture()),errors,trace={});self.assertTrue(any('Agent bracket' in e for e in errors))

    def test_unbracketed_global_is_diagnostic_not_causal_attribution(self):
        records=fixture();records[0]['source_status']=1;errors=[]
        _,coverage=evidence(log(records),errors)
        self.assertEqual(errors,[]);self.assertTrue(coverage['provenance_errors'])
        records[0].update(source_status=2,source_tag=0x5d,name_status=0,name='-')
        _,coverage=evidence(log(records),errors);self.assertTrue(coverage['provenance_errors'])

    def test_raw_pointer_padding_and_lifetime_values_are_not_cross_client_keys(self):
        a=fixture();b=deepcopy(a)
        for e in b:
            e.update(command=0x20000,record=0x20010,exec=123456,context=123,lifetime=456,thread=99)
            data=bytearray(e['payload']);data[:8]=(0x7ff600123456).to_bytes(8,'little');data[0x2c:0x30]=b'xxxx';e['payload']=bytes(data)
        errors=[];left,_=evidence(log(a),errors);right,_=evidence(log(b),errors)
        self.assertEqual(errors,[]);self.assertEqual(compare(left,right),[])
        for e in b:
            data=bytearray(e['payload']);data[0x20:0x24]=b'\0\0\x80\x3f';e['payload']=bytes(data)
        right,_=evidence(log(b),errors);self.assertIn('vector0',compare(left,right)[0]['fields'])

    def test_partial_duplicate_changed_stale_and_foreign_generations_fail(self):
        source=log(fixture())
        cases=[source.replace('restored=1','restored=0'),source.replace('errors=0','errors=4',1),
               source.replace('open=0 peak','open=1 peak'),source.replace('restore_open=0','restore_open=1'),
               source.replace('complete=2','complete=1'),source.replace('outstanding=0','outstanding=1'),
               source.replace('source_status=2','source_status=6'),source.replace('event=2','event=1'),
               source.replace('order_type=2c513d8','order_type=2c513d0')]
        for text in cases:
            errors=[];evidence(text,errors);self.assertTrue(errors,text)
        records=fixture();records[1]['reference']=78;errors=[];evidence(log(records),errors);self.assertTrue(errors)
        records=fixture();records[1]['enqueue']=9;errors=[];evidence(log(records),errors);self.assertTrue(errors)
        errors=[];evidence(source,errors,expected={100});self.assertTrue(errors)

    def test_queued_but_not_yet_dispatched_input_is_retained(self):
        errors=[];events,_=evidence(log(fixture()[:1],outstanding=1),errors)
        self.assertEqual(errors,[]);self.assertEqual(len(events),1)
        errors=[];evidence(log([]),errors);self.assertTrue(any('unexercised' in e for e in errors))

    def test_overlapping_concurrent_owner_has_no_arbitrary_total_order(self):
        records=fixture()[:1];other=deepcopy(records[0]);records[0]['leave']=4
        other.update(event=2,enqueue=2,enter=2,leave=3,command=0x20000,record=0x20010,thread=9)
        records.append(other);errors=[];evidence(log(records,2),errors)
        self.assertTrue(any('concurrent enqueue owner' in e for e in errors))

if __name__=='__main__':unittest.main()
