"""Identity diagnostics must reject partial, aliased and unrestored captures."""
import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('world_identity_validator',Path(__file__).resolve().parents[1]/'tools/dev/validate_native_gate.py')
validator=importlib.util.module_from_spec(spec)
spec.loader.exec_module(validator)


class WorldIdentityEvidenceTests(unittest.TestCase):
    def fixture(self):
        return ''.join(f'NATIVE_GATE WORLD_ACTOR id={i} saved={100+i:016x}\n' for i in range(1,13))+ \
            'NATIVE_GATE WORLD_IDENTITY_STOP calls=400000 count=390000 late=0 restored=1 failures=0\n'

    def test_complete_capture(self):
        errors=[]
        rows=validator.world_identity_evidence(self.fixture(),1,errors)
        self.assertEqual(errors,[])
        self.assertEqual(len(rows),12)

    def test_missing_duplicate_zero_conflict_and_failed_capture(self):
        source=self.fixture()
        bad=[source.split('\n',1)[1],source.replace('id=12','id=11'),
             source.replace('saved=0000000000000065','saved=0000000000000000'),
             source.replace('saved=0000000000000065','saved=0000000000000066'),
             source.replace('restored=1','restored=0'),source.replace('failures=0','failures=1'),
             source.replace('count=390000','count=524289'),source.replace('calls=400000','calls=389999')]
        for log in bad:
            with self.subTest(log=log):
                errors=[];validator.world_identity_evidence(log,1,errors)
                self.assertTrue(errors)
