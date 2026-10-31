from copy import deepcopy
import gzip
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools/dev'))
from build_native_gate_overlay import _snapshot_starts
from native_gate_wait_manifest import wait_manifest, double_bits, windows_saved_time_bits, selected_time_bits
from native_world_wait import PARTS, HEX, evidence, compare, bits, rng_step, configured_waits, paired_diagnostics
from validate_native_gate import validate


ENTRY = {'id': '0000000000080d54', 'name': 'move.idle', 'version': 4, 'index': 5,
         'time': 6.91667, 'minimum': .1, 'maximum': .5}


def fixture():
    rows = [{k: 0 for k in keys} for keys in PARTS]
    for part, row in enumerate(rows):
        row.update(event=1, part=part)
    rows[0].update(selected=1, enter=1, leave=2, result=1, boot=2, thread=7, ui=1, caller=0x1069bfc,
                   id=int(ENTRY['id'], 16), name=ENTRY['name'], version=4, xml_index=5, slot=20, pc=25,
                   time=bits(ENTRY['time']), expected_time=bits(ENTRY['time']), minimum=bits(.1), maximum=bits(.5),
                   rng_before=123, rng_after=rng_step(123), due_before=bits(-1), due_after=bits(7.1), reason_after=2)
    expression = bytearray(104)
    struct.pack_into('<III', expression, 8, 2, 1, 1)
    struct.pack_into('<I', expression, 0x18, 8); struct.pack_into('<d', expression, 0x20, .1)
    struct.pack_into('<I', expression, 0x28, 8); struct.pack_into('<d', expression, 0x30, .5)
    for row in rows[1:3]:
        row.update(self=0x10000, exec=0x20000, value=0x20038, context=0x30000, context_vtable=0x142c6a270, lifetime=1, entity=0x40000,
                   runtime=88, script=0x50000, program=0x60000, begin=0x70000, end=0x70800,
                   vtable=0x142c68d78, blocking=1, timeout=bytes(16), expression=bytes(expression),
                   phase_status=1, eligible=1, elapsed=100, view=1, tls_time=bits(7.083), game=bits(7.083),
                   real=bits(4), saved_time=bits(7.083), table=0x80000)
    rows[3].update({k: rows[0][k] for k in ('id', 'name', 'version', 'since', 'slot', 'pc', 'time')}); rows[3]['tag']=0x66
    return rows


def log(records, **overrides):
    def value(key, item):
        return item.hex() if isinstance(item, bytes) else f'{item:x}' if key in HEX else str(item)
    lines = ['NATIVE_GATE WORLD_WAIT_EVENT '+' '.join(f'{k}={value(k,v)}' for k,v in row.items()) for row in records]
    stop = dict(schema=1, capacity=128, selected=1, reserved=1, complete=1, rows=4, failures=0, dropped=0,
                calls=27, import_calls=24, foreign_calls=3, unknown=0, installed=0, restored=1, open=0, peak=1, restore_open=0, index_coverage=0)
    stop.update(overrides)
    lines.append('NATIVE_GATE WORLD_WAIT_STOP '+' '.join(f'{k}={v}' for k,v in stop.items()))
    return '\n'.join(lines)


