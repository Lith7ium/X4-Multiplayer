"""Pure owner-channel bindings; no launch, remote access or native memory."""
import copy
import json
import unittest

import native_portable_prelaunch as pre
import native_portable_ssh as ssh
from native_portable import canonical_json, encode_frame
from native_portable_coordinator import PortableBarrier
from test_native_portable import participants, TOKEN, frame
from test_portable_import_capability import reviewed_euler_contract
from test_linux_portable_probe import math_terminal, load_terminal
from test_linux_portable_prelaunch import waiting_native, regions
from test_native_portable_linux_first import hello
import linux_portable_probe as probe


def bindings():
    contract = reviewed_euler_contract(); peers = participants()
    cfg = dict(token=TOKEN, contract=contract, participants=peers, ticks=12, hold_tick=3, hold_seconds=3,
               startup_timeout=600, peer_timeout=120, lifetime=930, linux_first=True)
    ready = dict(kind='ready', platform='linux', hashes=ssh.provenance(contract), pid=200, start_ticks=300,
                 port=32123, report_path='/private/coordinator/runs/owned/report.json')
    prepared = dict(schema='x4-portable-prepared-v1', pid=100, start_ticks=150, request_sha256='a'*64,
        review_sha256='b'*64, stock_files=127, stock_sha256=contract['stock_sha256'],
        preparation_timeout_seconds=1800, handoff_timeout_seconds=1200)
    session_raw = (json.dumps(cfg, indent=2)+'\n').encode()
    ready_raw = (json.dumps(ready, indent=2)+'\n').encode()
    prepared_raw = (json.dumps(prepared, indent=2)+'\n').encode()
    request = pre.make_wait(contract, peers, session_raw, ready_raw, prepared_raw,
        session_id='owned-session', client_run='owned-b3-run', runner=dict(pid=123, created=456),
        nonce='c'*32, started_ns=1_000_000_000)
    return cfg, ready, prepared, request


def receipt_fixture():
    cfg, ready, prepared, request = bindings(); contract = cfg['contract']
    config = pre.linux_config(cfg, ready, '/private/coordinator/native_portable_ssh.py', request)
    review = dict(mode=probe.EULER_MODE, contract_sha256=probe.reviewed_capability(contract)['contract_sha256'],
        participant=cfg['participants']['2'], files={'bin/linux_portable_probe.py':'a'*64,
            'bin/native_portable_prelaunch.py':'c'*64,'fixture/native/x4mp_load_probe.so':'d'*64})
    game = frame(); game.update(contract_sha256=config['contract_sha256'],game_bits=probe.SAVED_GAME)
    for actual,expected in zip(game['actors'],contract['actors']):actual.update(expected)
    raw = encode_frame(game,contract); address=('127.0.0.1',31002)
    barrier=PortableBarrier(TOKEN,contract,cfg['participants'],lambda _:None,ticks=12)
    barrier.receive(hello(cfg,2),address,0)
    barrier.receive('|'.join(('NGPSTATE','1',TOKEN,'2','4059000000000000','f'*64,raw.hex())).encode(),address,.1)
    report=pre.decode(canonical_json(barrier.summary()))
    report_raw=(json.dumps(report,indent=2)+'\n').encode()
    run='portable-native-test';case='/private/cases/'+run;bundle='/private/bundle'
    claim=dict(pid=300,start_ticks=400,uid=1004)
    endpoint=dict(**claim,inode=600,fds=[6],source=list(address),destination=['127.0.0.1',ready['port']])
    native=waiting_native();load=load_terminal();load['restored']='0'
    original={name:dict(matches_original=True,sha256='0'*64) for name in probe.observer.REGIONS}
    original.update(same_vbi_table=True,both_slot13_clear=True)
    markers={'MENU':dict(text=f'MENU schema={probe.SCHEMA_SHA} snapshot={contract["snapshot_sha256"]} run={run}',observed_monotonic=10),
        'LOAD_REQUEST':dict(text=f'LOAD_REQUEST snapshot={contract["snapshot_sha256"]} contract={config["contract_sha256"]} run={run}',observed_monotonic=13)}
    value=dict(schema=pre.RECEIPT_SCHEMA,scope='Linux joined-main startup boundary only',native_pair_verified=False,
        request=request,request_raw_sha256=pre.object_sha(request),request_canonical_sha256=pre.object_sha(request),
        linux_config_raw_sha256=pre.object_sha(config),linux_config_canonical_sha256=pre.object_sha(config),
        review_sha256=prepared['review_sha256'],contract_sha256=config['contract_sha256'],participants=cfg['participants'],
        run=run,case=case,bundle=bundle,claim=claim,producer=dict(pid=500,start_ticks=600,uid=0,
            parent_pid=prepared['pid'],parent_start_ticks=prepared['start_ticks'],argv_sha256='e'*64,
            source_sha256=review['files']['bin/linux_portable_probe.py'],executable_identity=[1,2,6000000]),
        native_executable_identity=[1,3,60459280],agent_mapping=dict(path=bundle+'/bin/libx4mp_portable.so',
            device=1,inode=4,bytes=746208,executable_ranges=['1000-2000']),
        bridge_mapping=dict(device=1,inode=5,bytes=338480,sha256='d'*64,executable_ranges=[dict(path=case+'/bridge.so',range='3000-4000')]),
        python_sources={k:v for k,v in review['files'].items() if k.endswith('.py')},
        coordinator_lifetime=dict(pid=ready['pid'],start_ticks=ready['start_ticks'],live_observations=3,
            terminal_bound_while_alive=False,exited_after_terminal=False,
            endpoint=dict(address=['127.0.0.1',ready['port']],inode=700,fds=[7])),
        coordinator_report=report,coordinator_report_raw_sha256=pre.sha(report_raw),
        coordinator_report_canonical_sha256=pre.object_sha(report),frame0_sha256=pre.sha(raw),
        native_endpoint_first=copy.deepcopy(endpoint),native_endpoint_current=endpoint,native_status=native,
        load_status=load,rotation_status=math_terminal(),rotation_regions=regions(),rotation_table=0x30000000,rotation_manager=0x50000000,
        clocks_before=[probe.SAVED_GAME,'4059000000000000','4059000000000000'],
        clocks_after=[probe.SAVED_GAME,'4059000000000000','4059000000000000'],
        ready_request=dict(**claim,mode=probe.EULER_MODE,published_monotonic=11,pre_arm_regions=original),
        installed=dict(status=dict(kind='INSTALLED',owner='123'),observed_monotonic=12),markers=markers,
        log_cutoff={name:dict(bytes_consumed=100,consumed_sha256='a'*64,complete_records=True) for name in ('agent','world')},
        observed_monotonic_ns=14_000_000_000)
    return value,config,contract,review


