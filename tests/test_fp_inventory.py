"""Cross-platform FP inventory helpers. Synthetic inputs only; no X4 executable is read."""
from pathlib import Path
import collections
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/dev'))
import fp_inventory as inv  # noqa: E402
import fp_libm_compare as cmp_  # noqa: E402


def const_reader(table):
    """read_const stub: rip operands are looked up by instruction address."""
    return lambda a, s, op: table.get(a)


class InstructionClasses(unittest.TestCase):
    def test_classes(self):
        self.assertEqual(inv.insn_class('vfmadd231ss'), 'fma')
        self.assertEqual(inv.insn_class('rsqrtss'), 'approx')
        self.assertEqual(inv.insn_class('vrcpps'), 'approx')
        self.assertEqual(inv.insn_class('ldmxcsr'), 'fpenv')
        self.assertEqual(inv.insn_class('fprem'), 'x87')
        self.assertEqual(inv.insn_class('roundss'), 'sse41')
        self.assertEqual(inv.insn_class('divsd'), 'arith')
        self.assertEqual(inv.insn_class('cvtss2sd'), 'cvt')
        self.assertIsNone(inv.insn_class('mov'))

    def test_profile_and_tags(self):
        win = inv.profile({'mulss': 3, 'addss': 1, 'rsqrtss': 1, 'rcpss': 1})
        lin = inv.profile({'mulss': 2, 'divss': 1, 'addsd': 1, 'cvtss2sd': 1, 'sqrtss': 1})
        self.assertEqual(win['single'], 6)
        self.assertEqual(lin['double'], 1)
        tags = inv.profile_tags(win, lin, [], ['atan2f'])
        self.assertTrue({'libm_call', 'fewer_div_windows', 'approx_diff', 'sqrt_diff', 'precision_diff',
                         'arith_count_diff'} <= tags)
        same = inv.profile({'mulss': 2})
        self.assertEqual(inv.profile_tags(same, same, [], []), {'same_profile'})


class ReciprocalSubstitution(unittest.TestCase):
    def test_memory_and_register_divisors(self):
        insns = [(0x10, 8, 'divss', 'xmm0, dword ptr [rip + 0x100]'),
                 (0x18, 8, 'movss', 'xmm4, dword ptr [rip + 0x200]'),
                 (0x20, 4, 'divss', 'xmm1, xmm4'),
                 (0x24, 4, 'mulss', 'xmm4, xmm2'),
                 (0x28, 4, 'divss', 'xmm3, xmm4')]
        found = inv.divisor_constants(insns, const_reader({0x10: ('s', 3.0), 0x18: ('s', 100.0)}))
        self.assertEqual(found, [(0x10, ('s', 3.0)), (0x20, ('s', 100.0))])  # xmm4 overwritten before 0x28

    def test_matches(self):
        third = inv.f32(1 / 3.0)
        rows = inv.reciprocal_matches([(1, ('s', 3.0)), (2, ('s', 0.5)), (3, ('s', 7.0)), (4, ('d', 10.0))],
                                      {('s', 7.0)}, {('s', third), ('d', 0.1)})
        self.assertEqual([r[3] for r in rows], ['substituted', 'exact_pow2', 'windows_divides', 'substituted'])

    def test_unmatched(self):
        self.assertEqual(inv.reciprocal_matches([(1, ('s', 3.0))], set(), set())[0][3], 'unmatched')


class Domains(unittest.TestCase):
    def test_names(self):
        self.assertEqual(inv.name_domain('JPH::SphereShape::vfunc30'), 'physics')
        self.assertEqual(inv.name_domain('U::Path::SplineFlightControlModel::vfunc3'), 'movement')
        self.assertEqual(inv.name_domain('U::Weapon::vfunc998'), 'combat')
        self.assertEqual(inv.name_domain('U::Zone::vfunc941'), 'world')
        self.assertEqual(inv.name_domain('UI::Widget::vfunc1'), 'nonsim')
        self.assertIsNone(inv.name_domain('std::vector<int>::push_back'))

    def test_propagate_majority(self):
        fn = lambda *calls: dict(calls=[(0, 'call', t) for t in calls])
        functions = {1: fn(10), 2: fn(10), 3: fn(10, 11), 10: fn(12), 11: fn(), 12: fn()}
        domains, source = inv.propagate(functions, {1: 'combat', 2: 'combat', 3: 'nonsim'})
        self.assertEqual(domains[10], 'combat')   # 2 of 3 classified callers
        self.assertEqual(domains[12], 'combat')   # second round, through 10
        self.assertEqual(domains[11], 'nonsim')
        self.assertEqual(source[12], 'callers2')


class SortShape(unittest.TestCase):
    def test_introsort(self):
        body = [(0x100, 3, 'mov', 'rax, rsi'), (0x103, 3, 'sub', 'rax, rdi'), (0x106, 6, 'cmp', 'rax, 0x100'),
                (0x10c, 2, 'jle', '0x200'), (0x10e, 5, 'call', '0x100')]
        self.assertTrue(inv.introsort_like(body, 0x100))
        self.assertFalse(inv.introsort_like(body[:-1], 0x100))  # not self-recursive
        self.assertFalse(inv.introsort_like([body[0], (0x103, 6, 'cmp', 'rax, 0x100'), body[-1]], 0x100))


class Ulp(unittest.TestCase):
    def test_distance(self):
        f = lambda x: struct.unpack('<I', struct.pack('<f', x))[0]
        self.assertEqual(cmp_.ulp_distance(f(1.0), f(1.0), False), 0)
        self.assertEqual(cmp_.ulp_distance(f(1.0), f(1.0) + 1, False), 1)
        self.assertEqual(cmp_.ulp_distance(f(0.0), f(-0.0), False), 0)
        self.assertEqual(cmp_.ulp_distance(f(-1.0), f(-1.0) + 2, False), 2)
        smallest = 1
        self.assertEqual(cmp_.ulp_distance(smallest, smallest | 0x80000000, False), 2)
        d = lambda x: struct.unpack('<Q', struct.pack('<d', x))[0]
        self.assertEqual(cmp_.ulp_distance(d(2.0), d(2.0) - 3, True), 3)

    def test_wsl_path(self):
        if sys.platform == 'win32':
            self.assertEqual(cmp_.wsl_path('C:\\x\\y'), '/mnt/c/x/y')


class Tables(unittest.TestCase):
    def test_crt_table_unique_names(self):
        kinds = collections.Counter(k for _, k in inv.WINDOWS_CRT_MATH.values() if k in ('fma3', 'plain', 'exact', 'thunk'))
        self.assertEqual(kinds['fma3'], 14)
        self.assertEqual(sum(1 for n, k in inv.WINDOWS_CRT_MATH.values() if k.startswith('packed')), 3)
        for name, kind in inv.WINDOWS_CRT_MATH.values():
            self.assertTrue(name.rstrip('f') in ' '.join(inv.LINUX_MATH) or name in ('exp2', 'log2'), name)


if __name__ == '__main__':
    unittest.main()