class WaitEvidenceTests(unittest.TestCase):
    def read(self, text):
        errors=[]; events, status=evidence(text, [ENTRY], errors)
        return events, status, errors

    def test_valid_read_only_boundary_with_explicit_ordinal_limit(self):
        events, status, errors=self.read(log(fixture()))
        self.assertEqual(errors, [])
        self.assertFalse(status['xml_ordinal_directly_observed'])
        self.assertTrue(events[0]['input_output']['single_rng_step_matches'])
        self.assertTrue(events[0]['input_output']['deadline_changed'])

    def test_text_time_requires_exact_native_bits_and_never_rescues_old_failure(self):
        selected=deepcopy(ENTRY);del selected['time'];selected['time_text']='6.91667'
        rows=fixture();native=int(windows_saved_time_bits(selected['time_text']),16)
        rows[0]['time']=rows[0]['expected_time']=rows[3]['time']=native
        errors=[];evidence(log(rows),[selected],errors);self.assertEqual(errors,[])
        # Historical007 records used the old wire bits and a native error8.
        # Even supplying new text cannot retroactively make those valid.
        rows[0]['expected_time']=bits(ENTRY['time']);rows[0]['errors']=8
        for expected in ([ENTRY],[selected]):
            errors=[];evidence(log(rows,failures=1),expected,errors)
            self.assertIn('wait: event 1: native errors 8',errors)
            self.assertIn('wait: event 1: manifest mismatch',errors)

    def test_each_guard_failure_is_retained(self):
        changes=[(0,'errors',1),(0,'caller',0x1069bfd),(0,'boot',3),(0,'result',0),(0,'id',999),
                 (0,'version',5),(0,'xml_index',20),(0,'index_status',1),(0,'time',bits(0)),
                 (0,'leave',1),(1,'value',0x20039),(2,'lifetime',2),(2,'expression',bytes(104)),
                 (1,'eligible',0),(1,'phase_status',3),(1,'elapsed',120001),(1,'game',bits(float('nan'))),
                 (1,'listeners',1),(1,'end',0x70000),(3,'tag',0x5d),(3,'name','changed')]
        for part,key,value in changes:
            with self.subTest(part=part,key=key):
                rows=fixture();rows[part][key]=value
                self.assertTrue(self.read(log(rows))[2])

    def test_missing_duplicate_truncated_and_post_stop_records_fail(self):
        rows=fixture()
        for text in (log(rows[:-1]),log([rows[0]]+rows),log(rows)+'\n'+log(rows).splitlines()[0],
                     '\n'.join(log(rows).splitlines()[:-1]),log(rows).replace(' time=', ' bad='),
                     log(rows).replace(' expression=', ' expression=zz')):
            with self.subTest(text=text[:90]):self.assertTrue(self.read(text)[2])

    def test_stop_open_or_unexercised_never_complete(self):
        for key in ('failures','dropped','unknown','installed','open','restore_open'):
            with self.subTest(key=key):self.assertTrue(self.read(log(fixture(),**{key:1}))[2])
        self.assertTrue(self.read(log([],reserved=0,complete=0,rows=0))[2])
        self.assertTrue(self.read(log(fixture(),calls=28))[2])

    def test_comparison_uses_saved_owner_and_semantics_not_addresses(self):
        left=self.read(log(fixture()))[0];rows=fixture()
        for row in rows[1:3]:
            for key in ('self','exec','value','context','context_vtable','entity','script','program','begin','end','vtable','table'):row[key]+=0x100000
            row['real']=bits(8)
        right,status,errors=self.read(log(rows));self.assertEqual(errors,[]);self.assertEqual(compare(left,right),[])
        rows[0]['rng_before']=456;rows[0]['rng_after']=rng_step(456);rows[0]['due_after']=bits(7.2)
        right,status,errors=self.read(log(rows));self.assertEqual(errors,[])
        self.assertIn('due_after',{d['field'] for d in compare(left,right)})

    def test_existing_earlier_deadline_is_not_called_sampled_duration(self):
        rows=fixture();rows[0]['due_before']=rows[0]['due_after']=bits(6.99)
        events,status,errors=self.read(log(rows));self.assertEqual(errors,[])
        self.assertFalse(events[0]['input_output']['deadline_changed'])
        self.assertNotIn('duration_observed',events[0]['input_output'])

    def test_pair_diagnostics_never_overwrite_old_errors_or_call_invalid_capture_equal(self):
        events,status,problems=self.read(log(fixture()));self.assertFalse(problems)
        captures=[dict(slot=i,expected=[ENTRY],events=deepcopy(events),coverage=status,errors=[]) for i in (1,2)]
        captures[1]['events'][0]['input_output']['due_after']=bits(7.2)
        errors=['existing failure'];result=paired_diagnostics(captures,False,errors)
        self.assertEqual(errors,['existing failure']);self.assertTrue(result['capture_valid']);self.assertFalse(result['semantics_equal'])
        paired_diagnostics(captures,True,errors);self.assertEqual(errors[0],'existing failure');self.assertEqual(len(errors),2)
        captures[1]['errors']=['incomplete'];result=paired_diagnostics(captures,False,errors)
        self.assertFalse(result['capture_valid']);self.assertIsNone(result['semantics_equal']);self.assertEqual(result['differences'],[])
        self.assertFalse(paired_diagnostics(captures[:1],False,errors)['capture_valid'])

    def test_main_validator_reports_differences_and_rejects_only_claimed_equality(self):
        wire=f"{ENTRY['id']},4,5,{double_bits(ENTRY['time'])},{double_bits(.1)},{double_bits(.5)},move.idle"
        for verdict in ('fail','pass'):
            with self.subTest(verdict=verdict),tempfile.TemporaryDirectory() as directory:
                run=Path(directory)
                (run/'result.json').write_text(json.dumps({'verdict':verdict,'exit_code':1 if verdict=='fail' else 0}))
                states={str(tick):{str(slot):{'game':20+tick/60,'real':20+tick/60,
                    'digest':('a' if verdict=='pass' or tick==0 or slot==1 else 'b')*64,'command':'00000000'}
                    for slot in (1,2)} for tick in (0,1)}
                (run/'native-barrier.json').write_text(json.dumps({'states':states,'hold_seconds':0,
                    'verdict':verdict,'failure':'battle_state_mismatch_tick_1' if verdict=='fail' else None}))
                for slot in (1,2):
                    client=run/f'client{slot}';(client/'installed').mkdir(parents=True)
                    (client/'result.json').write_text(json.dumps({'lane_restored':True}))
                    (client/'installed/x4_lan_lab-config.lua').write_text('X4LAN_CONFIG.native_gate_world_wait_manifest = '+json.dumps(wire)+'\n')
                    rows=fixture()
                    if slot==2:rows[0]['due_after']=bits(7.2)
                    (client/'engine-output.log').write_text(f'NATIVE_GATE STOP slot=p{slot} completed=1 reason=1 restored=1 wait_game_delta=0 max_dt_error=0\n'+log(rows))
                result=validate(run,'battle')
            self.assertFalse(result['session_pass']);self.assertIn('three-second hold not proven',result['errors'])
            self.assertIn('player 1 has no actual battle samples',result['errors'])
            self.assertTrue(result['native_wait_restore']['capture_valid']);self.assertFalse(result['native_wait_restore']['semantics_equal'])
            self.assertIn('due_after',{d['field'] for d in result['native_wait_restore']['differences']})
            self.assertEqual('selected saved Wait restoration inputs/deadlines differ between players' in result['errors'],verdict=='pass')
            if verdict=='fail':self.assertEqual(result['first_mismatch'],1)


