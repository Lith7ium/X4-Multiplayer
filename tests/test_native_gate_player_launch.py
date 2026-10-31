"""Player-only launch RNG receipts must prove identity, restoration and timing."""
from pathlib import Path
import hashlib
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/dev'))
from native_player_launch_evidence import canonical,evidence,seed


class PlayerLaunchEvidenceTests(unittest.TestCase):
    context=('0000000000000001',)+('0000000000000004',)*3

    def fixture(self):
        value=seed(2,4,5,1)
        a=(f'index=1 tick=2 kind=1 ship={self.context[3]} gun=0000000000000005 call=1 spawn=0 caller=0000000000000007 '
           f'seed={value:016x} after={value+3:016x} restored=0000000000000007 original=1 valid=1 pre='+'0'*48+' post='+'0'*48)
        b=(f'index=2 tick=2 kind=2 ship={self.context[3]} gun=0000000000000005 call=1 spawn=1 caller=0000000000000000 '
           'seed=0000000000000000 after=0000000000000000 restored=0000000000000000 original=0 valid=1 pre='+'0'*48+' post='+'4020000000000000'*3)
        digest='0'*64
        for row in (a,b):digest=hashlib.sha256((digest+'\n'+canonical(row)).encode()).hexdigest()
        return ('NATIVE_GATE PLAYER_LAUNCH_ENABLED tick=1 schema=1 domain=504c5957504e3031 bullet_rng=0\n'
                'NATIVE_GATE PLAYER_FIRE_STATE tick=1 data=other|PLAYER_LAUNCH_1:1:504c5957504e3031:0:0:0:0:0:'+'0'*64+'\n'
                'NATIVE_GATE PLAYER_FIRE_EVENT index=1 tick=2 kind=1 ship='+self.context[3]+' gun=0000000000000005 spawn=1\n'
                'NATIVE_GATE PLAYER_FIRE_EVENT index=2 tick=2 kind=2 ship='+self.context[3]+' gun=0000000000000005 spawn=1 target=0000000000000000 call=1 amount=0000000000000000 absorbed=0000000000000000 before=4020000000000000\n'
                'NATIVE_GATE PLAYER_LAUNCH_EVENT '+a+'\nNATIVE_GATE PLAYER_LAUNCH_EVENT '+b+'\n'
                'NATIVE_GATE PLAYER_FIRE_STATE tick=2 data=other|PLAYER_LAUNCH_1:1:504c5957504e3031:2:0:1:1:0:'+digest+'\n'
                'NATIVE_GATE PLAYER_LAUNCH_STOP schema=1 domain=504c5957504e3031 count=2 failures=0 scopes=1 restored=1 bullet_rng=0 emitted=2')

    def test_saved_identity_tuple_matches_native_golden(self):
        self.assertEqual(seed(7,124,125,1),0x4e23dfabd32d0b4a)
        self.assertEqual(len({seed(7,124,125,1),seed(8,124,125,1),seed(7,125,125,1),seed(7,124,126,1),seed(7,124,125,2)}),5)

    def test_full_receipt_chain_and_actual_creation_times(self):
        errors=[];trace,result=evidence(self.fixture(),1,self.context,2,errors)
        self.assertEqual(errors,[]);self.assertEqual(len(trace),2)
        self.assertEqual(result['initializations'],1);self.assertTrue(result['caller_rng_restored'])
        self.assertFalse(result['bullet_move_rng_reseeded'])

    def test_ambient_rng_is_checked_locally_and_never_a_shared_seed(self):
        a,ea=evidence(self.fixture(),1,self.context,2,[])
        errors=[];b,_=evidence(self.fixture().replace('caller=0000000000000007','caller=0000000000000009').replace('restored=0000000000000007','restored=0000000000000009'),2,self.context,2,errors)
        self.assertEqual(errors,[]);self.assertEqual(a,b)
        errors=[];evidence(self.fixture().replace('restored=0000000000000007','restored=0000000000000009'),1,self.context,2,errors)
        self.assertTrue(any('invalid native launch receipt' in e for e in errors))

    def test_tampered_effect_and_domain_fail_even_if_counter_is_plausible(self):
        for before,after in [('post=4020000000000000','post=4022000000000000'),('domain=504c5957504e3031','domain=504c5957504e3032'),('valid=1','valid=0'),('original=1','original=0'),('call=1 spawn=0','call=2 spawn=0'),('bullet_rng=0','bullet_rng=1')]:
            with self.subTest(before=before):
                errors=[];evidence(self.fixture().replace(before,after),1,self.context,2,errors);self.assertTrue(errors)

    def test_missing_initialization_or_tick_cannot_pass(self):
        for fragment in ['NATIVE_GATE PLAYER_LAUNCH_EVENT index=2','NATIVE_GATE PLAYER_FIRE_STATE tick=1']:
            errors=[];evidence('\n'.join(line for line in self.fixture().splitlines() if not line.startswith(fragment)),1,self.context,2,errors)
            self.assertTrue(errors)


if __name__=='__main__':unittest.main()
