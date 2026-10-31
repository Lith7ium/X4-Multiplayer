"""Fully modeled one-copy contract, with real independent receipt parsers.

This constructs owned synthetic evidence, never native gameplay proof. In
particular the one-row census is deliberately not the real saved universe.
"""
from copy import deepcopy
import contextlib,gzip,hashlib,io,json,re,struct,sys,tempfile,unittest
from unittest.mock import patch
from types import SimpleNamespace
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/dev'))
import build_native_gate_overlay as builder
from native_getup_diagnostic import Diagnostic,CONTRACT,FALSE_CLAIMS
from validate_native_getup_diagnostic import local_evidence,configuration,OPTIONS,SNAPSHOT
from native_getup_diagnostic_contract import FIXED
from native_world_evidence import DOMAINS,roots
from test_native_gate_getup import fixture as getup_fixture,CONFIG_GETUP
from test_native_gate_world_trace import record,log as trace_log
from test_native_gate_world_queue import log as queue_log,typed_fixture


def fixture():
    log,_=getup_fixture()
    log=log.replace('ship_test_macro','ship_arg_s_scout_01_a_macro').replace('PLAYER_GETUP_STATE_1','PLAYER_GETUP_LOCAL_1')
    log=log.replace('PLAYER_GETUP_STOP schema=1','PLAYER_GETUP_STOP schema=2 contract='+CONTRACT+' native_clients=1 parity_checked=0')
    config=CONFIG_GETUP.replace('agreed-getup','diagnostic-getup').replace('ship_test_macro','ship_arg_s_scout_01_a_macro')
    config+='\nX4LAN_CONFIG.slot = "p1"\nnative_gate_diagnostic_contract = "'+CONTRACT+'"\n'
    config+='X4LAN_CONFIG.host = "127.0.0.1"\nX4LAN_CONFIG.port = 42000\n'
    config+='X4LAN_CONFIG.enabled = false\n'
    config+='native_gate_snapshot_sha = "'+SNAPSHOT+'"\nnative_gate_hash = "battle"\n'
    config+='native_gate_world_trace_ticks = 2\nnative_gate_world_hash_watch = {"0000000000000063"}\n'
    for key in ('test','serial_pathing','world_identities','actor_diag','flight_diag','ai_diag','agent_diag','ship_diag','nav_diag','spline_diag','world_timing_fields'):
        config+='native_gate_'+key+' = true\n'
    cfg=configuration(config);d=Diagnostic(cfg['token'])
    events=[]
    for tick,hook in [(0,'loaded'),(1,'baseline'),(1,'path_before'),(1,'path_after'),(2,'path_before'),(2,'path_after'),(1,'order')]:
        e=record(len(events)+1,tick=tick,hook=hook);e.update(boot=3,caller=0,script='-',pc_before=0,pc_after=0,ui=1)
        for state in e['states'].values():
            state.update(main_count=0,main_capacity=0,pending_count=0,pending_capacity=0,unknown_refs=0)
            state['blocks'].update(main_queue=b'',main_refs=b'',pending_queue=b'',pending_refs=b'')
        events.append(e)
    log+='\n'+trace_log(events).replace('last=12','last=2')+'\n'+queue_log(typed_fixture(0x2c513b8),schema=3)+'\n'
    footer=struct.pack('<IIQ',123,1,0);body=bytes(4)+footer
    row=dict(type=123,flags=1,parent=0,hashes=(hashlib.sha256(body).digest(),)*4)
    world={}
    for tick in range(1,121):
        if tick==1:
            log+=f'NATIVE_GATE WORLD_OBJECT tick=1 id=0000000000000063 type=7b flags=1 parent=0000000000000000 '+ ' '.join(k+'='+v.hex() for k,v in zip(DOMAINS,row['hashes']))+' timing_fields=00000000\n'
        for domain in DOMAINS:log+=f'NATIVE_GATE WORLD_DETAIL tick={tick} id=0000000000000063 domain={domain} bytes={body.hex()}\n'
        world[tick]=f'WORLD_IMPORTED_1:{tick}:1:'+':'.join(roots({99:row}))
        log+=f'NATIVE_GATE WORLD_HASH tick={tick} objects=1 present=1 changed={int(tick==1)} unknown_class=0 unknown_motion=0 unknown_references=0 state={world[tick]}\n'
    for tick in range(121):
        if not tick:preimage='SNAPSHOT_BYTES_1|'+SNAPSHOT+'|'+cfg['plan']
        else:
            player=re.search(rf'PLAYER_GETUP_STATE tick={tick} data=([^\n]+)',log)[1]
            inputs=re.search(rf'PLAYER_INPUTS tick={tick} data=([^\n]+)',log)[1]
            preimage='BATTLE_FIXTURE_PENDING_1|'+inputs+'|'+player+'|'+world[tick]
        bits=struct.pack('>d',100+tick/60).hex()
        packet=d.packet('NGDSTATE',tick,bits,bits,hashlib.sha256(preimage.encode()).hexdigest(),'87010000' if tick==29 else '00000000')
        d.receive(packet,('127.0.0.1',42000),float(tick))
    d.receive(d.packet('NGDDONE',120,1,1,121,121,0,0),('127.0.0.1',42000),121.)
    status=dict(schema=1,contract=CONTRACT,run=d.run,epoch=1,native_clients=1,physical_slot=1,options=OPTIONS,tick=120,issued=120,
                calls=121,returns=121,unwinds=0,failures=0,terminal_started=1,terminal_finished=1,closed=1,restored=1,reason=1,stage='closed',
                **{key:0 for key in FALSE_CLAIMS})
    log+='NATIVE_GATE GETUP_DIAGNOSTIC_STOP '+' '.join(f'{k}={v}' for k,v in status.items())+'\n'
    log+='NATIVE_GATE STOP slot=p1 completed=120 reason=1 restored=1 wait_game_delta=0 max_dt_error=0\n'
    for name in ('FLIGHT','AI','AGENT','SHIP','NAV','SPLINE'):log+=f'NATIVE_GATE {name}_STOP restored=1 failures=0\n'
    log+='NATIVE_GATE BOOT phase=3 dt_restored=1\nNATIVE_GATE PATH joins=1 restored=1 rng_seeds=0\nNATIVE_GATE WORLD_HASH_STOP scans=120 failures=0\n'
    log+='NATIVE_GATE WORLD_IDENTITY_STOP calls=1 count=1 late=0 restored=1 failures=0\n'
    file_entry=dict(path=str(Path(__file__).resolve()),sha256='a'*64,bytes=1)
    manifest=dict(FIXED,token=d.token,run=d.run,host='127.0.0.1',port=42000,lane='b4',execution_receipt=file_entry,inputs=dict(lane_tool=file_entry,
        snapshot=dict(file_entry,sha256=SNAPSHOT),base=dict(path=str(Path(__file__).resolve().parent),files={'ui/config.lua':dict(sha256='a'*64,bytes=1)}),
        overlay=dict(path=str(Path(__file__).resolve().parent),files={'native/x4lan_gate_64.dll':dict(sha256='a'*64,bytes=1)})))
    lane=dict(lane_restored=True,verdict='pass',exit_code=0,getup_diagnostic=dict(FIXED,run=d.run,host='127.0.0.1',port=42000,lane='b4',installed_gate_sha256='a'*64))
    return [log,config,json.loads(json.dumps(d.summary())),lane,manifest]


