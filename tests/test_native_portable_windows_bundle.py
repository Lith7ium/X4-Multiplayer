"""Inert bundle byte provenance; no native module loading or game launch."""
import hashlib
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

from test_native_portable import contract, participants
from test_native_portable_lane import RELEASE
import build_native_portable_windows as bundle
import native_portable_lane as lane


class WindowsBundle(unittest.TestCase):
    def test_extra_file_created_between_validation_and_copy_refuses_manifest(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); inputs = self.fixture(root)
            original_copy = bundle.shutil.copytree
            def changed_copy(source, destination, *args, **kwargs):
                if Path(source) == inputs[1]:
                    (inputs[1] / 'md/unmanifested.xml').write_bytes(b'<mdscript/>')
                return original_copy(source, destination, *args, **kwargs)
            with mock.patch.object(bundle.shutil, 'copytree', side_effect=changed_copy):
                with self.assertRaisesRegex(ValueError, 'unmanifested'):
                    bundle.build(*inputs, root / 'out')
            self.assertFalse((root / 'out/artifacts.json').exists())
            self.assertFalse((root / 'out/manifest.json').exists())

    def fixture(self, root):
        fixture = root / 'fixture'; fixture.mkdir()
        content = {'md/native_gate_battle.xml': b'<mdscript name="Fixture"><cues /></mdscript>',
            'md/x4lan.xml': b'<mdscript name="X4LAN_PortableInert"><cues /></mdscript>',
            'ui/native_portable_loader.lua': b'-- shared loader bytes\n'}
        for name, data in content.items():
            target = fixture / name; target.parent.mkdir(parents=True, exist_ok=True); target.write_bytes(data)
        cfg = contract(); cfg['fixture_files'] = {name: hashlib.sha256(data).hexdigest() for name, data in content.items()}
        path = root / 'contract.json'; path.write_text(json.dumps(cfg))
        dll = root / 'synthetic.dll'; dll.write_bytes(b'owned synthetic artifact for byte tests only')
        adapter = root / 'adapter.lua'; adapter.write_bytes(b'-- platform adapter\n')
        return path, fixture, dll, bundle.sha(dll), adapter, bundle.sha(adapter)

    def test_complete_inert_base_matches_declared_files_and_released_base_guard(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); inputs = self.fixture(root)
            report = bundle.build(*inputs, root / 'out')
            self.assertFalse(report['native_launch'])
            peers = participants(); peers['1']['agent_sha256'] = report['agent_sha256']
            artifacts = json.loads((root / 'out/artifacts.json').read_text())
            lane.validate_artifacts(artifacts, peers)
            fake = SimpleNamespace(lane=SimpleNamespace(lab=root / 'out/base', profile='45850', profile_dir=root / '45850'))
            lane.verify_installed(fake, json.loads(inputs[0].read_text()), artifacts)
            self.assertEqual((root / 'out/base/ui/settings.lua').read_bytes(), lane.INERT_SETTINGS)
            if RELEASE.exists(): lane.load_release(RELEASE).core.check_base_lab(root / 'out/base')
            with self.assertRaises(ValueError): bundle.build(*inputs, root / 'out')

    def test_changed_shared_or_participant_bytes_and_active_md_refuse_before_output(self):
        for kind in ('shared', 'dll', 'md'):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary); inputs = self.fixture(root)
                if kind == 'shared': (inputs[1] / 'ui/native_portable_loader.lua').write_bytes(b'changed')
                elif kind == 'dll': inputs[2].write_bytes(b'changed artifact')
                else:
                    target = inputs[1] / 'md/x4lan.xml'
                    target.write_bytes(b'<mdscript name="X4LAN_PortableInert"><cues><cue name="active" /></cues></mdscript>')
                    cfg = json.loads(inputs[0].read_text()); cfg['fixture_files']['md/x4lan.xml'] = bundle.sha(target)
                    inputs[0].write_text(json.dumps(cfg))
                with self.assertRaises(ValueError): bundle.build(*inputs, root / 'out')
                self.assertFalse((root / 'out').exists())


if __name__ == '__main__': unittest.main()
