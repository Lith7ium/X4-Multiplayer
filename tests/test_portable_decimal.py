"""Portable decimal reader: correct rounding and the engines' syntax on a corpus.

Builds src/native/portable_math/portable_decimal.c with the harness (Zig 0.15.2 from
X4LAN_ZIG or PATH, else cc on Linux) and compares every case with the independent model
in tools/dev/check_portable_decimal.py. The comparison with the original game readers
needs the executables: tools/dev/check_portable_decimal.py --windows/--linux.
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/dev'))
import check_portable_decimal as check  # noqa: E402


class PortableDecimalModel(unittest.TestCase):
    def test_model_values_on_known_cases(self):
        self.assertEqual(check.model(b'1.5;7', ord(';')), ('value', False, '15', -1, 4, 0))
        self.assertEqual(check.model(b'#x;2', ord(';')), ('zero', 0, '', 0, 3, 0))
        self.assertEqual(check.model(b'5#', ord(';'))[0], 'fallback')
        self.assertEqual(check.model(b'1e%101', ord(';'))[3], 5)
        self.assertEqual(check.round_bits(False, '1', -1, 53, -1022, 1023), 0x3fb999999999999a)
        self.assertEqual(check.round_bits(False, '24703282292062328', -340, 53, -1022, 1023), 1)
        self.assertEqual(check.round_bits(True, '1', 400, 53, -1022, 1023), 0xfff0000000000000)
        self.assertEqual(check.round_bits(False, '1', -1, 24, -126, 127), 0x3dcccccd)


class PortableDecimalBuilt(unittest.TestCase):
    def test_harness_matches_the_model_and_rounds_correctly(self):
        zig = os.environ.get('X4LAN_ZIG') or shutil.which('zig')
        args = ['--count', '3000']
        if zig:
            args += ['--zig', zig]
        elif os.name != 'nt' and shutil.which('cc'):
            args += ['--cc', 'cc']
        else:
            self.skipTest('needs Zig 0.15.2 (X4LAN_ZIG) or a C compiler')
        result = subprocess.run([sys.executable, str(ROOT / 'tools/dev/check_portable_decimal.py'), *args],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('values correctly rounded, syntax as modelled', result.stdout)


if __name__ == '__main__':
    unittest.main()
