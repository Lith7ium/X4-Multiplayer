"""Unmodified production main, modeled native joins, real bounded log files.

Adapted from the independent review counterexample; no game or proc access.
"""
import contextlib
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest import mock

root=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(root/'tools/dev'),str(root/'tests')]
import linux_load_observer_probe as observer
import linux_portable_probe as original
from test_linux_portable_probe import terminal,load_terminal,math_terminal,math_capability
import portable_import_capability as cap
from test_native_portable import contract

probe=original

def record(marker,values):
    return marker+' '+' '.join(k+'='+(json.dumps(v,separators=(',',':')) if isinstance(v,list) else str(v)) for k,v in values.items())+'\n'

def run_terminal_case(case):
    with tempfile.TemporaryDirectory() as temporary:
        base=Path(temporary)
        fields=('claim','agent_so','agent_log','world_log','ready_file','report','contract','config','disarm_file')
        args=types.SimpleNamespace(**{name:base/name for name in fields},agent_sha256='a'*64,
                                  run_id='portable-native-12345678',review_sha256='b'*64)
        args.agent_so.write_bytes(b'owned nongame fixture');args.claim.write_text('{}')
        euler=case.startswith('euler_')
        cfg=contract()
        if euler:
            cfg['snapshot_sha256']=cap.SNAPSHOT_SHA256;cfg['import_math']=cap.capability(cap.SNAPSHOT_SHA256)
        args.contract.write_text(json.dumps(cfg));sha=probe.validate_contract(cfg)
        capability=math_capability() if euler else {'mode':probe.MODE}
        if euler:capability['contract_sha256']=sha
        participant={'platform':'linux','executable_sha256':observer.EXE_SHA,'agent_sha256':args.agent_sha256}
        config={'startup_timeout_ms':100,'timeout_ms':1000,'ticks':12,'token':'1'*32,'participants':{'2':participant},
                'coordinator':{'port':30123,'report_path':str(base/'coordinator-report')}}
        endpoint={'source':['127.0.0.1',30124]}
        report={'verdict':'pass','contract_sha256':sha,'participants':config['participants'],'ticks':12,
                'peer_endpoints':{'2':endpoint},'frames':{'0':{'2':{'state':{'game_bits':probe.SAVED_GAME,'tick':0}}},
                                                     '3':{'2':{'state':{'game_bits':probe.SAVED_GAME},'real_bits':'4059000000000000'}}}}
        class Process:
            pid,start,uid,table=100,200,1000,0x30000000
            def __init__(self,*args):self.closed=False
            def verify(self):pass
            def verify_agent_mapping(self):return {'mapping':'owned-modeled-agent'}
            def bridge_mapping(self,*args):return {'mapping':'owned-modeled-bridge'}
            def endpoint(self,*args):return endpoint
            def clocks(self):return [probe.SAVED_GAME,'4059000000000000','4059000000000000']
            def restoration(self):
                return {**{key:{'matches_original':True} for key in observer.REGIONS},'same_vbi_table':True,'both_slot13_clear':True}
            def word(self,address):return self.table if address==observer.VBI_GLOBAL else 0x10000000
            def rotation_manager(self):return 0x50000000
            def rotation_restoration(self):return {name:dict(matches_original=True,sha256=pin) for name,(_,_,pin) in probe.ROTATION_REGIONS.items()}
            def read(self,address,size):return b'\0'*size
            def close(self):self.closed=True
        class Coordinator:
            def __init__(self,*args):self.calls=0;self.evidence={'terminal_bound_while_alive':True}
            def verify(self):pass
            def report(self,*args):
                self.calls+=1
                return None if self.calls==1 else report
        logs=[]
        class ScriptedLog(observer.FreshLog):
            def __init__(self,path):super().__init__(path);self.calls=0;logs.append(self)
            def lines(self):
                self.calls+=1;data=''
                if self.path==args.agent_log:
                    if self.calls==1:
                        data='X4MP_AGENT VERIFIED build=23660954 version=9.00 (611726) sites=25 pid=100\n'
                    elif self.calls==2:
                        native=terminal()
                        if euler:native.update(mode=probe.EULER_MODE,result='passed_subset_euler_import')
                        data='X4MP_AGENT LOAD_OBSERVER INSTALLED sites=3\n'+record('X4MP_AGENT LOAD_OBSERVER STATE',load_terminal())+record('X4MP_AGENT PORTABLE STATE',native)
                        if euler and case!='euler_missing_math':
                            math=math_terminal();math['contract']=sha
                            if case=='euler_class_swap':
                                for field in ('calls','returns','euler','absent','words'):
                                    a,b='role1_nav_'+field,'role1_player_'+field
                                    math[a],math[b]=math[b],math[a]
                            if case=='euler_wrong_serial':math['serial']='2'
                            data+=record('X4MP_AGENT ROTATION',math)
                        if case=='agent_unterminated_refusal':data+='X4MP_AGENT REFUSED schema=x4-build-refusal-v1 pid=100'
                        elif case=='agent_unread_backlog':
                            left=1048576-len(data.encode());row='X4MP_AGENT WAIT reason=review_padding\n'
                            number=left//len(row)-2;data+=row*number;left-=number*len(row)
                            prefix='X4MP_AGENT WAIT reason='
                            data+=prefix+'x'*(left-len(prefix)-1)+'\n'
                            assert len(data.encode())==1048576
                            data+='X4MP_AGENT REFUSED reason=native_fault\n'
                elif self.calls==1:
                    data=f'[X4LAN] PORTABLE_BOOT MENU schema={probe.SCHEMA_SHA} snapshot={cfg["snapshot_sha256"]} run={args.run_id}\n'
                elif self.calls==2:
                    data=f'[X4LAN] PORTABLE_BOOT LOAD_REQUEST snapshot={cfg["snapshot_sha256"]} contract={sha} run={args.run_id}\n'
                    if case=='world_unterminated_error':data+=f'[X4LAN] PORTABLE_BOOT ERROR failed run={args.run_id}'
                if data:
                    with self.path.open('ab') as stream:stream.write(data.encode())
                return super().lines()
        Path(str(args.agent_log)+'.imports').write_bytes(b'\0'*16)
        stdout=io.StringIO();fd=os.open(os.devnull,os.O_RDONLY)
        with contextlib.ExitStack() as stack:
            contexts=[mock.patch.object(probe.argparse.ArgumentParser,'parse_args',return_value=args),
                mock.patch.object(probe,'read_private',return_value=(config,'c'*64)),
                mock.patch.object(probe,'validate_session'),
                mock.patch.object(probe,'reviewed_capability',return_value=capability),
                mock.patch.object(probe,'review_bundle',return_value=({'participant':participant,'files':{'fixture/native/x4mp_load_probe.so':'d'*64}},cfg)),
                mock.patch.object(probe,'verify_coordinator',return_value=fd),
                mock.patch.object(probe,'CoordinatorLifetime',Coordinator),
                mock.patch.object(probe,'PortableProcess',Process),
                mock.patch.object(probe,'publish_request',return_value={'request':'owned-modeled'}),
                mock.patch.object(probe,'request_disarm'),
                mock.patch.object(probe,'coordinator_evidence',return_value={'valid':True,'errors':[]}),
                mock.patch.object(probe,'encode_frame'),
                mock.patch.object(probe,'held_clock_samples',return_value=[]),
                mock.patch.object(observer,'FreshLog',ScriptedLog),
                mock.patch.object(observer,'import_map',return_value=({},{})),
                mock.patch.object(observer,'fixture_identity',return_value={}),
                contextlib.redirect_stdout(stdout)]
            for context in contexts:stack.enter_context(context)
            try:probe.main()
            except SystemExit as error:exit_code=error.code
        evidence=json.loads(args.report.read_text())
        return {'exit_code':exit_code,'verdict':evidence['verdict'],'error':evidence.get('error'),
            'rotation_import_verified':evidence.get('rotation_import_verified'),
            'tails':[len(log.tail) for log in logs], 'unread_bytes':[log.path.stat().st_size-log.offset for log in logs]}


class TerminalMain(unittest.TestCase):
    def test_actual_v2_main_requires_independent_class_receipt_even_with_complete_portable_counts(self):
        for case in ('euler_clean','euler_missing_math','euler_class_swap','euler_wrong_serial'):
            with self.subTest(case=case):
                result=run_terminal_case(case)
                if case=='euler_clean':
                    self.assertEqual((result['exit_code'],result['verdict'],result['rotation_import_verified']),(0,'pass',True))
                else:
                    self.assertEqual((result['exit_code'],result['verdict'],result['rotation_import_verified']),(1,'fail',False))
    def test_actual_main_requires_drained_complete_terminal_logs(self):
        for case in ('clean','agent_unterminated_refusal','agent_unread_backlog','world_unterminated_error'):
            with self.subTest(case=case):
                result=run_terminal_case(case)
                if case=='clean':
                    self.assertEqual((result['exit_code'],result['verdict']),(0,'pass'))
                    self.assertIsNone(result['error'])
                else:
                    self.assertEqual((result['exit_code'],result['verdict']),(1,'fail'))
                    self.assertIn('terminal_log_',result['error'])


if __name__=='__main__':unittest.main()
