"""Real Linux PIDFD cleanup, including a child whose launcher already exited."""
import importlib.util
import hashlib
import os
from pathlib import Path
import select
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

path=Path(__file__).resolve().parents[1]/'tools/dev/linux_native_admission_run.py'
spec=importlib.util.spec_from_file_location('linux_native_admission_run',path)
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)


class ObserverIsolation(unittest.TestCase):
    def test_memory_telemetry_accepts_zero_and_unavailable_readings(self):
        for value, expected in [('MemAvailable: 0 kB\n', 0), ('MemAvailable: 1024 kB\n', 1048576),
                                ('MemTotal: 8000 kB\n', None), ('MemAvailable: invalid kB\n', None)]:
            with mock.patch.object(module.Path, 'read_text', return_value=value):
                self.assertEqual(module.memory_available_bytes(), expected)
        with mock.patch.object(module.Path, 'read_text', side_effect=OSError('unavailable')):
            self.assertIsNone(module.memory_available_bytes())

    def test_loaded_marker_preserves_terminal_failure_without_claiming_acceptance(self):
        result={'probe':{'verdict':'fail','error':'native_observer_fault','markers':{'LOADED':{'text':'exact owned marker'}}}}
        module.summarize_probe(result,True)
        self.assertTrue(result['loaded_world_marker_observed'])
        self.assertFalse(result['loaded_world_verified'])
        self.assertEqual(result['error'],'native_observer_fault')
        result={'probe':{'verdict':'pass','loaded_snapshot_verified':True,'markers':{'LOADED':{}}}}
        module.summarize_probe(result,True)
        self.assertTrue(result['loaded_world_verified']);self.assertNotIn('error',result)

    def test_observer_environment_excludes_scheduler_and_command_admission(self):
        old={'HOME':'private','X4MP_SCHED':'fixed60','X4MP_SCHED_WAIT_MS':'5',
             'X4MP_ADMISSION':'clock-noop-v1','X4MP_ADMISSION_TOKEN':'old','X4MP_AGENT':'1'}
        result=module.observer_environment(old,'a'*32,Path('/private/ready'))
        self.assertFalse(any(key.startswith(('X4MP_ADMISSION','X4MP_SCHED')) for key in result))
        self.assertEqual(result['X4MP_LOAD_OBSERVER'],'snapshot-import-observer-v1')
        self.assertEqual(result['HOME'],'private')
        self.assertEqual(old['X4MP_SCHED'],'fixed60')

    def test_fixture_contains_only_exact_snapshot_loader_and_common_md(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);bundle=root/'bundle';case=root/'snapshot-observer-test'
            profile=case/'home/.config/EgoSoft/X4/45841';(profile/'save').mkdir(parents=True)
            (root/'bin').mkdir();(root/'bin/native_portable_linux.lua').write_text('backend')
            (root/'bin/x4mp_load_probe.so').write_bytes(b'owned read-only Lua bridge')
            for name,data in [('md/native_gate_battle.xml','saved MD'),('ui/native_portable_loader.lua','common loader')]:
                target=bundle/'fixture'/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_text(data)
            snapshot=b'owned disposable snapshot';(bundle/'save_009.xml.gz').write_bytes(snapshot)
            contract={'snapshot_sha256':hashlib.sha256(snapshot).hexdigest(),'schema_sha256':'b'*64}
            boot=module.stage_observer_fixture(case,profile,root,bundle,contract)
            self.assertEqual((profile/'save/save_009.xml.gz').read_bytes(),snapshot)
            self.assertEqual((boot/'md/native_gate_battle.xml').read_text(),'saved MD')
            self.assertEqual((boot/'native/x4mp_load_probe.so').read_bytes(),b'owned read-only Lua bridge')
            self.assertEqual(sorted(p.name for p in (case/'extensions').iterdir()),['x4_lan_lab'])
            ui=(boot/'ui.xml').read_text()
            self.assertLess(ui.index('native_portable_linux.lua'),ui.index('native_portable_loader.lua'))
            settings=(boot/'ui/settings.lua').read_text()
            self.assertIn('snapshot-import-observer-v1',settings);self.assertNotIn('token',settings)
            self.assertNotIn('autostart',ui);self.assertNotIn('gamestart',settings)


@unittest.skipUnless(sys.platform=='linux' and hasattr(os,'pidfd_open'),'real Linux PIDFD required')
class OwnedCleanup(unittest.TestCase):
    def test_orphan_ignoring_term_is_still_bounded_and_owned(self):
        code='''import os,signal,time
r,w=os.pipe();pid=os.fork()
if pid:
 os.close(w);os.read(r,1);print(pid,flush=True);os._exit(0)
else:
 os.close(r);signal.signal(signal.SIGTERM,signal.SIG_IGN);os.write(w,b'1');time.sleep(60)
'''
        parent=subprocess.Popen([sys.executable,'-c',code],stdout=subprocess.PIPE,text=True)
        pid=int(parent.stdout.readline());fd=os.pidfd_open(pid)
        try:
            self.assertEqual(parent.wait(timeout=5),0)
            poll=select.poll();poll.register(fd,select.POLLIN)
            self.assertFalse(poll.poll(0))
            started=time.monotonic();module.stop_pidfd(fd)
            self.assertGreaterEqual(time.monotonic()-started,1.9)
            self.assertLess(time.monotonic()-started,8)
            self.assertTrue(poll.poll(0))
            module.stop_pidfd(fd)  # An exited identity cannot target a reused PID.
        finally:
            try:module.stop_pidfd(fd)
            finally:os.close(fd);parent.stdout.close()


if __name__=='__main__':unittest.main()
