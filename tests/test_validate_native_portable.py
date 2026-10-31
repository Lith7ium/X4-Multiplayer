"""Independent protocol/transport evidence checks; no native game acceptance."""
import copy
import json
import unittest

from native_portable_coordinator import PortableBarrier
from test_native_portable import ADDRESSES, TOKEN, bits, contract, frame, hello, packet, participants
from validate_native_portable import coordinator_evidence, transport_evidence
import portable_import_capability as cap
from native_portable import validate_contract


def completed():
    barrier = PortableBarrier(TOKEN, contract(), participants(), lambda _: None,
                              ticks=4, hold_tick=2, hold_seconds=3)
    for slot in (1, 2): barrier.receive(hello(slot), ADDRESSES[slot], 0)
    for tick, now in ((0, 0), (1, 1), (2, 2), (2, 5.1), (3, 6), (4, 7)):
        for slot in (1, 2): barrier.receive(packet(slot, frame(tick)), ADDRESSES[slot], now)
    for slot in (1, 2):
        barrier.receive(f'NGPDONE|1|{TOKEN}|{slot}|4|5|5|1'.encode(), ADDRESSES[slot], 8)
    assert barrier.done and not barrier.failure
    return json.loads(json.dumps(barrier.summary()))


def transport():
    policy = {'kind': 'explicit_owned_ssh_only', 'remote_ipv4': '192.0.2.17', 'remote_tcp_port': 22,
              'command_sha256': 'a' * 64, 'executable': 'C:/Windows/System32/OpenSSH/ssh.exe'}
    sources = {name: 'b' * 64 for name in ('native_portable.py', 'native_portable_coordinator.py', 'native_portable_ssh.py')}
    ready = {'kind': 'ready', 'platform': 'linux', 'hashes': sources, 'pid': 700, 'start_ticks': 800,
             'port': 31001, 'report_path': '/private/run/portable-barrier.json'}
    bridge = {'transport_complete': True, 'failure': None, 'ssh_exit_code': 0, 'ssh_stderr_bytes': 0,
              'native_processes_verified_by_transport': False, 'sequence': 11, 'remote': copy.deepcopy(ready),
              'local_native_endpoint': {'transport': 'loopback_udp', 'address': ['127.0.0.1', 32001]},
              'coordinator': {'peer_endpoints': {'1': {'transport': 'ssh_virtual', 'address': ['127.0.0.1', 1]},
                                                '2': {'transport': 'loopback_udp', 'address': ['127.0.0.1', 32002]}}}}
    endpoint = {'proto': 'tcp', 'remote': policy['remote_ipv4'], 'rport': 22, 'pid': 200,
                'created': 300, 'state': 'ESTABLISHED'}
    session = {'server': {'pid': 100, 'violations': [],
                         'authorized_transport_exception': {**policy, 'identity': [200, 300, 100], 'audit': [[endpoint]]}}}
    return session, bridge, ready, policy, sources


