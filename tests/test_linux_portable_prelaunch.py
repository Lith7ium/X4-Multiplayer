"""Independent Linux prelaunch predicates; no native process or gate activation."""
import copy
import unittest

import linux_portable_probe as probe
from test_linux_portable_probe import math_capability, math_terminal, load_terminal, terminal


def waiting_native():
    row = terminal()
    row.update(mode=probe.EULER_MODE, result='main_ready', tick='0', calls='0', returns='0', receipt_tick='0',
               terminal_ack='0', closed='0', restored='0', wait_kind='1')
    return row


def regions():
    return {name: dict(matches_original=True, sha256=pin) for name, (_, _, pin) in probe.ROTATION_REGIONS.items()}


class PrelaunchPredicate(unittest.TestCase):
    def test_prelaunch_is_separate_from_terminal_and_requires_full_native_boundary(self):
        native = waiting_native(); load = load_terminal(); load['restored'] = '0'
        evidence = probe.RotationEvidence(math_capability()); evidence.observe(math_terminal())
        self.assertTrue(evidence.prelaunch(native, load, 0x30000000, 0x50000000, regions()))
        self.assertFalse(probe.terminal_status(native, 12, probe.EULER_MODE))
        self.assertFalse(evidence.terminal(native, load, 0x30000000, 0x50000000))
        completed = terminal(); completed.update(mode=probe.EULER_MODE, result='passed_subset_euler_import')
        self.assertFalse(evidence.prelaunch(completed, load, 0x30000000, 0x50000000, regions()))
        # A complete terminal still needs actual restored Load, exactly as before.
        self.assertFalse(evidence.terminal(completed, load, 0x30000000, 0x50000000))
        load['restored'] = '1'
        self.assertTrue(evidence.terminal(completed, load, 0x30000000, 0x50000000))
        for key in native:
            changed = dict(native); del changed[key]
            with self.subTest(missing=key): self.assertFalse(probe.prelaunch_status(changed))
        for key, value in dict(result='waiting_load', first_boundary_ack='0', tick='1', calls='1', returns='1',
                receipt_tick='1', installed='0', failed='1', wait_kind='0', subset_changes='1', clock_changes='1',
                terminal_ack='1', closed='1', restored='1', restore_error='foreign', loaded_frame='0',
                loaded_module='0', subset_reads='0', loaded_padding='256', load_clocks='0',
                load_selectors='0,0', index_ns='5000000001', max_wait_ns='600000000001').items():
            with self.subTest(field=key): self.assertFalse(probe.prelaunch_status(dict(native, **{key:value})))

    def test_prelaunch_keeps_every_math_counter_lifetime_and_ownership_join(self):
        native = waiting_native(); load = load_terminal(); load['restored'] = '0'
        row = math_terminal()
        for field in row:
            bad = dict(row)
            if field in ('phase', 'failure', 'restore_error'): bad[field] = 'Failed' if field == 'phase' else 'fault'
            elif field in ('mode', 'descriptor', 'snapshot', 'contract', 'inputs'): bad[field] += '0'
            elif field in ('table', 'client', 'module', 'manager'): bad[field] = format(int(bad[field],16)+1,'x')
            else: bad[field] = str(int(bad[field])+1)
            with self.subTest(field=field):
                evidence = probe.RotationEvidence(math_capability())
                try: evidence.observe(bad)
                except ValueError: continue
                self.assertFalse(evidence.prelaunch(native, load, 0x30000000, 0x50000000, regions()))
        evidence = probe.RotationEvidence(math_capability()); evidence.observe(row)
        for key in regions():
            bad = regions(); bad[key]['matches_original'] = False
            self.assertFalse(evidence.prelaunch(native, load, 0x30000000, 0x50000000, bad))
            bad = regions(); bad[key]['sha256'] = '0'*64
            self.assertFalse(evidence.prelaunch(native, load, 0x30000000, 0x50000000, bad))
        self.assertFalse(evidence.prelaunch(native, load, 0x30000001, 0x50000000, regions()))
        self.assertFalse(evidence.prelaunch(native, load, 0x30000000, 0x50000001, regions()))
        for key in ('calls','returns','raw_calls','raw_returns'):
            bad = copy.deepcopy(load); bad[key][0] = 2
            self.assertFalse(evidence.prelaunch(native, bad, 0x30000000, 0x50000000, regions()))


if __name__ == '__main__': unittest.main()
