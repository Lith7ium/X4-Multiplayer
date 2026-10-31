"""Motion evidence must retain causal/lifetime failures and exclude local keys."""
from copy import deepcopy
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/dev'))
from native_world_trace import BLOCKS,evidence,compare,causal_keys


def record(index,tick=2,identity=99,hook='ship',parent=0,enter=None,leave=None,thread=22):
    state={'rng':123,'tls_time':0,'view':1,'read':0,'write':1,'read_end':0,'write_end':1,'model':0,'model_bytes':0,'stage':9,
           'blocks':{**{name:b'\0'*size for name,size in BLOCKS.items()},'model':b'','slot':b''}}
    return {'event':index,'tick':tick,'id':identity,'hook':hook,'parent':parent,'enter':enter or index*2-1,
            'leave':leave or index*2,'thread':thread,'ui':0,'errors':0,'dt':0,'time':0,'cheap':0,'player':0,
            'states':{'before':deepcopy(state),'after':deepcopy(state)}}


def log(events):
    rows=[]
    for event in events:
        head={key:(f'{value:016x}' if key in {'id','time','pc_before','pc_after'} else f'{value:x}' if key=='caller' else f'{value:08x}' if key=='dt' else value)
              for key,value in event.items() if key!='states'}
        rows.append('NATIVE_GATE WORLD_TRACE_EVENT '+' '.join(f'{k}={v}' for k,v in head.items()))
        for phase,state in event['states'].items():
            meta={key:(f'{value:x}' if key in {'rng','tls_time','model'} else value) for key,value in state.items() if key!='blocks'}
            rows.append(f"NATIVE_GATE WORLD_TRACE_STATE event={event['event']} phase={phase} "+' '.join(f'{k}={v}' for k,v in meta.items()))
            for block,data in state['blocks'].items():
                rows.append(f"NATIVE_GATE WORLD_TRACE_DATA event={event['event']} phase={phase} block={block} bytes={data.hex() or '-'}")
    count=len(events);watches=len({event['id'] for event in events})
    schema=3 if 'boot' in events[0] else 2 if 'main_count' in events[0]['states']['before'] else 1
    early=schema==3 and any(event['hook']=='loaded' for event in events)
    ending=f' early={int(early)} early_bound={watches if early else 0} early_imports={watches if early else 0} early_failures=0 early_stage={"complete" if early else "off"}' if schema==3 else ''
    rows.append(f'NATIVE_GATE WORLD_TRACE_STOP schema={schema} watches={watches} first={0 if early else 2} last=12 capacity=2048 reserved={count} complete={count} rows={len(rows)} failures=0 dropped=0 unmatched=0'+ending)
    return '\n'.join(rows)+'\n'


def with_queues(events):
    for event in events:
        for state in event['states'].values():
            state.update(main_count=0,main_capacity=0,pending_count=1,pending_capacity=2,unknown_refs=0)
            command=bytearray(0x90);command[:8]=(0x140001234).to_bytes(8,'little');command[0x10:0x18]=(77).to_bytes(8,'little')
            state['blocks'].update(main_queue=b'',main_refs=b'',pending_queue=bytes(command),pending_refs=(0x1234).to_bytes(4,'little')+(99).to_bytes(8,'little'))
    return events


