from pathlib import Path
import sys
import unittest
from lupa.luajit21 import LuaRuntime

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/dev'))
from native_construction_units_evidence import sample, sequence

SOURCE = (ROOT / 'tools/dev/native_construction_units.lua').read_text()
HANDLE = '0000000500000003'


class LogicalConstructionUnits(unittest.TestCase):
    def fixture(self):
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.execute('''
            local ffi=require('ffi');native={};local proxy={C=native}
            for _,key in ipairs({'cdef','typeof','istype','sizeof','offsetof','new','string'}) do proxy[key]=ffi[key] end
            require=function() return proxy end
            object=ffi.new('uint64_t',99);calls=0;reads=0;operational_reads=0
            rows={{'ship_gen_xs_buildingdrone_01_a_macro','build',30}}
            stored=30;unavailable=0
            native.IsComponentOperational=function(id)
                assert(id==object);operational_reads=operational_reads+1
                return not destroyed and not (die_during and operational_reads==2)
            end
            native.GetNumAllUnits=function(id,onlydrones)
                assert(id==object and onlydrones);calls=calls+1
                return count_override or #rows
            end
            native.GetAllUnits=function(out,n,id,onlydrones)
                assert(id==object and onlydrones);reads=reads+1
                if mutate and reads==2 then mutate() end
                for i=1,math.min(n,#rows) do
                    out[i-1].macro=rows[i][1];out[i-1].category=rows[i][2];out[i-1].amount=rows[i][3]
                end
                return return_override or #rows
            end
            native.GetNumStoredUnits=function(id,category,virtualammo)
                assert(id==object and category=='build' and virtualammo==false)
                return stored
            end
            native.GetNumUnavailableUnits=function(id,category)
                assert(id==object and category=='build');return unavailable
            end
        ''')
        lua.globals().module = lua.execute(SOURCE)
        lua.execute("census=module.new(object,'" + HANDLE + "',30)")
        return lua, lua.globals().census

    def decode(self, raw):
        return sample(raw, expected_handle=HANDLE)

    def test_logical_use_can_change_without_inventing_physical_drone_coverage(self):
        lua, census = self.fixture()
        records = [census.sample(6)]
        lua.execute('unavailable=12')
        records.append(census.sample(7))
        lua.execute('unavailable=0')
        records.append(census.sample(8))
        evidence = sequence(records, expected_handle=HANDLE, required_ticks=[6, 7, 8])
        self.assertTrue(evidence['logical_stock_constant'])
        self.assertTrue(evidence['logical_unavailable_observed'])
        self.assertFalse(evidence['accounting_unknown'])
        self.assertFalse(evidence['physical_identity_coverage'])
        self.assertFalse(evidence['construction_completion_proven'])
        self.assertEqual(lua.globals().reads, 6)

    def test_same_count_replacement_or_amount_change_poison_without_retry(self):
        for change in ("rows[1][1]='other_drone_macro'", 'rows[1][3]=29', 'stored=29', 'unavailable=12'):
            lua, census = self.fixture()
            lua.execute('mutate=function() ' + change + ' end')
            with self.assertRaisesRegex(Exception, 'unstable'): census.sample(6)
            reads = lua.globals().reads
            with self.assertRaisesRegex(Exception, 'poisoned'): census.sample(7)
            self.assertEqual(lua.globals().reads, reads)

    def test_aggregate_row_order_is_canonical_and_not_an_order_queue(self):
        lua, census = self.fixture()
        lua.execute("rows[2]={'repair_drone_macro','repair',2};mutate=function() rows[1],rows[2]=rows[2],rows[1] end")
        result = self.decode(census.sample(6))
        self.assertEqual(result['total'], 32)
        self.assertEqual(result['unknown_rows'], 1)
        self.assertTrue(result['accounting_unknown'])

    def test_missing_changed_or_oversized_inventory_is_refused(self):
        for setup in ('return_override=0', 'count_override=17', 'stored=101', 'unavailable=101',
                      "rows[2]={'another_macro','build',80}", "rows[2]=rows[1]",
                      'count_override=0.5', 'count_override=-1'):
            lua, census = self.fixture();lua.execute(setup)
            with self.assertRaises(Exception): census.sample(6)
            with self.assertRaisesRegex(Exception, 'poisoned'): census.sample(7)

    def test_null_malformed_and_unterminated_native_text_is_refused(self):
        for setup in ("rows[1][1]=nil", "rows[1][2]='build:spoof'", "rows[1][1]=''",
                      "rows[1][1]=string.rep('x',97)", "rows[1][2]=string.rep('y',25)",
                      "buffer=require('ffi').new('char[97]');for i=0,96 do buffer[i]=120 end;rows[1][1]=buffer"):
            lua, census = self.fixture();lua.execute(setup)
            with self.assertRaises(Exception): census.sample(6)

    def test_destroyed_builder_cannot_be_an_empty_inventory(self):
        for setup in ('destroyed=true', 'die_during=true'):
            lua, census = self.fixture();lua.execute(setup)
            with self.assertRaisesRegex(Exception, 'unavailable|lost'): census.sample(6)

    def test_stale_ticks_and_float_identity_are_refused_before_native_read(self):
        lua, census = self.fixture();census.sample(6)
        reads = lua.globals().reads
        for tick in (6, 5, 0, 6.5, 4294967296):
            with self.assertRaisesRegex(Exception, 'stale'): census.sample(tick)
        self.assertEqual(lua.globals().reads, reads)
        with self.assertRaisesRegex(Exception, 'exact native uint64'):
            lua.execute("module.new(99,'" + HANDLE + "',30)")
        self.assertEqual(lua.globals().reads, reads)

    def test_inconsistent_native_accounting_is_retained_as_unknown(self):
        for setup, delta in (('stored=28', 0), ('unavailable=31', 0), ('rows[1][3]=29', -1),
                            ("rows[1][2]='repair'", -30)):
            lua, census = self.fixture();lua.execute(setup)
            result = self.decode(census.sample(6))
            self.assertTrue(result['accounting_unknown'])
            self.assertEqual(result['stock_delta'], delta)
            self.assertFalse(result['physical_identity_coverage'])

    def test_empty_stock_does_not_claim_conservation(self):
        lua, census = self.fixture();lua.execute('rows={};stored=0')
        result = self.decode(census.sample(6))
        self.assertEqual(result['build'], 0)
        self.assertEqual(result['stock_delta'], -30)
        self.assertTrue(result['accounting_unknown'])

    def test_wrong_ffi_layout_is_refused(self):
        lua = LuaRuntime()
        lua.execute("require('ffi').cdef('typedef struct {uint32_t amount;} UnitData;')")
        with self.assertRaisesRegex(Exception, 'unsupported native UnitData layout'): lua.execute(SOURCE)

    def test_independent_parser_rejects_fabricated_counts_flags_and_scope(self):
        _, census = self.fixture();raw = census.sample(6)
        for index, value in ((0, 'OTHER'), (1, '06'), (1, '0'), (2, 'a'*16),
                             (3, 'logical_only=0'), (4, 'physical_ids=1'), (5, 'only_drones=0'), (6, '29'),
                             (7, '29'), (8, '29'), (9, '101'), (10, '-1'), (11, '0'),
                             (12, '0'), (13, '0'), (14, '1'), (15, '1'),
                             (16, 'ship_gen_xs_buildingdrone_01_a_macro,build,030')):
            fields = raw.split(':');fields[index] = value
            with self.subTest(index=index, value=value):
                with self.assertRaises(ValueError): self.decode(':'.join(fields))

    def test_independent_parser_rejects_duplicate_rows_and_missing_coverage(self):
        _, census = self.fixture();first = census.sample(6);second = census.sample(7)
        fields = first.split(':');fields[-1] += ';' + fields[-1]
        with self.assertRaisesRegex(ValueError, 'duplicate'): self.decode(':'.join(fields))
        for records in ([], [first], [second, first], [first, first]):
            with self.assertRaisesRegex(ValueError, 'coverage'):
                sequence(records, expected_handle=HANDLE, required_ticks=[6, 7])


if __name__ == '__main__':
    unittest.main()
