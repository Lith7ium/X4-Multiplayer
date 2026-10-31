"""Join independently scoped reports; no synthetic evidence is a real game run."""
import copy
import hashlib
import unittest

from native_portable import canonical_json, validate_contract, encode_frame, sha
from test_native_portable import contract, participants
from test_validate_native_portable import completed, transport
from test_linux_portable_probe import terminal
from test_linux_load_observer_probe import status
import linux_load_observer_probe as observer
from validate_mixed_native import combined_evidence, windows_terminal
import linux_portable_probe as linux_probe
import test_linux_portable_probe as linux_fixture
import test_native_portable_rotation as windows_rotation
from test_portable_import_capability import reviewed_euler_contract


def evidence():
    cfg, peers = contract(), participants(); session, bridge, ready, policy, sources = transport()
    coordinator = completed()
    coordinator['peer_endpoints'] = bridge['coordinator']['peer_endpoints']
    bridge['coordinator'] = coordinator
    native = terminal(); native.update(tick='4', receipt_tick='4', calls='5', returns='5')
    identity = dict(pid=900, created=1000, leases_ready_filetime=900, module_observed=True,
        owned_job_verified=True, owned_process_exited=True, artifact_leases_closed_after_exit=True,
        executable=dict(sha256=peers['1']['executable_sha256'], write_delete_sharing=False),
        module=dict(sha256=peers['1']['agent_sha256'], write_delete_sharing=False, path='C:/owned/gate.dll', image_size=4000),
        mapped_module=dict(path='C:/owned/gate.dll', image_size=4000),
        udp_source_ownership=dict(pid=900, created=1000, sources=[dict(address=['127.0.0.1',32001], observations=3, first_filetime=1001, last_filetime=1030)]))
    artifacts={'native/x4lan_gate_64.dll': peers['1']['agent_sha256']}
    session.update(verdict='pass', exit_code=0, clients=[dict(profile='45853', verdict='pass', run_id='owned-run',
        portable_native_identity=identity, x4={'pid':900}, lane_restored=True, native_snapshot={'restored':True},
        portable_installed=dict(shared_files=cfg['fixture_files'], participant_files=artifacts, snapshot={'sha256':cfg['snapshot_sha256']}),
        portable_launch_recheck=dict(shared_files_unchanged=True,participant_files_unchanged=True,snapshot_unchanged=True,autostart_and_profile_unchanged=True))])
    fields=dict(result='success',slot='1',run='owned-run',contract=validate_contract(cfg),snapshot=cfg['snapshot_sha256'],
        agent=peers['1']['agent_sha256'],schema=cfg['schema_sha256'],saved='0'*16,stage='complete',wire_stage='closed',
        configured='1',wire='1',draining='0',closed='1',terminal_restored='1',active='0',reason='1',failures='0',
        last='4',issued='4',completed='4',frames='5',calls='5',returns='5')
    log='[X4LAN] PORTABLE_NATIVE TERMINAL '+' '.join(f'{k}={v}' for k,v in fields.items())
    regions={key:{'matches_original':True} for key in observer.REGIONS}
    regions.update(same_vbi_table=True,both_slot13_clear=True,loaded_padding={'matches_native_loaded_baseline':True})
    held=coordinator['frames']['2']['2']
    clocks=[held['state']['game_bits'],held['real_bits'],held['real_bits']]
    endpoint=dict(pid=901,start_ticks=1100,uid=1004,inode=123,fds=[8],source=['127.0.0.1',32002],destination=['127.0.0.1',31001])
    probe=dict(verdict='pass',native_pair_verified=False,contract_sha256=validate_contract(cfg),agent_sha256=peers['2']['agent_sha256'],
        private_config_sha256='8'*64,
        owned_native_verified=True,native_frame0_verified=True,native_terminal_verified=True,restoration_verified=True,
        native_endpoint_pinned_before_request=True,claim={'pid':901,'start_ticks':1100,'uid':1004},
        final_native_status=native,final_load_status=status(),restoration=regions,
        independent_hold_samples=[dict(observed_monotonic=i/10,clocks=clocks[:]) for i in range(30)],
        coordinator_lifetime=dict(pid=ready['pid'],start_ticks=ready['start_ticks'],terminal_bound_while_alive=True,
            terminal_report_sha256=hashlib.sha256(canonical_json(coordinator)).hexdigest()),
        native_endpoint_first=endpoint,native_endpoint_last=copy.deepcopy(endpoint))
    linux=dict(probe_exit_code=0,review_sha256='7'*64,native_linux_game=True,synthetic_coordinator=False,native_pair_verified=False,
        contract_sha256=validate_contract(cfg),private_config_sha256='8'*64,
        game_sha256=peers['2']['executable_sha256'],agent_sha256=peers['2']['agent_sha256'],native_pid=901,native_start_ticks=1100,native_uid=1004,
        probe=probe,**{key:True for key in ('native_pidfd_exited','xvfb_pidfd_exited','stock_stamps_unchanged','owned_native_exited',
            'independent_probe_exited','private_xvfb_exited','steam_identity_unchanged')})
    return [session,bridge,linux,log,cfg,peers],dict(artifacts=artifacts,policy=policy,source_hashes=sources,review_sha256='7'*64,ticks=4,hold_tick=2)


