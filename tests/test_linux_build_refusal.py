"""Closed process attribution; adjacent START lines never bind a refusal."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/dev'))
from linux_load_observer_probe import AgentEvidence
from linux_portable_probe import ExclusiveAgentEvidence


def refused(pid=102, start=201, size=30944):
    return (f'X4MP_AGENT REFUSED schema=x4-build-refusal-v1 pid={pid} start_ticks={start} '
            f'reason=executable_size executable_bytes={size} pinned_bytes=60459280')


def verified(pid=100):
    return f'X4MP_AGENT VERIFIED build=23660954 version=9.00 (611726) sites=25 pid={pid}'


class BuildRefusal(unittest.TestCase):
    def test_before_after_and_interleaved_helpers_need_owned_claim_and_verified(self):
        for cls in (AgentEvidence, ExclusiveAgentEvidence):
            for order in ((refused(), verified()), (verified(), refused())):
                evidence = cls()
                evidence.observe('X4MP_AGENT START pid=999', None)
                for line in order: evidence.observe(line, None)
                self.assertFalse(evidence.verified(100))
                self.assertEqual(evidence.attributed_build_refusals[0]['attribution'], 'pending_owned_verification')
                evidence.bind_identity(100, 200)
                self.assertTrue(evidence.verified(100))
                evidence.observe('X4MP_AGENT START pid=100', 100, 200)
                evidence.observe(refused(103, 200, 236256), 100, 200)  # same clock tick is valid
                self.assertEqual([row['pid'] for row in evidence.attributed_build_refusals], [102, 103])
                self.assertTrue(all(row['attribution'] == 'attributed_foreign_small_executable'
                                    for row in evidence.attributed_build_refusals))

    def test_claimed_pid_is_fatal_even_with_different_start_and_pending_order(self):
        for start in (199, 200, 201):
            with self.assertRaisesRegex(ValueError, 'native_process_build_refused'):
                AgentEvidence().observe(refused(100, start), 100, 200)
            evidence = AgentEvidence(); evidence.observe(refused(100, start), None)
            with self.assertRaises(ValueError): evidence.bind_identity(100, 200)

    def test_duplicate_reused_stale_and_changed_claim_refuse(self):
        for row in (refused(), refused(102, 202), refused(102, 201, 236256)):
            evidence = AgentEvidence(); evidence.observe(refused(), 100, 200)
            with self.assertRaises(ValueError): evidence.observe(row, 100, 200)
        for pending in (False, True):
            evidence = AgentEvidence()
            if pending:
                evidence.observe(refused(start=199), None)
                with self.assertRaisesRegex(ValueError, 'stale'): evidence.bind_identity(100, 200)
            else:
                with self.assertRaisesRegex(ValueError, 'stale'): evidence.observe(refused(start=199), 100, 200)
        evidence = AgentEvidence(); evidence.bind_identity(100, 200)
        for pid, start in ((100, 201), (101, 200), (None, 200), (100, None), (True, 200)):
            with self.assertRaises(ValueError): evidence.bind_identity(pid, start)
        with self.assertRaises(ValueError): evidence.observe(refused(), 101)
        with self.assertRaises(ValueError): AgentEvidence().observe(refused(), 100)

    def test_foreign_verified_never_suppressed_by_helper_diagnostic(self):
        for cls in (AgentEvidence, ExclusiveAgentEvidence):
            for order in ((refused(), verified(102)), (verified(102), refused())):
                evidence = cls()
                with self.assertRaises(ValueError):
                    for line in order: evidence.observe(line, 100, 200)
            evidence = cls(); evidence.observe(verified(101), None)
            with self.assertRaises(ValueError): evidence.bind_identity(100, 200)
            evidence = cls(); evidence.observe(verified(), None)
            with self.assertRaises(ValueError): evidence.observe(verified(101), None)
            for claimed in (None, 100):
                evidence = cls(); evidence.observe(verified(), claimed)
                with self.assertRaisesRegex(ValueError, 'duplicate_native_verification'):
                    evidence.observe(verified(), claimed)

    def test_closed_schema_invalid_or_unavailable_attribution_is_fatal(self):
        rows = [refused(pid=0), refused(pid=1), refused(pid=2**32), refused(start=0), refused(start=2**64),
                refused(size=0), refused(size=60459280), refused(size=60459281), refused(start='unavailable'),
                refused().replace('pid=102', 'pid=unavailable'), refused(size='unavailable'),
                refused()+' extra=1', refused()+' pid=103', refused().replace('v1', 'v2'),
                refused().replace('pinned_bytes=60459280', 'pinned_bytes=30944')]
        rows += [refused().replace('executable_size', reason)
                 for reason in ('executable_stat', 'executable_hash', 'executable_read', 'live_signature', 'unknown')]
        for row in rows:
            with self.subTest(row=row), self.assertRaises(ValueError): AgentEvidence().observe(row, 100, 200)
        for size in (0, 60459280, 60459281):
            with self.assertRaises(ValueError):
                AgentEvidence().observe(f'X4MP_AGENT REFUSED executable size {size} (pinned 60459280)', None)

    def test_unattributed_and_runtime_faults_remain_fatal_after_verified(self):
        rows = ['X4MP_AGENT REFUSED executable size 30944 (pinned 60459280)',
                'X4MP_AGENT REFUSED reason=agent_slot_taken_during_arm',
                'X4MP_AGENT LOAD_OBSERVER REFUSED reason=foreign_load_thread',
                'X4MP_AGENT PORTABLE REFUSED reason=portable_failed',
                'X4MP_AGENT ADMISSION REFUSED reason=bad_contract']
        for row in rows:
            evidence = AgentEvidence(); evidence.observe(verified(), 100, 200)
            evidence.observe(refused(), 100, 200)
            with self.subTest(row=row), self.assertRaises(ValueError): evidence.observe(row, 100, 200)
        evidence = AgentEvidence(); evidence.observe(refused(), None)
        self.assertFalse(evidence.verified(100))
        with self.assertRaises(ValueError): evidence.observe('X4MP_AGENT LOAD_OBSERVER STATE phase=Installed', None)

    def test_diagnostics_are_bounded(self):
        evidence = AgentEvidence()
        for index in range(16): evidence.observe(refused(102+index, 201+index), 100, 200)
        with self.assertRaisesRegex(ValueError, 'excess'): evidence.observe(refused(200, 300), 100, 200)

    def test_exact_single_record_envelope_rejects_hidden_or_malformed_faults(self):
        rows = [verified(101)+' '+refused(), verified(101)+' '+verified(), verified()+' '+verified(),
                'garbage '+refused(), 'X4MP_AGENT START pid=999 log=/private/'+refused(),
                'X4MP_AGENT REFUSED', refused().replace('REFUSED ', 'REFUSED\t'), verified(101)+' extra=1',
                refused()+'\t', refused()+' ', '0.00 '+refused(), '-1.000 '+refused(),
                'X4MP_AGENT START pid=999 VERIFIED build=23660954',
                'X4MP_AGENT SCHED REFUSED reason=portable_failed restore=none',
                'X4MP_AGENT SCHED FAULT reason=foreign_slot restore=none',
                'X4MP_AGENT SCHED STALL progress=0',
                'X4MP_AGENT PORTABLE FINAL incomplete=terminal_exchange_or_original_return',
                'X4MP_AGENT LOAD_OBSERVER FINAL incomplete=native_continuation_not_acknowledged',
                'X4MP_AGENT ADMISSION FINAL incomplete=main_continuation_not_acknowledged']
        for cls in (AgentEvidence, ExclusiveAgentEvidence):
            for row in rows:
                evidence = cls(); evidence.observe(verified(), 100, 200)
                with self.subTest(row=row, cls=cls), self.assertRaises(ValueError): evidence.observe(row, 100, 200)
            evidence = cls(); evidence.observe('1793170426.520 '+verified(), 100, 200)
            evidence.observe('1793170426.693 '+refused(), 100, 200)
            self.assertTrue(evidence.verified(100))

    def test_disarm_acceptance_is_scoped_and_all_failure_witnesses_remain_fatal(self):
        good = 'X4MP_AGENT DISARMED reason=portable_ended sched=restored sched_reason=none restore=worker'
        evidence = AgentEvidence(); evidence.observe(verified(), 100, 200)
        with self.assertRaises(ValueError): evidence.observe(good, 100, 200)
        evidence.allow_clean_disarm('portable_ended'); evidence.observe(good, 100, 200)
        bad = [good.replace('portable_ended', reason) for reason in ('callback_panicked', 'reentrant_callback',
            'vbi_table_pointer_changed', 'agent_slot_no_longer_owned', 'game_client_slot_changed',
            'timer_values_invalid', 'load_observer_refused', 'disarm_file_present', 'none')]
        bad += [good.replace('sched=restored', 'sched=faulted'), good.replace('sched_reason=none', 'sched_reason=foreign'),
                good.replace('restore=worker', 'restore=worker_unlocked'), good+' extra=1',
                'X4MP_AGENT SCHED RESTORED restore=worker_unlocked']
        for line in bad:
            with self.subTest(line=line), self.assertRaises(ValueError): evidence.observe(line, 100, 200)
        owned = AgentEvidence(); owned.observe(verified(), 100, 200)
        line = 'X4MP_AGENT DISARMED reason=disarm_file_present sched=off sched_reason=none restore=none'
        with self.assertRaises(ValueError): owned.observe(line, 100, 200)
        owned.allow_clean_disarm('disarm_file_present'); owned.observe(line, 100, 200)


if __name__ == '__main__': unittest.main()