class PortableEvidence(unittest.TestCase):
    def test_transport_new_capability_requires_both_module_sources_and_contract(self):
        cfg=contract();cfg['snapshot_sha256']=cap.SNAPSHOT_SHA256
        cfg['import_math']=cap.capability(cfg['snapshot_sha256'])
        session,bridge,ready,policy,sources=transport()
        bridge['coordinator']['contract_sha256']=validate_contract(cfg)
        self.assertFalse(transport_evidence(session,bridge,ready,policy=policy,source_hashes=sources,contract=cfg)['valid'])
        sources['portable_import_capability.py']='c'*64;bridge['remote']['hashes']=dict(sources)
        self.assertFalse(transport_evidence(session,bridge,ready,policy=policy,source_hashes=sources,contract=cfg)['valid'])
        sources['portable_import_capability_v1.py']='d'*64;bridge['remote']['hashes']=dict(sources)
        result=transport_evidence(session,bridge,ready,policy=policy,source_hashes=sources,contract=cfg)
        self.assertTrue(result['valid'],result['errors'])
        self.assertFalse(transport_evidence(session,bridge,ready,policy=policy,source_hashes=sources)['valid'])
        bridge['coordinator']['contract_sha256']=validate_contract(contract())
        self.assertFalse(transport_evidence(session,bridge,ready,policy=policy,source_hashes=sources,contract=cfg)['valid'])
        self.assertFalse(transport_evidence(session,bridge,ready,policy=policy,source_hashes=sources,contract=contract())['valid'])

    def test_archived_v1_four_source_closure_requires_exact_historical_capability_bytes(self):
        import portable_import_capability_v1 as v1
        cfg=contract();cfg['snapshot_sha256']=v1.SNAPSHOT_SHA256;cfg['import_math']=v1.capability(v1.SNAPSHOT_SHA256)
        session,bridge,ready,policy,sources=transport()
        bridge['coordinator']['contract_sha256']=validate_contract(cfg)
        sources['portable_import_capability.py']='86d8c39f2ea119b04ceebc8767ed4d2f099127bf92f328ac38a04314ba5683d8'
        bridge['remote']['hashes']=dict(sources)
        self.assertTrue(transport_evidence(session,bridge,ready,policy=policy,source_hashes=sources,contract=cfg)['valid'])
        sources['portable_import_capability.py']='a'*64;bridge['remote']['hashes']=dict(sources)
        self.assertFalse(transport_evidence(session,bridge,ready,policy=policy,source_hashes=sources,contract=cfg)['valid'])
        sources['portable_import_capability_v1.py']='b'*64;bridge['remote']['hashes']=dict(sources)
        self.assertTrue(transport_evidence(session,bridge,ready,policy=policy,source_hashes=sources,contract=cfg)['valid'])
    def check(self, report):
        return coordinator_evidence(report, contract(), participants(), ticks=4, hold_tick=2, hold_seconds=3)

    def test_real_state_machine_report_passes_only_protocol_scope(self):
        result = self.check(completed())
        self.assertTrue(result['valid'], result['errors'])
        self.assertEqual(len(result['frame_roots']), 5)
        self.assertFalse(result['native_processes_verified'])
        self.assertIn('ai', result['unsupported'])

    def test_valid_verdict_cannot_hide_changed_fields_clocks_receipts_or_coverage(self):
        changes = {
            'field divergence': lambda r: r['frames']['1']['2']['state']['actors'][4].update(hull_bits=bits(99)),
            'clock step both': lambda r: [r['frames']['1'][s]['state'].update(game_bits=bits(8)) for s in ('1', '2')],
            'relative clock': lambda r: r['frames']['1']['2'].update(real_bits=bits(500)),
            'nonzero command': lambda r: r['frames']['1']['1']['state']['commands'].__setitem__(1, '00000001'),
            'wrong receipt': lambda r: r['frames']['1']['1']['state'].update(returns=0),
            'missing frame': lambda r: r['frames'].pop('2'),
            'missing peer': lambda r: r['frames']['2'].pop('2'),
            'wrong tick key': lambda r: r['frames'].__setitem__('2', copy.deepcopy(r['frames']['1'])),
            'wrong artifact': lambda r: r['participants']['2'].update(agent_sha256='9' * 64),
            'claimed native': lambda r: r.update(native_processes_verified_by_coordinator=True),
            'terminal missing return': lambda r: r['terminal']['2'].update(returns=4),
            'terminal bool': lambda r: r['terminal']['2'].update(restored=True),
            'missing endpoint': lambda r: r['peer_endpoints'].pop('2'),
            'false virtual endpoint': lambda r: r['peer_endpoints']['2'].update(transport='ssh_virtual'),
            'unbounded endpoint': lambda r: r['peer_endpoints']['2'].update(address=['127.0.0.1', 65536]),
        }
        for label, mutate in changes.items():
            with self.subTest(label=label):
                report = completed(); mutate(report)
                self.assertFalse(self.check(report)['valid'])

    def test_hold_requires_both_exact_current_frame_observations_after_duration(self):
        changes = {
            'missing proof': lambda r: r.pop('hold_release_observations'),
            'missing peer': lambda r: r['hold_release_observations'].pop('2'),
            'early peer': lambda r: r['hold_release_observations']['2'].update(elapsed=2.9),
            'later than release': lambda r: r['hold_release_observations']['1'].update(elapsed=9),
            'wrong frame': lambda r: r['hold_release_observations']['1'].update(frame_sha256='0' * 64),
            'wrong clock': lambda r: r['hold_release_observations']['2'].update(real_bits=bits(500)),
            'nan time': lambda r: r['hold_release_observations']['2'].update(elapsed=float('nan')),
            'wrong declared hold': lambda r: r.update(hold_required_seconds=0),
        }
        for label, mutate in changes.items():
            with self.subTest(label=label):
                report = completed(); mutate(report)
                self.assertFalse(self.check(report)['valid'])

    def test_transport_requires_exact_source_and_real_owned_endpoint_audit(self):
        session, bridge, ready, policy, sources = transport()
        result = transport_evidence(session, bridge, ready, policy=policy, source_hashes=sources)
        self.assertTrue(result['valid'], result['errors'])
        self.assertFalse(result['native_processes_verified'])
        changes = {
            'empty audit': lambda s, b, r: s['server']['authorized_transport_exception'].update(audit=[]),
            'wrong child': lambda s, b, r: s['server'].update(pid=101),
            'wrong start': lambda s, b, r: s['server']['authorized_transport_exception']['audit'][0][0].update(created=301),
            'wrong endpoint': lambda s, b, r: s['server']['authorized_transport_exception']['audit'][0][0].update(remote='192.0.2.18'),
            'wrong artifact': lambda s, b, r: b['remote']['hashes'].update({'native_portable.py': '0' * 64}),
            'stderr': lambda s, b, r: b.update(ssh_stderr_bytes=1),
            'EOF failure': lambda s, b, r: b.update(failure='remote EOF'),
            'claimed native': lambda s, b, r: b.update(native_processes_verified_by_transport=True),
            'server violation': lambda s, b, r: s['server'].update(violations=['foreign endpoint']),
            'missing native source': lambda s, b, r: b.update(local_native_endpoint=None),
            'nonvirtual SSH source': lambda s, b, r: b['coordinator']['peer_endpoints']['1'].update(transport='loopback_udp'),
        }
        for label, mutate in changes.items():
            with self.subTest(label=label):
                s, b, r = copy.deepcopy((session, bridge, ready)); mutate(s, b, r)
                self.assertFalse(transport_evidence(s, b, r, policy=policy, source_hashes=sources)['valid'])


if __name__ == '__main__': unittest.main()
