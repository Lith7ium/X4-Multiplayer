from copy import deepcopy
from contextlib import redirect_stdout
import hashlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools/dev'))
import native_world_portable as w
import native_world_portable_adapter as a
import native_portable as p
from test_native_portable import frame, contract


def sample():
    imports = {f'{i:016x}': 1000+i for i in range(1, 25)}
    imports['0000000000000100'] = 2000
    imports['fffffffffffffffe'] = 0
    cohort = w.Cohort(imports)
    header, rows = a.from_portable(frame(), contract(), cohort, imports, 'windows')
    return cohort, header, list(rows), imports


def encoded(header, rows, cohort):
    out = io.BytesIO(); w.encode(out, header, rows, cohort); return out.getvalue()


def comparison(header, rows, cohort, right=None, **kwargs):
    return w.compare(io.BytesIO(encoded(header, rows, cohort)),
                     io.BytesIO(encoded(header | {'source':'linux'}, right if right is not None else rows, cohort)), cohort, **kwargs)


class ImportedWorldSchemaTests(unittest.TestCase):
    def test_complete_inventory_and_independent_domain_coverage(self):
        cohort, header, rows, _ = sample(); result = comparison(header, rows, cohort)
        self.assertEqual(result['inventory_count'], 26)
        self.assertTrue(result['stream_valid']); self.assertNotIn('session_pass', result)
        self.assertNotIn('capture_valid', result)  # Syntax/roots cannot prove native ownership.
        for domain in w.DOMAINS:
            report = result['domains'][domain]
            self.assertTrue(report['root_equal']); self.assertIsNone(report['semantic_equal'])
            self.assertFalse(report['complete'])
            self.assertEqual(report['comparable'], 0 if domain=='timing' else 12)
        self.assertEqual(result['left']['identity_census']['not_captured'], 13)
        self.assertEqual(result['left']['identity_census']['unresolved_import'], 1)

    def test_all_unsupported_is_not_semantic_success(self):
        cohort, header, rows, _ = sample()
        rows = [a.unknown_row(saved, 'not_captured') for saved in cohort.ids]
        result = comparison(header, rows, cohort)
        for item in result['domains'].values():
            self.assertTrue(item['root_equal']); self.assertIsNone(item['semantic_equal']); self.assertIsNone(item['supported_subset_equal'])
        with self.assertRaises(ValueError): w.Cohort([])

    def test_independent_canonical_hash_golden(self):
        cohort, header, rows, _ = sample(); raw = encoded(header, rows, cohort)
        footer = json.loads(raw.splitlines()[-1])['footer']
        compact = lambda value: json.dumps(value,sort_keys=True,separators=(',',':'),ensure_ascii=True).encode('ascii')
        context = {k:v for k,v in header.items() if k!='source'}
        # Reconstruct every row/root independently, without Totals/canonical.
        for domain in w.DOMAINS:
            digest = hashlib.sha256(b'X4WDIAG1-ROOT\0'+domain.encode()+b'\0'+compact(context))
            for row in rows:
                identity = int(row['saved'],16).to_bytes(8,'little')
                payload = compact({'identity':row['identity'],'result':row['domains'][domain]})
                digest.update(identity+hashlib.sha256(b'X4WDIAG1-ROW\0'+domain.encode()+b'\0'+identity+payload).digest())
            self.assertEqual(digest.hexdigest(),footer['roots'][domain])
        self.assertEqual(cohort.ids[-1],'fffffffffffffffe')
        self.assertEqual(cohort.sha256,'be2cc2fee1fb411c0db40e350b42b0c061c6be5cc620b276913234ccaa4adba4')
        self.assertEqual(footer['roots'],{
            'entity':'5afbbc274966a4734394e124ed2697822098ab29fb6a8fdf219e202fca918dbb',
            'topology':'6d0331e38d82304159e78e4ceef0ef04a8ebdbda6173bb53115f21c752324305',
            'motion':'288d447e694a174ea0dabbf5b40cb8182673aa5ce0722525021f1e95c7ad4906',
            'health':'4bad35eda819fd7eaeb5497e37b1a7c3859f944ccbc1af4486833e35c003b48e',
            'timing':'9176e942de26599433c2772047bbdedf122309432c1dd4d70ca4824e229b4be4'})

    def test_domain_changes_report_saved_id_field_and_status(self):
        cohort, header, rows, _ = sample()
        mutations = (
            ('entity',lambda v:v.update(owner='xenon'),'value.owner','argon','xenon'),
            ('topology',lambda v:v.update(parent_saved='0000000000000000'),'value.parent_saved','0000000000000100','0000000000000000'),
            ('motion',lambda v:v['active']['transform'].__setitem__(0,'80000000'),'value.active.transform[0]','00000000','80000000'),
            ('health',lambda v:v.update(stored_hull_bits='4058c00000000000'),'value.stored_hull_bits','4059000000000000','4058c00000000000'))
        for domain, mutate, field, left, right in mutations:
            other=deepcopy(rows); mutate(other[0]['domains'][domain]['value'])
            result=comparison(header,rows,cohort,other)
            self.assertFalse(result['domains'][domain]['semantic_equal'])
            self.assertEqual(result['first_differences'],[dict(saved=cohort.ids[0],domain=domain,status='supported',field=field,
                left=left,right=right)])
        # A later imported owner is kept even without semantic fields.
        other=deepcopy(rows);other[20]=a.unknown_row(cohort.ids[20],'missing_import')
        result=comparison(header,rows,cohort,other)
        self.assertEqual(result['first_differences'][0]['saved'],cohort.ids[20]);self.assertEqual(result['identity_differences'],1)
        self.assertIsNone(result['domains']['motion']['semantic_equal'])

    def test_every_known_field_and_bank_role_changes_exact_bits(self):
        cohort,header,rows,_=sample()
        cases=[]
        for bank in ('active','other'):
            for index in range(12):
                cases.append(('motion',(bank,'transform',index),'80000000' if rows[0]['domains']['motion']['value'][bank]['transform'][index]=='00000000' else '3f800001'))
            for index in range(2):cases.append(('motion',(bank,'references',index),'0000000000000000'))
        cases.extend((('entity',('macro',),'different_macro'),('entity',('owner',),'different_owner'),
                      ('topology',('parent_saved',),'0000000000000000'),('health',('stored_hull_bits',),'8000000000000000')))
        for domain,path,value in cases:
            other=deepcopy(rows);target=other[0]['domains'][domain]['value']
            for part in path[:-1]:target=target[part]
            target[path[-1]]=value
            result=comparison(header,rows,cohort,other)
            with self.subTest(domain=domain,path=path):
                self.assertEqual(result['domains'][domain]['value_differences'],1)
                self.assertEqual(result['difference_count'],1)
                self.assertFalse(result['domains'][domain]['root_equal'])
        distinct=deepcopy(rows);distinct[0]['domains']['motion']['value']['other']['transform'][0]='3f800000'
        swapped=deepcopy(distinct);motion=swapped[0]['domains']['motion']['value'];motion['active'],motion['other']=motion['other'],motion['active']
        self.assertFalse(comparison(header,distinct,cohort,swapped)['domains']['motion']['semantic_equal'])

    def test_unclassified_live_object_preserves_coverage_without_inferred_fields(self):
        cohort,header,rows,_=sample();unknown=deepcopy(rows)
        for domain in w.DOMAINS:
            unknown[0]['domains'][domain]=dict(status='unsupported_layout' if domain=='timing' else 'unsupported_class',value=None)
        result=comparison(header,unknown,cohort)
        self.assertEqual(result['left']['identity_census']['live'],12)
        for domain in w.DOMAINS[:4]:
            self.assertEqual(result['domains'][domain]['left_census']['unsupported_class'],1)
            self.assertIsNone(result['domains'][domain]['semantic_equal'])
        # Never use the generic component parent offset to infer this row.
        unknown[0]['domains']['topology']=deepcopy(rows[0]['domains']['topology'])
        with self.assertRaises(ValueError):encoded(header,unknown,cohort)

    def test_every_status_is_explicit_and_nonlive_never_has_payload(self):
        cohort, header, rows, _=sample()
        for status in w.IDENTITIES[1:]:
            changed=deepcopy(rows);changed[0]=a.unknown_row(cohort.ids[0],status)
            result=comparison(header,changed,cohort)
            self.assertEqual(result['left']['identity_census'][status],sum(r['identity']==status for r in changed))
        for status in ('unsupported_class','unsupported_layout','unresolved_reference','read_failed','changed_during_capture','not_captured'):
            changed=deepcopy(rows);changed[0]['domains']['motion']=dict(status=status,value=None)
            self.assertIsNone(comparison(header,changed,cohort)['domains']['motion']['semantic_equal'])
        changed=deepcopy(rows);changed[0]['identity']='unresolved_runtime'
        with self.assertRaises(ValueError):encoded(header,changed,cohort)

    def test_rejects_missing_duplicate_unordered_rows_and_aliases(self):
        cohort, header, rows, _=sample()
        for changed in (rows[:-1],rows+[rows[-1]],[rows[1],rows[0]]+rows[2:],[rows[0]]+rows):
            with self.assertRaises(ValueError):encoded(header,changed,cohort)
        for saved in (['0000000000000000'],[cohort.ids[0]]*2,list(reversed(cohort.ids))):
            with self.assertRaises(ValueError):w.Cohort(saved)
        with patch.object(w,'MAX_IDS',2):
            with self.assertRaises(ValueError):w.Cohort(iter(cohort.ids))
        for identity in ('1','FFFFFFFFFFFFFFFF','000000000000000g',1,True):
            with self.assertRaises(ValueError):w.Cohort([identity])

    def test_unresolved_reference_is_not_null_and_opaque_fields_rejected(self):
        cohort, header, rows, _=sample()
        mutations=[lambda r:r['domains']['topology']['value'].update(parent_saved='0000000000000200'),
                   lambda r:r['domains']['motion']['value']['active']['references'].__setitem__(0,'0000000000000200'),
                   lambda r:r['domains']['motion']['value'].update(pointer='12345678'),
                   lambda r:r['domains']['entity']['value'].update(**{'class':'station'}),
                   lambda r:r['domains']['health']['value'].update(stored_hull_bits='7ff0000000000000'),
                   lambda r:r['domains']['motion']['value']['active']['transform'].__setitem__(0,'7fc00000'),
                   lambda r:r['domains']['timing'].update(status='supported',value={'opaque':'00'}),
                   lambda r:r['domains']['timing'].update(value={'clock':'0000000000000000'}),
                   lambda r:r['domains']['entity'].update(status='unresolved_import',value=None)]
        for mutate in mutations:
            other=deepcopy(rows);mutate(other[0])
            with self.assertRaises(ValueError):encoded(header,other,cohort)
        other=deepcopy(rows);other[0]['domains']['topology']=dict(status='unresolved_reference',value=None)
        result=comparison(header,rows,cohort,other)
        self.assertFalse(result['domains']['topology']['coverage_equal'])
        self.assertEqual(result['first_differences'][0]['field'],'status')

    def test_streaming_terminator_census_and_header_mutants_fail(self):
        cohort, header, rows, _=sample();raw=encoded(header,rows,cohort);lines=raw.splitlines(True)
        bad=[b''.join(lines[:-1]),raw[:-1],raw+b'\n',lines[0]+b''.join(lines[2:]),raw.replace(b'"rows":26',b'"rows":25'),
             raw.replace(b'"rows":26',b'"rows":true'),raw.replace(b'"rows":26',b'"rows":26,"rows":26'),
             lines[0]+b'X'*(w.MAX_LINE+1)+b'\n',raw.replace(b'"roots":{',b'"roots":{"unknown":"0",')]
        for value in bad:
            with self.assertRaises(ValueError):w.compare(io.BytesIO(raw),io.BytesIO(value),cohort)
        for key,value in (('snapshot_sha256','0'*64),('content_sha256','0'*64),('tick',1),('game_bits','401c000000000001')):
            with self.assertRaises(ValueError):w.compare(io.BytesIO(raw),io.BytesIO(encoded(header|{key:value},rows,cohort)),cohort)
        for key,value in (('stage','load_complete'),('cohort_count',True),('cohort_sha256','0'*64),('tick',True)):
            with self.assertRaises(ValueError):encoded(header|{key:value},rows,cohort)

    def test_bounded_first_differences_still_validate_entire_stream(self):
        cohort,header,rows,_=sample();other=[a.unknown_row(s,'missing_import') for s in cohort.ids]
        result=comparison(header,rows,cohort,other,difference_limit=2)
        self.assertTrue(result['differences_truncated']);self.assertEqual(len(result['first_differences']),2)
        self.assertGreater(result['difference_count'],2)
        broken=encoded(header,other,cohort).splitlines(True)
        with self.assertRaises(ValueError):w.compare(io.BytesIO(encoded(header,rows,cohort)),io.BytesIO(b''.join(broken[:-1])),cohort,2)
        for limit in (0,129,True):
            with self.assertRaises(ValueError):comparison(header,rows,cohort,difference_limit=limit)

    def test_complete_supported_domain_and_partial_domain_are_independent(self):
        cohort,header,rows,_=sample()
        # All remaining IDs have explicitly captured and proved ShipSmall fields;
        # this synthetic input tests comparison semantics, not native coverage.
        rows=[deepcopy(rows[0]) | {'saved':saved} for saved in cohort.ids]
        result=comparison(header,rows,cohort)
        for domain in w.DOMAINS[:4]:
            self.assertTrue(result['domains'][domain]['complete'])
            self.assertTrue(result['domains'][domain]['semantic_equal'])
        self.assertIsNone(result['domains']['timing']['semantic_equal'])
        other=deepcopy(rows);other[-1]['domains']['health']=dict(status='read_failed',value=None)
        result=comparison(header,rows,cohort,other)
        self.assertTrue(result['domains']['motion']['semantic_equal'])
        self.assertIsNone(result['domains']['health']['semantic_equal'])
        self.assertEqual(result['domains']['health']['comparable'],len(cohort.ids)-1)

    def test_bounded_parser_refuses_deep_json_nonfinite_and_after_end_calls(self):
        cohort,header,rows,_=sample();raw=encoded(header,rows,cohort)
        for value in (b'{"header":'+b'['*1500+b']'*1500+b'}\n',b'{"header":NaN}\n',b'\xff\n'):
            with self.assertRaises(ValueError):w.Reader(io.BytesIO(value),cohort)
        reader=w.Reader(io.BytesIO(raw),cohort)
        with self.assertRaises(ValueError):reader.finish()
        for _ in cohort.ids:reader.row()
        with self.assertRaises(ValueError):reader.row()
        reader.finish()
        with self.assertRaises(ValueError):reader.finish()
        with self.assertRaises(ValueError):reader.row()


