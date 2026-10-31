"""Created identities: parsing, census reporting and probe arming (synthetic logs only)."""
from contextlib import redirect_stdout
import hashlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools/dev'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import census_world
import compare_native_world
from native_world_evidence import DOMAINS, frames, roots, identity_origin, created_tick, is_created, CREATED_BIT
import test_native_gate_probe as probe

SAVED = 0x77
ZONE = CREATED_BIT | (14 << 40) | 0x1111111111
SHIP_A = CREATED_BIT | (14 << 40) | 0x2222222222
SHIP_B = CREATED_BIT | (14 << 40) | 0x3333333333
CONFIG = "X4LAN_CONFIG.native_gate_world_hash_schema = 'WORLD_IMPORTED_1'\nX4LAN_CONFIG.native_gate_world_hash_stride = 60\n"


def record(identity, parent=0, state='a', present=True):
    kind, flags = (0x4000, 1) if present else (0, 0)
    timing = (b'\x02\0\0\0' if present else b'')+struct.pack('<IIQ', kind, flags, parent)
    hashes = tuple(hashlib.sha256(f'{identity}/{state}/{d}'.encode()).digest() for d in DOMAINS[:3])+(hashlib.sha256(timing).digest(),)
    return {'type': kind, 'flags': flags, 'parent': parent, 'hashes': hashes, 'timing': timing[:-16]}


def object_line(tick, identity, row):
    hashes = ' '.join(f'{name}={value.hex()}' for name, value in zip(DOMAINS, row['hashes']))
    return (f'[X4LAN] NATIVE_GATE WORLD_OBJECT tick={tick} id={identity:016x} type={row["type"]:x} flags={row["flags"]:x} '
            f'parent={row["parent"]:016x} {hashes} timing_fields={row["timing"].hex() or "-"}\n')


def capture(tick, objects, changed=None, created=True, created_count=None):
    """One capture: rows for `changed` (default all) and the summary over `objects`."""
    changed = objects if changed is None else changed
    lines = ''.join(object_line(tick, i, objects[i]) for i in sorted(changed))
    count = len(objects)
    present = sum(1 for r in objects.values() if r['flags'])
    field = ''
    if created:
        field = f' created={sum(1 for i in objects if is_created(i)) if created_count is None else created_count}'
    state = f'WORLD_IMPORTED_1:{tick}:{count}:'+':'.join(roots({i: r for i, r in objects.items()}))
    return lines+(f'[X4LAN] NATIVE_GATE WORLD_HASH tick={tick} objects={count} present={present} changed={len(changed)} '
                  f'unknown_class=0 unknown_motion=0 unknown_references=0{field} state={state}\n')


def world(with_b=True):
    objects = {SAVED: record(SAVED), ZONE: record(ZONE, SAVED), SHIP_A: record(SHIP_A, ZONE)}
    if with_b: objects[SHIP_B] = record(SHIP_B, ZONE)
    return objects


def created_lines(digests, rows):
    text = ''.join(f'[X4LAN] NATIVE_GATE CREATED_IDENTITIES tick={t} armed=1 installed=1 recording=1 restored=0 resolved_tick={t-1} '
                   f'frames={t-1} calls=9 noted=9 assigned={a} last=0 live={a} digest={d} rows_armed=1 stage=frame failures=0\n'
                   for t, a, d in digests)
    return text+''.join(f'[X4LAN] NATIVE_GATE CREATED_ROW index={i} kind=assign tick=14 id={identity:016x} parent={parent:016x} '
                        f'type=4000 ordinal={ordinal} macro={macro}\n' for i, (identity, parent, ordinal, macro) in enumerate(rows))


def make_run(root, logs):
    for slot, text in enumerate(logs, 1):
        client = Path(root)/f'client{slot}'
        (client/'installed').mkdir(parents=True)
        (client/'installed/x4_lan_lab-config.lua').write_text(CONFIG)
        (client/'engine-output.log').write_text(text)
    return Path(root)


