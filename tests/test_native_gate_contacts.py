"""Contact evidence rejects partial traces and compares physical inputs."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/dev'))
import validate_native_gate as v


class ContactEvidenceTests(unittest.TestCase):
    def fixture(self):
        body='1:00000002:abcd:2b813c8:2a12340:'+('0'*56)+':'+('0'*48)
        settings='0'*48+':00:'+'0'*48
        return ('NATIVE_GATE CONTACT tick=10 ordinal=1 added=1 count=2 subshape=00000000,00000000 '
                'base='+('0'*24)+' normal='+('0'*24)+' depth=00000000 '
                'body0='+body+' points0='+('0'*48)+' settings0='+settings+' '
                'body1='+body.replace('1:','0:',1)+' points1='+('0'*48)+' settings1='+settings+'\n'
                'NATIVE_GATE CONTACT_STOP calls=1 restored=1 failures=0\n')

    def test_local_identity_is_excluded_but_geometry_and_settings_are_compared(self):
        log=self.fixture();errors=[];expected=v.contact_evidence(log,1,10,errors)
        self.assertEqual(errors,[]);self.assertEqual(len(expected),1)
        self.assertEqual(v.contact_evidence(log.replace(':abcd:',':1234:'),2,10,[]),expected)
        self.assertNotEqual(v.contact_evidence(log.replace('depth=00000000','depth=3f800000'),2,10,[]),expected)
        self.assertNotEqual(v.contact_evidence(log.replace(':00:',':01:'),2,10,[]),expected)

    def test_missing_truncated_unrestored_and_invalid_traces_fail(self):
        for before,after in [('calls=1','calls=2'),('restored=1','restored=0'),('failures=0','failures=1'),
                             ('count=2','count=65'),('count=2','count=1'),('tick=10','tick=11'),
                             ('ordinal=1','ordinal=2'),(':00:',':02:'),('points1=','missing=')]:
            with self.subTest(change=before):
                errors=[];v.contact_evidence(self.fixture().replace(before,after),1,10,errors)
                self.assertTrue(errors)

    def test_order_diagnostic_preserves_tick_physics_and_multiplicity(self):
        first=v.contact_evidence(self.fixture(),1,10,[])[0]
        second=dict(first,ordinal='2',depth='3f800000')
        ordered=[first,second];reordered=[dict(second,ordinal='1'),dict(first,ordinal='2')]
        self.assertNotEqual(ordered,reordered)
        self.assertEqual(v.contact_multiset(ordered),v.contact_multiset(reordered))
        for other in ([first],[first,first],[first,dict(second,tick='11')],[first,dict(second,depth='00000000')]):
            self.assertNotEqual(v.contact_multiset(ordered),v.contact_multiset(other))

    def test_dynamic_contact_ids_require_declaration_and_keep_ship_shape_roles(self):
        declared='NATIVE_GATE SHIP_REGISTRY_STOP count=1 failures=0 data=15/0000000500000003/5/6/6/6/2/0\n'
        contact=self.fixture().replace('body0=1:','body0=15:')
        errors=[];self.assertEqual(len(v.contact_evidence(contact+declared,1,10,errors)),1);self.assertEqual(errors,[])
        for invalid in (contact,contact.replace('body0=15:','body0=16:')+declared):
            errors=[];v.contact_evidence(invalid,1,10,errors);self.assertTrue(errors)
        shape='NATIVE_GATE SHAPE_LIFETIME tick=7 ordinal=1 id=15 which=0 seen=0 initialized=1 due_before=0000000000000000 before=0:0:0:00000000 after=1:1000:2000:0080001d\nNATIVE_GATE SHAPE_LIFETIME_STOP calls=1 failures=0\n'
        errors=[];self.assertEqual(v.shape_lifetime_evidence(shape+declared,1,10,errors),1);self.assertEqual(errors,[])
        errors=[];v.shape_lifetime_evidence(shape.replace('which=0','which=2')+declared,1,10,errors);self.assertTrue(errors)

    def test_traversal_trace_is_complete_and_separate_from_contact_gate(self):
        log=self.fixture();contacts=v.contact_evidence(log,1,10,[])
        order='NATIVE_GATE CONTACT_ORDER tick=10 ordinal=1 body0=00000007:00000009:0:0004:00:02:02:15 body1=ffffffff:ffffffff:0:0000:00:00:00:04\n'
        errors=[];trace=v.contact_order_evidence(log+order,1,contacts,errors)
        self.assertEqual(errors,[]);self.assertEqual(len(trace),1)
        different=order.replace('00000007:','00000008:')
        self.assertNotEqual(trace,v.contact_order_evidence(log+different,2,contacts,[]))
        self.assertEqual(contacts,v.contact_evidence(log+different,2,10,[]))
        for malformed in ('',order+order,order.replace('ordinal=1','ordinal=2'),
                          order.replace('tick=10','tick=9'),order.replace(':0:0004',':3:0004'),
                          order.replace(':02:02:15',':02:03:15'),order.replace(' body1=',' extra=1 body1=')):
            with self.subTest(trace=malformed):
                errors=[];v.contact_order_evidence(log+malformed,1,contacts,errors)
                self.assertTrue(errors)

    def test_property_trace_requires_complete_valid_restored_native_calls(self):
        body='0080000b:00000007:00000009:0:0001:02:01:00000008:00000000:15'
        log='NATIVE_GATE CONTACT_PROPERTIES tick=10 ordinal=1 id=1 before0='+body+' before1=none after0='+body+' after1=none stack=51bda5,123,0,0,0,0,0,0\nNATIVE_GATE CONTACT_PROPERTIES_STOP calls=1 restored=1 failures=0\n'
        errors=[];self.assertEqual(v.contact_properties_evidence(log,1,10,errors),1);self.assertEqual(errors,[])
        for before,after in [('calls=1','calls=2'),('restored=1','restored=0'),('failures=0','failures=1'),
                             ('ordinal=1','ordinal=2'),('tick=10','tick=11'),('id=1','id=0'),
                             (':0:0001',':3:0001'),(':02:01:',':03:01:'),('51bda5','ff51bda5')]:
            with self.subTest(change=before):
                errors=[];v.contact_properties_evidence(log.replace(before,after),1,10,errors);self.assertTrue(errors)

    def test_shape_lifetime_trace_requires_an_unbroken_local_identity_chain(self):
        log='NATIVE_GATE SHAPE_LIFETIME tick=2 ordinal=1 id=10 which=0 seen=0 initialized=1 due_before=0000000000000000 before=0:0:0:00000000 after=1:1000:2000:0080001d\nNATIVE_GATE SHAPE_LIFETIME tick=5 ordinal=2 id=10 which=0 seen=1 initialized=0 due_before=0000000000000000 before=1:1000:2000:0080001d after=1:1000:2000:0100001d\nNATIVE_GATE SHAPE_LIFETIME_STOP calls=2 failures=0\n'
        errors=[];self.assertEqual(v.shape_lifetime_evidence(log,1,10,errors),2);self.assertEqual(errors,[])
        for before,after in [('calls=2','calls=3'),('failures=0','failures=1'),('ordinal=2','ordinal=3'),
                             ('tick=5','tick=11'),('which=0','which=2'),('seen=1','seen=2'),
                             ('before=1:1000','before=1:1001'),('after=1:1000:2000:0100001d','after=1:1000:2000:0080001d')]:
            with self.subTest(change=before):
                errors=[];v.shape_lifetime_evidence(log.replace(before,after),1,10,errors);self.assertTrue(errors)

    def test_shape_lifetime_identity_distinguishes_a_recycled_holder(self):
        log='NATIVE_GATE SHAPE_LIFETIME tick=2 ordinal=1 id=10 which=0 seen=0 initialized=1 due_before=0000000000000000 before=0:0:0:00000000:0000000000000000 after=1:1000:2000:0080001d:0000000000000100\nNATIVE_GATE SHAPE_LIFETIME tick=5 ordinal=2 id=10 which=0 seen=0 initialized=1 due_before=0000000000000000 before=1:1000:2000:0080001d:0000000000000100 after=1:1000:2000:0100001d:0000000000000200\nNATIVE_GATE SHAPE_LIFETIME_STOP calls=2 failures=0\n'
        errors=[];self.assertEqual(v.shape_lifetime_evidence(log,1,10,errors,True),2);self.assertEqual(errors,[])
        for bad in (log.replace(':0000000000000200',':0000000000000000'),
                    log.replace(':0000000000000200',''),log.replace(':0000000000000100',':01')):
            errors=[];v.shape_lifetime_evidence(bad,1,10,errors,True);self.assertTrue(errors)


if __name__=='__main__':unittest.main()
