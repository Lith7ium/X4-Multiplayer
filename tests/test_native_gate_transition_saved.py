from copy import deepcopy
import contextlib
import gzip
import hashlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/dev'))
from native_transition_manifest import manifest,decode_wire,configured
from build_native_gate_overlay import _snapshot_starts
import build_native_gate_overlay as builder
from native_transition_evidence import evidence,configured_evidence,STATE
from validate_native_gate import validate
from test_native_gate_transition import fixture,log
import test_native_gate_transition_probe as probe
SOURCE,CONFIG=probe.SOURCE,probe.CONFIG

WIRE='2|0000000000000063/n/ship_test_macro/argon'
CONFIG2=CONFIG+'\nnative_gate_player_transition_manifest = "'+WIRE+'"\nnative_gate_player_transition_last_tick = 120\n'
ENABLE='NATIVE_GATE PLAYER_TRANSITION_ENABLED schema=2 resumed=0 actions=0 singleton=1 early_import_coverage=0 watches=0 tickets=0 raw_world_coverage=0\n'


def captures(records=None):
    rows=deepcopy(fixture() if records is None else records)
    for row in rows:
        if row['kind']==5:row['queue']=1;row['caller']=0xeacf66
    initial=deepcopy(fixture()[0]['s0'])
    watch={'schema':2,'index':1,'type':0x2c51158,'macro':'ship_test_macro','owner':'argon','generation':0,'raw_world_coverage':0,'s0':initial}
    raw=struct.pack('<QII',0x140000000+0x2c51c38,0,0).hex()
    tickets=[dict(schema=2,ticket=1,state=3,saved=99,runtime=123,command=0x70000,agent=initial['agent'],enqueue=4,dispatch=5,mode=0,before=raw,after=raw,image=0x140000000,raw_world_coverage=0)] if rows else []
    return rows,[watch],tickets


def text(rows,watches,captured_tickets,**overrides):
    def value(key,item):
        if key=='s0':return ','.join(f'{item[k]:x}' if i<10 else str(item[k]) for i,k in enumerate(STATE))
        return f'{item:x}' if key in ('type','saved','runtime','command','agent','image') else str(item)
    events=log(rows).splitlines()[:-1]
    watch_lines=['NATIVE_GATE PLAYER_TRANSITION_WATCH '+' '.join(f'{k}={value(k,v)}' for k,v in w.items()) for w in watches]
    ticket_lines=['NATIVE_GATE PLAYER_TRANSITION_TICKET '+' '.join(f'{k}={value(k,v)}' for k,v in t.items()) for t in captured_tickets]
    extra=dict(schema=2,watches=len(watches),bound=len(watches),watch_rows=len(watches),tickets=len(captured_tickets),ticket_rows=len(captured_tickets),
               watch_failures=0,queue_failures=0,import_calls=1,prebind=1,last=120,queue_calls=len(captured_tickets)*2,queue_returns=len(captured_tickets)*2,
               queue_other=0,queue_unknown=0,outstanding=0,raw_world_coverage=0)
    extra.update(overrides);footer=log(rows,**extra).splitlines()[-1]
    return '\n'.join(watch_lines+events+ticket_lines+[footer])


