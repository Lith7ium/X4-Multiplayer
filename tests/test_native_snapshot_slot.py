import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('native_gate_lane', ROOT/'tools/dev/native_gate_lane.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class LocalCore:
    @staticmethod
    def is_link(path): return Path(path).is_symlink()

    @staticmethod
    def assert_inside(path, root):
        if not Path(path).resolve().is_relative_to(Path(root).resolve()) or Path(path).resolve() == Path(root).resolve():
            raise ValueError('outside root')

    @staticmethod
    def check_tree_unlinked(path):
        if any(p.is_symlink() or p.stat().st_nlink != 1 for p in Path(path).rglob('*') if p.is_file()):
            raise ValueError('linked file')

    @staticmethod
    def safe_unlink(path, root):
        LocalCore.assert_inside(path, root)
        if Path(path).is_symlink(): raise ValueError('linked file')
        Path(path).unlink()


class SnapshotSlotTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.profile = self.root/'45850';self.profile.mkdir()
        self.source = self.root/'snapshot.xml.gz';self.source.write_bytes(b'owned test snapshot bytes')
        self.out = self.root/'out';self.out.mkdir()

    def test_stage_archive_and_restore_preserves_source(self):
        slot = module.SnapshotSlot('45850', self.profile, LocalCore)
        info = slot.stage(self.source)
        saved = slot.directory/'save_009.xml.gz'
        self.assertEqual(saved.read_bytes(), self.source.read_bytes())
        archived = slot.collect(self.out)
        self.assertEqual(archived[0]['sha256'], info['sha256'])
        slot.restore()
        self.assertFalse(saved.exists())
        self.assertEqual(self.source.read_bytes(), b'owned test snapshot bytes')

    def test_existing_slot_is_never_replaced(self):
        saves = self.profile/'save';saves.mkdir()
        existing = saves/'save_009.xml.gz';existing.write_bytes(b'keep this')
        with self.assertRaises(ValueError): module.SnapshotSlot('45850', self.profile, LocalCore)
        self.assertEqual(existing.read_bytes(), b'keep this')

    def test_protected_profile_is_refused(self):
        protected = self.root/'45841';protected.mkdir()
        with self.assertRaises(ValueError): module.SnapshotSlot('45841', protected, LocalCore)
        self.assertFalse((protected/'save').exists())

    def test_changed_file_after_archive_is_preserved(self):
        slot = module.SnapshotSlot('45850', self.profile, LocalCore)
        slot.stage(self.source);slot.collect(self.out)
        saved = slot.directory/'save_009.xml.gz';saved.write_bytes(b'changed')
        with self.assertRaises(ValueError): slot.restore()
        self.assertEqual(saved.read_bytes(), b'changed')

    def test_uncollected_save_is_not_removed(self):
        slot = module.SnapshotSlot('45850', self.profile, LocalCore)
        slot.stage(self.source)
        with self.assertRaises(ValueError): slot.restore()
        self.assertTrue((slot.directory/'save_009.xml.gz').exists())
