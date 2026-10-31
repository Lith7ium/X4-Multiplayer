import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/dev'))
import linux_load_observer_probe as probe
from test_native_portable import contract


def status():
    row = dict(kind='STATE', phase='Completed', sites='3', ready='0', active='0', failure='none',
               restored='1', restore_error='none', output_written='1', publishable='1', owner='123',
               calls=[1]*3, returns=[1]*3, raw_calls=[1]*3, raw_returns=[1]*3,
               entries='12', unresolved='0', capture_ns='100')
    for index, at in enumerate([1, 4, 2, 3, 5, 6]):
        row[f'mark{index}'] = ','.join(['123', str(at), *['0'*16]*3, '1', '0', '0', '1', *['0'*16]*2, '0', '1'])
    return row


class ObserverProbe(unittest.TestCase):
    def test_module_initialization_errors_fail_without_waiting_for_menu(self):
        for name in ('native_portable_linux.lua','native_portable_loader.lua'):
            self.assertTrue(probe.loader_script_error('Error while executing script extensions/x4_lan_lab/ui/'+name+': module refused'))
        self.assertFalse(probe.loader_script_error('PORTABLE_BOOT MENU'))
        self.assertFalse(probe.loader_script_error('Error while executing unrelated.lua'))

    def test_inherited_helper_build_refusals_need_later_exact_owned_verification(self):
        evidence = probe.AgentEvidence(); owned = 2518304
        for pid, size in [(2518309, 30944), (2518312, 236256)]:
            self.assertIsNone(evidence.observe(f'X4MP_AGENT START pid={pid}', owned))
            self.assertIsNone(evidence.observe(f'X4MP_AGENT REFUSED executable size {size} (pinned 60459280)', owned))
        self.assertEqual(len(evidence.preverification_build_refusals), 2)
        self.assertFalse(evidence.verified(owned))
        verified = 'X4MP_AGENT VERIFIED build=23660954 version=9.00 (611726) sites=25 pid='
        with self.assertRaises(ValueError): probe.AgentEvidence().observe(verified + '2518312', owned)
        evidence.observe(verified + str(owned), owned)
        self.assertTrue(evidence.verified(owned))
        self.assertEqual(evidence.observe('X4MP_AGENT LOAD_OBSERVER INSTALLED sites=3', owned)['kind'], 'INSTALLED')
        with self.assertRaises(ValueError): evidence.observe('X4MP_AGENT REFUSED executable size 30944 (pinned 60459280)', owned)
        with self.assertRaises(ValueError): evidence.observe('X4MP_AGENT LOAD_OBSERVER REFUSED reason=foreign_load_thread', owned)
        with self.assertRaises(ValueError): probe.AgentEvidence().observe('X4MP_AGENT REFUSED signature mismatch', owned)

    def test_completed_observer_requests_own_restoration_before_final_acceptance(self):
        drain = probe.RestorationDrain(); publications = []
        def publish(path, process):
            publications.append((path, process)); return {'reason': 'completed_observer_restore'}
        pending = status(); pending.update(phase='Loaded', restored='0', output_written='0')
        self.assertFalse(drain.observe(pending, 'owned process', '/private/disarm', publish))
        self.assertEqual(publications, [])
        provisional = status(); provisional['restored'] = '0'
        self.assertFalse(drain.observe(provisional, 'owned process', '/private/disarm', publish))
        self.assertEqual(publications, [('/private/disarm', 'owned process')])
        self.assertFalse(drain.observe(provisional, 'owned process', '/private/disarm', publish))
        self.assertEqual(len(publications), 1)
        broken = status(); broken['raw_returns'] = [1, 1, 0]
        self.assertFalse(drain.observe(broken, 'owned process', '/private/disarm', publish))
        self.assertTrue(drain.observe(status(), 'owned process', '/private/disarm', publish))
        self.assertEqual(len(publications), 1)
        with self.assertRaises(ValueError): probe.RestorationDrain().observe(status(), 'process', 'path', publish)

    def test_complete_requires_every_return_restoration_and_ordered_mark(self):
        self.assertTrue(probe.complete_status(status()))
        for key, value in [('restored', '0'), ('raw_returns', [1, 1, 0]), ('failure', 'foreign_thread'),
                           ('publishable', '0'), ('capture_ns', '5000000001'), ('mark5', '0')]:
            row = status(); row[key] = value
            self.assertFalse(probe.complete_status(row), key)
        row = status(); row['mark2'] = row['mark5']
        self.assertFalse(probe.complete_status(row))

    def test_exact_log_arrays_and_duplicate_refusal(self):
        row = probe.parse_status('prefix X4MP_AGENT LOAD_OBSERVER STATE sites=3 raw_calls=[1, 1, 1] raw_returns=[1, 1, 1] failure=none')
        self.assertEqual(row['raw_calls'], [1]*3)
        with self.assertRaises(ValueError): probe.parse_status('X4MP_AGENT LOAD_OBSERVER STATE sites=3 sites=2')
        self.assertIsNone(probe.parse_status('X4MP_AGENT WAIT reason=owned_preload_request'))

    def test_map_refuses_truncation_alias_missing_and_unresolved_fixtures(self):
        entries = [(i, 100+i) for i in range(1, 13)]

        def binary(rows): return b'X4IMP1\0\0' + struct.pack('<Q', len(rows)) + b''.join(struct.pack('<QQ', *r) for r in rows)

        fixtures, summary = probe.import_map(binary(entries), status(), contract())
        self.assertEqual(len(fixtures), 12); self.assertEqual(summary['unresolved'], 0)
        for data in [binary(entries)[:-1], binary(entries)+b'0', binary(entries[:-1]),
                     binary(entries[:1]+[(2, 101)]+entries[2:]),
                     binary([(1, 0)]+entries[1:]), binary(list(reversed(entries)))]:
            with self.assertRaises(ValueError): probe.import_map(data, status(), contract())
        aliases = status(); aliases['entries'] = '13'
        with self.assertRaises(ValueError): probe.import_map(binary(entries+[(13, 101)]), aliases, contract())

    def test_fresh_log_tracks_partial_lines_and_refuses_replacement_or_stale_input(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'game.log'; log = probe.FreshLog(path)
            self.assertEqual(log.lines(), [])
            path.write_bytes(b'MEN'); self.assertEqual(log.lines(), [])
            with path.open('ab') as stream: stream.write(b'U\nREADY\npartial')
            self.assertEqual(log.lines(), ['MENU', 'READY'])
            path.write_bytes(b'')
            with self.assertRaises(ValueError): log.lines()
            with self.assertRaises(ValueError): probe.FreshLog(path)

    def test_shared_terminal_acceptance_requires_complete_drained_logs(self):
        for target in ('agent', 'world'):
            for fault in ('tail', 'backlog', 'late_append', 'missing'):
                with self.subTest(target=target, fault=fault), tempfile.TemporaryDirectory() as directory:
                    paths = {name: Path(directory)/name for name in ('agent', 'world')}
                    logs = {name: probe.FreshLog(path) for name, path in paths.items()}
                    for name, path in paths.items():
                        if name == target and fault == 'missing': continue
                        data = b'complete\n'
                        if name == target and fault == 'tail': data += b'X4MP_AGENT REFUSED'
                        if name == target and fault == 'backlog': data = b'x\n' * 524288 + b'X4MP_AGENT REFUSED\n'
                        path.write_bytes(data); logs[name].lines()
                        if name == target and fault == 'late_append':
                            with path.open('ab') as stream: stream.write(b'X4MP_AGENT REFUSED\n')
                    result = {'verdict': 'incomplete'}
                    with self.assertRaises((ValueError, FileNotFoundError)):
                        probe.accept_terminal_logs(result, logs['agent'], logs['world'], native_proof=True)
                    self.assertEqual(result, {'verdict': 'incomplete'})
        with tempfile.TemporaryDirectory() as directory:
            logs = [probe.FreshLog(Path(directory)/name) for name in ('agent', 'world')]
            for log in logs: log.path.write_bytes(b'complete\n'); log.lines()
            result = {}; probe.accept_terminal_logs(result, *logs, native_proof=True)
            self.assertEqual(result['verdict'], 'pass')
            self.assertTrue(result['terminal_log_cutoff']['agent']['complete_records'])

    def test_invalid_utf8_is_never_silently_replaced_in_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'agent'; log = probe.FreshLog(path)
            path.write_bytes(b'X4MP_AGENT REFUSED\xff\n')
            with self.assertRaises(UnicodeDecodeError): log.lines()

    def test_terminal_cutoff_binds_consumed_bytes_despite_equal_length_rewrite(self):
        for restore_mtime in (False, True):
            with self.subTest(restore_mtime=restore_mtime), tempfile.TemporaryDirectory() as directory:
                path = Path(directory)/'agent'; log = probe.FreshLog(path)
                path.write_bytes(b'old complete record\n'); before = path.stat()
                log.lines()
                with path.open('r+b') as stream: stream.write(b'new complete record\n')
                if restore_mtime: os.utime(path, ns=(before.st_atime_ns, before.st_mtime_ns))
                self.assertEqual(path.stat().st_ino, before.st_ino)
                self.assertEqual(path.stat().st_size, before.st_size)
                with self.assertRaisesRegex(ValueError, 'consumed_content_changed'): log.require_drained()

    def test_terminal_rechecks_cap_after_append_between_read_stat_and_read(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'agent'; log = probe.FreshLog(path)
            chunk = b'x' * (1024 * 1024 - 1) + b'\n'
            path.write_bytes(chunk * 8)
            for _ in range(8): log.lines()
            original = os.fstat
            def append_after_stat(fd):
                value = original(fd)
                with path.open('ab') as stream: stream.write(chunk)
                return value
            with mock.patch.object(probe.os, 'fstat', side_effect=append_after_stat): log.lines()
            self.assertEqual(log.offset, 9 * 1024 * 1024)
            self.assertEqual(log.tail, b'')
            with self.assertRaisesRegex(ValueError, 'terminal_log_size_invalid'): log.require_drained()

    def test_registry_checks_full_id_generation_flags_and_class(self):
        memory = {}

        class Process:
            def read(self, address, size): return bytes(memory[address+i] for i in range(size))
            def word(self, address): return struct.unpack('<Q', self.read(address, 8))[0]

        def put(address, data): memory.update({address+i: byte for i, byte in enumerate(data)})
        runtime = 1 | (7 << 25); registry = 0x1000; page = 0x2000; obj = 0x3000
        put(registry+0x60, struct.pack('<Q', page))
        record = bytearray(32); struct.pack_into('<Q', record, 0, obj); struct.pack_into('<H', record, 24, 7)
        struct.pack_into('<I', record, 28, 5 << 16); put(page, record)
        put(obj, struct.pack('<QQ', 0x3d21748, runtime))
        self.assertEqual(probe.resolve_component(Process(), registry, runtime), obj)
        put(obj+8, struct.pack('<Q', runtime+1))
        with self.assertRaises(ValueError): probe.resolve_component(Process(), registry, runtime)
        put(obj+8, struct.pack('<Q', runtime)); record[24] = 6; put(page, record)
        with self.assertRaises(ValueError): probe.resolve_component(Process(), registry, runtime)
        record[24] = 7; record[30] = 0; put(page, record)
        with self.assertRaises(ValueError): probe.resolve_component(Process(), registry, runtime)

    @unittest.skipUnless(os.name == 'posix', 'requires Linux native UID file publication')
    def test_ready_publication_owned_atomic_no_replace_and_original_guard(self):
        class Process:
            uid = os.getuid(); pid = 123; start = 456
            def verify(self): pass
            def restoration(self, initial=False):
                return {**{k: {'matches_original': True, 'sha256': '0'*64} for k in probe.REGIONS},
                        'same_vbi_table': True, 'both_slot13_clear': True}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'ready'; path.parent.chmod(0o700)
            row = probe.publish_ready(path, Process(), 'a'*32)
            self.assertEqual(path.stat().st_mode & 0o777, 0o400)
            self.assertEqual(path.read_text(), probe.MODE+'|'+'a'*32+'|123|456\n')
            self.assertNotIn('a'*32, json.dumps(row))
            with self.assertRaises(FileExistsError): probe.publish_ready(path, Process(), 'a'*32)
            self.assertEqual(list(path.parent.iterdir()), [path])
            disarm = path.parent/'disarm'
            request = probe.publish_disarm(disarm, Process())
            self.assertEqual(disarm.stat().st_mode & 0o777, 0o400)
            self.assertEqual(request['reason'], 'completed_observer_restore')
            with self.assertRaises(FileExistsError): probe.publish_disarm(disarm, Process())


if __name__ == '__main__': unittest.main()
