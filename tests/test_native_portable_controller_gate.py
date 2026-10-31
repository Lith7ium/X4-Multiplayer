"""Controller handshake with modeled SSH output; never starts a game."""
import copy
import io
import json
from pathlib import Path
import tempfile
import subprocess
import sys
from types import SimpleNamespace
import unittest
from unittest import mock

import native_portable_controller_gate as controller
import native_portable_prelaunch as binding
from native_portable import canonical_json
from test_native_portable_start_gate import grant_fixture
from test_native_portable_prelaunch import bindings


class ControllerGate(unittest.TestCase):
    @unittest.skipUnless(sys.platform=='win32','actual owned Windows process handle')
    def test_actual_owned_nongame_runner_creation_and_exit(self):
        child=subprocess.Popen([sys.executable,'-B','-c','import sys;sys.stdin.buffer.read()'],
            stdin=subprocess.PIPE,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,
            creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            first=controller.owned_runner(child)
            self.assertEqual(first['pid'],child.pid);self.assertGreater(first['created'],0)
            self.assertEqual(controller.owned_runner(child),first)
            child.stdin.close();child.wait(timeout=5)
            with self.assertRaises(ValueError):controller.owned_runner(child)
        finally:
            if child.poll() is None:child.terminate();child.wait(timeout=5)
            child.stderr.close()

    def test_request_binds_actual_owned_runner_and_original_raw_inputs(self):
        for mutation in (None,'runner','prepared','session','ready','deadline'):
            with self.subTest(mutation=mutation),tempfile.TemporaryDirectory() as temporary:
                out=Path(temporary).resolve();windows=out/'windows';windows.mkdir()
                config,ready,prepared,request=bindings()
                (windows/'portable-prelaunch-request.json').write_bytes(canonical_json(request))
                for path,value in ((windows/'portable-session.json',config),(windows/'portable-barrier-remote.json',ready),
                                   (out/'linux-prepared.json',prepared)):
                    path.write_bytes((json.dumps(value,indent=2)+'\n').encode())
                if mutation in ('session','ready','prepared'):
                    path={'session':windows/'portable-session.json','ready':windows/'portable-barrier-remote.json',
                          'prepared':out/'linux-prepared.json'}[mutation]
                    path.write_bytes(path.read_bytes()+b' ')
                process=SimpleNamespace(poll=lambda:None);linux=SimpleNamespace(poll=lambda:None)
                identity=dict(request['runner']);identity['pid']+=mutation=='runner'
                with mock.patch.object(controller,'owned_runner',return_value=identity), \
                        mock.patch.object(controller.time,'monotonic_ns',return_value=request['deadline_monotonic_ns'] if mutation=='deadline' else 21_000_000_000):
                    args=dict(out=out,windows=windows,process=process,linux=linux,contract=config['contract'],
                        participants=config['participants'],prepared=prepared,remote_script='/private/native_portable_ssh.py',deadline=float('inf'))
                    if mutation:
                        with self.assertRaises(ValueError):controller.request_config(**args)
                    else:
                        actual=controller.request_config(**args)
                        self.assertEqual(actual['prelaunch_request'],request);self.assertEqual(actual['token'],config['token'])

    def test_only_one_verified_fetch_can_grant_and_failed_child_or_fetch_never_writes_pipe(self):
        for mutation in (None,'linux_exit','windows_exit','stderr','returncode','foreign_case','foreign_runner','missing_proof','expired'):
            with self.subTest(mutation=mutation),tempfile.TemporaryDirectory() as temporary:
                out=Path(temporary).resolve();windows=out/'windows';windows.mkdir()
                grant,config,contract,review=grant_fixture();receipt=grant['fetch']['receipt']
                plan=dict(remote_root='/private',linux_bundle=receipt['bundle'],remote_config='/private/config.json',
                          linux_review_sha256=receipt['review_sha256'])
                (out/'linux-runner.log').write_text('OWNED_NATIVE_READY '+json.dumps(dict(case=receipt['case']))+'\n')
                process=SimpleNamespace(poll=lambda:1 if mutation=='windows_exit' else None,stdin=io.BytesIO())
                linux=SimpleNamespace(poll=lambda:1 if mutation=='linux_exit' else None)
                envelope=copy.deepcopy(grant['fetch'])
                if mutation=='missing_proof':del envelope['receipt']['rotation_status']
                fetched=SimpleNamespace(returncode=1 if mutation=='returncode' else 0,
                    stderr=b'fault' if mutation=='stderr' else b'',stdout=canonical_json(envelope)+b'\n')
                identity=dict(config['prelaunch_request']['runner']);identity['created']+=mutation=='foreign_runner'
                with mock.patch.object(controller,'owned_runner',return_value=identity), \
                        mock.patch.object(controller.subprocess,'run',return_value=fetched) as fetch, \
                        mock.patch.object(controller.time,'monotonic_ns',return_value=700_000_000_000 if mutation=='expired' else 21_000_000_000):
                    args=dict(plan=plan,out=out,windows=windows,process=process,linux=linux,config=config,contract=contract,
                        review=review,remote_command=lambda args:args,
                        case_from_log=lambda *_:'/private/cases/foreign' if mutation=='foreign_case' else receipt['case'])
                    if mutation:
                        with self.assertRaises((ValueError,RuntimeError,TimeoutError)):controller.release_windows(**args)
                        self.assertEqual(process.stdin.getvalue(),b'')
                    else:
                        value=controller.release_windows(**args)
                        self.assertEqual(binding.decode(process.stdin.getvalue()),value)
                        self.assertFalse(process.stdin.closed);self.assertEqual(fetch.call_count,1)
                        self.assertEqual(fetch.call_args.args[0][2],plan['linux_bundle']+'/bin/linux_portable_prelaunch.py')
                        with self.assertRaisesRegex(ValueError,'one-shot'):controller.release_windows(**args)

    def test_rejection_diagnostic_missing_or_failed_never_claims_acceptance(self):
        with tempfile.TemporaryDirectory() as temporary:
            target=Path(temporary)/'rejection.json'
            value=controller.collect_rejection('', 'owned-run','a'*64,target)
            self.assertIs(value['acceptance'],False);self.assertIn('error',value)
            self.assertEqual(json.loads(target.read_text()),value)


if __name__=='__main__':unittest.main()