class WaitManifestTests(unittest.TestCase):
    def build(self, changes=None, xml_change=lambda s:s):
        with tempfile.TemporaryDirectory() as directory:
            snapshot=Path(directory)/'save.xml.gz'
            xml='<savegame><aidirector><patch><script name="move.idle" version="4"/></patch><entity id="[0x80d54]"><script name="move.idle" index="5" time="6.91667"/></entity></aidirector></savegame>'
            snapshot.write_bytes(gzip.compress(xml_change(xml).encode()))
            manifest={'schema':1,'snapshot_sha256':hashlib.sha256(snapshot.read_bytes()).hexdigest(),'entries':[deepcopy(ENTRY)]}
            if changes:changes(manifest)
            return wait_manifest(manifest,snapshot,_snapshot_starts)

    def test_exact_snapshot_bound_manifest(self):
        self.assertEqual(self.build(),f"{ENTRY['id']},4,5,{double_bits(6.91667)},{double_bits(.1)},{double_bits(.5)},move.idle")
        config='X4LAN_CONFIG.native_gate_world_wait_manifest = '+json.dumps(self.build())+'\n'
        self.assertEqual(configured_waits(config),[ENTRY])
        with self.assertRaises(ValueError):configured_waits(config+config)
        with self.assertRaises(ValueError):configured_waits(config.replace('move.idle','move idle'))

    def test_schema2_exact_persisted_text_and_windows_conversion(self):
        def schema2(manifest):
            manifest['schema']=2;del manifest['entries'][0]['time'];manifest['entries'][0]['time_text']='6.91667'
        wire=self.build(schema2)
        self.assertEqual(wire,f"{ENTRY['id']},4,5,401baaab8a5ce5b5,{double_bits(.1)},{double_bits(.5)},move.idle")
        self.assertNotEqual(wire,self.build())  # schema1 remains bit-for-bit unchanged
        for text in ('6.916670','6.916670000000001','6.91668'):
            with self.subTest(text=text),self.assertRaises(ValueError):
                self.build(schema2,lambda xml:xml.replace('time="6.91667"',f'time="{text}"'))
        for key,value in (('version',5),('index',4)):
            def changed(manifest):
                schema2(manifest);manifest['entries'][0][key]=value
            with self.assertRaises(ValueError):self.build(changed)
        with self.assertRaises(ValueError):
            self.build(lambda manifest:manifest.update(schema=2))  # numeric time is not schema2 text
        with self.assertRaises(ValueError):
            selected_time_bits({'time':6.91667,'time_text':'6.91667'})

    def test_saved_decimal_bits_and_bounded_grammar(self):
        vectors={'0':'0000000000000000','1':'3ff0000000000000','6.91667':'401baaab8a5ce5b5',
                 '6.31667':'4019444523f67f4e','7.083':'401c54fdf3b645a2','0.001':'3f50624dd2f1a9fd',
                 '0.123456789012345':'3fbf9add3746f62e','999999999999999.999999999999999':'430c6bf526340000',
                 '42.000000000000001':'4045000000000000','100000000000000.1':'42d6bcc41e900006',
                 '0.000000000000001':'3cd203af9ee7561a'}
        for text,expected in vectors.items():
            self.assertEqual(windows_saved_time_bits(text),expected,text)
        for text in ('','-0','+1','01','.1','1.',' 1','1 ','1\n','1e0','1E309','nan','inf','1;2','١',
                     '1000000000000000','0.0000000000000001',1,True,None):
            with self.subTest(text=text),self.assertRaises(ValueError):windows_saved_time_bits(text)
        self.assertEqual(double_bits(.001),'3f50624dd2f1a9fc')  # expression constants stay separate

    def test_snapshot_script_metadata_and_selection_bounds(self):
        mutations=[lambda m:m.update(snapshot_sha256='0'*64), lambda m:m.update(entries=[]),
                   lambda m:m.update(entries=m['entries']*2), lambda m:m['entries'][0].update(version=5),
                   lambda m:m['entries'][0].update(index=20), lambda m:m['entries'][0].update(time=0),
                   lambda m:m['entries'][0].update(minimum=float('nan')), lambda m:m['entries'][0].update(maximum=.01),
                   lambda m:m['entries'][0].update(name='bad;script'),lambda m:m['entries'][0].update(id='0'*16),
                   lambda m:m['entries'][0].update(version=True), lambda m:m['entries'][0].update(extra=1)]
        for mutation in mutations:
            with self.subTest(mutation=mutation):
                with self.assertRaises(ValueError):self.build(mutation)

    def test_missing_duplicate_or_explicit_mismatched_saved_version(self):
        for transform in (lambda s:s.replace('version="4"','version="3"'),
                          lambda s:s.replace('index="5"','index="4"'),
                          lambda s:s.replace('time="6.91667"','time="6.91667" version="3"'),
                          lambda s:s.replace('</patch>','<script name="move.idle" version="4"/></patch>')):
            with self.assertRaises(ValueError):self.build(xml_change=transform)


if __name__=='__main__':unittest.main()