class SavedTransitionEvidenceTests(unittest.TestCase):
    def test_impossible_dispatch_pointer_accounting_and_aliases_cannot_complete(self):
        for change in ('caller','pointer','accounting','alias'):
            rows,watches,tickets=captures();overrides={}
            if change=='caller':rows[4]['caller']=1
            if change=='pointer':tickets[0]['command']=1<<80
            if change=='accounting':overrides=dict(queue_calls=100,queue_returns=100)
            if change=='alias':
                second=deepcopy(watches[0]);second['index']=2;second['s0']['saved']=100;watches.append(second)
                overrides['import_calls']=2
            result=evidence(text(rows,watches,tickets,**overrides))
            self.assertFalse(result['evidence_valid'],change);self.assertEqual(result['observed_model_completions'],[],change)

    def test_emitted_widths_syntax_booleans_and_row_caps_refuse(self):
        for kind,key,value in [('event','thread',1<<32),('event','caller',1<<32),('event','enter',1<<64),
                               ('event','active0',2),('ticket','agent',1<<64),('ticket','enqueue',1<<64),
                               ('watch','generation',1<<32),('state','controller_type',1<<32)]:
            rows,watches,tickets=captures()
            target={'event':rows[0],'ticket':tickets[0],'watch':watches[0],'state':watches[0]['s0']}[kind];target[key]=value
            self.assertFalse(evidence(text(rows,watches,tickets))['evidence_valid'],(kind,key))
        original=text(*captures())
        for changed in (original.replace('caller=1','caller=+1',1),
                        original.replace('command=70000','command=-70000'),
                        original.replace('schema=2','schema=+2',1),
                        original.replace('s0=63,','s0=+63,',1),
                        original.replace('PLAYER_TRANSITION_WATCH ','PLAYER_TRANSITION_WATCH '+' '*1024,1),
                        original.replace('PLAYER_TRANSITION_TICKET ','PLAYER_TRANSITION_TICKET '+' '*768,1),
                        original.replace('PLAYER_TRANSITION_STOP ','PLAYER_TRANSITION_STOP '+' '*1536,1),
                        original.replace('PLAYER_TRANSITION ','PLAYER_TRANSITION '+' '*2048,1),
                        '\n'.join([original.splitlines()[0]]*800)):
            self.assertNotEqual(changed,original);self.assertFalse(evidence(changed)['evidence_valid'])
        for key,value in [('complete',1<<32),('queue_calls',1<<64),('excluded',-1)]:
            self.assertFalse(evidence(text(*captures(),**{key:value}))['evidence_valid'],key)

    def test_main_validator_compares_portable_config_without_comparing_local_pointers(self):
        with tempfile.TemporaryDirectory() as directory:
            run=Path(directory)
            (run/'result.json').write_text(json.dumps({'verdict':'fail','exit_code':1}))
            states={str(tick):{str(slot):{'game':20+tick/60,'real':20+tick/60,'digest':'a'*64,'command':'00000000'}
                    for slot in (1,2)} for tick in (0,1)}
            (run/'native-barrier.json').write_text(json.dumps({'states':states,'hold_seconds':0,'verdict':'fail','failure':'fixture'}))
            for slot in (1,2):
                client=run/f'client{slot}';(client/'installed').mkdir(parents=True)
                (client/'result.json').write_text(json.dumps({'lane_restored':True}))
                (client/'installed/x4_lan_lab-config.lua').write_text(CONFIG2)
                rows,watches,tickets=captures()
                if slot==2:
                    # Separate processes have different addresses/runtime IDs.
                    for state in [r[k] for r in rows for k in ('s0','s1')]+[watches[0]['s0']]:
                        for key in ('runtime','object','controller','agent','slots','faction','model'):
                            if state[key]:state[key]+=0x10000
                    for row in rows:
                        for key in ('old','new','result'):
                            if row[key]:row[key]+=0x10000
                    tickets[0]['runtime']+=0x10000;tickets[0]['agent']+=0x10000;tickets[0]['command']+=0x10000
                (client/'engine-output.log').write_text(f'NATIVE_GATE STOP slot=p{slot} completed=1 reason=1 restored=1 wait_game_delta=0 max_dt_error=0\n'+ENABLE+text(rows,watches,tickets))
            result=validate(run,'battle')
            self.assertEqual([c['evidence_valid'] for c in result['native_player_transitions']],[True,True])
            self.assertFalse(any('configurations differ' in e for e in result['errors']))
            self.assertFalse(result['session_pass']);self.assertIn('three-second hold not proven',result['errors'])
            config=run/'client2/installed/x4_lan_lab-config.lua';config.write_text(CONFIG2.replace('last_tick = 120','last_tick = 119'))
            engine=run/'client2/engine-output.log';engine.write_text(engine.read_text().replace('last=120','last=119'))
            result=validate(run,'battle')
            self.assertEqual([c['evidence_valid'] for c in result['native_player_transitions']],[True,True])
            self.assertIn('native transition observer configurations differ between players',result['errors'])

    def test_separate_local_ticket_completes_without_raw_world_coverage_or_authority(self):
        rows,watches,tickets=captures();errors=[]
        result=configured_evidence(ENABLE+text(rows,watches,tickets),CONFIG2,1,{999},errors)
        self.assertEqual(errors,[]);self.assertEqual(result['observed_model_completions'],[5])
        self.assertFalse(result['atomic_lease']);self.assertFalse(result['action_origin_proven'])
        self.assertEqual(result['stop']['raw_world_coverage'],0)
        result=evidence(text(*captures([])));self.assertTrue(result['evidence_valid']);self.assertFalse(result['transition_exercised'])

    def test_each_ticket_or_initial_lifetime_tamper_refuses_interpreted_completion(self):
        for key,value in [('state',2),('runtime',124),('agent',0x30001),('saved',100),('enqueue',3),('dispatch',4),('mode',1),('image',0x150000000),('raw_world_coverage',1),('before','00'*16),('after','00'*16)]:
            rows,watches,tickets=captures();tickets[0][key]=value
            result=evidence(text(rows,watches,tickets))
            self.assertFalse(result['evidence_valid'],key);self.assertEqual(result['observed_model_completions'],[],key)
        for key,value in [('generation',1),('type',0x2c39488),('owner','xenon'),('raw_world_coverage',1)]:
            rows,watches,tickets=captures();watches[0][key]=value
            result=evidence(text(rows,watches,tickets));self.assertFalse(result['evidence_valid']);self.assertEqual(result['observed_model_completions'],[],key)
        for key,value in [('runtime',124),('object',0x10001),('agent',0x30001),('faction',0x40001)]:
            rows,watches,tickets=captures();watches[0]['s0'][key]=value
            result=evidence(text(rows,watches,tickets));self.assertFalse(result['evidence_valid']);self.assertEqual(result['observed_model_completions'],[],key)
        rows,watches,tickets=captures();watches[0]['s0']['player']=1
        self.assertFalse(evidence(text(rows,watches,tickets))['evidence_valid'])

    def test_missing_duplicate_over_capacity_partial_stop_and_false_coverage_fail(self):
        rows,watches,tickets=captures()
        for changed in (text(rows,watches,[]),text(rows,[],tickets),text(rows,watches+watches,tickets),text(rows,watches,tickets+tickets)):
            result=evidence(changed);self.assertFalse(result['evidence_valid'])
        for key,value in [('bound',0),('watch_rows',0),('ticket_rows',0),('queue_returns',1),('outstanding',1),('tickets',257),('watch_failures',1),('queue_failures',1),('raw_world_coverage',1),('restore_open',1)]:
            self.assertFalse(evidence(text(rows,watches,tickets,**{key:value}))['evidence_valid'],key)
        errors=[];configured_evidence(ENABLE+text(rows,watches,tickets),CONFIG2.replace('ship_test_macro','wrong_macro'),1,{99},errors);self.assertTrue(errors)
        rows[4]['queue_errors']=256
        self.assertEqual(evidence(text(rows,watches,tickets))['observed_model_completions'],[])

    def test_production_lua_watch_and_ticket_drain_and_independent_parser(self):
        rows,watches,tickets=captures();capture=text(rows,watches,tickets).splitlines()
        helper=probe.TransitionProbeTests();lua=helper.runtime(rows)
        lua.globals().footer=capture[-1].split('PLAYER_TRANSITION_STOP ',1)[1]+' exception_schema=1 direct_calls=3 direct_returns=3 direct_unwinds=0 queue_exception_cleanup=0'
        lua.globals().watch_line=capture[0].split('NATIVE_GATE ',1)[1]
        lua.globals().ticket_line=capture[-2].split('NATIVE_GATE ',1)[1]
        lua.globals().X4LAN_CONFIG.native_gate_player_transition_manifest=WIRE
        lua.globals().X4LAN_CONFIG.native_gate_player_transition_last_tick=120
        lua.execute('''
          watches=0;tickets=0
          gate.player_transition_watch_config=function(w,last) assert(last==120);configured_watch=w;return 1 end
          gate.player_transition_watch=function(i) assert(i==watches+1 and i==1);watches=i;return watch_line end
          gate.player_transition_ticket=function(i) assert(stopped and i==tickets+1 and i==1);tickets=i;return ticket_line end
          local status=gate.player_transition_status
          gate.player_transition_status=function() return status():gsub('watch_rows=%d+','watch_rows='..watches):gsub('ticket_rows=%d+','ticket_rows='..tickets) end
        ''')
        lua.execute(SOURCE);lua.execute('X4LAN_NATIVE_TRANSITION.configure(gate);ready=5;X4LAN_NATIVE_TRANSITION.drain(gate,false)')
        self.assertEqual(lua.globals().watches,1);self.assertEqual(lua.globals().tickets,0)
        lua.execute(SOURCE);lua.execute('X4LAN_NATIVE_TRANSITION.configure(gate);stopped=true;X4LAN_NATIVE_TRANSITION.drain(gate,true)')
        errors=[];result=configured_evidence('\n'.join(lua.globals().logs.values()),CONFIG2,1,{999},errors)
        self.assertEqual(errors,[]);self.assertEqual(result['observed_model_completions'],[5])
        self.assertEqual(lua.globals().configured_watch,WIRE)

    def test_lua_exports_footer_when_ticket_drain_refuses_an_open_native_call(self):
        rows,watches,tickets=captures();capture=text(rows,watches,tickets,open=1,restore_open=1).splitlines()
        lua=probe.TransitionProbeTests().runtime(rows)
        lua.globals().footer=capture[-1].split('PLAYER_TRANSITION_STOP ',1)[1]
        lua.globals().watch_line=capture[0].split('NATIVE_GATE ',1)[1]
        lua.globals().X4LAN_CONFIG.native_gate_player_transition_manifest=WIRE
        lua.globals().X4LAN_CONFIG.native_gate_player_transition_last_tick=120
        lua.execute('''
          watches=0
          gate.player_transition_watch_config=function() return 1 end
          gate.player_transition_watch=function(i) watches=i;return watch_line end
          gate.player_transition_ticket=function() return nil end
          local status=gate.player_transition_status
          gate.player_transition_status=function() return status():gsub('watch_rows=%d+','watch_rows='..watches):gsub('ticket_rows=%d+','ticket_rows=0') end
        ''')
        lua.execute(SOURCE);lua.execute('X4LAN_NATIVE_TRANSITION.configure(gate);ready=5;stopped=true')
        with self.assertRaisesRegex(Exception,'restoration/capture incomplete'):lua.execute('X4LAN_NATIVE_TRANSITION.drain(gate,true)')
        result=evidence('\n'.join(lua.globals().logs.values()))
        self.assertEqual(result['stop']['restore_open'],1);self.assertEqual(result['stop']['ticket_rows'],0)
        self.assertFalse(result['evidence_valid']);self.assertEqual(result['observed_model_completions'],[])