def euler_evidence():
    """Retarget the modeled legacy run; this does not represent native gameplay."""
    args, kwargs = evidence()
    old = args[4]; cfg = reviewed_euler_contract(); digest = validate_contract(cfg)
    args[4] = cfg
    client = args[0]['clients'][0]
    client['portable_installed']['shared_files'] = cfg['fixture_files']
    client['portable_installed']['snapshot']['sha256'] = cfg['snapshot_sha256']
    args[3] = args[3].replace(validate_contract(old), digest).replace(old['snapshot_sha256'], cfg['snapshot_sha256'])
    args[3] += ('\n[X4LAN] PORTABLE_ROTATION TERMINAL result=complete slot=1 run=' + client['run_id']
                + ' agent=' + args[5]['1']['agent_sha256'] + ' ' + windows_rotation.text(windows_rotation.status()))
    coordinator = args[1]['coordinator']; coordinator['contract_sha256'] = digest
    for frames in coordinator['frames'].values():
        for frame in frames.values():
            frame['state']['contract_sha256'] = digest
            for actor, expected in zip(frame['state']['actors'], cfg['actors']): actor.update(expected)
    for slot, row in coordinator['hold_release_observations'].items():
        row['frame_sha256'] = sha(encode_frame(coordinator['frames'][str(kwargs['hold_tick'])][slot]['state'], cfg))
    kwargs['source_hashes']['portable_import_capability.py'] = 'c' * 64
    kwargs['source_hashes']['portable_import_capability_v1.py'] = 'd' * 64
    args[1]['remote']['hashes'] = dict(kwargs['source_hashes'])
    linux = args[2]; probe = linux['probe']; capability = linux_fixture.math_capability()
    linux.update(capability); probe.update(capability)
    probe['final_native_status'].update(mode=linux_probe.EULER_MODE, result='passed_subset_euler_import')
    probe.update(rotation_import_verified=True, rotation_receipt_seen=True,
                 final_load_status=linux_fixture.load_terminal(),
                 final_rotation_status=linux_fixture.math_terminal(), last_rotation_status=linux_fixture.math_terminal(),
                 rotation_table='30000000', rotation_manager='50000000',
                 rotation_restoration={name: dict(matches_original=True, sha256=pin)
                                       for name, (_, _, pin) in linux_probe.ROTATION_REGIONS.items()})
    probe['coordinator_lifetime']['terminal_report_sha256'] = hashlib.sha256(canonical_json(coordinator)).hexdigest()
    return args, kwargs