@contextlib.contextmanager
def generated_overlay(extra=()):
    with tempfile.TemporaryDirectory() as directory:
        root=Path(directory);dll=root/'owned.dll';dll.write_bytes(b'owned fake DLL; never loaded')
        save=root/'owned.xml.gz'
        save.write_bytes(gzip.compress(b'<savegame><component id="[0xae79c]" class="ship_s" macro="ship_arg_s_scout_01_a_macro" owner="player"><movement class="player" player="1"/></component></savegame>'))
        raw=save.read_bytes();watch=root/'watch.json'
        watch.write_text(json.dumps(dict(schema=2,snapshot_sha256=SNAPSHOT,last_tick=120,entries=[dict(saved='00000000000ae79c',controller='player',macro='ship_arg_s_scout_01_a_macro',owner='player')])))
        out=root/'overlay'
        argv=['builder','--gate-dll',str(dll),'--snapshot',str(save),'--mode','battle','--player-probe','diagnostic-getup',
              '--player-transition-observer','--player-transition-manifest',str(watch),'--serial-pathing','--world-identities',
              '--flight-diag','--ai-diag','--agent-diag','--ship-diag','--nav-diag','--spline-diag','--world-hash-stride','1',
              '--world-hash-schema','WORLD_IMPORTED_2','--world-hash-watch','000000000006d940','--world-trace-ticks','12',
              '--world-trace-early','--world-queue-trace','--out',str(out)]
        original_sha,original_file=hashlib.sha256,hashlib.file_digest
        # Only this owned synthetic save gets the fixture pin. Real XML
        # identity validation, builder emission and closed parser all run.
        def pinned_sha(data=b'',*args,**kwargs):
            return SimpleNamespace(hexdigest=lambda:SNAPSHOT) if data==raw else original_sha(data,*args,**kwargs)
        def pinned_file(stream,digest,*args,**kwargs):
            return SimpleNamespace(hexdigest=lambda:SNAPSHOT) if Path(stream.name)==save else original_file(stream,digest,*args,**kwargs)
        with patch.object(sys,'argv',argv+list(extra)),patch.object(hashlib,'sha256',pinned_sha),patch.object(hashlib,'file_digest',pinned_file),contextlib.redirect_stdout(io.StringIO()):
            builder.main()
        yield out


