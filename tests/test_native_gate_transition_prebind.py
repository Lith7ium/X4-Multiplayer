"""Owned modeled receipts plus actual builder/Lua/bootstrap; no native game."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/dev'))
import native_transition_prebind as p
from native_transition_evidence import evidence,configured_evidence,FIELDS,STATE
from native_player_getup import bootstrap_plan
from native_getup_diagnostic_contract import check
from test_native_gate_getup_diagnostic_evidence import fixture as full_fixture,generated_overlay,configuration,local_evidence,SNAPSHOT
from test_native_gate_transition_saved import text as transition_text
from test_native_gate_transition_queue_diag import CONFIG as QUEUE_CONFIG,fixture as copied_fixture,render as copied_render,words as copied_words
from test_native_gate_getup_startup import GetUpStartupTests

CONFIG=QUEUE_CONFIG+'\nnative_gate_player_transition_prebind_receipt = true'
RUN=hashlib.sha256(b'0'*32).hexdigest()

def phases(initial):
    image=0x140000000;raw=struct.pack('<QII',image+0x2c51c38,0,0).hex()
    zero={k:(0 if k!='owner' else '-') for k in STATE}
    def event(**kw):
        row={k:0 for k in FIELDS};row.update(s0=deepcopy(zero),s1=deepcopy(zero),thread=7,boot=2);row.update(kw);return row
    parent=event(event=1,kind=3,caller=0x1252026,enter=1,leave=4,arg=1,extra=8,active1=1)
    source=event(event=2,kind=4,caller=0xd8dd26,enter=2,leave=3,parent=1,queue=1,enqueue=1,queue_valid=1,s0=deepcopy(initial))
    dispatch=event(event=3,kind=5,caller=0xeacf66,enter=5,leave=6,queue=1,enqueue=1,queue_valid=1,tick=1,boot=3,
                   s0=deepcopy(initial),s1=deepcopy(initial))
    dispatch['s1'].update(model=0,model_type=0)
    ticket=dict(schema=2,ticket=1,state=3,saved=0xae79c,runtime=initial['runtime'],command=0x70000,agent=initial['agent'],enqueue=2,dispatch=3,mode=0,before=raw,after=raw,image=image,raw_world_coverage=0)
    index=(initial['runtime']&0x1ffffff)-1;page=0x90000
    a=dict(schema=1,ordinal=1,phase=1,thread=7,boot=2,tick=0,clock=2,event=2,ticket=1,parent=1,caller=0xd8dd26,command=0x70000,
           bytes=raw,registry=0x80000,page=page,block=page+((index&0xfffff)//3)*32,index=index,lane=(index&0xfffff)%3,
           generation=(initial['runtime']>>25)&0xffff,status=5,s0=deepcopy(initial))
    b=dict(a,ordinal=2,phase=2,clock=3,caller=0);c=dict(a,ordinal=3,phase=3,clock=4,boot=3,caller=0)
    footer=dict(schema=1,capacity=3,count=3,rows=3,source=1,returns=1,pending=0,finalized=1,poison=0,open=0,restore_open=0,closing=1,
                run=RUN,image=image,atomic_lease=0,allocation_generation_known=0,queue_exception_cleanup=0,
                action_tick=0,action_clock=0,action_thread=0,action_ticket=0,action_dispatch=0)
    return [parent,source,dispatch],ticket,[a,b,c],footer

def word(key,value):
    if key=='s0':return ','.join([f'{value[k]:016x}' for k in ('saved','runtime')]+[f'{value[k]:x}' for k in STATE[2:10]]+[str(value[k]) for k in STATE[10:]])
    return f'{value:x}' if key in p.HEX else str(value)
def render(rows,footer):
    def words(r):return ' '.join(k+'='+word(k,v) for k,v in r.items())
    return p.PREFIX+'_ENABLED schema=1 resumed=0 run='+footer['run']+' atomic_lease=0\n'+\
        '\n'.join(p.PREFIX+' '+words(r) for r in rows)+'\n'+p.PREFIX+'_STOP '+words(footer)

def fixture():
    core=evidence(full_fixture()[0]);watch=deepcopy(core['watches'][1]);events,ticket,rows,footer=phases(watch['s0'])
    return transition_text(events,[watch],[ticket]),rows,footer

def complete_fixture():
    args=full_fixture();old=evidence(args[0]);watch=old['watches'][1]
    events,ticket,rows,footer=phases(watch['s0']);old_events=list(old['events'].values());old_tickets=list(old['tickets'].values())
    for e in old_events:
        e['event']+=3;e['enter']+=6;e['leave']+=6
        for k in ('parent','transfer','publication'):
            if e[k]:e[k]+=3
        for k in ('queue','enqueue'):
            if e[k]:e[k]+=1
    for t in old_tickets:t['ticket']+=1;t['enqueue']+=3;t['dispatch']+=3
    combined=transition_text(events+old_events,[watch],[ticket]+old_tickets)
    output=[];inserted=False
    for line in args[0].splitlines():
        if any('NATIVE_GATE '+key in line for key in ('PLAYER_TRANSITION ','PLAYER_TRANSITION_WATCH ','PLAYER_TRANSITION_TICKET ','PLAYER_TRANSITION_STOP ')):
            if not inserted:output.append(combined);inserted=True
            continue
        if 'PLAYER_ORIGIN_EDGE ' in line:line=re.sub(r'\btransition=(\d+)',lambda m:'transition='+str(int(m[1])+3),line)
        if 'PLAYER_ORIGIN_TICKET ' in line:line=re.sub(r'\b(transition_before|transition_after)=(\d+)',lambda m:m[1]+'='+str(int(m[2])+6),line)
        output.append(line)
    args[1]+='\nnative_gate_player_transition_queue_evidence = true\nnative_gate_player_transition_prebind_receipt = true\n'
    cfg=configuration(args[1]);footer.update(run=cfg['run'],action_tick=30,action_clock=6,action_thread=7,action_ticket=1,action_dispatch=3)
    args[0]='\n'.join(output)+'\n'+render(rows,footer)
    copied,stop,_=copied_fixture();copied[0].update(caller=0x140d8dd26,order=3);copied[1].update(order=4)
    q=deepcopy(copied[0]);q.update(ordinal=3,tick=31,boot=3,bound=1,bound_agent=0x30000,parent=0,parent_kind=0,order=10,command=0x90000,
                                 first=old_tickets[0]['before'],second=old_tickets[0]['before'])
    d=deepcopy(copied[1]);d.update(ordinal=4,tick=32,order=12,source=3,command=0x90000,first=q['first'],second=q['second'])
    stop.update(count=4,rows=4,calls=4,run=cfg['run']);args[0]+='\n'+copied_render(copied+[q,d],stop).replace('run='+RUN,'run='+cfg['run'])+'\n'
    args[4].update(queue_evidence=1,prebind_receipt=1);args[3]['getup_diagnostic'].update(queue_evidence=1,prebind_receipt=1)
    old_digest=args[2]['states']['0']['digest'];digest=hashlib.sha256(('SNAPSHOT_BYTES_1|'+SNAPSHOT+'|'+cfg['plan']).encode()).hexdigest()
    args[2]=json.loads(json.dumps(args[2]).replace(old_digest,digest))
    return args

class PrebindTests(unittest.TestCase):
    def test_complete_prospective_chain_is_not_release_or_input_authority(self):
        core,rows,footer=fixture();result=evidence(core+'\n'+render(rows,footer))
        self.assertEqual(result['errors'],[]);self.assertTrue(result['prebind_receipt']['evidence_valid'])
        self.assertEqual(result['observed_model_completions'],[])
        for k in ('atomic_lease','allocation_generation_known','independent_player_context','completed_release'):self.assertIs(result['prebind_receipt'][k],False)
        self.assertFalse(evidence(core)['evidence_valid'])
        # Relabeling only the child as bound-phase cannot erase its actual
        # boot2 activation ancestry when the new proof is stripped.
        relabeled='\n'.join(re.sub(r'\bboot=2\b','boot=3',line) if 'event=2 ' in line else line for line in core.splitlines())
        self.assertNotEqual(relabeled,core);self.assertFalse(evidence(relabeled)['evidence_valid'])

    def test_every_phase_identity_chronology_and_width_is_required(self):
        core,rows,footer=fixture();self.assertTrue(evidence(core+'\n'+render(rows,footer))['evidence_valid'])
        for phase,key,value in [(0,'caller',0xeac95a),(0,'clock',0),(0,'thread',1<<32),(0,'command',1<<64),(0,'parent',0),(0,'generation',1),
            (0,'registry',0),(0,'block',1),(0,'status',4),(1,'clock',4),(1,'boot',3),(1,'caller',1),(1,'thread',8),
            (2,'clock',1<<64),(2,'clock',5),(2,'boot',2),(2,'event',1),(2,'ticket',2)]:
            changed=deepcopy(rows);changed[phase][key]=value
            with self.subTest(phase=phase,key=key,value=value):self.assertFalse(evidence(core+'\n'+render(changed,footer))['evidence_valid'])
        for phase in range(3):
            changed=deepcopy(rows);changed[phase]['s0']['controller']+=1
            self.assertFalse(evidence(core+'\n'+render(changed,footer))['evidence_valid'])
            self.assertFalse(evidence(core+'\n'+render(rows[:phase]+rows[phase+1:],footer))['evidence_valid'])
        for key,value in [('returns',0),('pending',1),('poison',1),('open',1),('restore_open',1),('finalized',0),('image',0x150000000),('atomic_lease',1)]:
            with self.subTest(key=key):self.assertFalse(evidence(core+'\n'+render(rows,dict(footer,**{key:value})))['evidence_valid'])
        self.assertFalse(p.evidence(render(rows,footer),evidence(core),'')['evidence_valid'])

    def test_complete_local_contract_and_omissions_keep_hard_false_claims(self):
        args=complete_fixture();result=local_evidence(*args)
        self.assertEqual(result['errors'],[]);self.assertTrue(result['local_contract_valid'])
        for key in ('session_pass','parity_checked','independent_native_players','completed_release'):self.assertIs(result[key],False)
        for fault in ('phase','all','manifest','config','lane','digest'):
            changed=deepcopy(args)
            if fault=='phase':changed[0]='\n'.join(l for l in changed[0].splitlines() if not l.startswith(p.PREFIX+' schema=1 ordinal=2 '))
            if fault=='all':changed[0]='\n'.join(l for l in changed[0].splitlines() if p.PREFIX not in l)
            if fault=='manifest':del changed[4]['prebind_receipt']
            if fault=='config':changed[1]=changed[1].replace('native_gate_player_transition_prebind_receipt = true','')
            if fault=='lane':del changed[3]['getup_diagnostic']['prebind_receipt']
            if fault=='digest':changed[2]['states']['0']['digest']=full_fixture()[2]['states']['0']['digest']
            with self.subTest(fault=fault):self.assertFalse(local_evidence(*changed)['local_contract_valid'])

    def test_closed_manifest_and_native_bootstrap_vector(self):
        self.assertEqual(bootstrap_plan(CONFIG,diagnostic=True),bootstrap_plan(QUEUE_CONFIG,diagnostic=True)+p.SUFFIX)
        self.assertEqual(hashlib.sha256(('SNAPSHOT_BYTES_1|'+SNAPSHOT+'|'+bootstrap_plan(CONFIG,diagnostic=True)).encode()).hexdigest(),
                         '22701acf4492101258d3ec8b0872b69550a75bb63be70b96b0b2f9136b397df1')
        manifest=full_fixture()[4];check(dict(manifest,queue_evidence=1,prebind_receipt=1))
        for value in (True,False,0,2,'1',None):
            with self.subTest(value=value),self.assertRaises(ValueError):check(dict(manifest,queue_evidence=1,prebind_receipt=value))
        with self.assertRaises(ValueError):check(dict(manifest,prebind_receipt=1))
        for bad in (CONFIG.replace('prebind_receipt = true','prebind_receipt = false'),CONFIG.replace('queue_evidence = true','queue_evidence = 1'),
                    CONFIG+'\nnative_gate_player_transition_prebind_receipt = true',CONFIG.replace('diagnostic-getup"','agreed-getup"')):
            with self.assertRaises(ValueError):bootstrap_plan(bad,diagnostic=True)

    def test_initialization_must_return_before_action_prerequisite(self):
        args=complete_fixture();self.assertTrue(local_evidence(*args)['local_contract_valid'])
        for key,value in [('action_tick',0),('action_clock',5),('action_clock',7),('action_thread',8),('action_ticket',2),('action_dispatch',4)]:
            changed=deepcopy(args)
            changed[0]='\n'.join(re.sub(r'\b'+key+r'=[^ ]+',key+'='+str(value),line) if p.PREFIX+'_STOP ' in line else line for line in changed[0].splitlines())
            with self.subTest(key=key,value=value):self.assertFalse(local_evidence(*changed)['local_contract_valid'])
        # Dispatch enters at tick1 but returns after the action-clock sample.
        # A retired final footer and early entry tick alone are insufficient.
        changed=deepcopy(args)
        changed[0]=changed[0].replace('action_tick=30 action_clock=6','action_tick=30 action_clock=5')
        self.assertNotEqual(changed[0],args[0])
        result=evidence(changed[0]);self.assertEqual(result['prebind_receipt']['errors'],['prebind action prerequisite chronology'])
        self.assertFalse(local_evidence(*changed)['local_contract_valid'])

    def test_real_builder_packaged_startup_and_fresh_lua_reattachment(self):
        GetUpStartupTests.setUpClass();harness=GetUpStartupTests()
        with generated_overlay(['--player-transition-queue-evidence','--player-transition-prebind-receipt']) as out:
            config=(out/'ui/config.lua.append').read_text()
            for old,new in {'@X4LANE_TOKEN@':'a'*32,'@X4LANE_PORT@':'42000','@X4LANE_SLOT@':'p1'}.items():config=config.replace(old,new)
            harness.modules=[(name,(out/name).read_text()) for name in (out/'ui.xml.files').read_text().splitlines()]
        self.assertEqual(configuration(config)['prebind_receipt'],1);backend=None
        for phase in (0,1,2):
            if backend:backend['state']['boot_phase']=phase
            lua,backend,failures=harness.runtime(backend,config=config);self.assertEqual(failures,[])
            run=hashlib.sha256(b'a'*32).hexdigest()
            _,qfooter,_=copied_fixture();qfooter.update(count=0,rows=0,calls=0,closing=0,run=run)
            _,_,pfooter=fixture();pfooter.update(count=0,rows=0,source=0,returns=0,finalized=0,closing=0,run=run)
            lua.globals().qfooter=copied_words(qfooter);lua.globals().pfooter=' '.join(k+'='+word(k,v) for k,v in pfooter.items())
            def configure(name,actual_run):self.assertEqual(actual_run,run);backend['events'].append(name);return 1
            lua.globals().nprebind=lambda run:configure('prebind',run);lua.globals().ncopyconfig=lambda run:configure('copied',run)
            lua.execute('''local g=package.loadlib('gate')()
              g.player_transition_prebind_config=function(run)return nprebind(run)end
              g.player_transition_queue_evidence_config=function(run)return ncopyconfig(run)end
              g.player_transition_prebind_status=function()return pfooter end
              g.player_transition_queue_evidence_status=function()return qfooter end
              g.player_transition_prebind_row=function()error('no pre-Load phase')end
              update();update();update()''')
            self.assertEqual((backend['loads'],backend['stops'],backend['native_actions']),(1,0,0),'\n'.join(lua.globals().logs.values()))
            events=backend['events'];index=len(events)-1-events[::-1].index('prebind')
            self.assertEqual(events[index-1:index+3],['transition_watch','prebind','copied','transition'])
            preimage='SNAPSHOT_BYTES_1|'+SNAPSHOT+'|'+bootstrap_plan(config,diagnostic=True)
            self.assertEqual(preimage.count(p.SUFFIX),1);self.assertEqual(backend['boot_args'][4],hashlib.sha256(preimage.encode()).hexdigest())
        with self.assertRaises(SystemExit):
            with generated_overlay(['--player-transition-prebind-receipt']):pass

if __name__=='__main__':unittest.main()
