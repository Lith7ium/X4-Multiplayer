"""Diagnostic interpretation must retain raw failures and native idle inputs."""
from copy import deepcopy
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/dev'))
from analyze_native_requests import (compare, idle_empty_boundaries, indexed,
                                     IDLE_MODEL, VELOCITY_SOLVER, UNIT_GAINS)
from native_request_evidence import evidence
from test_native_gate_request import fixture


def rows():
    errors = []
    trace, _ = evidence(fixture(), 1, 2, errors)
    assert not errors
    result = indexed(trace)
    for r in result[0].values():
        r.update(model=IDLE_MODEL.hex(), solver=VELOCITY_SOLVER.hex(), gains=UNIT_GAINS.hex())
        if r['ordinal'] == 3:
            r['value'] = struct.pack('>d', 0.01 * (1 if r['actor'] == 1 else -1)).hex()
    for r in result[1].values():
        r['model'] = IDLE_MODEL.hex()
    return result


class RequestAnalysisTests(unittest.TestCase):
    def test_idle_empty_still_records_nonzero_native_requests(self):
        observed = idle_empty_boundaries(rows())
        self.assertEqual(len(observed), 2)
        self.assertEqual(observed[0]['channels']['angular2']['minimum'], 0.01)
        self.assertEqual(observed[1]['channels']['angular2']['maximum'], -0.01)
        self.assertEqual(observed[0]['channels']['linear2']['nonzero'], 0)
        self.assertEqual(observed[0]['tick_ranges'], [[2, 2]])
        self.assertEqual(observed[0]['all_six_requests_neutral_scopes'], 0)

    def test_both_boundaries_all_requests_and_current_modes_are_required(self):
        original = rows()
        scope_key = min(original[1])
        owner = scope_key[1]
        for target, key, field, value in (
            (1, (*scope_key[:3], 1), 'main', 1),
            (1, scope_key, 'solver_same', 0),
            (1, scope_key, 'other_requests', 1),
            (1, scope_key, 'model_rva', '00000000'),
            (0, (*scope_key[:3], 6), 'solver', '00' * 20),
            (0, (*scope_key[:3], 6), 'gains', struct.pack('<6d', *([0.5] * 6)).hex()),
        ):
            changed = deepcopy(original)
            changed[target][key][field] = value
            self.assertNotIn(owner, [r['owner'] for r in idle_empty_boundaries(changed)])
        del original[0][(*scope_key[:3], 6)]
        self.assertNotIn(owner, [r['owner'] for r in idle_empty_boundaries(original)])

    def test_raw_unknown_bytes_and_runtime_references_remain_differences(self):
        original = rows()
        altered = deepcopy(original)
        key = min(altered[1])
        for field, offset in [('banks', 0xb0), ('params', 0xe9)]:
            value = bytearray.fromhex(altered[1][key][field])
            value[offset] ^= 1
            altered[1][key][field] = value.hex()
        result = compare(original, altered)
        self.assertFalse(result['raw_equal'])
        self.assertEqual(result['byte_offsets'], {'scope.banks': {'0xb0': 1}, 'scope.params': {'0xe9': 1}})
        self.assertEqual(idle_empty_boundaries(original), idle_empty_boundaries(altered))

    def test_changed_native_input_and_missing_record_cannot_compare_equal(self):
        original = rows()
        altered = deepcopy(original)
        key = min(altered[0])
        altered[0][key]['value'] = struct.pack('>d', 0.02).hex()
        self.assertEqual(compare(original, altered)['differing_fields'], {'request.value': 1})
        del altered[0][key]
        self.assertEqual(compare(original, altered)['differing_fields'], {'request.record': 1})


if __name__ == '__main__':
    unittest.main()
