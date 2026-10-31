"""Frozen dependency joins and source-only imports on owned synthetic files."""
from copy import deepcopy
import hashlib,importlib,importlib.util,json,subprocess,sys,types,unittest
from pathlib import Path
from unittest.mock import patch
import test_native_gate_getup_diagnostic_runner as fixtures
from native_getup_diagnostic_contract import check,file_record
import native_getup_diagnostic_execution as execution


class ExecutionReceiptTests(unittest.TestCase):
    setUp=fixtures.DiagnosticRunnerTests.setUp

    def test_changed_missing_local_lane_autostart_and_native_helper_refuse(self):
        targets=[self.source_root/'tools/dev/native_getup_diagnostic.py',self.lane_root/'dev/lane/lanecore.py',
                 self.lane_root/'dev/lane/lanewin.py',self.lane_root/'native/engine/signatures.py',
                 self.lane_root/'dev/lane/x4lane_autostart/content.xml',self.x4lock]
        check(self.value,True)
        for path in targets:
            original=path.read_bytes()
            for missing in (False,True):
                with self.subTest(path=path.name,missing=missing):
                    if missing:path.unlink()
                    else:path.write_bytes(original+b' changed')
                    with self.assertRaises((ValueError,OSError)):check(self.value,True)
                    path.write_bytes(original)
        check(self.value,True)

    def test_receipt_bytes_path_and_current_root_are_bound(self):
        reference=self.value['execution_receipt'];execution.load(reference)
        wrong=dict(reference,sha256='0'*64)
        with self.assertRaisesRegex(ValueError,'path/digest'):execution.load(wrong)
        wrong=dict(reference,path=str(self.execution_path.parent/'missing.json'))
        with self.assertRaises(OSError):execution.load(wrong)
        wrong=dict(reference,path=str(self.execution_path.parent/'sub/../execution.json'))
        (self.execution_path.parent/'sub').mkdir()
        with self.assertRaisesRegex(ValueError,'path/digest'):execution.load(wrong)
        with self.assertRaisesRegex(ValueError,'current source ROOT'):execution.load(reference,current_root=self.path/'foreign')
        original=self.execution_path.read_bytes();self.execution_path.write_bytes(original+b' ')
        with self.assertRaisesRegex(ValueError,'path/digest'):execution.load(reference)

    def test_cached_module_and_actual_runtime_helper_locations_refuse(self):
        value=json.loads(self.execution_path.read_text())
        paths=types.SimpleNamespace(repo=self.lane_root,autostart_source=self.lane_root/'dev/lane/x4lane_autostart',x4lock_exe=self.x4lock)
        lane=types.SimpleNamespace(ROOT=self.lane_root,core=types.SimpleNamespace(__file__=str(self.lane_root/'dev/lane/lanecore.py')))
        expected={name:types.SimpleNamespace(__file__=str(self.lane_root/'dev/lane'/(name+'.py'))) for name in ('lanecore','lanewin')}
        with patch.dict(sys.modules,expected):
            execution.verify_runtime(value,lane,paths)
            for name in expected:
                with patch.dict(sys.modules,{name:types.SimpleNamespace(__file__=str(self.path/'wrong.py'))}),self.assertRaisesRegex(ValueError,'cached .* origin'):
                    execution.verify_runtime(value,lane,paths)
            paths.x4lock_exe=self.tool
            with self.assertRaisesRegex(ValueError,'x4lock location'):execution.verify_runtime(value,lane,paths)

    def test_poisoned_matching_caches_never_execute_or_change(self):
        from importlib._bootstrap_external import _code_to_timestamp_pyc
        policy=sys.modules['x4lan_getup_source_only'];policy.bound=None;policy.bound_roots=[]
        for index,root in enumerate((self.source_root/'tools/dev',self.lane_root/'dev/lane')):
            name='getup_owned_cache_'+str(index);path=root/(name+'.py');good=b"VALUE='source'\n";bad=b"VALUE='cached'\n"
            self.assertEqual(len(good),len(bad));path.write_bytes(good)
            cache=Path(importlib.util.cache_from_source(str(path)));cache.parent.mkdir(exist_ok=True)
            poisoned=bytes(_code_to_timestamp_pyc(compile(bad,str(path),'exec'),int(path.stat().st_mtime),len(good)));cache.write_bytes(poisoned)
            # Establish that an ordinary interpreter really consumes this cache.
            command='import sys;sys.path.insert(0,'+repr(str(root))+');import '+name+';print('+name+'.VALUE)'
            result=subprocess.run([sys.executable,'-c',command],capture_output=True,text=True,check=True)
            self.assertEqual(result.stdout.strip(),'cached')
            policy.bound=None;policy.bind([execution.inventory(root)])
            with patch.object(sys,'path',[str(root)]+sys.path):
                module=importlib.import_module(name);self.assertEqual(module.VALUE,'source');sys.modules.pop(name)
            self.assertEqual(cache.read_bytes(),poisoned)
            self.assertEqual(policy.compiled[str(path.resolve())],hashlib.sha256(good).hexdigest())

    def test_source_only_explicit_lane_and_sourceless_refusal(self):
        from importlib._bootstrap_external import _code_to_timestamp_pyc
        policy=sys.modules['x4lan_getup_source_only'];policy.bound=None;policy.bound_roots=[]
        path=self.path/'explicit.py';data=b"VALUE='source'\n";path.write_bytes(data)
        module=types.ModuleType('explicit');policy.execute(module,path,hashlib.sha256(data).hexdigest());self.assertEqual(module.VALUE,'source')
        path.write_bytes(b"VALUE='unsafe'\n")
        with self.assertRaises(ImportError):policy.execute(module,path,hashlib.sha256(data).hexdigest())
        name='getup_owned_sourceless';cache=self.path/(name+'.pyc');sentinel=self.path/'sourceless-executed'
        code=compile('from pathlib import Path;Path('+repr(str(sentinel))+').write_text("bad")',str(cache),'exec')
        cache.write_bytes(bytes(_code_to_timestamp_pyc(code,0,0)))
        with patch.object(sys,'path',[str(self.path)]+sys.path),self.assertRaisesRegex(ImportError,'sourceless'):
            importlib.import_module(name)
        self.assertFalse(sentinel.exists())

    def test_compilation_history_is_sticky_after_changed_source(self):
        policy=sys.modules['x4lan_getup_source_only'];policy.bound=None;policy.bound_roots=[]
        path=self.path/'history.py';first=b"VALUE='first'\n";changed=b"VALUE='other'\n"
        path.write_bytes(first);module=types.ModuleType('history')
        digest=hashlib.sha256(first).hexdigest();policy.execute(module,path,digest)
        policy.execute(module,path,digest)  # Identical repeated execution is permitted.
        path.write_bytes(changed)
        with self.assertRaisesRegex(ImportError,'changed between executions'):
            policy.execute(module,path,hashlib.sha256(changed).hexdigest())
        self.assertEqual(module.VALUE,'first');self.assertEqual(policy.compiled[str(path.resolve())],digest)
        path.write_bytes(first)
        with self.assertRaisesRegex(ImportError,'changed between executions'):policy.execute(module,path,digest)
        with self.assertRaisesRegex(ValueError,'changed between executions'):policy.verify([execution.inventory(self.path)])

    def test_unrecorded_or_changed_compiled_module_cannot_gain_attestation(self):
        policy=sys.modules['x4lan_getup_source_only'];tree=execution.inventory(self.source_root/'tools/dev')
        path=self.source_root/'tools/dev/native_getup_diagnostic.py'
        recorded=policy.compiled.pop(str(path.resolve()))
        with patch.dict(sys.modules,{'getup_unrecorded':types.SimpleNamespace(__file__=str(path))}),self.assertRaisesRegex(ValueError,'unrecorded'):
            policy.verify([tree])
        policy.compiled[str(path.resolve())]=recorded
        with patch.dict(sys.modules,{'native_getup_diagnostic':types.SimpleNamespace(__file__=str(self.path/'wrong.py'))}),self.assertRaisesRegex(ValueError,'cached diagnostic module origin'):
            policy.verify([tree])
        before=dict(policy.compiled)
        try:
            policy.compiled[str(path.resolve())]='0'*64
            with self.assertRaisesRegex(ValueError,'executed .* differs'):policy.verify([tree])
        finally:policy.compiled.clear();policy.compiled.update(before)

if __name__=='__main__':unittest.main()