class CreatedEvidenceTests(unittest.TestCase):
    def parse(self, text):
        errors = []
        return errors, list(frames(io.StringIO(text), errors, 1, None, True))

    def test_identity_helpers_mark_created_and_decode_the_creation_tick(self):
        self.assertEqual((identity_origin(SAVED), identity_origin(ZONE)), ('saved', 'created'))
        self.assertEqual((created_tick(SAVED), created_tick(ZONE)), (None, 14))

    def test_created_rows_follow_saved_rows_and_are_counted(self):
        errors, captured = self.parse(capture(60, world()))
        self.assertEqual(errors, [])
        self.assertEqual(captured[0]['coverage']['imported'], 1)
        self.assertEqual(captured[0]['coverage']['created'], 3)
        self.assertEqual(captured[0]['roots'], roots(world()))

    def test_destroyed_created_row_stays_absent_in_the_next_delta(self):
        objects = world()
        later = {**objects, SHIP_B: record(SHIP_B, ZONE, present=False)}
        errors, captured = self.parse(capture(60, objects)+capture(120, later, {SHIP_B: later[SHIP_B]}))
        self.assertEqual(errors, [])
        self.assertEqual([c['coverage']['present'] for c in captured], [4, 3])
        self.assertEqual(captured[1]['coverage']['created'], 3)

    def test_created_count_and_creation_tick_are_checked(self):
        for text in (capture(60, world(), created_count=2), capture(10, world())):
            with self.subTest(text=text[-120:]):
                errors, _ = self.parse(text)
                self.assertTrue(errors)

    def test_legacy_summary_without_created_field_is_unchanged(self):
        errors, captured = self.parse(capture(60, {SAVED: record(SAVED)}, created=False))
        self.assertEqual(errors, [])
        self.assertEqual(captured[0]['coverage']['created'], 0)
        self.assertEqual(captured[0]['coverage']['imported'], 1)


class CreatedCensusTests(unittest.TestCase):
    def logs(self):
        rows_a = [(ZONE, SAVED, 0, 'tempzone'), (SHIP_A, ZONE, 0, 'ship_arg_s_fighter_01_a_macro'),
                  (SHIP_B, ZONE, 1, 'ship_arg_s_fighter_01_a_macro')]
        first = created_lines([(14, 0, '0'*16), (15, 3, 'a'*16)], rows_a)+capture(60, world())
        second = created_lines([(14, 0, '0'*16), (15, 2, 'b'*16)], rows_a[:2])+capture(60, world(with_b=False))
        return first, second

    def test_creation_on_one_client_is_a_created_inventory_divergence(self):
        with tempfile.TemporaryDirectory() as d:
            run = make_run(d, self.logs())
            out = io.StringIO()
            with mock.patch('sys.argv', ['census_world', str(run)]), redirect_stdout(out):
                census_world.main()
            result = json.loads(out.getvalue())
        world_result = result['world']
        self.assertEqual(world_result['errors'], [])
        self.assertEqual((world_result['diverged_objects'], world_result['diverged_created'], world_result['created_inventory']), (1, 1, 1))
        root = world_result['roots'][0]
        self.assertEqual((root['parent'], root['parent_origin']), (f'{ZONE:016x}', 'created'))
        self.assertEqual(root['seeds'][0], {'saved_id': f'{SHIP_B:016x}', 'origin': 'created', 'domains': ['inventory'],
                                            'type': '4000', 'created_tick': 14, 'macro': 'ship_arg_s_fighter_01_a_macro'})
        self.assertEqual(world_result['timeline'][0]['diverged_created'], {'objects': 1, 'inventory': 1})
        created = result['side']['created']
        self.assertEqual((created['first_digest_difference'], created['assigned'], created['rows']), (15, [3, 2], [3, 2]))
        self.assertEqual(created['first_row_difference']['index'], 2)
        self.assertIsNone(created['first_row_difference']['right'])
        self.assertEqual(created['first_row_difference']['left']['id'], f'{SHIP_B:016x}')

    def test_compare_marks_created_objects(self):
        with tempfile.TemporaryDirectory() as d:
            result = compare_native_world.compare(make_run(d, self.logs()))
        self.assertEqual(result['inventory_difference_count'], 1)
        self.assertEqual([(o['saved_id'], o['origin']) for o in result['objects']], [(f'{SHIP_B:016x}', 'created')])

    def test_runs_without_created_identities_keep_their_report(self):
        saved = capture(60, {SAVED: record(SAVED)}, created=False)
        with tempfile.TemporaryDirectory() as d:
            result = census_world.census(make_run(d, [saved, saved]), 64, [1, 2])
            sides = census_world.side_logs(Path(d), [1, 2])
        self.assertEqual((result['diverged_objects'], result['diverged_created'], result['created_inventory']), (0, 0, 0))
        self.assertNotIn('created', census_world.compare_side(sides))