class TransitionManifestTests(unittest.TestCase):
    def test_builder_installs_snapshot_checked_separate_manifest_and_refuses_mismatch_before_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);dll=root/'gate.dll';dll.write_bytes(b'fixture');save=root/'snapshot.xml.gz'
            save.write_bytes(gzip.compress(b'<savegame><component class="ship_s" macro="ship_test_macro" owner="player" id="[0x63]"><movement class="player" player="1"/></component></savegame>'))
            data=dict(schema=2,snapshot_sha256=hashlib.sha256(save.read_bytes()).hexdigest(),last_tick=120,
                      entries=[dict(saved='0000000000000063',controller='player',macro='ship_test_macro',owner='player')])
            selected=root/'watch.json';selected.write_text(json.dumps(data))
            base=['builder','--gate-dll',str(dll),'--snapshot',str(save),'--mode','battle','--serial-pathing',
                  '--player-probe','observe','--world-hash-stride','1','--world-hash-watch','0000000000000064',
                  '--world-trace-ticks','12','--world-trace-early','--world-queue-trace',
                  '--player-transition-observer','--player-transition-manifest',str(selected)]
            out=root/'overlay'
            with patch.object(sys,'argv',base+['--out',str(out)]),contextlib.redirect_stdout(io.StringIO()):builder.main()
            config=(out/'ui/config.lua.append').read_text()
            self.assertEqual(configured(config),([data['entries'][0]],120))
            self.assertIn('0000000000000064',config) # Raw watch remains separate.
            data['entries'][0]['owner']='argon';selected.write_text(json.dumps(data))
            refused=root/'refused'
            with patch.object(sys,'argv',base+['--out',str(refused)]),contextlib.redirect_stderr(io.StringIO()),self.assertRaises(SystemExit):builder.main()
            self.assertFalse(refused.exists())

    def build(self,change=lambda d:None,xml_change=lambda s:s):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'snapshot.xml.gz'
            xml='<savegame><component class="ship_s" macro="ship_test_macro" owner="player" id="[0x63]"><movement class="player" player="1"/></component></savegame>'
            path.write_bytes(gzip.compress(xml_change(xml).encode()))
            data=dict(schema=2,snapshot_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),last_tick=120,
                      entries=[dict(saved='0000000000000063',controller='player',macro='ship_test_macro',owner='player')])
            change(data);return manifest(data,path,_snapshot_starts)

    def test_snapshot_binding_and_wire_config(self):
        wire,last=self.build();self.assertEqual(wire,'2|0000000000000063/p/ship_test_macro/player');self.assertEqual(last,120)
        config='native_gate_player_transition_manifest = "'+wire+'"\nnative_gate_player_transition_last_tick = 120'
        self.assertEqual(configured(config),(decode_wire(wire),last))
        with self.assertRaises(ValueError):configured(config+config)

    def test_snapshot_metadata_and_bounded_manifest_refuse(self):
        changes=[lambda d:d.update(schema=1),lambda d:d.update(schema=2.0),lambda d:d.update(last_tick=True),lambda d:d.update(last_tick=121),lambda d:d.update(snapshot_sha256='0'*64),
                 lambda d:d.update(entries=[]),lambda d:d.update(entries=d['entries']*2),lambda d:d['entries'][0].update(owner='argon'),
                 lambda d:d['entries'][0].update(controller='navigation'),lambda d:d['entries'][0].update(saved='0'*16),lambda d:d['entries'][0].update(extra=1)]
        for change in changes:
            with self.assertRaises(ValueError):self.build(change)
        for old,new in [('player="1"','player="0"'),('ship_s','ship_xl'),('movement','other'),('id="[0x63]"','id="[0x64]"')]:
            with self.assertRaises(ValueError):self.build(xml_change=lambda s:s.replace(old,new))


if __name__=='__main__':unittest.main()
