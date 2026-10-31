"""Real owned nongame Windows process checks; never launches X4."""
import hashlib
import importlib.util
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/dev'))
from native_portable_windows_identity import ArtifactLease, NativeIdentity, WindowsAPI


def digest(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()


@unittest.skipUnless(sys.platform == 'win32', 'real owned Windows process handles required')
class WindowsIdentity(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = Path(__file__).resolve().parents[2] / 'lanes/release/dev/lane/lanewin.py'
        spec = importlib.util.spec_from_file_location('portable_identity_released_windows', path)
        cls.win = importlib.util.module_from_spec(spec); sys.modules[spec.name] = cls.win; spec.loader.exec_module(cls.win)
        cls.exe = Path(sys.executable).resolve(); cls.exe_sha = digest(cls.exe)

    def child(self):
        job = self.win.KillOnCloseJob()
        child = subprocess.Popen([str(self.exe), '-c', 'import sys;print("READY",flush=True);sys.stdin.readline()'],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            job.assign(int(child._handle))
            self.assertEqual(child.stdout.readline().strip(), b'READY')
        except Exception:
            child.kill(); child.wait(timeout=5); job.close(); raise
        self.addCleanup(child.stdin.close); self.addCleanup(child.stdout.close); self.addCleanup(child.stderr.close)
        self.addCleanup(job.close)
        self.addCleanup(lambda: (child.kill(), child.wait(timeout=5)) if child.poll() is None else None)
        return child, job

    def test_existing_owned_handle_job_loaded_image_and_exact_exit(self):
        identity = NativeIdentity(self.exe, self.exe, self.exe_sha, self.exe_sha)
        self.addCleanup(identity.close)
        child, job = self.child()
        attached = identity.attach(int(child._handle), child.pid, job.handle)
        self.assertEqual(attached['pid'], child.pid); self.assertGreater(attached['created'], 0)
        observed = identity.observe()
        self.assertTrue(observed['module_observed']); self.assertTrue(observed['owned_job_verified'])
        self.assertFalse(observed['module']['write_delete_sharing'])
        self.assertEqual(observed['mapped_module']['image_size'], observed['module']['image_size'])
        with self.assertRaisesRegex(ValueError, 'not exited'): identity.finished()
        child.stdin.write(b'\n'); child.stdin.flush(); self.assertEqual(child.wait(timeout=5), 0)
        self.assertTrue(identity.finished()['owned_process_exited'])
        with self.assertRaisesRegex(ValueError, 'not running'): identity.observe()

    def test_owned_udp_source_join_uses_existing_handle_and_released_endpoint_table(self):
        identity = NativeIdentity(self.exe, self.exe, self.exe_sha, self.exe_sha)
        self.addCleanup(identity.close); job = self.win.KillOnCloseJob(); self.addCleanup(job.close)
        receiver = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); self.addCleanup(receiver.close)
        receiver.bind(('127.0.0.1', 0)); receiver.settimeout(5)
        child = subprocess.Popen([str(self.exe), '-c',
            'import socket,sys; s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);'
            f's.connect(("127.0.0.1",{receiver.getsockname()[1]}));s.send(b"owned-udp-probe");'
            'print(s.getsockname()[1],flush=True);sys.stdin.readline()'],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            job.assign(int(child._handle)); port = int(child.stdout.readline())
            data, source = receiver.recvfrom(128)
            self.assertEqual(data, b'owned-udp-probe'); self.assertEqual(source, ('127.0.0.1', port))
            identity.attach(int(child._handle), child.pid, job.handle)
            identity.observe_udp_sources(self.win.endpoints)
            observed = identity.observe_udp_sources(self.win.endpoints)['udp_source_ownership']
            self.assertEqual(observed['pid'], child.pid); self.assertEqual(observed['created'], identity.created)
            # Like the native gate, connect() without bind() is represented by
            # the Windows owner-PID table as a wildcard binding, even though the
            # connected route and received packet source are loopback.
            row = next(row for row in observed['sources'] if row['address'] == ['0.0.0.0', port])
            self.assertEqual(row['observations'], 2); self.assertLessEqual(row['first_filetime'], row['last_filetime'])
            with self.assertRaisesRegex(ValueError, 'attribution'):
                identity.observe_udp_sources(lambda _: [{'proto': 'udp', 'local': '127.0.0.1', 'lport': port, 'pid': child.pid + 1}])
            calls = []
            def unstable(_):
                calls.append(1)
                return [] if len(calls) == 1 else [{'proto': 'udp', 'local': '127.0.0.1', 'lport': port, 'pid': child.pid}]
            with self.assertRaisesRegex(ValueError, 'changed during'): identity.observe_udp_sources(unstable)
            with self.assertRaisesRegex(ValueError, 'unbounded'): identity.observe_udp_sources(lambda _: [{}] * 4097)
            duplicate = {'proto': 'udp', 'local': '0.0.0.0', 'lport': port, 'pid': child.pid}
            with self.assertRaisesRegex(ValueError, 'ambiguous'): identity.observe_udp_sources(lambda _: [duplicate, duplicate])
            child.stdin.write(b'\n'); child.stdin.flush(); self.assertEqual(child.wait(timeout=5), 0)
            with self.assertRaisesRegex(ValueError, 'not running'): identity.observe_udp_sources(self.win.endpoints)
        finally:
            if child.poll() is None: child.kill(); child.wait(timeout=5)
            child.stdin.close(); child.stdout.close(); child.stderr.close()

    def test_wrong_pid_and_foreign_same_name_module_refuse(self):
        identity = NativeIdentity(self.exe, self.exe, self.exe_sha, self.exe_sha)
        child, job = self.child()
        try:
            with self.assertRaisesRegex(ValueError, 'PID differs'):
                identity.attach(int(child._handle), child.pid + 1, job.handle)
        finally: identity.close()
        with tempfile.TemporaryDirectory() as temporary:
            foreign = Path(temporary) / self.exe.name; shutil.copyfile(self.exe, foreign)
            identity = NativeIdentity(self.exe, foreign, self.exe_sha, self.exe_sha)
            child, job = self.child()
            try:
                identity.attach(int(child._handle), child.pid, job.handle)
                with self.assertRaisesRegex(ValueError, 'foreign path'): identity.observe()
            finally: identity.close()

    def test_lease_hashes_same_handle_and_blocks_write_and_delete_until_closed(self):
        with tempfile.TemporaryDirectory() as temporary:
            artifact = Path(temporary) / 'owned.exe'; shutil.copyfile(self.exe, artifact)
            with self.assertRaisesRegex(ValueError, 'digest'):
                ArtifactLease(artifact, '0' * 64, WindowsAPI())
            lease = ArtifactLease(artifact, self.exe_sha, WindowsAPI())
            try:
                with self.assertRaises(PermissionError): artifact.write_bytes(b'changed')
                with self.assertRaises(PermissionError): artifact.unlink()
                self.assertEqual(digest(artifact), self.exe_sha)
            finally: lease.close()
            artifact.write_bytes(b'owned temporary artifact after closed lease')

    def test_wrong_job_and_missing_module_are_not_native_proof(self):
        identity = NativeIdentity(self.exe, self.exe, self.exe_sha, self.exe_sha)
        child, job = self.child(); other_job = self.win.KillOnCloseJob(); self.addCleanup(other_job.close)
        try:
            with self.assertRaisesRegex(ValueError, 'outside owning job'):
                identity.attach(int(child._handle), child.pid, other_job.handle)
        finally: identity.close()
        with tempfile.TemporaryDirectory() as temporary:
            missing = Path(temporary) / 'unloaded.dll'; shutil.copyfile(self.exe, missing)
            identity = NativeIdentity(self.exe, missing, self.exe_sha, self.exe_sha)
            child, job = self.child()
            try:
                identity.attach(int(child._handle), child.pid, job.handle)
                self.assertFalse(identity.observe()['module_observed'])
            finally: identity.close()

    def test_existing_process_cannot_be_retroactively_bound_to_new_artifact_leases(self):
        child, job = self.child()
        identity = NativeIdentity(self.exe, self.exe, self.exe_sha, self.exe_sha)
        try:
            with self.assertRaisesRegex(ValueError, 'predates artifact leases'):
                identity.attach(int(child._handle), child.pid, job.handle)
        finally: identity.close()


if __name__ == '__main__': unittest.main()