class CreatedProbeTests(unittest.TestCase):
    setUp = probe.SnapshotProbeTests.setUp

    def arm(self, extra=''):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_world_identities=true
          X4LAN_CONFIG.native_gate_created_identities=true
          X4LAN_CONFIG.native_gate_created_rows=true
          local gate=package.loadlib('gate')();arm_calls=0;arm_rows={}
          gate.created_identities=function(rows) arm_calls=arm_calls+1;arm_rows[#arm_rows+1]=rows;return 1 end
          gate.created_identities_status=function() return 'armed=1 installed=1 recording=1 restored=0 resolved_tick=0 assigned=0 digest=0000000000000000 stage=recording failures=0' end
          gate.created_row=function(index)
            if index<2 then return 'index='..index..' kind=assign tick=14 id=8000000e00000001 parent=0000000000000077 type=4000 ordinal='..index..' macro=tempzone' end
          end
          local load=LoadGame;LoadGame=function(slot) assert(arm_calls==1);load(slot) end
        '''+extra)

    def test_arms_before_loading_and_logs_status_rows_and_stop(self):
        self.arm()
        self.lua.execute(probe.SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().arm_calls, 1);self.assertEqual(self.lua.globals().loads, 1)
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(probe.SOURCE);self.lua.execute('update()')
        self.assertEqual(list(self.lua.globals().arm_rows.values()), [1, 1])
        self.lua.execute("state.active=0;state.completed=1;setmetatable(state,{__index=function() return 0 end})")
        self.lua.execute('update()')
        lines = list(self.lua.globals().logs.values())
        armed = next(i for i, line in enumerate(lines) if 'CREATED_IDENTITIES_ARMED rows=1' in line)
        tick = next(i for i, line in enumerate(lines) if 'CREATED_IDENTITIES tick=1 armed=1 installed=1 recording=1' in line)
        rows = [i for i, line in enumerate(lines) if 'NATIVE_GATE CREATED_ROW index=' in line]
        stop = next(i for i, line in enumerate(lines) if 'CREATED_IDENTITIES_STOP armed=1' in line)
        self.assertEqual(len(rows), 2);self.assertLess(armed, tick);self.assertLess(tick, rows[0]);self.assertLess(rows[-1], stop)
        self.assertFalse(any('ERROR' in line for line in lines))

    def test_requires_exact_flags_saved_identities_and_native_acceptance(self):
        cases = (('true', 'true', 'nil', '1', True), ('true', 'true', 'true', '1', True), ('1', 'true', 'nil', '1', False),
                 ("'on'", 'true', 'nil', '1', False), ('true', 'nil', 'nil', '1', False), ('true', 'true', '1', '1', False),
                 ('true', 'true', 'nil', '0', False), ('nil', 'true', 'true', '1', False))
        for value, identities, rows, result, accepted in cases:
            self.setUp()
            self.lua.execute('''
              X4LAN_CONFIG.native_gate_created_identities=%s
              X4LAN_CONFIG.native_gate_world_identities=%s
              X4LAN_CONFIG.native_gate_created_rows=%s
              local gate=package.loadlib('gate')()
              gate.created_identities=function() arm_calls=(arm_calls or 0)+1;return %s end
              gate.created_identities_status=function() return 'armed=1' end
              gate.created_row=function() end
            ''' % (value, identities, rows, result))
            self.lua.execute(probe.SOURCE);self.lua.execute('update();update();update()')
            refused = any('created identities refused' in line for line in self.lua.globals().logs.values())
            case = (value, identities, rows, result)
            self.assertEqual(self.lua.globals().loads, int(accepted), case);self.assertEqual(refused, not accepted, case)

    def test_portable_created_arms_after_created_identities(self):
        self.arm('''
          X4LAN_CONFIG.native_gate_portable_created=true
          portable_calls=0
          gate.portable_created=function() assert(arm_calls==1);portable_calls=portable_calls+1;return 1 end
        ''')
        self.lua.execute(probe.SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual((self.lua.globals().portable_calls, self.lua.globals().loads), (1, 1))
        lines = list(self.lua.globals().logs.values())
        created = next(i for i, line in enumerate(lines) if 'CREATED_IDENTITIES_ARMED rows=1' in line)
        portable = next(i for i, line in enumerate(lines) if 'PORTABLE_CREATED_ARMED' in line)
        self.assertLess(created, portable);self.assertFalse(any('ERROR' in line for line in lines))

    def test_portable_created_requires_exact_flag_created_identities_and_native_acceptance(self):
        # (portable flag, created identities, native result, accepted)
        cases = (('true', 'true', '1', True), ('nil', 'true', '1', True), ('1', 'true', '1', False), ("'on'", 'true', '1', False),
                 ('true', 'true', '0', False), ('true', 'nil', '1', False))
        for value, created, result, accepted in cases:
            self.setUp()
            self.lua.execute('''
              X4LAN_CONFIG.native_gate_world_identities=true
              X4LAN_CONFIG.native_gate_created_identities=%s
              X4LAN_CONFIG.native_gate_portable_created=%s
              local gate=package.loadlib('gate')()
              gate.created_identities=function() return 1 end
              gate.created_identities_status=function() return 'armed=1' end
              gate.created_row=function() end
              gate.portable_created=function() portable_calls=(portable_calls or 0)+1;return %s end
            ''' % (created, value, result))
            self.lua.execute(probe.SOURCE);self.lua.execute('update();update();update()')
            logs = list(self.lua.globals().logs.values())
            refused = any('portable created keys refused' in line for line in logs)
            armed = any('PORTABLE_CREATED_ARMED' in line for line in logs)
            case = (value, created, result)
            self.assertEqual(self.lua.globals().loads, int(accepted), case);self.assertEqual(refused, not accepted, case)
            self.assertEqual(armed, accepted and value == 'true', case)

    def test_created_load_arms_after_created_identities_before_loading(self):
        self.arm('''
          X4LAN_CONFIG.native_gate_created_load=true
          load_calls=0
          gate.created_load=function(...) assert(select('#',...)==0 and arm_calls==1);load_calls=load_calls+1;return 1 end
        ''')
        self.lua.execute(probe.SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual((self.lua.globals().load_calls, self.lua.globals().loads), (1, 1))
        lines = list(self.lua.globals().logs.values())
        created = next(i for i, line in enumerate(lines) if 'CREATED_IDENTITIES_ARMED rows=1' in line)
        loaded = next(i for i, line in enumerate(lines) if 'CREATED_LOAD_ARMED' in line)
        self.assertLess(created, loaded);self.assertFalse(any('ERROR' in line for line in lines))

    def test_created_load_requires_exact_flag_created_identities_and_native_acceptance(self):
        # (load flag, created identities, native result, accepted)
        cases = (('true', 'true', '1', True), ('nil', 'true', '1', True), ('1', 'true', '1', False), ("'on'", 'true', '1', False),
                 ('true', 'true', '0', False), ('true', 'nil', '1', False))
        for value, created, result, accepted in cases:
            self.setUp()
            self.lua.execute('''
              X4LAN_CONFIG.native_gate_world_identities=true
              X4LAN_CONFIG.native_gate_created_identities=%s
              X4LAN_CONFIG.native_gate_created_load=%s
              local gate=package.loadlib('gate')()
              gate.created_identities=function() return 1 end
              gate.created_identities_status=function() return 'armed=1' end
              gate.created_row=function() end
              gate.created_load=function() return %s end
            ''' % (created, value, result))
            self.lua.execute(probe.SOURCE);self.lua.execute('update();update();update()')
            logs = list(self.lua.globals().logs.values())
            refused = any('created load identities refused' in line for line in logs)
            armed = any('CREATED_LOAD_ARMED' in line for line in logs)
            case = (value, created, result)
            self.assertEqual(self.lua.globals().loads, int(accepted), case);self.assertEqual(refused, not accepted, case)
            self.assertEqual(armed, accepted and value == 'true', case)


if __name__ == '__main__':
    unittest.main()
