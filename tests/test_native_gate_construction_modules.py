from pathlib import Path
import sys
import unittest

from lupa.luajit21 import LuaRuntime

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/dev'))
from native_construction_modules_evidence import sample, sequence

SOURCE = (ROOT / 'tools/dev/native_construction_modules.lua').read_text()
HANDLE = '0000000500000001'


class ConstructionModuleCensus(unittest.TestCase):
    def fixture(self):
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.execute('''
          local ffi=require('ffi');native={};local proxy={C=native}
          for _,k in ipairs({'cdef','typeof','istype','new'}) do proxy[k]=ffi[k] end
          require=function() return proxy end
          station=ffi.new('uint64_t',99);module_id=ffi.new('uint64_t',101)
          rows={};list_reads=0;count_reads=0;meta_reads=0;con_reads=0
          station_macro='station_gen_factory_base_01_macro';module_macro='struct_arg_base_02_macro'
          station_op=false;station_con=true;module_op=false;module_con=true
          function ConvertStringTo64Bit(s) return s end
          function ConvertIDTo64Bit(s)
            local v=ffi.new('uint64_t',0)
            for i=1,#s do v=v*10+tonumber(s:sub(i,i)) end
            return conversion_bad and v+1 or v
          end
          function GetComponentData(id,key)
            assert(key=='macro');meta_reads=meta_reads+1
            if metadata_mutate then metadata_mutate(id) end
            if id=='99' then return station_macro end
            return module_macro
          end
          function IsComponentConstruction(id)
            con_reads=con_reads+1
            if id=='99' then return station_con end
            return module_con
          end
          native.IsComponentOperational=function(id)
            if id==station then return station_op end
            return module_op
          end
          native.GetNumStationModules=function(id,a,b)
            assert(id==station and a==true and b==true);count_reads=count_reads+1
            if count_mutate then count_mutate() end
            return count_override or #rows
          end
          native.GetStationModules=function(out,capacity,id,a,b)
            assert(id==station and capacity==1 and a==true and b==true)
            list_reads=list_reads+1
            if list_mutate then list_mutate() end
            if rows[1] then out[0]=rows[1] end
            return return_override or math.min(#rows,capacity)
          end
          function GetObjectPositionInSector() error('forbidden transform getter') end
          native.GetNumPlannedStationModules=function() error('forbidden planned-list getter') end
        ''')
        lua.globals().module = lua.execute(SOURCE)
        lua.execute("census=module.new(station,'" + HANDLE + "')")
        return lua, lua.globals().census

    def decode(self, pair):
        return sample(*pair, expected_handle=HANDLE)

    def test_empty_birth_construction_and_operational_are_only_observations(self):
        lua, census = self.fixture()
        records = [census.sample(6)]
        lua.execute('rows={module_id}')
        records.append(census.sample(31))
        lua.execute('module_con=false;module_op=true;station_con=false;station_op=true')
        records.append(census.sample(9000))
        proof = sequence(records, expected_handle=HANDLE, required_ticks=[6, 31, 9000])
        self.assertTrue(proof['observed_birth'])
        self.assertTrue(proof['construction_observed'])
        self.assertTrue(proof['operational_observed'])
        self.assertFalse(proof['construction_completion_proven'])
        for key in ('native_plan_link_supported', 'sector_pose_supported', 'atomic_snapshot', 'generation_lease'):
            self.assertFalse(proof[key])
        self.assertEqual(lua.globals().list_reads, 9)
        self.assertEqual(proof['samples'][-1]['first_seen'], 31)

    def test_different_native_ids_produce_equal_portable_records(self):
        all_records = []
        for value in (101, 456):
            lua, census = self.fixture();census.sample(6)
            lua.execute('module_id=ConvertIDTo64Bit("%d");rows={module_id}' % value)
            all_records.append(census.sample(31))
        self.assertEqual(all_records[0][0], all_records[1][0])
        self.assertNotEqual(all_records[0][1], all_records[1][1])

    def test_count_list_birth_and_removal_races_poison(self):
        for setup in (
            'list_mutate=function() rows={module_id} end',
            'count_mutate=function() if count_reads==2 then rows={module_id} end end',
            'return_override=1', 'count_override=2', 'count_override=-1', 'count_override=0.5',
        ):
            lua, census = self.fixture();lua.execute(setup)
            with self.subTest(setup=setup):
                with self.assertRaises(Exception): census.sample(6)
                calls = lua.globals().list_reads
                with self.assertRaisesRegex(Exception, 'poisoned'): census.sample(7)
                self.assertEqual(lua.globals().list_reads, calls)

    def test_same_count_replacement_before_each_identity_reread_is_refused(self):
        for boundary in (5, 6):
            lua, census = self.fixture();census.sample(6)
            lua.execute('rows={module_id};list_mutate=function() if list_reads==%d then rows={ConvertIDTo64Bit("102")} end end' % boundary)
            with self.assertRaisesRegex(Exception, 'identities unstable'): census.sample(31)
            with self.assertRaisesRegex(Exception, 'poisoned'): census.sample(32)

    def test_metadata_same_count_mutation_is_refused(self):
        lua, census = self.fixture();census.sample(6)
        lua.execute('''rows={module_id};module_meta=0;metadata_mutate=function(id)
          if id~='99' then module_meta=module_meta+1;if module_meta==2 then module_con=false;module_op=true end end
        end''')
        with self.assertRaisesRegex(Exception, 'metadata unstable'): census.sample(31)

    def test_unexpected_macro_and_contradictory_flags_are_refused(self):
        for setup in ("module_macro='other_macro'", 'module_macro=nil', 'module_con=true;module_op=true',
                      'module_con=1', 'conversion_bad=true'):
            lua, census = self.fixture();census.sample(6)
            lua.execute('rows={module_id};' + setup)
            with self.subTest(setup=setup):
                with self.assertRaises(Exception): census.sample(31)

    def test_station_loss_or_change_cannot_become_empty_inventory(self):
        for setup in ('station_con=false', "station_macro=nil", "station_macro='other_macro'",
                      'metadata_mutate=function(id) if meta_reads==2 then station_op=true;station_con=false end end'):
            lua, census = self.fixture();lua.execute(setup)
            with self.assertRaisesRegex(Exception, 'station|macro'): census.sample(6)

    def test_unknown_wreck_state_is_retained_without_operational_claim(self):
        lua, census = self.fixture();census.sample(6)
        lua.execute('rows={module_id};module_con=false;module_op=false')
        result = self.decode(census.sample(31))
        self.assertTrue(result['unknown_state'])
        self.assertFalse(result['operational'])

    def test_disappearance_and_different_generation_after_birth_are_refused(self):
        for replacement in ('rows={}', 'rows={ConvertIDTo64Bit("33554533")}'):
            lua, census = self.fixture();census.sample(6)
            lua.execute('rows={module_id}');census.sample(31)
            lua.execute(replacement)
            with self.assertRaisesRegex(Exception, 'disappeared|replaced|reused'): census.sample(32)
            lua.execute('rows={module_id}')
            with self.assertRaisesRegex(Exception, 'poisoned'): census.sample(33)

    def test_uint64_identity_above_double_precision_is_exact(self):
        lua, census = self.fixture();baseline = census.sample(6)
        lua.execute('module_id=ConvertIDTo64Bit("9007199254740993");rows={module_id}')
        birth = census.sample(31)
        self.assertEqual(self.decode(birth)['module_id'], 9007199254740993)
        lua.execute('rows={ConvertIDTo64Bit("9007199254740992")}')
        with self.assertRaisesRegex(Exception, 'replaced'): census.sample(32)
        self.assertTrue(sequence([baseline, birth], expected_handle=HANDLE, required_ticks=[6, 31])['observed_birth'])

    def test_existing_module_without_empty_baseline_and_float_station_are_refused(self):
        lua, census = self.fixture();lua.execute('rows={module_id}')
        with self.assertRaisesRegex(Exception, 'empty baseline'): census.sample(6)
        with self.assertRaisesRegex(Exception, 'exact native uint64'):
            lua.execute("module.new(99,'" + HANDLE + "')")

    def test_stale_tick_does_not_make_native_calls(self):
        lua, census = self.fixture();census.sample(6)
        reads = lua.globals().list_reads
        for tick in (0, 5, 6, 6.5, 4294967296):
            with self.assertRaisesRegex(Exception, 'stale'): census.sample(tick)
        self.assertEqual(reads, lua.globals().list_reads)

    def test_independent_parser_rejects_fabricated_scope_and_lifecycle(self):
        lua, census = self.fixture();census.sample(6)
        lua.execute('rows={module_id}');raw, private = census.sample(31)
        for index, value in ((0, 'OTHER'), (1, '031'), (2, 'a'*16), (3, 'native_plan_link_supported=1'),
                             (4, 'sector_pose_supported=1'), (5, 'atomic_snapshot=1'), (6, 'generation_lease=1'),
                             (7, '1'), (9, '2'), (10, '32'), (11, 'other_macro'), (12, '1'), (14, '1')):
            fields = raw.split(':');fields[index] = value
            with self.subTest(index=index):
                with self.assertRaises(ValueError): self.decode((':'.join(fields), private))
        for index, value in ((0, 'OTHER'), (1, '32'), (2, 'a'*16), (3, '099'), (4, '0'), (4, '18446744073709551616')):
            fields = private.split(':');fields[index] = value
            with self.assertRaises(ValueError): self.decode((raw, ':'.join(fields)))

    def test_independent_sequence_rejects_missing_private_coverage_and_replacement(self):
        lua, census = self.fixture();first = census.sample(6)
        lua.execute('rows={module_id}');second = census.sample(31);third = census.sample(32)
        for records in ([], [first], [second, first], [first, first]):
            with self.assertRaisesRegex(ValueError, 'coverage'):
                sequence(records, expected_handle=HANDLE, required_ticks=[6, 31])
        for field, replacement in ((3, '100'), (4, '102')):
            detail = third[1].split(':');detail[field] = replacement
            with self.assertRaisesRegex(ValueError, 'lifetime|replaced'):
                sequence([first, second, (third[0], ':'.join(detail))], expected_handle=HANDLE, required_ticks=[6, 31, 32])
        changed = third[0].split(':');changed[10] = '32'
        with self.assertRaisesRegex(ValueError, 'first-seen'):
            sequence([first, second, (':'.join(changed), third[1])], expected_handle=HANDLE, required_ticks=[6, 31, 32])


if __name__ == '__main__':
    unittest.main()
