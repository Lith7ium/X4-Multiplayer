from copy import deepcopy
import contextlib
import gzip
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
import build_native_gate_overlay as builder
from native_world_action import PARTS, STOP, HEX, configured, evidence, manifest_wire, paired_diagnostics, selections
from validate_native_gate import validate

ENTRY = dict(id='0000000000080d54', name='move.idle', version=4)


def bits(value):
    return int.from_bytes(struct.pack('<d', value), 'little')


def fixture():
    rows = [{k: 0 for k in fields} for fields in PARTS]
    for part, row in enumerate(rows):
        row.update(event=1, part=part)
    rows[0].update(selected=1, stage=1, thread=7, ui=1, boot=2, enter=1, leave=2, depth=1,
                   id=int(ENTRY['id'], 16), name=ENTRY['name'], version=4, program_hash=123,
                   type=0x2c0cc88, action=0xbe2320, pc_before=1, pc_after=2, rng_before=50, rng_after=57)
    for row in rows[1:]:
        row.update(node=0x10000, exec=0x20000, frame=0x20038, current=0x20000, context=0x30000,
                   lifetime=1, entity=0x40000, runtime=88, script=0x50000, program=0x60000,
                   begin=0x70000, end=0x70800, vtable=0x142c0cc88, target=0x140be2320,
                   id=int(ENTRY['id'], 16), name=ENTRY['name'], version=4, program_hash=123, tag=0x66,
                   phase_status=1, eligible=1, elapsed=100, app_flags=0x20, view=1,
                   tls_time=bits(7.083), game=bits(7.083), real=bits(4), saved_time=bits(7.083), table=0x80000)
    return rows


def log(records, **overrides):
    def text(key, value):
        return f'{value:x}' if key in HEX else str(value)
    lines = ['NATIVE_GATE WORLD_ACTION_EVENT '+' '.join(f'{k}={text(k,v)}' for k,v in r.items()) for r in records]
    stop = dict.fromkeys(STOP, 0)
    stop.update(schema=1, capacity=2048, selected=1, reserved=1, complete=1, rows=3, calls=1, peak=1, closed=1)
    stop.update(overrides)
    lines.append('NATIVE_GATE WORLD_ACTION_STOP '+' '.join(f'{k}={v}' for k,v in stop.items()))
    return '\n'.join(lines)


