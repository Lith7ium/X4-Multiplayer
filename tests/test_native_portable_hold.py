"""Source boundaries complement the actual-memory/UDP mutation harness."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / 'src/native/engine'


class PortableLocalHold(unittest.TestCase):
    def test_physical_guards_stay_outside_canonical_schema(self):
        canonical = (ROOT / 'lockstep_portable_state.h').read_text()
        wire = (ROOT / 'lockstep_portable_wire.h').read_text()
        for field in ('unpaused', 'portable_native_hold', 'world_bank_phase'):
            self.assertNotIn(field, canonical)
            self.assertNotIn(field, wire)
        source = (ROOT / 'lockstep_portable_windows.h').read_text()
        self.assertIn('portable_native_holds[tick]=held;', source)
        self.assertEqual(source.count('portable_native_holds[tick]='), 1)

    def test_full_reread_and_final_guard_remain_before_acceptance(self):
        source = (ROOT / 'lockstep_portable_transport.h').read_text()
        held = source[source.index('static int portable_transport_held('):source.index('static int portable_transport_accept(')]
        self.assertIn('portable_native_two_pass(', held)
        self.assertIn('portable_native_hold_same(&held,&portable_native_holds[tick])', held)
        accept = source[source.index('static int portable_transport_accept('):source.index('static void portable_transport_terminal(')]
        sample = accept.index('int held=portable_transport_held(tick,frame,loading);')
        lock = accept.index('AcquireSRWLockExclusive', sample)
        guard = accept.index('portable_native_hold_current(&portable_native_holds[tick],frame,loading)', lock)
        self.assertLess(guard, accept.index('++stats.issued'))
        self.assertLess(guard, accept.index('portable_wire_draining=1'))
        self.assertLess(accept.index('ReleaseSRWLockExclusive'), sample)

    def test_reader_writer_pins_only_reconstruct_owned_dt_word(self):
        source = (ROOT / 'lockstep_portable_windows.h').read_text()
        pins = source[source.index('static int portable_native_clock_pins('):source.index('static int portable_native_hold_read(')]
        self.assertIn('hash_ok(image+0x4b5e50,57,', pins)
        self.assertIn('unsigned char tick[2127]', pins)
        self.assertLess(pins.index('observed!=0x9066'), pins.index('memcpy(tick+0xfa3302-TICK_RVA,&original,2)'))
        self.assertEqual(pins.count('portable_native_clock_owned()'), 2)
        self.assertNotIn('VirtualProtect', source)

    def test_held_samples_repeat_existing_boundary_guards(self):
        source = (ROOT / 'lockstep_portable_windows.h').read_text()
        read = source[source.index('static int portable_native_hold_read('):source.index('static int portable_native_hold_same(')]
        for guard in ('caller_frame && !loading', 'portable_native_clock_owned()', 'world_identity_installed',
                      'imported==world_identity_patched_word', 'image+TABLE_RVA', 'hold->table+0x130',
                      '!hold->loading', 'image+SPEED_RVA', '0x3ff0000000000000'):
            self.assertIn(guard, read)
        same = source[source.index('static int portable_native_hold_same('):source.index('static int portable_native_hold_current(')]
        for guard in ('a->frame==b->frame', 'a->table==b->table', 'a->loading==b->loading', 'a->speed==b->speed'):
            self.assertIn(guard, same)
        transport = (ROOT / 'lockstep_portable_transport.h').read_text()
        fail = transport[transport.index('static void portable_transport_fail('):transport.index('static int portable_transport_send(')]
        self.assertLess(fail.index('AcquireSRWLockExclusive'), fail.index('stats.active && !portable_wire_finish_started'))
        self.assertLess(fail.index('ReleaseSRWLockExclusive'), fail.index('finish(76)'))


if __name__ == '__main__':
    unittest.main()