class ExistingCaptureAdapterTests(unittest.TestCase):
    def test_linux_import_format_full_width_unresolved_and_failures(self):
        make=lambda rows:b'X4IMP1\0\0'+struct.pack('<Q',len(rows))+b''.join(struct.pack('<QQ',*row) for row in rows)
        raw=make([(1,0xffff000000000001),(2,0),(0xffffffffffffffff,0xffff000000000002)])
        result=a.read_imports(io.BytesIO(raw))
        self.assertEqual(result['ffffffffffffffff'],0xffff000000000002);self.assertEqual(result['0000000000000002'],0)
        for bad in (raw[:-1],raw+b'0',b'bad',make([]),make([(1,10),(2,10)]),make([(2,10),(1,11)]),make([(1,10),(1,11)])):
            with self.assertRaises(ValueError):a.read_imports(io.BytesIO(bad))
        with patch.object(a,'MAX_IDS',2):
            with self.assertRaises(ValueError):a.read_imports(io.BytesIO(raw))

    def test_adapter_preserves_strict_frame_and_refuses_inventory_filtering(self):
        cohort,header,rows,imports=sample();original=frame();before=p.encode_frame(original,contract())
        out_header,out_rows=a.from_portable(original,contract(),cohort,imports,'linux');encoded(out_header,out_rows,cohort)
        self.assertEqual(p.encode_frame(original,contract()),before)
        smaller=w.Cohort(list(imports)[:12])
        with self.assertRaises(ValueError):a.from_portable(original,contract(),smaller,imports,'linux')
        changed=deepcopy(original);changed['commands'][0]='00000001'
        with self.assertRaises(ValueError):a.from_portable(changed,contract(),cohort,imports,'linux')
        for change in ('unresolved','alias','missing'):
            bad=dict(imports)
            if change=='unresolved':bad[cohort.ids[0]]=0
            elif change=='alias':bad[cohort.ids[1]]=bad[cohort.ids[0]]
            else:del bad[cohort.ids[0]]
            with self.assertRaises(ValueError):a.from_portable(original,contract(),cohort,bad,'linux')

    def test_missing_nonfixture_import_is_explicit_and_cli_is_fresh_output_only(self):
        cohort,_,_,imports=sample();missing=dict(imports);del missing[cohort.ids[20]]
        header,rows=a.from_portable(frame(),contract(),cohort,missing,'linux');rows=list(rows)
        self.assertEqual(rows[20],a.unknown_row(cohort.ids[20],'missing_import'))
        self.assertIsNone(comparison(header,rows,cohort)['domains']['entity']['semantic_equal'])
        with tempfile.TemporaryDirectory() as directory:
            directory=Path(directory)
            inputs={'frame':frame(),'contract':contract(),'cohort':list(cohort.ids)}
            for name,value in inputs.items():
                (directory/name).write_text(json.dumps(value),encoding='ascii')
            (directory/'imports').write_bytes(b'X4IMP1\0\0'+struct.pack('<Q',len(missing))+
                b''.join(struct.pack('<QQ',int(saved,16),runtime) for saved,runtime in missing.items()))
            args=['adapter','--source','linux','--out',str(directory/'out')]
            for name in (*inputs,'imports'):args.extend(('--'+name,str(directory/name)))
            stdout=io.StringIO()
            with patch.object(sys,'argv',args),redirect_stdout(stdout):a.main()
            output=(directory/'out').read_bytes()
            self.assertEqual(output,encoded(header,rows,cohort))
            self.assertTrue(json.loads(stdout.getvalue())['diagnostic_only'])
            with patch.object(sys,'argv',args):
                with self.assertRaises(FileExistsError):a.main()
            self.assertEqual((directory/'out').read_bytes(),output)
            with self.assertRaises(ValueError):a.bounded_json(directory/'frame',10)
            (directory/'duplicate').write_text('{"x":1,"x":2}',encoding='ascii')
            with self.assertRaises(ValueError):a.bounded_json(directory/'duplicate',100)


if __name__=='__main__':unittest.main()