class WorldMotionTraceTests(unittest.TestCase):
    def fixture(self):return [record(1,1,hook='baseline'),record(2,enter=3,leave=6),record(3,hook='nav',parent=2,enter=4,leave=5)]

    def test_complete_nested_evidence_and_thread_independent_keys(self):
        events=self.fixture();errors=[];left,coverage=evidence(log(events),errors,expected={99})
        self.assertEqual(errors,[]);self.assertEqual(coverage['hooks']['0000000000000063']['nav'],1)
        for event in events:event['thread']=889
        right,_=evidence(log(events),errors)
        self.assertEqual(errors,[]);self.assertEqual(causal_keys(left),causal_keys(right));self.assertEqual(compare(left,right),[])

    def test_corrupt_partial_failed_and_unmatched_records_fail_coverage(self):
        source=log(self.fixture());line=next(row for row in source.splitlines(True) if 'block=timer' in row)
        for broken in (source.replace(line,''),source.replace(line,line+line),source.replace('dropped=0','dropped=1'),
                       source.replace('unmatched=0','unmatched=1'),source.replace('errors=0','errors=2',1),
                       source.replace('model_bytes=0','model_bytes=928',1),source.replace('write_end=1','write_end=0',1),
                       source[:source.index('NATIVE_GATE WORLD_TRACE_STOP')],source.replace('bytes=000000000000000000\n','bytes=00\n')):
            errors=[];evidence(broken,errors);self.assertTrue(errors)
        errors=[];evidence(source,errors,expected={100});self.assertTrue(errors)

    def test_before_input_and_after_motion_differences_are_separate(self):
        events=self.fixture();errors=[];left,_=evidence(log(events),errors)
        events[2]['states']['before']['rng']=456
        data=bytearray(events[2]['states']['after']['blocks']['banks']);data[0:4]=b'\0\0\x90\x40'
        events[2]['states']['after']['blocks']['banks']=bytes(data)
        right,_=evidence(log(events),errors);self.assertEqual(errors,[])
        delta=compare(left,right);self.assertEqual(len(delta),1)
        self.assertEqual(delta[0]['before']['rng'],{'left':123,'right':456})
        self.assertIn('bank0.vector0',delta[0]['after'])

    def test_runtime_reference_and_padding_are_only_raw_differences(self):
        events=self.fixture();errors=[];left,_=evidence(log(events),errors)
        data=bytearray(events[2]['states']['before']['blocks']['banks']);data[12]=7;data[0xb0]=4
        events[2]['states']['before']['blocks']['banks']=bytes(data)
        right,_=evidence(log(events),errors);delta=compare(left,right)
        self.assertEqual(errors,[]);self.assertNotIn('before',delta[0])
        self.assertEqual(delta[0]['before_raw_local_differences']['banks']['different_bytes'],2)

    def test_linear_model_uses_verified_short_extent_and_typed_fields(self):
        events=self.fixture()
        for event in events:
            for state in event['states'].values():
                state.update(model=0x2c4f610,model_bytes=0x270);state['blocks']['model']=b'\0'*0x270
        errors=[];left,_=evidence(log(events),errors)
        data=bytearray(events[2]['states']['before']['blocks']['model']);data[0x88]=1
        events[2]['states']['before']['blocks']['model']=bytes(data)
        right,_=evidence(log(events),errors);self.assertEqual(errors,[])
        self.assertIn('linear.time88',compare(left,right)[0]['before'])
        events[2]['states']['before']['model_bytes']=0x3a0
        errors=[];evidence(log(events),errors);self.assertTrue(errors)

    def test_cross_thread_global_order_is_not_a_match_key(self):
        a=[record(1,1,hook='baseline'),record(2,1,100,'baseline'),record(3),record(4,identity=100,thread=33)]
        errors=[];left,_=evidence(log(a),errors)
        b=deepcopy(a)
        for index in (2,3):b[index]['enter'],b[index]['leave']=a[5-index]['enter'],a[5-index]['leave']
        right,_=evidence(log(b),errors);self.assertEqual(errors,[]);self.assertEqual(compare(left,right),[])

    def test_concurrent_same_owner_and_bad_parent_are_not_silently_aligned(self):
        events=self.fixture();events[2]['thread']=33
        errors=[];evidence(log(events),errors)
        self.assertTrue(any('invalid parent' in error for error in errors))
        self.assertTrue(any('concurrent owner' in error for error in errors))

    def test_queue_input_is_compared_using_canonical_reference_and_vectors(self):
        events=with_queues(self.fixture());errors=[];left,_=evidence(log(events),errors)
        data=bytearray(events[2]['states']['before']['blocks']['pending_queue'])
        data[:8]=(0x7ff600001234).to_bytes(8,'little');data[0x10:0x18]=(88).to_bytes(8,'little')
        events[2]['states']['before']['blocks']['pending_queue']=bytes(data)
        right,_=evidence(log(events),errors);delta=compare(left,right)
        self.assertEqual(errors,[]);self.assertNotIn('before',delta[0])
        data[0x20:0x24]=b'\0\0\x80\x3f';events[2]['states']['before']['blocks']['pending_queue']=bytes(data)
        right,_=evidence(log(events),errors)
        self.assertIn('pending.0.vector0',compare(left,right)[0]['before'])

    def test_queue_count_reference_and_extent_failures_are_not_passes(self):
        source=log(with_queues(self.fixture()))
        cases=(source.replace('unknown_refs=0','unknown_refs=1',1),source.replace('pending_count=1','pending_count=17',1),
               source.replace('pending_capacity=2','pending_capacity=0',1),source.replace('pending_capacity=2','pending_capacity=65537',1),
               source.replace('block=main_refs bytes=-','block=main_refs bytes=00',1),
               source.replace('341200006300000000000000','341200000000000000000000',1))
        for broken in cases:
            errors=[];evidence(broken,errors);self.assertTrue(errors)

    def test_empty_queue_transition_is_retained(self):
        events=with_queues(self.fixture());errors=[];left,_=evidence(log(events),errors)
        state=events[2]['states']['after'];state.update(pending_count=0)
        state['blocks']['pending_queue']=state['blocks']['pending_refs']=b''
        right,_=evidence(log(events),errors);self.assertEqual(errors,[])
        self.assertEqual(compare(left,right)[0]['after']['pending.count'],{'left':1,'right':0})

    def test_early_lifecycle_and_ai_provenance_are_required_and_comparable(self):
        events=with_queues([record(i+1,0 if kind=='loaded' else 1,hook=kind) for i,kind in
                           enumerate(('loaded','path_before','agent','path_after','ai','baseline'))])
        for event in events:event.update(boot=3,caller=0,script='-',pc_before=0,pc_after=0)
        for event in events:
            if event['hook'] in {'path_before','path_after','ai'}:event['ui']=1
        events[4].update(caller=0x106e3fb,script='patrol.test',pc_before=7,pc_after=19)
        source=log(events);errors=[];left,_=evidence(source,errors);self.assertEqual(errors,[])
        for broken in (source.replace('early_bound=1','early_bound=0'),source.replace('early_failures=0','early_failures=1'),
                       source.replace('caller=106e3fb','caller=106e3fc'),source.replace('script=patrol.test','script=-'),
                       source.replace('hook=loaded','hook=agent'),source.replace('hook=path_before','hook=ship')):
            errors=[];evidence(broken,errors);self.assertTrue(errors)
        events[4]['script']='move.generic';errors=[];right,_=evidence(log(events),errors)
        self.assertEqual(errors,[]);self.assertEqual(compare(left,right)[0]['arguments']['script'],{'left':'patrol.test','right':'move.generic'})

    def test_schema3_retains_default_late_trace_mode(self):
        events=with_queues(self.fixture())
        for event in events:event.update(boot=3,caller=0,script='-',pc_before=0,pc_after=0)
        errors=[];_,coverage=evidence(log(events),errors)
        self.assertEqual(errors,[]);self.assertEqual(coverage['stop']['early'],0)


if __name__=='__main__':unittest.main()
