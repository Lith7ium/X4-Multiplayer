import importlib.util
from pathlib import Path
import unittest

PATH = Path(__file__).resolve().parents[1] / 'tools/dev/portable_rotation_reference.py'
spec = importlib.util.spec_from_file_location('rotation_reference', PATH)
r = importlib.util.module_from_spec(spec)
spec.loader.exec_module(r)


class RotationReferenceTests(unittest.TestCase):
    def test_proposed_common_decimal_rule_resolves_actual_parser_difference(self):
        self.assertEqual(r.decimal_to_binary32('0.8875748217105865478515625'), 0x3f63381a)
        self.assertEqual(r.degrees_to_radians('0.8875748217105865478515625'), 0x3c7dce65)
        self.assertEqual(r.degrees_to_radians('90'), 0x3fc90fdb)
        self.assertEqual(r.degrees_to_radians('-90'), 0xbfc90fdb)
        self.assertEqual(r.degrees_to_radians(None), 0)
        self.assertEqual(r.degrees_to_radians(''), 0)
        self.assertEqual(r.text_basis(yaw='90'), r.basis(0, 0x3fc90fdb, 0))
        self.assertEqual(r.decimal_to_binary32('90.0'), 0x42b40000)
        self.assertEqual(r.decimal_to_binary32('-.0'), 0x80000000)
        self.assertEqual(r.decimal_to_binary32('-0.'+'0'*200+'1'), 0x80000000)
        self.assertEqual(r.decimal_to_binary32('340282346638528859811704183484516925440'), 0x7f7fffff)
        for text in ('', 'nan', 'inf', '1.5deg', '1,5', '1 2', '1e3', '1e99999', '9'*100, '+90', ' 90', '90;', '\u0661', '0'*257):
            with self.assertRaises(ValueError, msg=text):
                r.decimal_to_binary32(text)

    def test_real_mismatch_input_bits(self):
        self.assertEqual(r.sincos(0), (0, 0x3f800000))
        self.assertEqual(r.sincos(0x3fc90fdb), (0x3f800000, 0xb33bbd2e))
        self.assertEqual(r.sincos(0xbfc90fdb), (0xbf800000, 0xb33bbd2e))
        positive = r.basis(0, 0x3fc90fdb, 0)
        negative = r.basis(0, 0xbfc90fdb, 0)
        self.assertEqual(positive, ((0xb33bbd2e, 0x80000000, 0xbf800000),
                                    (0x80000000, 0x3f800000, 0),
                                    (0x3f800000, 0, 0xb33bbd2e)))
        self.assertEqual(negative, ((0xb33bbd2e, 0x80000000, 0x3f800000),
                                    (0, 0x3f800000, 0),
                                    (0xbf800000, 0, 0xb33bbd2e)))

    def test_signed_zero_and_gradual_underflow(self):
        self.assertEqual(r.sincos(0x80000000), (0x80000000, 0x3f800000))
        for bits in (1, 0x80000001, 0x7fffff, 0x807fffff, 0x800000):
            self.assertEqual(r.sincos(bits), (bits, 0x3f800000))
        self.assertEqual(r.multiply(1, 0x3f000000), 0)
        self.assertEqual(r.multiply(3, 0x3f000000), 2)
        self.assertEqual(r.multiply(0x80000001, 0x3f000000), 0x80000000)
        self.assertEqual(r.add(0x80000000, 0x80000000), 0x80000000)
        self.assertEqual(r.add(0x3f800000, 0xbf800000), 0)

    def test_round_ties_even_and_carry(self):
        self.assertEqual(r.round_ratio(5, 2), 2)
        self.assertEqual(r.round_ratio(7, 2), 4)
        self.assertEqual(r.round_ratio(-7, 2), -4)
        self.assertEqual(r.add(0x3f800000, 0x33800000), 0x3f800000)
        self.assertEqual(r.add(0x3f800001, 0x33800000), 0x3f800002)
        self.assertEqual(r.pack((1 << 25) - 1, -25), 0x3f800000)

    def test_full_finite_range_and_refusal(self):
        for bits in (0x7f7fffff, 0xff7fffff, 0x00800000, 0x80800000):
            sine, cosine = r.sincos(bits)
            self.assertLessEqual(sine & 0x7fffffff, 0x3f800000)
            self.assertLessEqual(cosine & 0x7fffffff, 0x3f800000)
        for bits in (0x7f800000, 0xff800000, 0x7fc00000, -1, 1 << 32, True):
            with self.assertRaises(ValueError):
                r.sincos(bits)
        with self.assertRaises(ValueError):
            r.multiply(0x7f7fffff, 0x40000000)

    def test_sine_odd_cosine_even(self):
        for bits in (1, 0x3f000000, 0x3fc90fdb, 0x40490fdb, 0x53800001, 0x7f7fffff):
            s, c = r.sincos(bits)
            ns, nc = r.sincos(bits ^ 0x80000000)
            self.assertEqual(ns, s ^ 0x80000000)
            self.assertEqual(nc, c)


if __name__ == '__main__':
    unittest.main()
