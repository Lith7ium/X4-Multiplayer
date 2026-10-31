from decimal import localcontext
from contextlib import ExitStack
from fractions import Fraction
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/dev'))
import check_portable_rotation as check


class RotationOracleTests(unittest.TestCase):
    def test_independent_exact_rounding_and_signed_zero(self):
        self.assertEqual(check.rational_round(Fraction(1, 1 << 150)), 0)
        self.assertEqual(check.rational_round(Fraction(3, 1 << 150)), 2)
        self.assertEqual(check.rational_round(-Fraction(1, 1 << 150)), 0x80000000)
        self.assertEqual(check.oracle_operation('add', 0x80000000, 0x80000000), 0x80000000)
        self.assertEqual(check.oracle_operation('add', 0x80000000, 0), 0)
        self.assertEqual(check.oracle_operation('mul', 0x80000000, 0x3f800000), 0x80000000)
        self.assertEqual(check.oracle_operation('add', 0x3f800000, 0x33800000), 0x3f800000)
        self.assertEqual(check.oracle_operation('add', 0x3f800001, 0x33800000), 0x3f800002)

    def test_known_combined_grouping_counterexample(self):
        inputs = (0x3f490fdb, 0x3f490fdb, 0x7f7fffff)
        with localcontext() as context:
            context.prec = 180
            expected = check.reference_basis(*inputs, check.decimal_pi())
        self.assertEqual(expected[0][0], 0x3eaf39e9)
        self.assertEqual(check.candidate.basis(*inputs), expected)

    def test_oracle_has_no_candidate_arithmetic_dependency(self):
        with ExitStack() as stack:
            for name in ('sincos', 'add', 'multiply', 'negate', 'pack', 'unpack', 'round_ratio'):
                stack.enter_context(patch.object(check.candidate, name, side_effect=AssertionError('candidate dependency')))
            with localcontext() as context:
                context.prec = 180
                result = check.reference_basis(0x3f490fdb, 0x3f490fdb, 0x7f7fffff, check.decimal_pi())
        self.assertEqual(result[0][0], 0x3eaf39e9)

    def test_reassociated_multiplication_mutant_is_rejected(self):
        original = check.candidate.basis
        def mutant(pitch, yaw, roll):
            result = [list(row) for row in original(pitch, yaw, roll)]
            sp, _ = check.candidate.sincos(pitch)
            sy, cy = check.candidate.sincos(yaw)
            sr, cr = check.candidate.sincos(roll)
            # The review's exact mutation: sr*(sp*sy), instead of (sr*sp)*sy.
            result[0][0] = check.candidate.add(check.candidate.multiply(cr, cy),
                check.candidate.multiply(sr, check.candidate.multiply(sp, sy)))
            return tuple(tuple(row) for row in result)
        self.assertEqual(mutant(0x3f490fdb, 0x3f490fdb, 0x7f7fffff)[0][0], 0x3eaf39ea)
        with patch.object(check.candidate, 'basis', mutant):
            report = check.run(0)
        self.assertEqual(report['sincos_mismatch_count'], 0)
        self.assertEqual(report['basis_mismatch_count'], 2)
        self.assertEqual(report['mismatch_count'], 2)
        self.assertIn({'domain': 'basis', 'inputs': ['3f490fdb','3f490fdb','7f7fffff'],
                       'vector': 0, 'coefficient': 0, 'actual': '3eaf39ea', 'expected': '3eaf39e9'},
                      report['mismatches'])


if __name__ == '__main__':
    unittest.main()
