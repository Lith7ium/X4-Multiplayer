from pathlib import Path
import sys
import unittest
from lupa import LuaRuntime

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/dev'))
import native_registry_evidence as registry

class NativeRegistryEvidenceTests(unittest.TestCase):
    def capture(self):
        handle='0000000500000003'
        lines=[]
        for tick in range(6,9):
            lines.append(f'NATIVE_GATE SHIP_REGISTRY tick={tick} macro=ship_arg_xl_builder_01_a_macro owner=player status=SHIP_REGISTRY_1;handle={handle};slot=15;reserved_tick=5;bound_tick=6;prepared_tick=6;first_update_tick=6;first_update_kind=2;late_updates=0;identity_ok=1')
        lines.append(f'NATIVE_GATE SHIP_REGISTRY_STOP count=1 failures=0 data=15/{handle}/5/6/6/6/2/0')
        lines.append(f'NATIVE_GATE SHIP_WEAPON_SCOPE handle={handle} slot=15 weapon_slots=0 registered=0 turret_slots=8 occupied_turrets=0')
        rng=registry.seed(0x4e41565f55504454,6,int(handle,16))
        lines.append(f'NATIVE_GATE SHIP_NAV_FIRST data=15/{handle}/6/1/1/401c660f04c756b3/0000000000000000/401eb3c98fc756b3/{rng:016x}/0000000000000001')
        return '\n'.join(lines)+'\n'

    def test_complete_declared_registry_and_creation_seed(self):
        log=self.capture();errors=[]
        self.assertEqual(registry.ship_ids(log),frozenset(range(1,13))|{15})
        self.assertEqual(len(registry.evidence(log,1,8,errors)),3)
        self.assertEqual(len(registry.navigation(log,1,errors)),1)
        self.assertEqual(errors,[])
        self.assertEqual(registry.ship_ids(''),frozenset(range(1,13)))
        self.assertNotEqual(registry.seed(7,6,0x500000003),registry.seed(7,6,0x500000004))

    def test_invalid_boundary_identity_scope_or_missing_rows_are_refused(self):
        log=self.capture()
        for before,after in [('data=15/','data=13/'),('owner=player','owner=argon',),
            ('late_updates=0','late_updates=1'),('identity_ok=1','identity_ok=0'),
            ('prepared_tick=6','prepared_tick=7'),('occupied_turrets=0','occupied_turrets=1'),
            ('failures=0','failures=1'),('registered=0','registered=1')]:
            with self.subTest(before=before):
                errors=[];registry.evidence(log.replace(before,after,1),1,8,errors);self.assertTrue(errors)
        errors=[];registry.evidence('\n'.join(log.splitlines()[1:]),1,8,errors);self.assertTrue(errors)
        errors=[];registry.navigation(log.replace('/6/1/1/401c','/6/2/1/401c'),1,errors);self.assertTrue(errors)
        errors=[];registry.navigation(log.replace(f'{registry.seed(0x4e41565f55504454,6,0x500000003):016x}','0000000000000000'),1,errors);self.assertTrue(errors)

    def test_loaded_deadline_diagnostic_is_retained_separately(self):
        errors=[];a=registry.navigation(self.capture(),1,errors)
        b=registry.navigation(self.capture().replace('/0000000000000000/401eb3','/0000000000000002/401eb3'),2,errors)
        self.assertEqual(errors,[]);self.assertNotEqual(a,b)
        self.assertEqual(registry.navigation_results(a),registry.navigation_results(b))
        b[15]['deadline_after']='0000000000000003'
        self.assertNotEqual(registry.navigation_results(a),registry.navigation_results(b))

    def test_first_update_is_immutable_and_weapon_slot_must_exist(self):
        log=self.capture();errors=[]
        registry.evidence(log.replace('first_update_kind=2','first_update_kind=3',1),1,8,errors)
        self.assertTrue(errors)
        log=log.replace('weapon_slots=0 registered=0','weapon_slots=1 registered=1')+'NATIVE_GATE WEAPON_REGISTER id=15 slot=2 macro=weapon_macro\n'
        errors=[];registry.evidence(log,1,8,errors);self.assertTrue(errors)

class NativeRegistryWeaponHelperTests(unittest.TestCase):
    def setUp(self):
        source=(ROOT/'tools/dev/native_gate_probe.lua').read_text()
        helper=source[source.index('local function enroll_dynamic_weapons('):source.index('local function trace_gravidar(')]
        self.lua=LuaRuntime(unpack_returned_tuples=True)
        self.lua.execute('''
          weapons={};logs={};stopped=0;registered=0;weapon_count=0;turret_count=8;occupied=false
          log=function(s) logs[#logs+1]=s end
          ffi={C={GetNumUpgradeSlots=function(object,group,kind) return kind=='weapon' and weapon_count or turret_count end,
            GetUpgradeSlotCurrentComponent=function(object,kind,slot)
              if kind=='turret' then return occupied and slot==1 and 123 or 0 end
              return '9007199254740995ULL'
            end,IsComponentOperational=function() return true end}}
          gate={register_weapon=function(id,actor,slot) assert(id=='9007199254740995' and actor==15 and slot==1);registered=registered+1;return 1 end,
            stop=function() stopped=stopped+1 end}
          ConvertStringToLuaID=function(id) return id end
          GetComponentData=function() return 'weapon_macro' end
        ''')
        self.lua.execute(helper+'\nhelper=enroll_dynamic_weapons')

    def test_empty_turret_capacity_is_checked_and_allowed(self):
        self.assertEqual(self.lua.globals().helper(999,15,'0000000500000003'),1)
        self.assertEqual(self.lua.globals().stopped,0)
        self.assertTrue(any('turret_slots=8 occupied_turrets=0' in line for line in self.lua.globals().logs.values()))

    def test_occupied_turret_refuses_before_weapon_registration(self):
        self.lua.execute('occupied=true;weapon_count=1')
        result=self.lua.globals().helper(999,15,'0000000500000003')
        self.assertIsNone(result[0]);self.assertEqual(self.lua.globals().stopped,1);self.assertEqual(self.lua.globals().registered,0)

    def test_exact_weapon_native_id_is_preserved(self):
        self.lua.execute('weapon_count=1')
        self.assertEqual(self.lua.globals().helper(999,15,'0000000500000003'),1)
        self.assertEqual(self.lua.globals().registered,1)

if __name__=='__main__':unittest.main()
