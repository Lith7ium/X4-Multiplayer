"""Real owned pipes and synthetic launch seams; no game or remote operations."""
import copy
import json
import os
from pathlib import Path
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest import mock

import native_portable_prelaunch as binding
import native_portable_start_gate as gate
from native_portable import canonical_json
from test_native_portable_prelaunch import receipt_fixture


def grant_fixture():
    receipt, config, contract, review = receipt_fixture()
    raw = (json.dumps(receipt['coordinator_report'], indent=2) + '\n').encode()
    fetched = dict(schema=binding.FETCH_SCHEMA, ready=True, receipt=receipt,
        receipt_raw_sha256=binding.object_sha(receipt), receipt_canonical_sha256=binding.object_sha(receipt),
        report_raw_hex=raw.hex(), fetched_monotonic_ns=14_500_000_000, remote_age_ns=500_000_000,
        generations_verified=True, sockets_verified=True)
    value = dict(schema='x4-portable-launch-grant-v1', request_sha256=binding.object_sha(config['prelaunch_request']),
                 fetch_started_monotonic_ns=20_000_000_000, fetch=fetched)
    return value, config, contract, review


class StartGateTests(unittest.TestCase):
    def test_request_publication_is_complete_single_link_and_cannot_replace(self):
        with tempfile.TemporaryDirectory() as temporary:
            path=Path(temporary)/'request.json';gate.publish_request(path,dict(challenge='owned'))
            self.assertEqual(path.read_bytes(),canonical_json(dict(challenge='owned')))
            self.assertEqual(path.stat().st_nlink,1)
            with self.assertRaises(OSError):gate.publish_request(path,dict(replaced=True))
            self.assertEqual(path.read_bytes(),canonical_json(dict(challenge='owned')))
            self.assertEqual(list(path.parent.iterdir()),[path])
    def test_closed_grant_every_join_and_cross_host_age(self):
        grant, config, contract, review = grant_fixture(); request = config['prelaunch_request']
        def verify(value, now=21_000_000_000):
            return binding.validate_grant(value, request, config, contract, review, now)
        self.assertEqual(verify(grant), grant['fetch']['receipt'])
        self.assertLess(len(canonical_json(grant)), binding.MAX_MESSAGE_BYTES)
        for key in grant:
            bad = copy.deepcopy(grant); del bad[key]
            with self.subTest(missing=key), self.assertRaises(ValueError): verify(bad)
        for key in grant['fetch']:
            bad = copy.deepcopy(grant); del bad['fetch'][key]
            with self.subTest(missing=key), self.assertRaises(ValueError): verify(bad)
        for key, value in [('schema','wrong'), ('ready',False), ('receipt_raw_sha256','f'*64),
            ('receipt_canonical_sha256','f'*64), ('generations_verified',False), ('sockets_verified',False),
            ('report_raw_hex','7b7d'), ('fetched_monotonic_ns',13_000_000_000), ('remote_age_ns',0)]:
            bad=copy.deepcopy(grant); bad['fetch'][key]=value
            with self.subTest(field=key), self.assertRaises(ValueError): verify(bad)
        for now in (19_000_000_000, 30_000_000_001, request['deadline_monotonic_ns']):
            with self.subTest(now=now), self.assertRaises(ValueError): verify(grant, now)
        bad=copy.deepcopy(grant);bad['fetch']['receipt']['request']['runner']['pid']+=1
        with self.assertRaises(ValueError):verify(bad)

    def test_real_pipe_one_grant_then_eof_extra_partial_and_deadline_refuse(self):
        grant, config, contract, review = grant_fixture()
        for case in ('good', 'eof', 'extra', 'partial', 'duplicate', 'delayed', 'server'):
            with self.subTest(case=case):
                readfd, writefd = os.pipe(); reader = os.fdopen(readfd,'rb',buffering=0)
                now = [21_000_000_000]; failure = []
                def live(force):
                    if failure: raise ValueError('owned server failed')
                proof = gate.StartGate(config['prelaunch_request'], config, contract, review, reader, live, now=lambda:now[0])
                try:
                    wire=canonical_json(grant)+b'\n'
                    # write may exceed a Windows pipe buffer; use a bounded
                    # writer thread so the real reader drains concurrently.
                    import threading
                    def write_all(raw):
                        try:
                            offset=0
                            while offset<len(raw): offset += os.write(writefd,raw[offset:])
                        except OSError: pass
                    worker=threading.Thread(target=write_all,args=(wire[:-1] if case=='partial' else wire,));worker.start()
                    worker.join(3);self.assertFalse(worker.is_alive())
                    if case=='partial':
                        self.assertFalse(proof.check(require_grant=False));os.close(writefd);writefd=None
                    else:
                        deadline=time.monotonic()+3
                        while proof.channel.snapshot() is None and time.monotonic()<deadline:time.sleep(.005)
                        self.assertTrue(proof.check())
                        if case=='good':continue
                        if case=='eof':os.close(writefd);writefd=None
                        elif case in ('extra','duplicate'):os.write(writefd,b' ' if case=='extra' else b'{')
                        elif case=='delayed':now[0]=31_000_000_000
                        elif case=='server':failure.append(True)
                    if case in ('eof','extra','partial','duplicate'):
                        proof.channel.thread.join(3);self.assertFalse(proof.channel.thread.is_alive())
                    with self.assertRaises(ValueError):proof.check()
                finally:
                    if writefd is not None:os.close(writefd)
                    proof.channel.thread.join(3);reader.close()

    def test_actual_wrapper_seams_wait_before_launch_and_recheck_delayed_sequence(self):
        # Install into minimal released-shaped objects, not a replacement runner.
        grant, config, contract, review=grant_fixture();request=config['prelaunch_request'];events=[]
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);prepared=root/'prepared.json'; reviewed=root/'review.json'
            review_raw=canonical_json(review); reviewed.write_bytes(review_raw)
            saved=copy.deepcopy(request['linux_prepared']);saved['review_sha256']=binding.sha(review_raw)
            prepared.write_bytes(canonical_json(saved))
            class Run:
                def start_sequence(self):events.append('sequence');return self.start_x4()
                def start_x4(self):events.append('x4')
                def refresh_reserved(self):events.append('refresh')
            class Session:
                def start_server(self):events.append('server')
                def launch(self):events.append('launch');return self.clients[0].start_sequence()
            core=SimpleNamespace(read_owner=lambda _:dict(run='owned'))
            lane=SimpleNamespace(Run=Run,SessionRun=Session,core=core,win=lambda:SimpleNamespace(process_created=lambda _:123))
            gate.install(lane,contract=contract,participants=config['participants'],remote_script=config['remote_script'],
                         prepared_path=prepared,review_path=reviewed)
            session=Session();client=Run();session.clients=[client];session.outdir=root;session.session_id='test-session';session.result={}
            client.run_id='test-client';client.lock=SimpleNamespace(held=True,path=root/'lock',info=dict(run='owned'))
            session.quiet=None;session.server=SimpleNamespace(poll=lambda:None,check_network=lambda:events.append('network'))
            (root/'portable-session.json').write_bytes(b'{}');(root/'portable-barrier-remote.json').write_bytes(b'{}')
            fake=mock.Mock();fake.grant=grant;fake.check.side_effect=[True,True,ValueError('expired during lock wait')]
            def wait():events.append('wait');self.assertNotIn('launch',events);self.assertNotIn('x4',events)
            fake.wait.side_effect=wait
            with mock.patch.object(binding,'make_wait',return_value=request),mock.patch.object(binding,'linux_config',return_value=config), \
                    mock.patch.object(gate,'StartGate',return_value=fake):
                session.start_server()
                self.assertEqual(events,['server','wait'])
                with self.assertRaisesRegex(ValueError,'expired during lock wait'):session.launch()
            self.assertNotIn('x4',events);self.assertLess(events.index('wait'),events.index('launch'))
            # Required final seam refuses when the optional gate disappeared.
            with self.assertRaisesRegex(ValueError,'gate missing'):gate.final_check(SimpleNamespace())

    def test_production_alive_cadence_preserves_bounded_audit_and_forces_launch_checks(self):
        grant,config,contract,review=grant_fixture();request=config['prelaunch_request']
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);reviewed=root/'review.json';prepared=root/'prepared.json'
            raw=canonical_json(review);reviewed.write_bytes(raw)
            saved=copy.deepcopy(request['linux_prepared']);saved['review_sha256']=binding.sha(raw)
            prepared.write_bytes(canonical_json(saved))
            class Run:
                def start_sequence(self):pass
                def start_x4(self):pass
                def refresh_reserved(self):self.refreshed+=1
            class Session:
                def start_server(self):pass
                def launch(self):pass
            core=SimpleNamespace(read_owner=lambda _:dict(run='owned'))
            lane=SimpleNamespace(Run=Run,SessionRun=Session,core=core,win=lambda:SimpleNamespace(process_created=lambda _:123))
            gate.install(lane,contract=contract,participants=config['participants'],remote_script=config['remote_script'],
                         prepared_path=prepared,review_path=reviewed)
            session=Session();client=Run();client.refreshed=0;client.run_id='owned';session.clients=[client]
            client.lock=SimpleNamespace(held=True,path=root/'lock',info=dict(run='owned'))
            session.quiet=SimpleNamespace(held=True,path=root/'quiet',info=dict(run='owned'),refresh=mock.Mock())
            session.outdir=root;session.session_id='session';session.result={}
            session.server=SimpleNamespace(poll=mock.Mock(return_value=None),check_network=mock.Mock())
            for name in ('portable-session.json','portable-barrier-remote.json'):(root/name).write_bytes(b'{}')
            captured={};fake=mock.Mock();fake.grant=grant
            def make(*args,**kwargs):captured['alive']=args[5];return fake
            with mock.patch.object(binding,'make_wait',return_value=request),mock.patch.object(binding,'linux_config',return_value=config), \
                    mock.patch.object(gate,'StartGate',side_effect=make):session.start_server()
            now=[0.0]
            with mock.patch.object(gate.time,'monotonic',side_effect=lambda:now[0]):
                for index in range(30000):
                    now[0]=index*.02;captured['alive'](False)
                self.assertEqual(session.server.poll.call_count,30000)
                self.assertLessEqual(session.server.check_network.call_count,120)
                self.assertLessEqual(client.refreshed,600)
                previous=session.server.check_network.call_count;refresh=client.refreshed
                captured['alive'](True)
                self.assertEqual(session.server.check_network.call_count,previous+1)
                self.assertEqual(client.refreshed,refresh+1)
                client.lock.held=False
                with self.assertRaisesRegex(ValueError,'reservation lost'):captured['alive'](True)
                session.server.poll.return_value=('server_failed','owned child exited')
                with self.assertRaisesRegex(ValueError,'owned server failed'):captured['alive'](False)


if __name__=='__main__':unittest.main()
