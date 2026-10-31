"""Independent probe validation and real nongame Linux identity primitives."""
import copy
import hashlib
import json
import os
from pathlib import Path
import select
import shutil
import socket
import subprocess
import sys
import tempfile
import unittest
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/dev'))
import linux_portable_probe as probe
import portable_import_capability as cap
from test_native_portable import contract, TOKEN


def terminal():
    return dict(mode=probe.MODE, load_tail_included='1', result='passed_subset_zero_only', failed='0', first_boundary_ack='1',
        tick='12', calls='13', returns='13', receipt_tick='12', subset_reads='48', subset_changes='0', clock_changes='0',
        wait_kind='2', max_wait_ns='3100000000', terminal_ack='1', closed='1', restored='1', restore_error='none',
        loaded_frame='10000000', loaded_padding='0', load_clocks=probe.SAVED_GAME+',4059000000000000,4059000000000000',
        index_ns='1000000', load_selectors='0,1', installed='1', loaded_module='20000000')


def math_capability():
    return dict(mode=cap.MODE,contract_sha256=cap.CONTRACT_SHA256,snapshot_sha256=cap.SNAPSHOT_SHA256,
                import_math_sha256=cap.DESCRIPTOR_SHA256)


def math_terminal():
    value=dict(phase='Completed',mode=cap.MODE,descriptor=cap.DESCRIPTOR_SHA256,snapshot=cap.SNAPSHOT_SHA256,
        contract=cap.CONTRACT_SHA256,serial='1',owner='123',table='30000000',client='40000000',module='20000000',
        manager='50000000',installed='1',load_calls='1',load_returns='1',load_true='1',abnormal='0',scope_open='0',
        active='0',cancelled='0',failure='none',restored='1',restore_error='none',publishable='1',
        lifetimes='10009',paired='10009',inputs=cap.INPUTS_SHA256)
    for role in ('read','write'):
        value.update({role+'_calls':'10009',role+'_returns':'10009',role+'_euler':'10006',role+'_absent':'3',role+'_words':'90054'})
    for role in (0,1):
        for kind,calls,euler,absent in [('nav',10008,10005,3),('player',1,1,0)]:
            value.update({f'role{role}_{kind}_{field}':str(n) for field,n in
                dict(calls=calls,returns=calls,euler=euler,absent=absent,words=9*euler).items()})
    return value


def load_terminal():
    value=dict(phase='Completed',sites='3',ready='0',active='0',failure='none',restore_error='none',
        output_written='1',publishable='1',restored='1',owner='123',entries='12',capture_ns='10',
        install_last='123,30000000,40000000,60000000,1,2,1,0,0,1,0,0')
    for key in ('calls','returns','raw_calls','raw_returns'):value[key]=[1,1,1]
    for index,elapsed in enumerate((0,3,1,2,4,5)):
        value['mark'+str(index)]=','.join(('123',str(elapsed),'0'*16,'0'*16,'0'*16,'0','0','0','0','0'*16,'0'*16,'0','1'))
    return value


