"""Consistency of the generated exact-reciprocal site tables (no game files needed).

tools/dev/gen_exact_reciprocal.py --windows/--linux ... --check compares a header with a
fresh scan of the executable; this test checks what can be checked without it: the
Windows gate's table and the Linux preload's table."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
HEADERS = {
    # name, header, prefix of the defines, stack drop of the recipe
    'windows': (ROOT / 'src/native/engine/lockstep_exact_reciprocal_sites.h', 'XRCP', 16),
    'linux': (ROOT / 'src/native/portable_math/preload/exact_reciprocal_linux_sites.h', 'XRCP_LINUX', 144),
}
ROW = re.compile(r'^    \{0x([0-9a-f]+)u,(\d+),(\d+),0x([0-9a-f]{16})ULL,\{((?:\{\d+,\d+,\d+,\d+\},?)+)\}\},$', re.M)


class ExactReciprocalSites(unittest.TestCase):
    def tables(self):
        for name, (header, prefix, drop) in HEADERS.items():
            text = header.read_text()
            with self.subTest(table=name):
                yield name, text, prefix, drop, ROW.findall(text)

    def test_counts_match_the_declared_totals(self):
        for name, text, prefix, _, rows in self.tables():
            windows = int(re.search(rf'#define {prefix}_WINDOWS (\d+)u', text).group(1))
            rewritten = int(re.search(rf'#define {prefix}_REWRITTEN (\d+)u', text).group(1))
            self.assertEqual(len(rows), windows)
            kinds = [int(k) for row in rows for _, _, k, _ in re.findall(r'\{(\d+),(\d+),(\d+),(\d+)\}', row[4])]
            self.assertEqual(sum(1 for k in kinds if k), rewritten)
            self.assertGreater(rewritten, 1180)
            # every rcp(rsqrt) chain has both halves, and they are most of the pairs
            # (4 closes an in-place chain, 5 one whose result went through a register copy, 6 one into another register)
            self.assertEqual(kinds.count(3), kinds.count(4) + kinds.count(5) + kinds.count(6))
            self.assertGreater(kinds.count(3), 450)
            open_chains = 0
            for k in kinds:  # in address order a chain end never comes before its sqrtss
                open_chains += (k == 3) - (k in (4, 5, 6))
                self.assertGreaterEqual(open_chains, 0)

    def test_windows_are_sorted_disjoint_and_made_of_whole_instructions(self):
        for name, text, prefix, drop, rows in self.tables():
            end = 0
            lea = 5 if drop < 128 else 8
            for rva, length, count, _, insns in rows:
                rva, length, count = int(rva, 16), int(length), int(count)
                self.assertGreaterEqual(rva, end)
                self.assertTrue(5 <= length <= 24)
                parts = [tuple(map(int, p)) for p in re.findall(r'\{(\d+),(\d+),(\d+),(\d+)\}', insns)]
                self.assertEqual(len(parts), count)
                self.assertTrue(1 <= count <= 4)
                offset, trampoline = 0, 5
                for at, size, kind, rip in parts:
                    self.assertEqual(at, offset)
                    self.assertIn(kind, (0, 1, 2, 3, 4, 5, 6))
                    if kind:
                        self.assertIn(size, (4, 5))
                        self.assertEqual(rip, 0)
                        if kind in (1, 2, 5, 6):   # 5/6: the rcpss recipe on the chain's rsqrtss register (6 after a movss)
                            trampoline += 2 * lea + 6 + 8 + 8 + 6 + (12 if kind == 2 else 0) + (size if kind == 6 else 0)
                        elif kind == 3:
                            trampoline += size  # sqrtss d, s; a chain end (4) emits nothing
                    else:
                        if rip:
                            self.assertLessEqual(rip + 4, size)
                        trampoline += size
                    offset += size
                self.assertEqual(offset, length)
                self.assertTrue(parts[0][2], 'a window starts at a rewritten instruction')
                self.assertLess(length - parts[-1][1], 5, 'no instruction beyond the first five bytes is needed')
                self.assertLessEqual(trampoline, 128, f'{rva:#x}: trampoline may exceed its 128-byte slot')
                end = rva + length

    def test_linux_windows_avoid_the_agent_pins_and_the_decimal_readers(self):
        pinned = ROOT / 'src/rust/x4mp-agent/src/pinned.rs'
        busy = [(int(va, 16), int(va, 16) + int(n, 16)) for va, n in
                re.findall(r'va: (0x[0-9a-f]+), len: (0x[0-9a-f]+)', pinned.read_text())]
        self.assertGreater(len(busy), 20)
        busy += [(0x1a3b1a0, 0x1a3b1a0 + 0x39f), (0x1a3ac40, 0x1a3ac40 + 0x3bf)]
        rows = ROW.findall(HEADERS['linux'][0].read_text())
        for rva, length, *_ in rows:
            lo, hi = int(rva, 16), int(rva, 16) + int(length)
            for b, e in busy:
                self.assertFalse(lo < e and b < hi, f'{lo:#x} overlaps {b:#x}-{e:#x}')

if __name__ == '__main__':
    unittest.main()
