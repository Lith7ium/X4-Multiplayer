"""Portable Jolt (native_gate_jolt_portable): our own Jolt Physics 5.5.0 builds for Windows (MSVC)
and Linux (g++) and the offline scenario that proves them bit-identical.

Covers the pin and licence files, the build configuration of both variants, the symbol renaming
used by the offline redirect model and the result comparison. The builds themselves and the
scenario runs need the toolchains (and, for x4-harness, the owned Linux executable); recorded
results under .local/jolt-portable/results are checked when present."""
import contextlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('jolt_portable_build', ROOT / 'tools/dev/jolt_portable_build.py')
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)
spec = importlib.util.spec_from_file_location('jolt_portable_winmap', ROOT / 'tools/dev/jolt_portable_winmap.py')
winmap = importlib.util.module_from_spec(spec)
spec.loader.exec_module(winmap)
spec = importlib.util.spec_from_file_location('jolt_portable_feasibility', ROOT / 'tools/dev/jolt_portable_feasibility.py')
feasibility = importlib.util.module_from_spec(spec)
spec.loader.exec_module(feasibility)
RESULTS = ROOT / '.local/jolt-portable/results'


class Pin(unittest.TestCase):
    def test_pin_file_matches_the_build_tool(self):
        pin = (ROOT / 'third_party/JoltPhysics-5.5.0/PIN.txt').read_text(encoding='utf-8')
        self.assertIn(f'Tag:      {build.JOLT_TAG}', pin)
        self.assertIn(f'Commit:   {build.JOLT_COMMIT}', pin)
        self.assertIn(build.JOLT_URL.removesuffix('.git'), pin)

    def test_licence_is_kept(self):
        licence = (ROOT / 'third_party/JoltPhysics-5.5.0/LICENSE').read_text(encoding='utf-8')
        self.assertTrue(licence.startswith('Copyright 2021 Jorrit Rouwe'))
        self.assertIn('Permission is hereby granted, free of charge', licence)
        self.assertIn('third_party/JoltPhysics-5.5.0/LICENSE', (ROOT / 'NOTICE.txt').read_text(encoding='utf-8'))
        self.assertIn('third_party/JoltPhysics-5.5.0/LICENSE', (ROOT / 'tools/distribution/THIRD-PARTY.txt').read_text(encoding='utf-8'))

    def test_sources_stay_out_of_the_repository(self):
        self.assertEqual(build.JOLT, ROOT / '.local/jolt/JoltPhysics-5.5.0')
        self.assertFalse((ROOT / 'third_party/JoltPhysics-5.5.0/Jolt').exists())


class Flags(unittest.TestCase):
    def test_both_variants_use_x4s_feature_set(self):
        for windows in (True, False):
            for variant in ('deterministic', 'control'):
                flags = build.compile_flags(variant, windows, 'cc')
                define = '/D' if windows else '-D'
                self.assertIn(f'{define}JPH_OBJECT_STREAM', flags)
                self.assertIn(f'{define}NDEBUG', flags)
                for absent in ('JPH_DOUBLE_PRECISION', 'JPH_ENABLE_ASSERTS', 'JPH_PROFILE_ENABLED', 'JPH_DEBUG_RENDERER',
                               'JPH_USE_AVX', 'JPH_USE_FMADD', 'JPH_USE_SSE4_1'):
                    self.assertFalse(any(absent in f for f in flags), (windows, variant, absent))
                self.assertFalse(any(f.startswith(('-march', '-mfma', '-mavx', '/arch')) for f in flags))

    def test_deterministic_variant(self):
        windows = build.compile_flags('deterministic', True, 'cl')
        self.assertIn('/DJPH_CROSS_PLATFORM_DETERMINISTIC', windows)
        self.assertIn('/fp:precise', windows)
        self.assertNotIn('/fp:fast', windows)
        self.assertIn('/MT', windows)
        linux = build.compile_flags('deterministic', False, 'g++')
        self.assertIn('-DJPH_CROSS_PLATFORM_DETERMINISTIC', linux)
        self.assertIn('-ffp-contract=off', linux)
        self.assertFalse(any('fast-math' in f for f in linux))

    def test_control_variant_models_x4_windows(self):
        windows = build.compile_flags('control', True, 'cl')
        self.assertIn('/fp:fast', windows)
        self.assertFalse(any('DETERMINISTIC' in f for f in windows))
        self.assertNotIn('/GL', windows)
        self.assertIn('/GL', build.compile_flags('mapping', True, 'cl'))


class Redirect(unittest.TestCase):
    def test_twin_names_keep_their_length(self):
        for name in ('_ZN3JPH8BoxShapeC2ERKNS_16BoxShapeSettingsERNS_6ResultINS_3RefINS_5ShapeEEEEE',
                     '_ZNK3JPH13PhysicsSystem12GetBodyStatsEv'):
            twin = build.qph_name(name)
            self.assertEqual(len(twin), len(name))
            self.assertNotIn('3JPH', twin)
            self.assertEqual(twin.replace('3QPH', '3JPH'), name)

    def test_shared_data_excludes_per_copy_objects(self):
        names = {'_ZN3JPH8AllocateE', '_ZN3JPH4Body15sFixedToWorldE', '_ZN3JPH17CollisionDispatch13sCollideShapeE',
                 '_ZTVN3JPH8BoxShapeE', '_ZTIN3JPH5ShapeE', '_ZZN3JPH13GetRTTIOfTypeEPNS_4Vec3EE4rtti',
                 '_ZGVZN3JPH4Body15sFixedToWorldE', 'some_engine_global'}
        self.assertEqual(build.shared_names(names), {'_ZN3JPH8AllocateE', '_ZN3JPH4Body15sFixedToWorldE',
                                                     '_ZN3JPH17CollisionDispatch13sCollideShapeE'})


