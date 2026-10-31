"""Owned nongame files/processes and modeled read-only native observations."""
import copy
import json
import os
from pathlib import Path, PurePosixPath
import select
import subprocess
import sys
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest import mock

import linux_portable_prelaunch as live
import linux_portable_probe as probe
import linux_load_observer_probe as observer
import native_portable_prelaunch as binding
from native_portable import canonical_json
from test_native_portable_prelaunch import receipt_fixture


class LinuxPrelaunchLive(unittest.TestCase):
    @unittest.skipUnless(sys.platform=='linux','Linux fixed receipt path joins')
    def test_production_fetch_rechecks_all_generations_and_rejects_changed_evidence(self):
        import linux_native_portable_run as runner
        from contextlib import ExitStack
        for mutation in (None,'producer','supervisor','native','socket','mapping','receipt','report','config','closure','cancel','stale'):
            with self.subTest(mutation=mutation):
                receipt,config,contract,review=receipt_fixture();root=Path('/private');case=Path(receipt['case']);bundle=Path(receipt['bundle'])
                config_path=root/'config.json';review_sha=receipt['review_sha256']
                py=Path(sys.executable).stat();python_id=[py.st_dev,py.st_ino,py.st_size]
                receipt['producer']['executable_identity']=python_id
                argv=live.command_bytes(live.probe_arguments(case,bundle,review_sha,review['participant']['agent_sha256'],config_path))
                receipt['producer']['argv_sha256']=binding.sha(argv)
                report_raw=(json.dumps(receipt['coordinator_report'],indent=2)+'\n').encode()
                reads={}; identities=[]
                class Identity:
                    def __init__(self,pid,*args,**kwargs):self.pid=pid;self.calls=0;identities.append(self)
                    def __enter__(self):return self
                    def __exit__(self,*_):pass
                    def verify(self):
                        self.calls+=1
                        target={'supervisor':config['prelaunch_request']['linux_prepared']['pid'],
                                'producer':receipt['producer']['pid'],'native':receipt['claim']['pid']}.get(mutation)
                        if self.pid==target and self.calls>1:raise ValueError('generation changed')
                def read(path,*_):
                    path=str(path);reads[path]=reads.get(path,0)+1
                    if path==str(config_path):
                        raw=canonical_json(config)+(b' ' if mutation=='config' and reads[path]>1 else b'');return config,raw
                    if path==str(case/'prelaunch.json'):
                        raw=canonical_json(receipt)+(b' ' if mutation=='receipt' and reads[path]>1 else b'');return receipt,raw
                    return receipt['coordinator_report'],report_raw+(b' ' if mutation=='report' else b'')
                coordinator=SimpleNamespace(verify=mock.Mock(),socket=receipt['coordinator_lifetime']['endpoint'])
                endpoint=copy.deepcopy(receipt['native_endpoint_current'])
                if mutation=='socket':endpoint['inode']+=1
                with ExitStack() as stack:
                    stack.enter_context(mock.patch.object(live,'owned_paths'))
                    stack.enter_context(mock.patch.object(live,'read_owned',side_effect=read))
                    stack.enter_context(mock.patch.object(live,'supervisor_command',return_value='f'*64))
                    stack.enter_context(mock.patch.object(live,'LiveIdentity',Identity))
                    stack.enter_context(mock.patch.object(live,'verify_mappings',side_effect=ValueError('mapping changed') if mutation=='mapping' else None))
                    stack.enter_context(mock.patch.object(live.os.path,'lexists',side_effect=lambda p:mutation=='cancel' and Path(p).name=='disarm'))
                    stack.enter_context(mock.patch.object(live.time,'monotonic_ns',return_value=30_000_000_000 if mutation=='stale' else 14_500_000_000))
                    reviews=iter([(review,contract),(dict(review,mode='changed'),contract)])
                    stack.enter_context(mock.patch.object(runner,'review_bundle',side_effect=(lambda *_:next(reviews)) if mutation=='closure' else None,return_value=(review,contract)))
                    fd=os.open('/dev/null',os.O_RDONLY)
                    stack.enter_context(mock.patch.object(runner,'verify_coordinator',return_value=fd))
                    stack.enter_context(mock.patch.object(probe,'CoordinatorLifetime',return_value=coordinator))
                    stack.enter_context(mock.patch.object(probe.PortableProcess,'endpoint',return_value=endpoint))
                    if mutation:
                        with self.assertRaises(ValueError):live.fetch(root,case,bundle,config_path,review_sha)
                    else:
                        fetched=live.fetch(root,case,bundle,config_path,review_sha)
                        self.assertTrue(fetched['ready']);self.assertTrue(fetched['generations_verified'])
                        self.assertEqual(fetched['remote_age_ns'],500_000_000)
                        self.assertTrue(all(identity.calls>=2 for identity in identities))

    @unittest.skipUnless(sys.platform=='linux','Linux owned receipt paths and retained reader')
    def test_capture_is_from_retained_reader_and_publication_never_marks_pair_success(self):
        for mutation in (None, 'clock', 'stable_third', 'native_baseline', 'endpoint', 'bridge', 'manager', 'source', 'report', 'prefix', 'partial', 'backlog'):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as temporary:
                root=Path(temporary); case=root/'cases/portable-native-test';case.mkdir(parents=True)
                bundle=root/'bundle';(bundle/'bin').mkdir(parents=True)
                receipt,config,contract,review=receipt_fixture()
                review['files']['bin/linux_portable_probe.py']=binding.sha(b'probe')
                review['files']['bin/native_portable_prelaunch.py']=binding.sha(b'helper')
                (bundle/'bin/linux_portable_probe.py').write_bytes(b'probe')
                (bundle/'bin/native_portable_prelaunch.py').write_bytes(b'helper')
                producer=copy.deepcopy(receipt['producer']);producer['source_sha256']=review['files']['bin/linux_portable_probe.py']
                args=SimpleNamespace(claim=case/'claim.json',agent_so=bundle/'bin/libx4mp_portable.so',
                    config=root/'config.json',review_sha256=config['prelaunch_request']['linux_prepared']['review_sha256'],
                    agent_sha256=review['participant']['agent_sha256'],run_id=case.name)
                agent=observer.FreshLog(case/'agent.log');world=observer.FreshLog(case/'world.log')
                for log in (agent,world):log.path.write_bytes(b'parsed clean record\n');log.lines()
                if mutation=='prefix':agent.path.write_bytes(b'changed bad record!\n')
                if mutation=='partial':agent.path.write_bytes(b'parsed clean record\npartial');agent.lines()
                if mutation=='backlog':agent.path.write_bytes(b'parsed clean record\nunread\n')
                if mutation=='source':(bundle/'bin/native_portable_prelaunch.py').write_bytes(b'changed')
                mapping=copy.deepcopy(receipt['agent_mapping']);mapping['path']=str(bundle/'bin/libx4mp_portable.so')
                process=SimpleNamespace(pid=receipt['claim']['pid'],start=receipt['claim']['start_ticks'],uid=1004,
                    table=receipt['rotation_table'],executable_identity=tuple(receipt['native_executable_identity']),
                    verify=mock.Mock(), rotation_manager=mock.Mock(return_value=receipt['rotation_manager']),
                    word=mock.Mock(return_value=receipt['rotation_table']),rotation_restoration=mock.Mock(return_value=receipt['rotation_regions']),
                    endpoint=mock.Mock(return_value=receipt['native_endpoint_current']),
                    bridge_mapping=mock.Mock(return_value=receipt['bridge_mapping']),verify_agent_mapping=mock.Mock(return_value=mapping),
                    clocks=mock.Mock(return_value=receipt['clocks_before']))
                if mutation=='clock':process.clocks.side_effect=[receipt['clocks_before'],['0'*16]*3]
                if mutation=='stable_third':process.clocks.return_value=[*receipt['clocks_before'][:2],'4059000000000001']
                if mutation=='native_baseline':receipt['native_status']['load_clocks']=','.join([*receipt['clocks_before'][:2],'4059000000000001'])
                if mutation=='endpoint':process.endpoint.return_value={**receipt['native_endpoint_current'],'inode':9}
                if mutation=='bridge':process.bridge_mapping.return_value={**receipt['bridge_mapping'],'inode':9}
                if mutation=='manager':process.rotation_manager.side_effect=[receipt['rotation_manager'],receipt['rotation_manager']+1]
                coordinator=SimpleNamespace(verify=mock.Mock(),evidence=receipt['coordinator_lifetime'])
                rotation=probe.RotationEvidence(probe.reviewed_capability(contract));rotation.observe(receipt['rotation_status'])
                report_raw=(json.dumps(receipt['coordinator_report'],indent=2)+'\n').encode();reads=[]
                def read(path,*_):
                    if Path(path)==args.config:return config,canonical_json(config)
                    reads.append(path)
                    return receipt['coordinator_report'],report_raw+(b' ' if mutation=='report' and len(reads)>1 else b'')
                params=dict(args=args,config=config,contract=contract,review=review,process=process,coordinator=coordinator,
                    provenance=SimpleNamespace(verified=lambda _:True),request=receipt['ready_request'],installed=receipt['installed'],
                    native=receipt['native_status'],load=receipt['load_status'],rotation=rotation,
                    loader=SimpleNamespace(markers=receipt['markers']),first_endpoint=receipt['native_endpoint_first'],
                    bridge_identity=receipt['bridge_mapping'],agent_log=agent,world_log=world)
                with mock.patch.object(live,'probe_identity',return_value=producer),mock.patch.object(live,'read_owned',side_effect=read), \
                        mock.patch.object(live.preparation,'publish_handoff') as publish, mock.patch.object(live.time,'monotonic_ns',return_value=14_000_000_000):
                    if mutation in ('partial','backlog'):
                        self.assertIsNone(live.capture(**params));publish.assert_not_called()
                    elif mutation:
                        with self.assertRaises(ValueError):live.capture(**params)
                        publish.assert_not_called()
                    else:
                        result=live.capture(**params);self.assertFalse(result['native_pair_verified'])
                        publish.assert_called_once();payload=publish.call_args.args[2]
                        self.assertFalse(payload['native_pair_verified']);self.assertNotIn('verdict',payload)
                        binding.validate_receipt(payload,config,contract,review)
                        self.assertGreaterEqual(process.verify.call_count,2);self.assertGreaterEqual(coordinator.verify.call_count,2)
                        self.assertEqual(process.clocks.call_count,2)

    @unittest.skipUnless(sys.platform=='linux','Linux PIDFD and private ownership')
    def test_actual_pidfd_exec_exit_and_private_no_replace_publication(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);root.chmod(0o700);path=root/'receipt.json'
            live.preparation.publish_handoff(root,path,dict(proof='nongame'),uid=os.getuid())
            value,raw=live.read_owned(path,uid=os.getuid());self.assertEqual(value,dict(proof='nongame'))
            self.assertEqual(path.stat().st_nlink,1)
            with self.assertRaises(ValueError):live.preparation.publish_handoff(root,path,dict(replace=True),uid=os.getuid())
            linked=root/'linked.json';linked.symlink_to(path)
            with self.assertRaises(ValueError):live.read_owned(linked,uid=os.getuid())
            path.chmod(0o644)
            with self.assertRaises(ValueError):live.read_owned(path,uid=os.getuid())
            child=subprocess.Popen([sys.executable,'-B','-c','import sys; sys.stdin.buffer.read()'],stdin=subprocess.PIPE)
            try:
                identity=live.process_identity(child.pid)
                with live.LiveIdentity(child.pid,identity['start_ticks'],os.getuid(),
                    executable=identity['executable_identity'],argv_sha=identity['argv_sha256']) as pinned:
                    self.assertEqual(pinned.verify(),identity)
                    with self.assertRaises(ValueError):live.LiveIdentity(child.pid,identity['start_ticks']+1,os.getuid())
                    child.stdin.close();child.wait(timeout=5)
                    with self.assertRaises(ValueError):pinned.verify()
            finally:
                if child.poll() is None:child.terminate();child.wait(timeout=5)

    def test_shared_probe_argv_retains_exact_production_order(self):
        argv=live.probe_arguments(PurePosixPath('/private/cases/portable-native-12345678'),PurePosixPath('/private/bundle'),
                                  'a'*64,'b'*64,PurePosixPath('/private/config.json'))
        self.assertEqual(argv[:3],['python3','-B','/private/bundle/bin/linux_portable_probe.py'])
        self.assertEqual(argv[-4:],['--config','/private/config.json','--disarm-file','/private/cases/portable-native-12345678/disarm'])
        self.assertEqual(live.command_bytes(argv).count(b'\0'),len(argv))


if __name__=='__main__':unittest.main()
