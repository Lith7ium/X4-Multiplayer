"""Copied queue evidence, real Lua adapters and distinct bootstrap binding."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/dev'))
import native_transition_queue_diag as d
from native_player_getup import bootstrap_plan
from native_getup_diagnostic_contract import check
from validate_native_getup_diagnostic import local_evidence
from test_native_gate_getup_diagnostic_evidence import fixture as full_fixture,generated_overlay,configuration,SNAPSHOT
from test_native_gate_getup_startup import GetUpStartupTests
from test_native_gate_transition_probe import TransitionProbeTests,SOURCE
from test_native_gate_transition_saved import text as transition_text

TOKEN='0'*32
RUN=hashlib.sha256(TOKEN.encode()).hexdigest()
WIRE='2|00000000000ae79c/p/ship_arg_s_scout_01_a_macro/player'
CONFIG='\n'.join(['token = "'+TOKEN+'"','native_gate_player_probe = "diagnostic-getup"',
 'native_gate_diagnostic_contract = "diagnostic-getup-1"','native_gate_player_transition_manifest = "'+WIRE+'"',
 'native_gate_player_transition_observer = true',
 'native_gate_player_transition_last_tick = 120','native_gate_player_transition_queue_evidence = true',
 'native_gate_player_origin_token = "1|0000000000011e12|00000000000ae79c|1|120"'])

def fixture():
    image=0x140000000;header=struct.pack('<QII',image+0x2c51c38,0,0).hex()
    q=dict(schema=1,ordinal=1,kind=1,tick=0,boot=2,thread=7,bound=0,selection=1,watch=1,imports=1,id_generation=0,
           runtime=123,saved=0xae79c,command=0x70000,agent=0,object=0,imported_object=0x10000,bound_agent=0,manager=0x90000,
           recipient_reads=0,manager_reads=1,manager_match=1,caller=image+0xeac95a,reads=7,decode=1,type=image+0x2c51c38,
           first=header,second=header,parent=1,parent_kind=3,order=1,candidates=0,source=0,byte_match=0)
    r=dict(q,ordinal=2,kind=2,tick=1,boot=3,bound=1,agent=0x30000,object=0x10000,bound_agent=0x30000,manager=0,
           recipient_reads=2,manager_reads=0,manager_match=0,caller=image+0xeacf66,parent=0,parent_kind=0,order=2,
           candidates=1,source=1,byte_match=1)
    stop=dict(schema=1,capacity=256,count=2,rows=2,calls=2,unknown=0,unreadable=0,ambiguous=0,dropped=0,open=0,
              restore_open=0,closing=1,run=RUN,image=image,allocation_generation_known=0,ticket_authority=0,atomic_lease=0)
    core=dict(stop=dict(schema=2,count=1,complete=1,rows=1,queue_calls=2,open=0,restore_open=0),watches={1:dict(s0=dict(saved=0xae79c,runtime=123,object=0x10000,agent=0x30000))},
              events={1:dict(kind=3,thread=7,tick=0,boot=2,enter=1,leave=2)})
    return [q,r],stop,core

def words(row):
    return ' '.join(k+'='+(f'{v:016x}' if k in ('saved','runtime') else f'{v:x}' if k in d.HEX else str(v)) for k,v in row.items())
def render(rows,stop):
    return d.PREFIX+'_ENABLED schema=1 resumed=0 run='+RUN+' allocation_generation_known=0 ticket_authority=0\n'+\
        '\n'.join(d.PREFIX+' '+words(r) for r in rows)+'\n'+d.PREFIX+'_STOP '+words(stop)

class QueueEvidenceTests(unittest.TestCase):
    def test_separate_copy_cannot_create_a_ticket_or_erase_core_failure(self):
        rows,stop,core=fixture();core['evidence_valid']=False;core['errors']=['missing ticket'];before=deepcopy(core)
        result=d.evidence(render(rows,stop),CONFIG,core)
        self.assertEqual(result['errors'],[]);self.assertEqual(core,before)
        self.assertFalse(result['allocation_generation_known']);self.assertFalse(result['ticket_authority'])
        self.assertEqual(result['rows'][2]['byte_match'],1)
        self.assertIsNone(d.evidence('', '', {}))
        self.assertFalse(d.evidence(render(rows,stop),'',core)['evidence_valid'])

    def test_tamper_widths_fields_parent_identity_and_admission(self):
        cases=[('caller',1),('runtime',124),('saved',0xae79d),('imported_object',0x10001),('bound_agent',0),
               ('decode',3),('reads',0),('first','00'*16),('candidates',2),('source',0),('byte_match',0),
               ('thread',1<<32),('command',1<<64),('bound',2),('selection',2),('id_generation',1),('recipient_reads',0),
               ('boot',0),('boot',1),('boot',2),('order',(1<<64)-1),('order',3)]
        for key,value in cases:
            rows,stop,core=fixture();rows[1][key]=value
            with self.subTest(key=key):self.assertFalse(d.evidence(render(rows,stop),CONFIG,core)['evidence_valid'])
        for key,value in [('parent',2),('parent_kind',1),('order',2)]:
            rows,stop,core=fixture();rows[0][key]=value
            self.assertFalse(d.evidence(render(rows,stop),CONFIG,core)['evidence_valid'])
        rows,stop,core=fixture();core['stop']['queue_calls']=3
        self.assertFalse(d.evidence(render(rows,stop),CONFIG,core)['evidence_valid'])
        for mutation in (lambda s:s.replace('ordinal=1','ordinal=+1'),lambda s:s.replace('calls=2','calls=02'),
                         lambda s:s.replace('schema=1 ordinal=1','schema=1 extra=0 ordinal=1'),
                         lambda s:s+'\n'+s,lambda s:s.replace('ordinal=1','ordinal=1 '+'x'*1200)):
            rows,stop,core=fixture();self.assertFalse(d.evidence(mutation(render(rows,stop)),CONFIG,core)['evidence_valid'])

    def test_zero_and_concurrent_clock_samples_do_not_require_row_order(self):
        rows,stop,core=fixture();core['events']={};core['stop'].update(count=0,complete=0,rows=0)
        for row in rows:row.update(tick=0,boot=0,order=0,parent=0,parent_kind=0)
        self.assertEqual(d.evidence(render(rows,stop),CONFIG,core)['errors'],[])
        rows,stop,core=fixture();rows[0].update(parent=0,parent_kind=0,order=2);rows[1]['order']=1
        self.assertEqual(d.evidence(render(rows,stop),CONFIG,core)['errors'],[])
        for field in ('count','complete','rows'):
            broken=deepcopy(core);broken['stop'][field]+=1
            self.assertFalse(d.evidence(render(rows,stop),CONFIG,broken)['evidence_valid'])

    def test_decode_failure_data_is_explicit_and_never_passes(self):
        for decode,reads in [(3,0),(4,1),(5,1),(6,3),(7,7),(8,7),(9,7)]:
            rows,stop,core=fixture();r=rows[1];r.update(decode=decode,reads=reads,byte_match=0)
            if not reads&1:r['type']=0
            if not reads&2:r['first']='00'*16
            if not reads&4:r['second']='00'*16
            if decode==4:r['type']=1
            if decode==7:r['second']='00'*16
            if decode==8:r['first']=r['second']='00'*16
            if decode==9:r['first']=r['second']=struct.pack('<QII',stop['image']+0x2c51c38,7,0).hex()
            self.assertTrue(d.decode_valid(r,stop['image']),decode)
            self.assertFalse(d.evidence(render(rows,stop),CONFIG,core)['evidence_valid'],decode)

    def test_duplicate_address_only_reports_ambiguity(self):
        rows,stop,core=fixture();q=deepcopy(rows[0]);q.update(ordinal=2)
        rows[1].update(ordinal=3,candidates=2,source=0,byte_match=0);rows.insert(1,q)
        stop.update(count=3,rows=3,calls=3);core['stop']['queue_calls']=3
        result=d.evidence(render(rows,stop),CONFIG,core);self.assertEqual(result['errors'],[])
        self.assertFalse(result['ticket_authority']);self.assertEqual(result['rows'][3]['source'],0)
        for key,value in [('dropped',1),('open',1),('restore_open',1),('allocation_generation_known',1),('ticket_authority',1),('unreadable',1)]:
            bad=dict(stop,**{key:value});self.assertFalse(d.evidence(render(rows,bad),CONFIG,core)['evidence_valid'],key)

    def test_bootstrap_and_closed_manifest_extension(self):
        old=CONFIG.replace('\nnative_gate_player_transition_queue_evidence = true','')
        self.assertEqual(bootstrap_plan(CONFIG,diagnostic=True),bootstrap_plan(old,diagnostic=True)+d.SUFFIX)
        self.assertEqual(hashlib.sha256(('SNAPSHOT_BYTES_1|'+SNAPSHOT+'|'+bootstrap_plan(CONFIG,diagnostic=True)).encode()).hexdigest(),
                         'bd14169fada13d6d918437df27fa6c36281736525138300fbdc674a3024b581d')  # actual C configuration vector
        manifest=full_fixture()[4];check(manifest)
        check(dict(manifest,queue_evidence=1))
        for value in (True,False,0,2,'1',None):
            with self.subTest(value=value),self.assertRaises(ValueError):check(dict(manifest,queue_evidence=value))
        for config in (CONFIG.replace(' = true',' = false'),CONFIG+'\nnative_gate_player_transition_queue_evidence = true',
                       CONFIG.replace('diagnostic-getup"','agreed-getup"'),CONFIG.replace('last_tick = 120','last_tick = 119')):
            with self.assertRaises(ValueError):bootstrap_plan(config,diagnostic=True)

    def test_actual_builder_and_lua_bootstrap_reload_bind_once_before_transition(self):
        GetUpStartupTests.setUpClass();harness=GetUpStartupTests()
        with generated_overlay(['--player-transition-queue-evidence']) as out:
            config=(out/'ui/config.lua.append').read_text()
            for old,new in {'@X4LANE_TOKEN@':'a'*32,'@X4LANE_PORT@':'42000','@X4LANE_SLOT@':'p1'}.items():config=config.replace(old,new)
            modules=[(name,(out/name).read_text()) for name in (out/'ui.xml.files').read_text().splitlines()]
        self.assertEqual(configuration(config)['queue_evidence'],1)
        self.assertEqual(config.count('native_gate_player_transition_queue_evidence = true'),1)
        harness.modules=modules;backend=None
        for phase in (0,1,2):
            if backend:backend['state']['boot_phase']=phase
            lua,backend,failures=harness.runtime(backend,config=config);self.assertEqual(failures,[])
            run=hashlib.sha256(b'a'*32).hexdigest();_,footer,_=fixture()
            footer.update(count=0,rows=0,calls=0,closing=0,run=run)
            lua.globals().qfooter=words(footer)
            def configure(run_arg):
                self.assertEqual(run_arg,run);backend['events'].append('queue_evidence');return 1
            lua.globals().nqconfigure=configure
            lua.execute("""local g=package.loadlib('gate')()
              g.player_transition_queue_evidence_config=function(run)return nqconfigure(run)end
              g.player_transition_queue_evidence_status=function()return qfooter end
              g.player_transition_queue_evidence_row=function()error('no rows before Load')end
              update();update();update()""")
            self.assertEqual((backend['loads'],backend['stops'],backend['native_actions']),(1,0,0))
            expected='SNAPSHOT_BYTES_1|'+SNAPSHOT+'|'+configuration(config)['plan']
            self.assertEqual(expected.count(d.SUFFIX),1)
            self.assertEqual(backend['boot_args'][4],hashlib.sha256(expected.encode()).hexdigest())
            events=backend['events'];index=len(events)-1-events[::-1].index('queue_evidence')
            self.assertEqual(events[index-1:index+2],['transition_watch','queue_evidence','transition'])

    def test_enabled_missing_footer_and_unknown_configuration_fail(self):
        rows,stop,core=fixture()
        for log in ('',render(rows,stop).split(d.PREFIX+'_STOP')[0]):
            self.assertFalse(d.evidence(log,CONFIG,core)['evidence_valid'])
        with generated_overlay(['--player-transition-queue-evidence']) as out:
            text=(out/'ui/config.lua.append').read_text().replace('@X4LANE_TOKEN@','a'*32).replace('@X4LANE_PORT@','42000').replace('@X4LANE_SLOT@','p1')
            for bad in (text.replace('queue_evidence = true','queue_evidence = false'),
                        text.replace('queue_evidence = true','queue_evidence = 1'),
                        text+'\nX4LAN_CONFIG.native_gate_player_transition_queue_evidence = true\n'):
                with self.assertRaises(ValueError):configuration(bad)

    def test_complete_modeled_contract_requires_its_own_diagnostic_footer(self):
        # Start from an otherwise valid full local receipt, so an unrelated
        # broken baseline cannot conceal a missing stream or manifest join.
        args=full_fixture();self.assertTrue(local_evidence(*args)['local_contract_valid'])
        args[1]+='\nnative_gate_player_transition_queue_evidence = true\n'
        cfg=configuration(args[1]);args[4]['queue_evidence']=1;args[3]['getup_diagnostic']['queue_evidence']=1
        old_digest=args[2]['states']['0']['digest']
        args[2]['states']['0']['digest']=hashlib.sha256(('SNAPSHOT_BYTES_1|'+SNAPSHOT+'|'+cfg['plan']).encode()).hexdigest()
        args[2]['transcript'][0]['packet']=args[2]['transcript'][0]['packet'].replace(old_digest,args[2]['states']['0']['digest'])
        rows,stop,_=fixture();rows[0].update(tick=31,boot=3,bound=1,bound_agent=0x30000,parent=0,parent_kind=0,order=4)
        rows[1].update(tick=32,order=6);stop['run']=cfg['run']
        args[0]+='\n'+render(rows,stop).replace('run='+RUN,'run='+cfg['run'])+'\n'
        result=local_evidence(*args);self.assertEqual(result['errors'],[])
        self.assertTrue(result['local_contract_valid'])
        for key in ('session_pass','parity_checked','independent_native_players','completed_release'):self.assertIs(result[key],False)
        for mutate in ('footer','marker','lane','digest'):
            changed=deepcopy(args)
            if mutate=='footer':changed[0]=changed[0].replace(d.PREFIX+'_STOP','MISSING_QUEUE_STOP')
            if mutate=='marker':del changed[4]['queue_evidence']
            if mutate=='lane':del changed[3]['getup_diagnostic']['queue_evidence']
            if mutate=='digest':changed[2]['states']['0']['digest']=full_fixture()[2]['states']['0']['digest']
            with self.subTest(mutate=mutate):self.assertFalse(local_evidence(*changed)['local_contract_valid'])

    def test_production_lua_drains_diagnostics_before_retaining_original_failure(self):
        rows,stop,_=fixture();lua=TransitionProbeTests().runtime([])
        lua.execute('X4LAN_CONFIG.token="'+TOKEN+'";X4LAN_SHA256={digest=function()return "'+RUN+'" end}')
        cfg=lua.globals().X4LAN_CONFIG;cfg.native_gate_player_probe='diagnostic-getup';cfg.native_gate_diagnostic_contract='diagnostic-getup-1'
        cfg.native_gate_player_transition_manifest=WIRE;cfg.native_gate_player_transition_last_tick=120;cfg.native_gate_player_transition_queue_evidence=True
        lua.globals().footer=transition_text([],[],[],bound=0,watch_rows=0,import_calls=0,failures=1).split('PLAYER_TRANSITION_STOP ',1)[1].replace('watches=0','watches=1')
        lua.globals().qfooter=words(stop);lua.globals().qrows=lua.table_from(['PLAYER_TRANSITION_QUEUE_DIAG '+words(r) for r in rows])
        lua.execute('''qcursor=0;gate.player_transition_watch_config=function()return 1 end
          gate.player_transition_queue_evidence_config=function(run)assert(run==X4LAN_SHA256.digest());return 1 end
          gate.player_transition_queue_evidence_status=function()return qfooter:gsub('rows=2','rows='..qcursor):gsub('closing=1','closing='..(stopped and 1 or 0)) end
          gate.player_transition_queue_evidence_row=function(i)assert(i==qcursor+1);qcursor=i;return qrows[i] end''')
        lua.execute(SOURCE);lua.execute('X4LAN_NATIVE_TRANSITION.configure(gate);stopped=true')
        with self.assertRaisesRegex(Exception,'transition observation failed'):lua.execute('X4LAN_NATIVE_TRANSITION.drain(gate,true)')
        output='\n'.join(lua.globals().logs.values())
        self.assertIn('QUEUE_DIAG schema=1 ordinal=2',output);self.assertIn('QUEUE_DIAG_STOP schema=1',output)
        self.assertIn('PLAYER_TRANSITION_STOP ',output);self.assertIn('failures=1',output)

if __name__=='__main__':unittest.main()