class Names(unittest.TestCase):
    """Name handling of the Windows matcher: MSVC undecorated and Itanium demangled names meet."""

    def test_qualified(self):
        self.assertEqual(winmap.qualified('public: virtual float __cdecl JPH::SphereShape::GetVolume(void)const __ptr64'),
                         'JPH::SphereShape::GetVolume')
        self.assertEqual(winmap.qualified('JPH::BroadPhase::Init(JPH::BodyManager*, JPH::BroadPhaseLayerInterface const&)'),
                         'JPH::BroadPhase::Init')
        self.assertEqual(winmap.qualified('void JPH::QuickSort<int*, std::less<int> >(int*, int*, std::less<int>)'),
                         'JPH::QuickSort<int*, std::less<int> >')

    def test_comparable(self):
        self.assertEqual(winmap.comparable('JPH::Array<JPH::Vec3,JPH::STLAllocator<JPH::Vec3> >::reserve'),
                         winmap.comparable('JPH::Array<JPH::Vec3, JPH::STLAllocator<JPH::Vec3> >::reserve'))
        self.assertEqual(winmap.comparable("JPH::BoxShape::`scalar deleting destructor'"), 'JPH::BoxShape::~BoxShape')

    def test_read_map(self):
        text = '\n'.join([' 0001:00002b30       ??1BoxShapeSettings@JPH@@UEAA@XZ 0000000140003b30 f i scenario.obj',
                          ' 0002:00000010       ??_7BoxShape@JPH@@6B@ 0000000140100010     jolt:BoxShape.obj',
                          ' 0001:0010ee40       ?GetVolume@SphereShape@JPH@@UEBAMXZ 000000014010fe40 f   jolt:SphereShape.obj']) + '\n'
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'x.map'
            path.write_text(text)
            names, objects, data = winmap.read_map(path, 0x140000000)
        self.assertEqual(names[0x3b30], ['??1BoxShapeSettings@JPH@@UEAA@XZ'])
        self.assertEqual(objects[0x10fe40], 'jolt:SphereShape.obj')
        self.assertEqual(data[0x100010], ['??_7BoxShape@JPH@@6B@'])

    def test_fp_census_classes(self):
        for mnemonic, kind in (('mulss', 'ss'), ('vaddps', 'ps'), ('sqrtsd', 'sd'), ('vfmadd231ss', 'ss')):
            self.assertEqual(feasibility.FP.match(mnemonic).group(2), kind)
        for mnemonic in ('movss', 'shufps', 'comiss', 'cvtss2sd'):
            self.assertIsNone(feasibility.FP.match(mnemonic))


class Compare(unittest.TestCase):
    def write(self, folder, name, lines):
        path = Path(folder) / name
        path.write_text('\n'.join(lines) + '\n')
        return path

    def test_identical_different_and_short(self):
        with tempfile.TemporaryDirectory() as folder:
            a = self.write(folder, 'a', ['frame 1 00 11', 'final 22'])
            b = self.write(folder, 'b', ['frame 1 00 11', 'final 22'])
            c = self.write(folder, 'c', ['frame 1 00 12', 'final 22'])
            d = self.write(folder, 'd', ['frame 1 00 11'])
            with contextlib.redirect_stdout(io.StringIO()):
                same, changed, short = build.compare(a, b), build.compare(a, c), build.compare(a, d)
            self.assertEqual((same, changed, short), (0, 1, 1))


class RecordedResults(unittest.TestCase):
    """Runs recorded on this machine (tools/dev/jolt_portable_build.py run / x4-run)."""

    def final(self, name):
        path = RESULTS / name
        if not path.exists():
            self.skipTest(f'{path} not recorded here')
        lines = path.read_text().splitlines()
        self.assertTrue(lines and lines[-1].startswith('final '), name)
        return path.read_text()

    def test_windows_and_linux_deterministic_builds_agree(self):
        self.assertEqual(self.final('windows-det-j4.txt'), self.final('linux-det-j4.txt'))

    def test_thread_count_does_not_matter(self):
        self.assertEqual(self.final('windows-det-j4.txt'), self.final('windows-det-j1.txt'))
        self.assertEqual(self.final('linux-det-j4.txt'), self.final('linux-det-j1.txt'))

    def test_fast_math_control_differs(self):
        self.assertNotEqual(self.final('windows-ctl-j4.txt'), self.final('windows-det-j4.txt'))

    def test_x4_linux_jolt_agrees(self):
        self.assertEqual(self.final('x4linux-own-j4.txt'), self.final('linux-det-j4.txt'))
        self.assertEqual(self.final('x4linux-redirect-j4.txt'), self.final('linux-det-j4.txt'))


if __name__ == '__main__':
    unittest.main()