class MixedEvidence(unittest.TestCase):
    def test_closed_join_accepts_complete_modeled_evidence_and_keeps_subset_scope(self):
        args,kwargs=evidence(); result=combined_evidence(*args,**kwargs)
        self.assertTrue(result['valid'],result['errors']); self.assertTrue(result['native_pair_verified'])
        self.assertIn('ai',result['unsupported']); self.assertEqual(result['commands'],'zero only')

    def test_native_endpoint_terminal_cleanup_and_cross_report_mutations_refuse(self):
        changes={
            'native Windows PID':lambda a:a[0]['clients'][0]['x4'].update(pid=999),
            'Windows source owner':lambda a:a[0]['clients'][0]['portable_native_identity']['udp_source_ownership'].update(pid=999),
            'Windows source unseen':lambda a:a[1]['local_native_endpoint'].update(address=['127.0.0.1',55555]),
            'Windows only one observation':lambda a:a[0]['clients'][0]['portable_native_identity']['udp_source_ownership']['sources'][0].update(observations=1),
            'Windows DLL':lambda a:a[0]['clients'][0]['portable_native_identity']['module'].update(sha256='9'*64),
            'Windows unrestored':lambda a:a[0]['clients'][0].update(lane_restored=False),
            'Windows terminal missing':lambda a:a.__setitem__(3,''),
            'Windows terminal failure':lambda a:a.__setitem__(3,a[3].replace('result=success','result=failure')),
            'Linux synthetic':lambda a:a[2].update(synthetic_coordinator=True),
            'Linux live process':lambda a:a[2].update(native_pidfd_exited=False),
            'Linux claim':lambda a:a[2]['probe']['claim'].update(start_ticks=999),
            'Linux config different':lambda a:a[2]['probe'].update(private_config_sha256='9'*64),
            'Linux hold third clock':lambda a:a[2]['probe']['independent_hold_samples'][15]['clocks'].__setitem__(2,'0'*16),
            'Linux code restoration':lambda a:a[2]['probe']['restoration'].update(both_slot13_clear=False),
            'coordinator exit before terminal':lambda a:a[2]['probe']['coordinator_lifetime'].update(terminal_bound_while_alive=False),
            'coordinator report different':lambda a:a[2]['probe']['coordinator_lifetime'].update(terminal_report_sha256='9'*64),
            'Linux socket changed':lambda a:a[2]['probe']['native_endpoint_last'].update(inode=456),
            'Linux request before attribution':lambda a:a[2]['probe'].update(native_endpoint_pinned_before_request=False),
        }
        for label,mutate in changes.items():
            with self.subTest(label=label):
                args,kwargs=evidence();mutate(args);self.assertFalse(combined_evidence(*args,**kwargs)['valid'])

    def test_windows_receipt_is_exact_single_fresh_run_and_native_counts(self):
        args,_=evidence(); log,cfg,peers=args[3:]
        for text in (log+' calls=5',log+'\n'+log,log.replace('run=owned-run','run=foreign'),
                     log.replace('returns=5','returns=4'),log.replace('draining=0','draining=1')):
            with self.assertRaises(ValueError):windows_terminal(text,'owned-run',cfg,peers['1']['agent_sha256'],4)

    def test_actual_wildcard_binding_is_not_misreported_as_loopback_destination(self):
        args,kwargs=evidence()
        source=args[0]['clients'][0]['portable_native_identity']['udp_source_ownership']['sources'][0]
        source['address'][0]='0.0.0.0'
        result=combined_evidence(*args,**kwargs)
        self.assertTrue(result['valid'],result['errors'])
        self.assertEqual(result['windows_udp_source'],['127.0.0.1',32001])
        self.assertEqual(result['windows_udp_binding'],['0.0.0.0',32001])
        sources=args[0]['clients'][0]['portable_native_identity']['udp_source_ownership']['sources']
        sources.append({**source,'address':['127.0.0.1',32001]})
        self.assertFalse(combined_evidence(*args,**kwargs)['valid']);sources.pop()
        source['address'][0]='192.0.2.5'
        self.assertFalse(combined_evidence(*args,**kwargs)['valid'])