class ProbeEvidence(unittest.TestCase):
    def test_euler_mode_does_not_relabel_legacy_portable_terminal(self):
        row=terminal()
        self.assertFalse(probe.terminal_status(row,12,probe.EULER_MODE))
        row.update(mode=probe.EULER_MODE,result='passed_subset_euler_import')
        line='X4MP_AGENT PORTABLE STATE '+' '.join(f'{k}={v}' for k,v in row.items())
        self.assertEqual(probe.parse_portable(line,probe.EULER_MODE),row)
        self.assertTrue(probe.terminal_status(row,12,probe.EULER_MODE))
        with self.assertRaises(ValueError):probe.parse_portable(line)
        # A passing ordinary terminal cannot replace the separate receipt.
        evidence=probe.RotationEvidence(math_capability())
        self.assertFalse(evidence.terminal(row,load_terminal(),0x30000000,0x50000000))

    def test_rotation_closed_schema_requires_each_field_and_exact_scalar_grammar(self):
        row=math_terminal();line='X4MP_AGENT ROTATION '+' '.join(f'{k}={v}' for k,v in row.items())
        self.assertEqual(probe.parse_rotation(line),row)
        for field in row:
            bad=dict(row);del bad[field]
            with self.subTest(missing=field),self.assertRaises(ValueError):
                probe.parse_rotation('X4MP_AGENT ROTATION '+' '.join(f'{k}={v}' for k,v in bad.items()))
        for text in (line+' owner=123',line+' unknown=1',line+'x'*4096,line.replace('serial=1','serial=01'),
                     line.replace('owner=123','owner=-1'),line.replace('load_true=1','load_true=true'),
                     line.replace('table=30000000','table=800000000000'),
                     'path/'+line,line+' X4MP_AGENT VERIFIED pid=1',line.replace('ROTATION ','ROTATION\t')):
            with self.assertRaises(ValueError):probe.parse_rotation(text)
        self.assertIsNone(probe.parse_rotation('unrelated receipt'))

    def test_rotation_terminal_requires_all_role_counts_load_and_ownership_joins(self):
        native=terminal();native.update(mode=probe.EULER_MODE,result='passed_subset_euler_import')
        row=math_terminal();load=load_terminal()
        evidence=probe.RotationEvidence(math_capability());evidence.observe(row)
        self.assertTrue(evidence.terminal(native,load,0x30000000,0x50000000))
        for key in ('serial','installed','load_calls','load_returns','load_true','scope_open','active','restored','publishable')+tuple(
                role+'_'+field for role in ('read','write') for field in ('calls','returns','euler','absent','words')) + probe.ROTATION_CLASS_COUNTERS + ('lifetimes','paired'):
            bad=dict(row);bad[key]=str(int(bad[key])+1)
            changed=probe.RotationEvidence(math_capability());changed.observe(bad)
            with self.subTest(key=key):self.assertFalse(changed.terminal(native,load,0x30000000,0x50000000))
        for key in ('serial','owner','table','client','module','manager'):
            bad=dict(row);bad[key]='0';changed=probe.RotationEvidence(math_capability());changed.observe(bad)
            with self.subTest(zero=key):self.assertFalse(changed.terminal(native,load,0x30000000,0x50000000))
        for bad_load,bad_native,table,manager in (
            (dict(load,owner='124'),native,0x30000000,0x50000000),
            (dict(load,load_true='0',raw_returns=[0,1,1]),native,0x30000000,0x50000000),
            (dict(load,install_last='bad'),native,0x30000000,0x50000000),
            (load,dict(native,loaded_module='20000001'),0x30000000,0x50000000),
            (load,native,0x30000001,0x50000000),(load,native,0x30000000,0x50000001)):
            self.assertFalse(evidence.terminal(bad_native,bad_load,table,manager))

    def test_rotation_failures_and_changed_lifetime_never_recover_by_later_good_counts(self):
        row=math_terminal()
        for key,value in (('failure','shape'),('restore_error','foreign'),('abnormal','1'),('cancelled','1'),
                          ('phase','Failed'),('mode',probe.MODE),('descriptor','0'*64),('snapshot','0'*64),('contract','0'*64),('inputs','0'*64)):
            changed=dict(row);changed[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):probe.RotationEvidence(math_capability()).observe(changed)
        with self.assertRaises(ValueError):probe.RotationEvidence(dict(mode=probe.MODE)).observe(row)
        for key in ('serial','owner','table','client','module','manager','read_calls','write_returns','load_returns','lifetimes','paired') + tuple(k for k in probe.ROTATION_CLASS_COUNTERS if row[k]!='0'):
            evidence=probe.RotationEvidence(math_capability());evidence.observe(row)
            changed=dict(row);changed[key]='0'
            with self.subTest(regressed=key),self.assertRaises(ValueError):evidence.observe(changed)
        evidence=probe.RotationEvidence(math_capability());evidence.observe(row)
        with self.assertRaises(ValueError):evidence.observe(dict(row,phase='Loading'))
        # Before exact Load entry, these three anchors are deliberately absent.
        initial=dict(row,phase='Installed',serial='0',module='0',manager='0',load_calls='0',load_returns='0',load_true='0',
                     restored='0',publishable='0')
        for key in initial:
            if key.startswith(('read_','write_','role0_','role1_')) or key in ('lifetimes','paired'):initial[key]='0'
        evidence=probe.RotationEvidence(math_capability());evidence.observe(initial);evidence.observe(row)
        native=terminal();native.update(mode=probe.EULER_MODE,result='passed_subset_euler_import')
        self.assertTrue(evidence.terminal(native,load_terminal(),0x30000000,0x50000000))

    def test_class_swaps_and_old_v1_receipts_cannot_pass_total_only_evidence(self):
        native=terminal();native.update(mode=probe.EULER_MODE,result='passed_subset_euler_import')
        original=math_terminal()
        # Identical overall totals and lifetime count cannot cover a class swap.
        for role in (0,1):
            row=dict(original)
            for field in ('calls','returns','euler','absent','words'):
                a,b=f'role{role}_nav_{field}',f'role{role}_player_{field}'
                row[a],row[b]=row[b],row[a]
            evidence=probe.RotationEvidence(math_capability());evidence.observe(row)
            self.assertFalse(evidence.terminal(native,load_terminal(),0x30000000,0x50000000))
        row={k:v for k,v in original.items() if k not in set(probe.ROTATION_CLASS_COUNTERS)|{'lifetimes','paired','inputs'}}
        row['mode']='portable-fixture12-euler-v1'
        with self.assertRaises(ValueError):probe.parse_rotation('X4MP_AGENT ROTATION '+' '.join(f'{k}={v}' for k,v in row.items()))

    def test_rotation_restoration_and_manager_are_independent_stable_memory_reads(self):
        process=object.__new__(probe.PortableProcess)
        process.rotation_expected={name:bytes([index])*16 for index,name in enumerate(probe.ROTATION_REGIONS)}
        memory={address:process.rotation_expected[name] for name,(address,_,_) in probe.ROTATION_REGIONS.items()}
        process.read=lambda address,size:memory[address]
        self.assertTrue(all(r['matches_original'] for r in process.rotation_restoration().values()))
        address=next(iter(memory));memory[address]=b'X'*16
        self.assertFalse(all(r['matches_original'] for r in process.rotation_restoration().values()))
        process.word=lambda address:0x50000000
        self.assertEqual(process.rotation_manager(),0x50000000)
        words=iter((0x50000000,0x50000001));process.word=lambda address:next(words)
        with self.assertRaises(ValueError):process.rotation_manager()
        process.rotation_expected={}
        with self.assertRaises(ValueError):process.rotation_restoration()

    def test_failure_drain_requires_math_receipt_and_each_independently_restored_word(self):
        evidence=probe.RotationEvidence(math_capability())
        row=math_terminal();row.update(phase='Failed',failure='cancelled',cancelled='1',publishable='0')
        regions={name:dict(matches_original=True,sha256=pin) for name,(_,_,pin) in probe.ROTATION_REGIONS.items()}
        self.assertTrue(evidence.drained(row,regions))
        self.assertFalse(evidence.terminal(terminal(),load_terminal(),0x30000000,0x50000000))
        for changed in (None,dict(row,restored='0'),dict(row,active='1'),dict(row,scope_open='1'),
                        dict(row,restore_error='foreign'),dict(row,contract='0'*64)):
            self.assertFalse(evidence.drained(changed,regions))
        self.assertFalse(evidence.drained(row,{}))
        for key in regions:
            changed=copy.deepcopy(regions);changed[key]['matches_original']=False
            self.assertFalse(evidence.drained(row,changed))

    def test_pidless_lifecycle_refuses_any_foreign_verified_native_pid(self):
        def verified(pid): return f'X4MP_AGENT VERIFIED build=23660954 version=9.00 (611726) sites=25 pid={pid}'
        evidence = probe.ExclusiveAgentEvidence()
        evidence.observe('X4MP_AGENT REFUSED executable size 1234 (pinned 60459280)', None)
        evidence.observe(verified(10), None); evidence.bind(10)
        evidence.bind_identity(10, 200)
        self.assertTrue(evidence.verified(10)); self.assertEqual(len(evidence.preverification_build_refusals), 1)
        with self.assertRaises(ValueError): evidence.observe(verified(11), 10)
        for lines, claim in (([verified(11)], 10), ([verified(10), verified(11)], None)):
            evidence = probe.ExclusiveAgentEvidence()
            with self.assertRaises(ValueError):
                for line in lines: evidence.observe(line, None)
                evidence.bind(claim)

    def test_exact_terminal_requires_native_counts_closed_restoration_and_loaded_boundary(self):
        row = terminal(); self.assertTrue(probe.terminal_status(row, 12))
        changes = [('first_boundary_ack', '0'), ('installed', '0'), ('calls', '12'), ('returns', '12'),
            ('receipt_tick', '13'), ('failed', '1'), ('subset_changes', '1'), ('clock_changes', '1'),
            ('terminal_ack', '0'), ('closed', '0'), ('restored', '0'), ('restore_error', 'foreign'),
            ('loaded_frame', '0'), ('loaded_padding', '256'), ('load_selectors', '1,1'),
            ('load_clocks', '401c000000000000,4059000000000000,4059000000000000')]
        for key, value in changes:
            with self.subTest(key=key):
                changed = dict(row); changed[key] = value
                self.assertFalse(probe.terminal_status(changed, 12))

    def test_status_parser_rejects_duplicate_unknown_missing_and_oversized_fields(self):
        line = 'X4MP_AGENT PORTABLE STATE ' + ' '.join(f'{k}={v}' for k, v in terminal().items())
        self.assertEqual(probe.parse_portable(line), terminal())
        for text in (line + ' calls=13', line + ' unknown=1', line.replace(' closed=1', ''), line + 'x' * 2048):
            with self.assertRaises(ValueError): probe.parse_portable(text)
        self.assertIsNone(probe.parse_portable('X4MP_AGENT WAIT'))

    def test_loader_requires_fresh_menu_install_ack_and_exact_snapshot(self):
        cfg = contract(); run = 'portable-native-12345678'
        menu = f'[X4LAN] PORTABLE_BOOT MENU schema={probe.SCHEMA_SHA} snapshot={cfg["snapshot_sha256"]} run={run}'
        load = f'[X4LAN] PORTABLE_BOOT LOAD_REQUEST snapshot={cfg["snapshot_sha256"]} contract={probe.validate_contract(cfg)} run={run}'
        evidence = probe.LoaderEvidence(run, cfg); evidence.observe(menu, None)
        with self.assertRaisesRegex(ValueError, 'installed_ack'): evidence.observe(load, None)
        evidence.observe(load, {'kind': 'INSTALLED'})
        self.assertEqual(set(evidence.markers), {'MENU', 'LOAD_REQUEST'})
        # No late LOADED requirement or implicit continuation is introduced.
        loaded = f'[X4LAN] PORTABLE_BOOT LOADED game=7.083 paused=false snapshot={cfg["snapshot_sha256"]} run={run}'
        evidence.observe(loaded, {'kind': 'INSTALLED'}); self.assertIn('LOADED', evidence.markers)
        for text in (menu, load, f'[X4LAN] PORTABLE_BOOT ERROR failed run={run}',
                     'Error while executing native_portable_linux_live.lua: refusal'):
            with self.assertRaises(ValueError): evidence.observe(text, {'kind': 'INSTALLED'})

    def test_hold_matches_exact_wire_clocks_after_release_and_rejects_third_clock_motion(self):
        hold = {'state': {'game_bits': probe.SAVED_GAME}, 'real_bits': '4059000000000000'}
        clocks = [probe.SAVED_GAME, hold['real_bits'], hold['real_bits']]
        samples = [{'observed_monotonic': i / 10, 'clocks': list(clocks)} for i in range(30)]
        self.assertEqual(probe.held_clock_samples(samples, hold), samples)
        for changed in (samples[:10], [{**r, 'observed_monotonic': 0} for r in samples],
                        [{**r, 'clocks': ['0' * 16, *r['clocks'][1:]]} for r in samples]):
            with self.assertRaises(ValueError): probe.held_clock_samples(changed, hold)
        changed = copy.deepcopy(samples); changed[15]['clocks'][2] = '4059000000000001'
        with self.assertRaises(ValueError): probe.held_clock_samples(changed, hold)


