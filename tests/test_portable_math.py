"""Portable math library: contract checks and a short bit-identity run.

Builds src/native/portable_math with the available toolchain (Zig 0.15.2 from
X4LAN_ZIG or PATH, else cc on Linux) and runs the harness's 'check' and a small
'hash'. The full cross-platform proof is tools/dev/check_portable_math.py.
"""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'src/native/portable_math'
FLAGS = ['-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off']


def toolchain():
    zig = os.environ.get('X4LAN_ZIG') or shutil.which('zig')
    if zig:
        try:
            if subprocess.check_output([zig, 'version'], text=True).strip() == '0.15.2':
                target = 'x86_64-windows-gnu' if os.name == 'nt' else 'x86_64-linux-gnu'
                return [zig, 'cc', '-target', target], ('.exe' if os.name == 'nt' else '')
        except OSError:
            pass
    if os.name != 'nt':
        cc = shutil.which('cc') or shutil.which('gcc')
        if cc:
            return [cc], ''
    return None, None


class SourceContract(unittest.TestCase):
    def test_derived_sources_use_only_plain_ieee_paths(self):
        for path in sorted((SOURCE / 'musl').glob('*.h')):
            code = re.sub(r'/\*.*?\*/|//[^\n]*', '', path.read_text(), flags=re.S).replace('#include <stdint.h>', '')
            for banned in ('__builtin_fma', 'long double', '__FP_FAST_FMA', 'TOINT_INTRINSICS', 'LDBL_', '#include'):
                self.assertFalse(banned in code, f'{path.name}: {banned}')
        self.assertIn('LICENSE-musl.txt', (SOURCE / 'portable_math.h').read_text())
        self.assertTrue((SOURCE / 'musl/LICENSE-musl.txt').exists())

    def test_every_declared_function_is_defined(self):
        header = (SOURCE / 'portable_math.h').read_text()
        source = (SOURCE / 'portable_math.c').read_text()
        declared = set(re.findall(r'X4PM_API \w+ (x4pm_\w+)\(', header))
        defined = set(re.findall(r'X4PM_API \w+ (x4pm_\w+)\(', source))
        defined |= {'x4pm_' + n for n in re.findall(r'^PM_DOUBLE[12]\((\w+),', source, re.M)}
        defined |= {'x4pm_' + n + 'f' for n in re.findall(r'^PM_FLOAT[12]\((\w+),', source, re.M)}
        self.assertEqual(declared, defined)
        self.assertEqual(len(declared), 45)


class BuiltLibrary(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cc, cls.ext = toolchain()
        if not cls.cc:
            raise unittest.SkipTest('needs Zig 0.15.2 (X4LAN_ZIG) or a C compiler')
        cls.dir = tempfile.TemporaryDirectory()
        cls.exe = str(Path(cls.dir.name) / ('tpm' + cls.ext))
        subprocess.run(cls.cc + FLAGS + [str(SOURCE / 'portable_math.c'), str(SOURCE / 'test_portable_math.c'), '-o', cls.exe],
                       check=True)

    @classmethod
    def tearDownClass(cls):
        cls.dir.cleanup()

    def test_contract_checks_and_known_answers(self):
        out = subprocess.run([self.exe, 'check', '20000'], capture_output=True, text=True)
        self.assertEqual(out.returncode, 0, out.stdout + out.stderr)
        kat = re.search(r'kat ([0-9a-f]{16}) expected ([0-9a-f]{16})', out.stdout)
        self.assertEqual(kat.group(1), kat.group(2))
        self.assertIn('check failures=0', out.stdout)

    def test_results_do_not_depend_on_the_callers_mxcsr(self):
        out = subprocess.run([self.exe, 'hash', '20000'], capture_output=True, text=True)
        self.assertEqual(out.returncode, 0, out.stdout + out.stderr)
        totals = re.findall(r'^total mxcsr=(\w+) functions=44 points=(\d+) sha256=(\w+)$', out.stdout, re.M)
        self.assertEqual([t[0] for t in totals], ['1f80', 'ffc0', '1900'])
        self.assertEqual(len({t[2] for t in totals}), 1)

    def test_refuses_fast_math_and_unconfirmed_fma_builds(self):
        for flags in (['-ffast-math'], ['-mfma']):
            result = subprocess.run(self.cc + ['-std=c11', '-c', str(SOURCE / 'portable_math.c'), '-o',
                                               str(Path(self.dir.name) / 'refused.o')] + flags,
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0, flags)


if __name__ == '__main__':
    unittest.main()