class DiagnosticEvidenceTests(unittest.TestCase):
    def test_complete_modeled_local_contract_never_claims_parity_or_release(self):
        result=local_evidence(*fixture());self.assertEqual(result['errors'],[])
        self.assertTrue(result['local_contract_valid']);self.assertTrue(result['request_accepted']);self.assertTrue(result['delivery_observed'])
        for key in FALSE_CLAIMS:self.assertIs(result[key],False)

    def test_raw_failures_missing_receipts_and_config_changes_fail(self):
        original=fixture();self.assertTrue(local_evidence(*original)['local_contract_valid'])
        for before,after in [('WORLD_HASH_STOP scans=120 failures=0','WORLD_HASH_STOP scans=120 failures=1'),
                             ('queue_failures=0','queue_failures=1'),
                             ('unmatched=0','unmatched=1'),('PLAYER_ORIGIN_STOP','MISSING_ORIGIN_STOP'),
                             ('GETUP_DIAGNOSTIC_STOP','MISSING_DIAGNOSTIC_STOP'),('calls=121 returns=121','calls=122 returns=121'),
                             ('raw_world_coverage=0','raw_world_coverage=1'),('reason=1 stage=closed','reason=1 stage=closing')]:
            with self.subTest(before=before):
                args=deepcopy(original);self.assertIn(before,args[0]);args[0]=args[0].replace(before,after)
                self.assertFalse(local_evidence(*args)['local_contract_valid'])
        for before,after in [('diagnostic-getup','agreed-getup'),('p1','p2'),('|1|120','|1|119'),('native_gate_ai_diag = true','native_gate_ai_diag = false')]:
            args=deepcopy(original);args[1]=args[1].replace(before,after);self.assertFalse(local_evidence(*args)['local_contract_valid'])

    def test_actual_builder_output_matches_closed_passive_configuration(self):
        with generated_overlay() as out:
            text=(out/'ui/config.lua.append').read_text()
            for old,new in {'@X4LANE_TOKEN@':'a'*32,'@X4LANE_PORT@':'42000','@X4LANE_SLOT@':'p1'}.items():text=text.replace(old,new)
            cfg=configuration(text)
            self.assertEqual(cfg['native_values']['actor_diag'],'true');self.assertEqual(cfg['schema'],'WORLD_IMPORTED_2')
            self.assertEqual(cfg['trace_last'],12);self.assertEqual(cfg['watches'],{0x6d940})
            flag='X4LAN_CONFIG.native_gate_actor_diag = true'
            self.assertEqual(text.count(flag),1)
            for replacement in ('',flag.replace('true','false'),flag.replace('true','1'),flag+'\n'+flag):
                with self.subTest(replacement=replacement),self.assertRaises(ValueError):configuration(text.replace(flag,replacement))

    def test_coordinator_transcript_and_cleanup_are_independently_checked(self):
        for mutation in ('missing','digest','outgoing','endpoint','summary','claim','cleanup'):
            with self.subTest(mutation=mutation):
                args=fixture();doc=args[2]
                if mutation=='missing':doc['transcript'].pop(2)
                if mutation=='digest':doc['states']['30']['digest']='0'*64
                if mutation=='outgoing':doc['transcript'][1]['packet']=doc['transcript'][1]['packet'].replace('NGDGO','NGGO')
                if mutation=='endpoint':doc['transcript'][2]['endpoint'][1]+=1
                if mutation=='summary':doc['native_clients']=2
                if mutation=='claim':doc['session_pass']=True
                if mutation=='cleanup':args[3]['lane_restored']=False
                self.assertFalse(local_evidence(*args)['local_contract_valid'])


if __name__=='__main__':unittest.main()