class PrelaunchBindings(unittest.TestCase):
    def test_full_receipt_is_only_prelaunch_and_every_top_level_join_is_required(self):
        receipt,config,contract,review=receipt_fixture()
        self.assertEqual(pre.validate_receipt(receipt,config,contract,review),receipt)
        self.assertLess(len(canonical_json(receipt)),pre.MAX_RECEIPT_BYTES)
        for field in receipt:
            bad=copy.deepcopy(receipt);del bad[field]
            with self.subTest(missing=field),self.assertRaises(ValueError):pre.validate_receipt(bad,config,contract,review)
        mutations=[('native_pair_verified',True),('scope','pass'),('linux_config_raw_sha256','0'*64),
            ('request_raw_sha256','0'*64),('frame0_sha256','0'*64),('coordinator_report_canonical_sha256','0'*64),
            ('python_sources',{}),('rotation_regions',{}),('rotation_table',0x30000001),('rotation_manager',0x50000001)]
        for field,value in mutations:
            bad=copy.deepcopy(receipt);bad[field]=value
            with self.subTest(field=field),self.assertRaises(ValueError):pre.validate_receipt(bad,config,contract,review)
        for target,key,value in [('producer','parent_pid',101),('producer','parent_start_ticks',151),
            ('producer','source_sha256','0'*64),('claim','pid',301),('claim','uid',1005),
            ('native_endpoint_current','inode',601),('coordinator_lifetime','exited_after_terminal',True),
            ('native_status','first_boundary_ack','0'),('rotation_status','serial','2')]:
            bad=copy.deepcopy(receipt);bad[target][key]=value
            with self.subTest(target=target,key=key),self.assertRaises(ValueError):pre.validate_receipt(bad,config,contract,review)
        bad=copy.deepcopy(receipt);bad['clocks_after'][2]='4059000000000001'
        with self.assertRaises(ValueError):pre.validate_receipt(bad,config,contract,review)
        bad=copy.deepcopy(receipt)
        bad['clocks_before'][2]=bad['clocks_after'][2]='4059000000000001'
        with self.assertRaisesRegex(ValueError,'native loaded baseline'):pre.validate_receipt(bad,config,contract,review)
        bad=copy.deepcopy(receipt)
        bad['native_status']['load_clocks']=','.join([receipt['clocks_before'][0],receipt['clocks_before'][1],'4059000000000001'])
        with self.assertRaisesRegex(ValueError,'native loaded baseline'):pre.validate_receipt(bad,config,contract,review)
        for key,value in [('released',0),('hello_slots',[1,2]),('verdict','pass'),('failure','stopped'),('terminal',{'2':{}})]:
            bad=copy.deepcopy(receipt);bad['coordinator_report'][key]=value
            bad['coordinator_report_canonical_sha256']=pre.object_sha(bad['coordinator_report'])
            with self.subTest(report=key),self.assertRaises(ValueError):pre.validate_receipt(bad,config,contract,review)

    def test_raw_and_canonical_bindings_and_exact_linux_config(self):
        cfg, ready, prepared, request = bindings()
        self.assertNotEqual(request['session_config_raw_sha256'], request['session_config_canonical_sha256'])
        self.assertNotEqual(request['coordinator_ready_raw_sha256'], request['coordinator_ready_canonical_sha256'])
        self.assertNotEqual(request['linux_prepared_raw_sha256'], request['linux_prepared_canonical_sha256'])
        self.assertEqual(pre.validate_wait(request, cfg['contract'], cfg['participants'], session_config=cfg, ready=ready), request)
        linux = pre.linux_config(cfg, ready, '/private/coordinator/native_portable_ssh.py', request)
        self.assertEqual(pre.validate_linux_config(linux, cfg['contract']), request)
        self.assertEqual(request['deadline_monotonic_ns'], 601_000_000_000)
        for field in ('ticks', 'timeout_ms', 'startup_timeout_ms'):
            changed = copy.deepcopy(linux); changed[field] += 1
            with self.subTest(field=field), self.assertRaises(ValueError): pre.validate_linux_config(changed, cfg['contract'])
        changed = copy.deepcopy(linux); changed['prelaunch_request']['nonce'] = 'd'*32
        # An otherwise valid request can have a different challenge; the owner
        # compares the entire object to its retained original before granting.
        self.assertNotEqual(pre.object_sha(changed), pre.object_sha(linux))

    def test_request_missing_fields_and_identity_or_deadline_mutations_refuse(self):
        cfg, ready, prepared, request = bindings()
        for field in request:
            changed = copy.deepcopy(request); del changed[field]
            with self.subTest(missing=field), self.assertRaises(ValueError):
                pre.validate_wait(changed, cfg['contract'], cfg['participants'], session_config=cfg, ready=ready)
        mutations = [('deadline_monotonic_ns',request['deadline_monotonic_ns']+1),('schema','unknown'),
            ('startup_timeout_ms',600001),('nonce','bad'),('client_run','bad run'),('contract_sha256','0'*64),
            ('session_config_canonical_sha256','0'*64),('coordinator_ready_canonical_sha256','0'*64),
            ('linux_prepared_canonical_sha256','0'*64),('runner',dict(pid=123,created=0))]
        for field,value in mutations:
            changed = copy.deepcopy(request); changed[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                pre.validate_wait(changed, cfg['contract'], cfg['participants'], session_config=cfg, ready=ready)
        changed=copy.deepcopy(ready);changed['hashes']['native_portable_ssh.py']='0'*64
        with self.assertRaises(ValueError):pre.validate_ready(changed,cfg['contract'])
        changed=copy.deepcopy(request);changed['linux_prepared']['stock_files']=126
        changed['linux_prepared_canonical_sha256']=pre.object_sha(changed['linux_prepared'])
        with self.assertRaises(ValueError):pre.validate_wait(changed,cfg['contract'],cfg['participants'])

    def test_json_bounds_duplicate_fields_nonfinite_and_future_or_stale_clock_refuse(self):
        for raw in (b'', b'{}'*(pre.MAX_MESSAGE_BYTES), b'{"x":1,"x":2}', b'{"x":NaN}'):
            with self.assertRaises(ValueError):pre.decode(raw)
        self.assertEqual(pre.receipt_age(100,200),100)
        for observed,now in ((200,100),(1,pre.MAX_AGE_NS+2),(True,2),(1,float('nan'))):
            with self.assertRaises(ValueError):pre.receipt_age(observed,now)


if __name__ == '__main__':unittest.main()
