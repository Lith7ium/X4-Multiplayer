"""Production Lua logging must preserve C pages under the observed native cap."""
import copy
import hashlib
import io
import struct
import unittest

import test_native_gate_probe as probe_fixture
import test_native_gate_world_nav as nav_fixture
from native_world_evidence import DOMAINS, frames, roots


def object_row(tick, identity, record):
    hashes=' '.join(name+'='+value.hex() for name,value in zip(DOMAINS,record['hashes']))
    timing=record['timing_data'][:-16].hex() or '-'
    return (f'[X4LAN] NATIVE_GATE WORLD_OBJECT tick={tick} id={identity:016x} '
        f'type={record["type"]:x} flags={record["flags"]:x} parent={record["parent"]:016x} '
        f'{hashes} timing_fields={timing} nav_raw={record["nav_raw"]}\n')


def captures():
    footer=struct.pack('<IIQ',0,0,0)
    absent={'type':0,'flags':0,'parent':0,'hashes':tuple(hashlib.sha256(footer).digest() for _ in DOMAINS),
        'timing_data':footer,'nav_raw':'-'}
    result={}
    for tick,read,changed in ((1,0,range(1,14)),(2,1,range(1,13)),(3,1,())):
        records={i:nav_fixture.row(read) for i in range(1,13)};records[13]=absent
        rows=[object_row(tick,i,records[i]) for i in changed]
        result[tick]={
            'pages':[''.join(rows[i:i+12]) for i in range(0,len(rows),12)],
            'banks':f'tick={tick} read_before={read} write_before={1-read} read_after={read} write_after={1-read}',
            'summary':f'tick={tick} objects=13 present=12 changed={len(rows)} unknown_class=0 unknown_motion=0 unknown_references=0 '
                +f'state=WORLD_IMPORTED_2:{tick}:13:'+':'.join(roots(records)),
        }
    return result


def run_probe(captured, source=probe_fixture.SOURCE, native_payload_limit=8191):
    fixture=probe_fixture.SnapshotProbeTests();fixture.setUp();lua=fixture.lua
    lua.globals().captures=lua.table_from(captured,recursive=True)
    lua.globals().native_payload_limit=native_payload_limit
    lua.execute('''
      X4LAN_CONFIG.native_gate_world_hash_stride=1
      X4LAN_CONFIG.native_gate_world_hash_schema='WORLD_IMPORTED_2'
      local gate=package.loadlib('gate')()
      gate.world_capture_nav_v2=function(tick) captured_tick=tick;return captures[tick].summary end
      gate.world_banks=function() return captures[captured_tick].banks end
      gate.world_page=function(n)
        if n==0 then return #captures[captured_tick].pages end
        return captures[captured_tick].pages[n]
      end
      gate.world_detail=function(n) assert(n==0);return 0 end
      log_lengths={}
      DebugError=function(text)
        log_lengths[#log_lengths+1]=#text
        logs[#logs+1]=text:sub(1,native_payload_limit)
      end
      state.boot_phase=3;is_menu=false
    ''')
    lua.execute(source)
    for tick in captured:
        lua.globals().state.issued=tick;lua.execute('update()')
    return lua,list(lua.globals().logs.values())


def parse(logs):
    errors=[]
    captured=[copy.deepcopy(frame) for frame in frames(io.StringIO('\n'.join(logs)+'\n'),errors,1,
        expected_watches=set(),require_timing_fields=True,expected_schema='WORLD_IMPORTED_2')]
    return captured,errors