@unittest.skipUnless(sys.platform == 'linux' and hasattr(os, 'pidfd_open'), 'Linux ownership/read-only proc primitives')
class RealLinuxIdentity(unittest.TestCase):
    def test_euler_ready_and_disarm_prefixes_keep_owned_file_and_prearm_proofs(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary).resolve();root.chmod(0o700)
            baseline={key:dict(matches_original=True) for key in probe.observer.REGIONS}
            baseline.update(same_vbi_table=True,both_slot13_clear=True)
            regions={key:dict(matches_original=True,sha256=value[2]) for key,value in probe.ROTATION_REGIONS.items()}
            process=SimpleNamespace(pid=1234,start=5678,uid=os.getuid(),verify=lambda:None,
                restoration=lambda initial:baseline,rotation_restoration=lambda:regions,
                word=lambda address:4096,read=lambda address,size:b'\0')
            record=probe.publish_request(root/'ready',process,TOKEN,probe.EULER_MODE)
            self.assertEqual((root/'ready').read_text(),f'{probe.EULER_MODE}|{TOKEN}|1234|5678\n')
            self.assertEqual(record['pre_arm_rotation_regions'],regions)
            self.assertNotIn(TOKEN,json.dumps(record));self.assertEqual((root/'ready').stat().st_mode&0o777,0o400)
            probe.request_disarm(root/'disarm',process,probe.EULER_MODE)
            self.assertEqual((root/'disarm').read_text(),f'{probe.EULER_MODE}|1234|5678\n')
            regions[next(iter(regions))]['matches_original']=False
            with self.assertRaises(ValueError):probe.publish_request(root/'tampered',process,TOKEN,probe.EULER_MODE)
            self.assertFalse((root/'tampered').exists())
    def test_atomic_preload_request_is_private_owned_bound_and_never_replaced(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve(); root.chmod(0o700); path = root / 'ready'
            regions = {key: {'matches_original': True, 'sha256': 'a' * 64} for key in probe.observer.REGIONS}
            regions.update(same_vbi_table=True, both_slot13_clear=True)
            process = SimpleNamespace(pid=1234, start=5678, uid=os.getuid(), verify=lambda: None,
                restoration=lambda initial: regions, word=lambda address: 4096, read=lambda address, size: b'\0')
            record = probe.publish_request(path, process, TOKEN)
            self.assertEqual(path.read_text(), f'{probe.MODE}|{TOKEN}|1234|5678\n')
            self.assertEqual(path.stat().st_mode & 0o777, 0o400); self.assertEqual(path.stat().st_nlink, 1)
            self.assertEqual(path.stat().st_uid, os.getuid()); self.assertNotIn(TOKEN, json.dumps(record))
            with self.assertRaises(FileExistsError): probe.publish_request(path, process, TOKEN)
            self.assertEqual(len(list(root.iterdir())), 1)

    def test_real_owned_udp_inode_and_mapped_bridge_file_are_bound_to_same_pidfd(self):
        with tempfile.TemporaryDirectory() as temporary, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
            root = Path(temporary); server.bind(('127.0.0.1', 0)); target = server.getsockname()[1]
            # A copied system libm is a nongame mapping fixture, never the native agent.
            libm = Path('/lib/x86_64-linux-gnu/libm.so.6').resolve(); bridge = root / 'owned_bridge.so'; shutil.copyfile(libm, bridge)
            code = '''import ctypes,json,os,socket,sys
from pathlib import Path
library=ctypes.CDLL(sys.argv[1])
with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as peer:
 peer.bind(('127.0.0.1',0));peer.connect(('127.0.0.1',int(sys.argv[2])))
 print(json.dumps({'pid':os.getpid(),'start':int(Path('/proc/self/stat').read_text().rsplit(')',1)[1].split()[19]),'port':peer.getsockname()[1]}),flush=True)
 sys.stdin.readline()
'''
            child = subprocess.Popen([sys.executable, '-c', code, str(bridge), str(target)], stdin=subprocess.PIPE,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            process = None
            try:
                owned = json.loads(child.stdout.readline())
                # Exercise only generic read-only identity methods with an actual
                # owned nongame process. Production construction still pins X4.
                process = object.__new__(probe.PortableProcess); process.pid = owned['pid']; process.start = owned['start']
                process.uid = os.getuid(); process.mem = None; process.pidfd = os.pidfd_open(child.pid)
                process.poll = select.poll(); process.poll.register(process.pidfd, select.POLLIN)
                info = Path(f'/proc/{child.pid}/exe').stat()
                process.executable_identity = info.st_dev, info.st_ino, info.st_size
                endpoint = {'transport': 'loopback_udp', 'address': ['127.0.0.1', owned['port']]}
                first = process.endpoint(endpoint, target); self.assertEqual(first, process.endpoint(endpoint, target))
                # Discovery precedes request/publication; later coordinator
                # attribution must match this exact native-owned socket.
                self.assertEqual(process.endpoint(None, target), first)
                self.assertEqual(first['pid'], child.pid); self.assertGreater(first['inode'], 0)
                with self.assertRaises(ValueError): process.endpoint({'transport': 'loopback_udp', 'address': list(server.getsockname())}, target)
                with self.assertRaises(ValueError): process.endpoint(endpoint, target + 1 if target < 65535 else target - 1)
                digest = hashlib.sha256(bridge.read_bytes()).hexdigest()
                mapping = process.bridge_mapping(bridge, digest); self.assertEqual(mapping['inode'], bridge.stat().st_ino)
                with self.assertRaises(ValueError): process.bridge_mapping(bridge, '0' * 64)
                other = root / 'unmapped_bridge.so'; shutil.copyfile(bridge, other)
                with self.assertRaises(ValueError): process.bridge_mapping(other, digest)
                process.start += 1
                with self.assertRaises(ValueError): process.endpoint(endpoint, target)
                process.start -= 1
                child.stdin.write('\n'); child.stdin.flush(); self.assertEqual(child.wait(timeout=5), 0)
                with self.assertRaises(ValueError): process.endpoint(endpoint, target)
            finally:
                if process: process.close()
                if child.poll() is None: child.kill(); child.wait(timeout=5)
                child.stdin.close(); child.stdout.close(); child.stderr.close()

    def test_coordinator_same_pidfd_socket_until_terminal_then_exit_is_explicit(self):
        code = '''import json,os,socket,sys
from pathlib import Path
sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
print(json.dumps({'pid':os.getpid(),'start_ticks':int(Path('/proc/self/stat').read_text().rsplit(')',1)[1].split()[19]),'port':port}),flush=True)
if sys.stdin.readline().strip()=='rebind':
 sock.close();sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);sock.bind(('127.0.0.1',port))
 print('REBOUND',flush=True);sys.stdin.readline()
sock.close()
'''
        for scenario in ('exit_before_terminal', 'exit_after_terminal', 'socket_rebound'):
            with self.subTest(scenario=scenario):
                temporary = tempfile.TemporaryDirectory(); script = Path(temporary.name) / 'native_portable_ssh.py'
                script.write_text(code)
                child = subprocess.Popen([sys.executable, str(script), '--remote'], stdin=subprocess.PIPE,
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                fd = None
                try:
                    ready = json.loads(child.stdout.readline()); fd = os.pidfd_open(child.pid)
                    foreign = script.with_name('foreign.py'); foreign.write_text('# not the running command\n')
                    with self.assertRaisesRegex(ValueError, 'initial_executable_or_command'):
                        probe.CoordinatorLifetime(fd, {'coordinator': ready, 'remote_script': str(foreign)})
                    lifetime = probe.CoordinatorLifetime(fd, {'coordinator': ready, 'remote_script': str(script)})
                    self.assertGreater(lifetime.evidence['endpoint']['inode'], 0)
                    self.assertIsNone(lifetime.report(lambda: None, lambda _: None))
                    accepted = {'verdict': 'pass', 'immutable': [1, 2]}; calls = []
                    if scenario == 'exit_after_terminal':
                        lifetime.report(lambda: accepted, lambda value: calls.append(value['verdict']))
                        self.assertEqual(calls, ['pass']); accepted['immutable'].append(3)
                    child.stdin.write('rebind\n' if scenario == 'socket_rebound' else '\n'); child.stdin.flush()
                    if scenario == 'socket_rebound':
                        self.assertEqual(child.stdout.readline().strip(), 'REBOUND')
                        with self.assertRaisesRegex(ValueError, 'socket_identity_changed'): lifetime.report(lambda: accepted, lambda _: None)
                    else:
                        self.assertEqual(child.wait(timeout=5), 0)
                        # A different live socket on the old port must not
                        # revive the retained, exited process generation.
                        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as replacement:
                            replacement.bind(('127.0.0.1', ready['port']))
                            if scenario == 'exit_before_terminal':
                                with self.assertRaisesRegex(ValueError, 'exited_before_terminal'): lifetime.report(lambda: accepted, lambda _: None)
                            else:
                                value = lifetime.report(lambda: self.fail('must not read replaced report after terminal'), lambda _: self.fail('must not reaccept'))
                                self.assertEqual(value, {'verdict': 'pass', 'immutable': [1, 2]})
                                self.assertTrue(lifetime.evidence['exited_after_terminal'])
                                self.assertTrue(lifetime.evidence['terminal_bound_while_alive'])
                finally:
                    if fd is not None: os.close(fd)
                    if child.poll() is None: child.kill(); child.wait(timeout=5)
                    child.stdin.close(); child.stdout.close(); child.stderr.close()
                    temporary.cleanup()


if __name__ == '__main__': unittest.main()