class ActionEvidenceTests(unittest.TestCase):
    def read(self, text):
        errors = []; events, coverage = evidence(text, [ENTRY], errors)
        return events, coverage, errors

    def capture(self, rows, slot=1):
        events, coverage, errors = self.read(log(rows))
        return dict(slot=slot, expected=[ENTRY], events=events, coverage=coverage, errors=errors)

    def test_valid_raw_capture_does_not_claim_evaluated_values(self):
        events, coverage, errors = self.read(log(fixture()))
        self.assertEqual(errors, [])
        self.assertTrue(coverage['rng_changing_action_classes_known'])
        self.assertFalse(coverage['evaluated_values_observed']); self.assertFalse(coverage['pre_dispatch_chance_observed'])
        self.assertEqual(events[0]['event']['rng_after'], 57)

    def test_guard_phase_lifetime_slot_and_dispatch_mutations_fail(self):
        changes = [(0,'errors',1),(0,'id',99),(0,'name','foreign'),(0,'version',5),(0,'boot',3),(0,'tick',1),
                   (0,'ui',0),(0,'leave',1),(0,'parent',1),(0,'depth',65),(0,'pc_before',2),(0,'action',0xbe2321),
                   (0,'stage',0),(0,'children',1),(1,'frame',0x20039),(1,'current',0),(2,'lifetime',2),
                   (2,'chance',1),(2,'program_hash',9),(1,'end',0x90000),(1,'begin',0x70800),
                   (1,'tag',0x5d),(1,'target',0x140be2321),(1,'eligible',0),(1,'phase_status',3),
                   (1,'app_flags',0x26),(1,'view',0),(1,'main',1),(1,'elapsed',120001),
                   (1,'game',bits(float('nan'))),(2,'tls_time',bits(7.084))]
        for part, key, value in changes:
            with self.subTest(part=part,key=key):
                rows = fixture(); rows[part][key] = value
                self.assertTrue(self.read(log(rows))[2])

    def test_missing_duplicate_truncated_and_unknown_records_fail(self):
        original = log(fixture()); rows = fixture()
        for text in (log(rows[:-1]), log([rows[0]]+rows), original+'\n'+original.splitlines()[0],
                     '\n'.join(original.splitlines()[:-1]), original.replace(' id=', ' bad='),
                     original.replace(' stage=1', ' stage=1 stage=1'), original.replace('WORLD_ACTION_EVENT','WORLD_ACTION_UNKNOWN',1)):
            with self.subTest(text=text[:70]): self.assertTrue(self.read(text)[2])

    def test_native_widths_image_bounds_and_emitted_row_caps(self):
        changes=[(0,'thread',1<<32),(0,'flags_before',1<<16),(0,'flags_after',1<<16),(0,'chance',1<<32),
                 (0,'enter',1<<64),(1,'tag',1<<32),(1,'app_flags',1<<32),(1,'elapsed',1<<64),(1,'node',1)]
        for part,key,value in changes:
            with self.subTest(part=part,key=key):
                rows=fixture();rows[part][key]=value;self.assertTrue(self.read(log(rows))[2])
        for type_rva,action_rva,base in ((0x7000000,1,0x140000000),(1,0x6fa9000,0x140000000),(1,1,0x140000001)):
            rows=fixture();rows[0].update(stage=0,type=type_rva,action=action_rva)
            for r in rows[1:]:r.update(vtable=base+type_rva,target=base+action_rva)
            self.assertTrue(self.read(log(rows,unknown_rng=1))[2])
        original=log(fixture());lines=original.splitlines()
        for index,padding in ((0,2100),(1,2048),(3,1024)):
            changed=list(lines);changed[index]+=' '*padding
            self.assertTrue(self.read('\n'.join(changed))[2])
        rows=fixture();rows[0]['thread']=1<<32
        captures=[self.capture(rows,slot) for slot in (1,2)];errors=[]
        result=paired_diagnostics(captures,True,errors)
        self.assertFalse(result['capture_valid']);self.assertIsNone(result['traces_equal']);self.assertTrue(errors)

    def test_closed_and_bounded_completeness_counters(self):
        for key in ('failures','dropped','unknown','threads','open','restore_open','value_coverage','chance_coverage','exception_cleanup'):
            with self.subTest(key=key): self.assertTrue(self.read(log(fixture(),**{key:1}))[2])
        for key,value in (('schema',2),('closed',0),('peak',0),('calls',0),('rows',2),('reserved',2),('selected',2),('unknown_rng',1)):
            with self.subTest(key=key):self.assertTrue(self.read(log(fixture(),**{key:value}))[2])
        self.assertTrue(self.read(log([],reserved=0,complete=0,rows=0,calls=0,peak=0))[2])

    def test_unknown_rng_class_and_foreign_children_remain_partial(self):
        rows = fixture();rows[0].update(stage=0,type=0x20000,action=0x10000,unobserved_children=1)
        for r in rows[1:]:r.update(vtable=0x140020000,target=0x140010000)
        _,coverage,errors=self.read(log(rows,unknown_rng=1))
        self.assertEqual(errors,[]);self.assertFalse(coverage['rng_changing_action_classes_known']);self.assertEqual(coverage['unobserved_nested_calls'],1)

    def test_nested_parent_relationships_and_non_global_comparison(self):
        rows=fixture();rows[0].update(leave=4,children=1)
        child=fixture()
        for r in child:r['event']=2
        child[0].update(enter=2,leave=3,parent=1,depth=2)
        _,_,errors=self.read(log(rows+child,reserved=2,complete=2,rows=6,calls=2,peak=2));self.assertEqual(errors,[])
        child[0]['depth']=1
        self.assertTrue(self.read(log(rows+child,reserved=2,complete=2,rows=6,calls=2,peak=2))[2])
        a=self.capture(fixture());changed=fixture();changed[0]['thread']=900
        # Local pointers and scheduling ordinals are not portable keys.
        for r in changed[1:]:
            for key in ('node','exec','frame','current','context','entity','script','program','begin','end','table'):
                r[key]+=0x100000
        b=self.capture(changed,2);errors=[];result=paired_diagnostics([a,b],True,errors)
        self.assertTrue(result['capture_valid']);self.assertTrue(result['traces_equal']);self.assertEqual(errors,[])

    def test_raw_rng_difference_is_retained_and_claimed_pass_rejected(self):
        a=self.capture(fixture());changed=fixture();changed[0]['rng_after']+=1;b=self.capture(changed,2)
        errors=['existing whole-world mismatch'];result=paired_diagnostics([a,b],False,errors)
        self.assertTrue(result['capture_valid']);self.assertFalse(result['traces_equal']);self.assertEqual(errors,['existing whole-world mismatch'])
        self.assertIn('rng_after',result['differences'][0]['fields'])
        paired_diagnostics([a,b],True,errors);self.assertIn('selected early Action capture incomplete or input/output traces differ',errors)

    def test_main_validator_includes_diagnostics_without_hiding_failures(self):
        wire=f'{ENTRY["id"]},4,move.idle'
        with tempfile.TemporaryDirectory() as directory:
            run=Path(directory);(run/'result.json').write_text(json.dumps({'verdict':'fail','exit_code':5,'exit_reason':'fail_pattern'}))
            states={str(tick):{str(slot):{'game':20+tick/60,'real':20+tick/60,
                'digest':('a' if tick==0 or slot==1 else 'b')*64,'command':'00000000'} for slot in (1,2)} for tick in (0,1)}
            (run/'native-barrier.json').write_text(json.dumps({'states':states,'hold_seconds':0,'protocol_version':1,'verdict':'fail','failure':'battle_state_mismatch_tick_1'}))
            for slot in (1,2):
                client=run/f'client{slot}';(client/'installed').mkdir(parents=True)
                (client/'result.json').write_text(json.dumps({'lane_restored':True}))
                (client/'installed/x4_lan_lab-config.lua').write_text('X4LAN_CONFIG.native_gate_world_action_manifest = '+json.dumps(wire)+'\n')
                rows=fixture();rows[0]['rng_after']+=slot
                (client/'engine-output.log').write_text(f'NATIVE_GATE STOP slot=p{slot} completed=1 reason=1 restored=1 wait_game_delta=0 max_dt_error=0\n'+log(rows))
            result=validate(run,'battle')
        self.assertFalse(result['session_pass']);self.assertIn('three-second hold not proven',result['errors'])
        self.assertTrue(result['native_early_actions']['capture_valid']);self.assertFalse(result['native_early_actions']['traces_equal'])


