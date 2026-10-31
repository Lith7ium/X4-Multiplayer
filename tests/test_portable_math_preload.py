"""libx4mp_portable_math.so: export list and, on a Linux host, the interposition test.

The runtime part builds the preload and the test programs with the host compiler and
runs tools/dev/build_portable_math_preload.py's checks (interposition, refusal of the
engine patches outside X4, the exact-reciprocal recipes); on Windows it is skipped (use
the tool with --wsl; --elf adds the patch tests over the mapped Linux X4). The export list is checked everywhere against the inventory's
Linux patch targets.
"""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'src/native/portable_math/preload/x4mp_portable_math.c'
SERVICE = {'x4mp_portable_math_info', 'x4mp_portable_decimal_install', 'x4mp_exact_reciprocal_install',
           'x4mp_exact_reciprocal_region', 'x4mp_portable_math_phdrs_are_x4'}


class PreloadExports(unittest.TestCase):
    def test_exports_are_exactly_the_transcendental_imports_of_the_linux_build(self):
        exported = set(re.findall(r'^EXPORT \w+ (\w+)\(', SOURCE.read_text(), re.M)) - SERVICE
        targets = json.loads((ROOT / 'reports/LOCKSTEP-CROSSPLATFORM-PATCH-TARGETS-20261029.json').read_text())
        interpose = {e['symbol'] for e in targets['math']['linux'] if e['action'].startswith('interpose')}
        keep = {e['symbol'] for e in targets['math']['linux'] if e['action'].startswith('keep')}
        self.assertEqual(exported, interpose)
        self.assertEqual(len(exported), 28)
        self.assertFalse(exported & keep)

    def test_every_export_forwards_to_its_portable_function(self):
        for name, target in re.findall(r'^EXPORT \w+ (\w+)\([^)]*\) \{ (?:return )?(x4pm_\w+)\(', SOURCE.read_text(), re.M):
            self.assertEqual(target, 'x4pm_' + name)


@unittest.skipUnless(sys.platform.startswith('linux') and shutil.which('cc'), 'needs a Linux host with cc')
class PreloadInterposition(unittest.TestCase):
    def test_interposes_every_symbol_for_pie_nonpie_and_bind_now(self):
        result = subprocess.run([sys.executable, str(ROOT / 'tools/dev/build_portable_math_preload.py'), '--test', '--cc', 'cc'],
                                capture_output=True, text=True, env=dict(os.environ, X4LAN_ZIG=''))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('every symbol interposed', result.stdout)
        self.assertIn('refused before any read (exit 97)', result.stdout)
        self.assertIn('exact bits, lanes, red zone, flags and RSP kept', result.stdout)


if __name__ == '__main__':
    unittest.main()