class MixedEulerEvidence(unittest.TestCase):
    def test_complete_modeled_math_and_native_join_retains_bounded_scope(self):
        args, kwargs = euler_evidence(); result = combined_evidence(*args, **kwargs)
        self.assertTrue(result['valid'], result['errors'])
        self.assertTrue(result['windows_terminal']['rotation']['valid'])
        self.assertTrue(result['linux_rotation']['valid'])
        self.assertFalse(result['linux_rotation']['native_processes_verified'])
        self.assertIn('ai', result['unsupported']); self.assertEqual(result['commands'], 'zero only')

    def test_independent_review_witnesses_cannot_bypass_windows_math_proof(self):
        args, kwargs = euler_evidence(); native, math = args[3].split('\n')
        changes = {
            'missing': '', 'wrong count': math.replace('role0_euler=10006', 'role0_euler=10005'),
            'wrong artifact': math.replace(args[5]['1']['agent_sha256'], '9' * 64),
            'wrong run': math.replace('run=owned-run', 'run=foreign'),
            'failure': math.replace('result=complete', 'result=failure'),
            'duplicate': math + '\n' + math,
        }
        for label, addition in changes.items():
            with self.subTest(label=label):
                changed = copy.deepcopy(args); changed[3] = native + '\n' + addition
                result = combined_evidence(*changed, **kwargs)
                self.assertFalse(result['valid']); self.assertFalse(result['native_pair_verified'])
                self.assertIn('Windows rotation proof failed', result['errors'][0])
                with self.assertRaises(ValueError):
                    windows_terminal(changed[3], 'owned-run', changed[4], changed[5]['1']['agent_sha256'], kwargs['ticks'])

    def test_linux_success_flags_cannot_replace_raw_math_and_independent_restoration(self):
        changes = {
            'no receipt': lambda p: p.pop('final_rotation_status'),
            'no observed receipt': lambda p: p.update(rotation_receipt_seen=False),
            'unverified': lambda p: p.update(rotation_import_verified=False),
            'legacy mode': lambda p: p.update(mode=linux_probe.MODE),
            'wrong descriptor': lambda p: p.update(import_math_sha256='9' * 64),
            'different last observation': lambda p: p['last_rotation_status'].update(failure='fault'),
            'no table observation': lambda p: p.pop('rotation_table'),
            'different table': lambda p: p.update(rotation_table='30000001'),
            'different manager': lambda p: p.update(rotation_manager='50000001'),
            'bad independent pointer': lambda p: p.update(rotation_manager=True),
            'different Load owner': lambda p: p['final_load_status'].update(owner='124'),
            'different Load serial': lambda p: (p['final_rotation_status'].update(serial='2'),
                                                p['last_rotation_status'].update(serial='2')),
            'different loaded module': lambda p: p['final_native_status'].update(loaded_module='20000001'),
            'missing restored bytes': lambda p: p.update(rotation_restoration={}),
            'false restoration': lambda p: p['rotation_restoration']['navigation_read_import'].update(matches_original=False),
            'restoration hash differs': lambda p: p['rotation_restoration']['navigation_write_import'].update(sha256='9' * 64),
        }
        for label, mutate in changes.items():
            with self.subTest(label=label):
                args, kwargs = euler_evidence(); mutate(args[2]['probe'])
                result = combined_evidence(*args, **kwargs)
                self.assertFalse(result['valid']); self.assertFalse(result['native_pair_verified'])

    def test_linux_each_missing_or_false_raw_terminal_field_refuses(self):
        for key, value in linux_fixture.math_terminal().items():
            for bad in (None, '0' if value != '0' else '1', True):
                with self.subTest(field=key, value=bad):
                    args, kwargs = euler_evidence(); probe = args[2]['probe']
                    row = probe['final_rotation_status']
                    if bad is None: row.pop(key)
                    else: row[key] = bad
                    probe['last_rotation_status'] = copy.deepcopy(row)
                    self.assertFalse(combined_evidence(*args, **kwargs)['valid'])

    def test_five_source_provenance_and_launch_capability_are_mandatory(self):
        for module in ('portable_import_capability.py', 'portable_import_capability_v1.py'):
            with self.subTest(module=module):
                args, kwargs = euler_evidence()
                kwargs['source_hashes'].pop(module)
                args[1]['remote']['hashes'] = dict(kwargs['source_hashes'])
                self.assertFalse(combined_evidence(*args, **kwargs)['valid'])
        for key in linux_fixture.math_capability():
            with self.subTest(key=key):
                args, kwargs = euler_evidence(); args[2].pop(key)
                self.assertFalse(combined_evidence(*args, **kwargs)['valid'])

    def test_legacy_contract_cannot_adopt_euler_terminal(self):
        args, kwargs = evidence(); new, _ = euler_evidence()
        args[3] += '\n' + new[3].split('\n')[1]
        self.assertFalse(combined_evidence(*args, **kwargs)['valid'])


if __name__=='__main__':unittest.main()