class ActionManifestTests(unittest.TestCase):
    def create(self, directory, change=lambda d:None, xml_change=lambda x:x):
        snapshot=Path(directory)/'save.xml.gz'
        xml='<savegame><aidirector><patch><script name="move.idle" version="4"/></patch><entity id="[0x80d54]"/></aidirector></savegame>'
        snapshot.write_bytes(gzip.compress(xml_change(xml).encode()))
        document=dict(schema=1,snapshot_sha256=hashlib.sha256(snapshot.read_bytes()).hexdigest(),entries=[deepcopy(ENTRY)]);change(document)
        return snapshot,document

    def test_exact_snapshot_identity_version_and_config(self):
        with tempfile.TemporaryDirectory() as directory:
            snapshot,doc=self.create(directory);wire=manifest_wire(doc,snapshot,builder._snapshot_starts)
        config='X4LAN_CONFIG.native_gate_world_action_manifest = '+json.dumps(wire)+'\n'
        self.assertEqual(configured(config),[ENTRY])
        for invalid in (config+config,config.replace('4,','-1,'),config.replace('move.idle','bad;name')):
            with self.assertRaises(ValueError):configured(invalid)

    def test_manifest_rejects_unbounded_ambiguous_or_changed_selections(self):
        mutations=[lambda d:d.update(schema=True),lambda d:d.update(snapshot_sha256='0'*64),lambda d:d.update(entries=[]),
                   lambda d:d.update(entries=d['entries']*2),lambda d:d['entries'][0].update(version=5),
                   lambda d:d['entries'][0].update(id='0'*16),lambda d:d['entries'][0].update(version=True),
                   lambda d:d['entries'][0].update(name='bad script'),lambda d:d['entries'][0].update(extra=1)]
        for mutate in mutations:
            with self.subTest(mutation=mutate),tempfile.TemporaryDirectory() as directory:
                snapshot,doc=self.create(directory,mutate)
                with self.assertRaises(ValueError):manifest_wire(doc,snapshot,builder._snapshot_starts)
        for transform in (lambda s:s.replace('version="4"','version="3"'),lambda s:s.replace('</patch>','<script name="move.idle" version="4"/></patch>'),
                          lambda s:s.replace('id="[0x80d54]"','id="[0x80d55]"'),lambda s:s.replace('</aidirector>','<entity id="[0x80d54]"/></aidirector>')):
            with tempfile.TemporaryDirectory() as directory:
                snapshot,doc=self.create(directory,xml_change=transform)
                with self.assertRaises(ValueError):manifest_wire(doc,snapshot,builder._snapshot_starts)
        with self.assertRaises(ValueError):selections([dict(ENTRY,id=f'{i:016x}') for i in (1,2,3)])

    def test_builder_requires_explicit_early_and_npc_opt_in(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);snapshot,doc=self.create(root);manifest=root/'action.json';manifest.write_text(json.dumps(doc));dll=root/'gate.dll';dll.write_bytes(b'test only')
            args=['overlay','--gate-dll',str(dll),'--mode','battle','--snapshot',str(snapshot),'--out',str(root/'out'),
                  '--world-action-manifest',str(manifest)]
            with mock.patch.object(sys,'argv',args),contextlib.redirect_stderr(io.StringIO()) as err,self.assertRaises(SystemExit):builder.main()
            self.assertIn('explicit --npc-diag',err.getvalue());self.assertFalse((root/'out').exists())
            args+=['--world-identities','--world-hash-stride','1','--world-hash-watch','0000000000000001','--world-trace-ticks','12','--world-trace-early','--world-queue-trace','--serial-pathing','--npc-diag']
            with mock.patch.object(sys,'argv',args),contextlib.redirect_stdout(io.StringIO()):builder.main()
            configs=list((root/'out').rglob('config.lua.append'));self.assertEqual(len(configs),1)
            config=configs[0].read_text();self.assertEqual(configured(config),[ENTRY]);self.assertNotIn('native_gate_world_wait_rng_manifest',config)


if __name__=='__main__':unittest.main()