class WorldPageLogging(unittest.TestCase):
    def test_full_pages_roundtrip_with_sparse_phase_updates_under_native_truncation_sink(self):
        native=captures()
        self.assertGreater(len(native[1]['pages'][0]),8191)
        # Raw World011 blocks contain 8191 LF-normalized bytes including the
        # logger's final newline. Cover both payload/newline interpretations.
        for limit in (8190,8191):
            with self.subTest(native_payload_limit=limit):
                lua,logs=run_probe(native,native_payload_limit=limit)
                self.assertEqual(lua.globals().reports,3)
                self.assertEqual(lua.globals().state.active,1)
                self.assertFalse(any('NATIVE_GATE ERROR ' in text for text in logs))
                self.assertLessEqual(max(lua.globals().log_lengths.values()),6000)
                actual=''.join(text for text in logs if text.startswith('[X4LAN] NATIVE_GATE WORLD_OBJECT '))
                expected=''.join(page for frame in native.values() for page in frame['pages'])
                self.assertEqual(actual,expected)
                observed,errors=parse(logs)
                self.assertEqual(errors,[])
                self.assertEqual([frame['tick'] for frame in observed],[1,2,3])
                self.assertTrue(all(frame['roots']==observed[0]['roots'] for frame in observed))
                self.assertNotEqual(observed[0]['objects'][1]['nav_raw'],observed[1]['objects'][1]['nav_raw'])
                self.assertEqual(observed[1]['objects'],observed[2]['objects'])

    def test_old_single_call_mutant_reproduces_incomplete_receipts(self):
        old=probe_fixture.SOURCE.replace('log_world_page(assert(gate.world_page(page)))',
                                         'DebugError(assert(gate.world_page(page)))')
        self.assertNotEqual(old,probe_fixture.SOURCE)
        lua,logs=run_probe(captures(),old)
        self.assertGreater(max(lua.globals().log_lengths.values()),8191)
        self.assertTrue(parse(logs)[1])

    def test_worst_native_formatter_width_is_lossless_but_does_not_waive_schema_errors(self):
        # Exercise the C page buffer's widest scalar/text fields. These maxima
        # deliberately exceed the semantic schema and must remain rejected.
        text=('[X4LAN] NATIVE_GATE WORLD_OBJECT tick=18446744073709551615 id=ffffffffffffffff '
            'type=ffffffff flags=ffffffff parent=ffffffffffffffff '
            +' '.join(name+'='+'f'*64 for name in DOMAINS)
            +' timing_fields='+'f'*192+' nav_raw='+'f'*198+'\n')*12
        self.assertTrue(8191<len(text)<12288)
        native={1:captures()[1]};native[1]['pages']=[text]
        lua,logs=run_probe(native)
        self.assertEqual(''.join(x for x in logs if x.startswith('[X4LAN] NATIVE_GATE WORLD_OBJECT ')),text)
        self.assertLessEqual(max(lua.globals().log_lengths.values()),6000)
        self.assertTrue(parse(logs)[1])

    def test_malformed_or_oversize_row_aborts_before_any_page_chunk_or_report(self):
        first=captures()[1]['pages'][0].splitlines(keepends=True)[0]
        for page in ('',23,first+'x'*6000+'\n',first+'unterminated',first+'\n','x'*12288):
            with self.subTest(page_type=type(page).__name__,length=len(page) if isinstance(page,str) else None):
                native={1:captures()[1]};native[1]['pages']=[page]
                lua,logs=run_probe(native)
                self.assertEqual(lua.globals().reports,0)
                self.assertEqual(lua.globals().state.active,0)
                self.assertTrue(any('NATIVE_GATE ERROR ' in text for text in logs))
                self.assertFalse(any('NATIVE_GATE WORLD_OBJECT ' in text or 'NATIVE_GATE WORLD_HASH ' in text for text in logs))

    def test_missing_sparse_row_and_corrupt_raw_bytes_still_fail_independent_parser(self):
        native=captures();_,logs=run_probe(native)
        original='\n'.join(logs)+'\n'
        missing=native[2]['pages'][0].splitlines(keepends=True)[-1]
        raw=nav_fixture.row(1)['nav_raw']
        changed=bytearray.fromhex(raw);changed[37]^=1
        for invalid in (original.replace(missing,'',1),original.replace(raw,changed.hex(),1)):
            self.assertTrue(parse([invalid])[1])


if __name__=='__main__':unittest.main()
