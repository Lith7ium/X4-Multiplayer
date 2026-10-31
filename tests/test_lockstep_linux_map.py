"""Offline Windows->Linux gate site mapper: source pins, PE/ELF function tables, anchors.
All inputs are synthetic; no X4 executable is needed or read."""
from pathlib import Path
import json
import re
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/dev'))
import lockstep_linux_map as m

SHA = ['%064x' % (i + 1) for i in range(6)]


def gate_dir(tmp):
    (tmp / 'lockstep_gate.c').write_text(f'''
#define FOO_RVA 0x1000u
#define FOO_SHA "{SHA[1]}"
#define BAR (FOO_RVA+0x10)
#define TWICE 0x5000
static int pins(void){{
    return hash_ok(image+FOO_RVA,32,"{SHA[0]}") && hash_ok(image+BAR, 0x8u, FOO_SHA);
}}
/* hash_ok(image+0x9999,4,"{SHA[2]}") is retired */
// hash_ok(image+0x9998,4,"{SHA[2]}")
void patch(void){{ swap(image+0x4000); read_local(image+FOO_RVA+0x20,&x,8); use(image+p->rva); }}
''', encoding='utf-8')
    (tmp / 'lockstep_world_nav.h').write_text(f'''
#define TWICE 0x6000
typedef struct {{unsigned rva,size;const char *sha;}} pin_t;
static const pin_t list[]={{
    {{0x2000u,16u,"{SHA[3]}"}},
    {{0x3000,8,"{SHA[4]}"}}}};
static int bad(void){{return hash_ok(image+TWICE,4,"{SHA[5]}");}}
''', encoding='utf-8')
    return tmp


class Sources(unittest.TestCase):
    def test_pins_refs_tables_comments_and_ambiguous_macros(self):
        with tempfile.TemporaryDirectory() as raw:
            pins, refs, unresolved = m.extract_sites(m.gate_files(gate_dir(Path(raw))))
        got = sorted((p['rva'], p['length'], p['sha256'], p['kind']) for p in pins)
        self.assertEqual(got, [(0x1000, 32, SHA[0], 'hash'), (0x1010, 8, SHA[1], 'hash'),
                               (0x2000, 16, SHA[3], 'table'), (0x3000, 8, SHA[4], 'table')])
        self.assertEqual(sorted(r['rva'] for r in refs), [0x1020, 0x4000])
        self.assertNotIn(0x9999, [p['rva'] for p in pins])  # commented pins are not pins
        self.assertEqual([(u['expr'], u['kind']) for u in unresolved], [('TWICE', 'hash')])  # two definitions
        nav = next(p for p in pins if p['rva'] == 0x2000)
        self.assertEqual((nav['file'], nav['line']), ('lockstep_world_nav.h', 5))

    def test_evaluate_is_a_closed_integer_language(self):
        macros = {'A': ['0x10u'], 'B': ['(A<<4)'], 'C': ['1', '2'], 'D': ['D']}
        self.assertEqual(m.evaluate('(unsigned)0x10u+2', macros), 18)
        self.assertEqual(m.evaluate('B|1', macros), 0x101)
        self.assertEqual(m.evaluate('0x2c0f3c8+0x18ull', macros), 0x2c0f3e0)
        for bad in ('C', 'D', '__import__("os")', 'sizeof(x)', 'a.b', '1/2', 'E'):
            self.assertIsNone(m.evaluate(bad, macros), bad)

    def test_families(self):
        cases = {'lockstep_gate.c': 'frame_gate', 'lockstep_world_position_rng_pins.h': 'script_rng',
                 'lockstep_world_nav.h': 'nav', 'lockstep_thunk_emit.h': 'infrastructure',
                 'lockstep_registry_md.h': 'md', 'lockstep_gravidar.h': 'gravidar',
                 'lockstep_thread_rng.h': 'tls_rng', 'lockstep_bullet_cadence.h': 'bullets_physics',
                 'lockstep_world_created.h': 'created_ids', 'lockstep_pilot_inputs.h': 'pilot_player',
                 'lockstep_event_order.h': 'events', 'lockstep_path_determinism.h': 'pathing',
                 'lockstep_view_points.h': 'attention', 'lockstep_world_queue.h': 'world_queue'}
        for name, family in cases.items(): self.assertEqual(m.family(name), family, name)


def make_pe():
    """x64 PE: .text 0x1000 (code), .rdata 0x2000 (unwind + .pdata), .data 0x3000."""
    unwind_plain = bytes([0x01, 0, 0, 0])
    unwind_chained = bytes([0x01 | 4 << 3, 0, 0, 0]) + struct.pack('<III', 0x1000, 0x1010, 0x2000)
    rdata = bytearray(0x200)
    rdata[0:4] = unwind_plain
    rdata[0x10:0x10 + len(unwind_chained)] = unwind_chained
    pdata = struct.pack('<III', 0x1000, 0x1010, 0x2000) + struct.pack('<III', 0x1010, 0x1020, 0x2010) + \
        struct.pack('<III', 0x1020, 0x1030, 0x2000)
    rdata[0x100:0x100 + len(pdata)] = pdata
    text = bytes(range(0x30)) + b'\xcc' * 0x1d0
    sections = [(b'.text', 0x1000, text, 0x60000020), (b'.rdata', 0x2000, bytes(rdata), 0x40000040),
                (b'.data', 0x3000, b'\0' * 0x200, 0xc0000040)]
    head = bytearray(0x400)
    head[0:2] = b'MZ'; struct.pack_into('<I', head, 0x3c, 0x80)
    head[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<HHIIIHH', head, 0x84, 0x8664, len(sections), 0x1234, 0, 0, 0xf0, 0x22)
    opt = 0x98
    struct.pack_into('<H', head, opt, 0x20b)
    struct.pack_into('<I', head, opt + 56, 0x4000)
    struct.pack_into('<I', head, opt + 108, 16)
    struct.pack_into('<II', head, opt + 112 + 24, 0x2100, len(pdata))
    blob = bytearray(head)
    for i, (name, va, body, flags) in enumerate(sections):
        at = opt + 0xf0 + 40 * i
        struct.pack_into('<8sIIII12xI', blob, at, name, len(body), va, len(body), len(blob), flags)
        blob += body
    return bytes(blob)


class Pe(unittest.TestCase):
    def test_pdata_chains_fold_into_their_root(self):
        pe = m.PeImage(make_pe())
        self.assertEqual(pe.function_of(0x1015), (0x1000, 0x1010, 0x1020))
        self.assertEqual(pe.function_of(0x1025), (0x1020, 0x1020, 0x1030))
        self.assertIsNone(pe.function_of(0x1030))
        self.assertEqual(pe.ranges(0x1000), [(0x1000, 0x1010), (0x1010, 0x1020)])
        self.assertEqual(pe.read(0x1002, 3), bytes([2, 3, 4]))
        self.assertTrue(pe.is_code(0x1005)); self.assertFalse(pe.is_code(0x3005))
        self.assertEqual((pe.section(0x2005), pe.data_class(0x2005), pe.data_class(0x3008)), ('.rdata', 'ro', 'rw'))
        self.assertIsNone(pe.data_class(0x1000))

    def test_rejects_non_pe(self):
        with self.assertRaises(ValueError): m.PeImage(b'\x7fELF' + bytes(200))


def eh_frame(frame_addr, functions):
    """CIE (zR, pcrel|sdata4) followed by one FDE per (start, length)."""
    cie_body = struct.pack('<I', 0) + bytes([1]) + b'zR\0' + bytes([1, 0x78, 16, 1, 0x1b])
    cie_body += b'\0' * (-(len(cie_body) + 4) % 8)
    frame = bytearray(struct.pack('<I', len(cie_body)) + cie_body)
    fdes = []
    for start, length in functions:
        at = len(frame)
        body = struct.pack('<I', at + 4) + struct.pack('<i', start - (frame_addr + at + 8)) + \
            struct.pack('<i', length) + b'\0'
        body += b'\0' * (-(len(body) + 4) % 8)
        frame += struct.pack('<I', len(body)) + body
        fdes.append((start, frame_addr + at))
    return bytes(frame), fdes


def eh_frame_hdr(hdr_addr, frame_addr, fdes):
    hdr = bytearray([1, 0x1b, 0x03, 0x3b]) + struct.pack('<iI', frame_addr - (hdr_addr + 4), len(fdes))
    for start, fde in sorted(fdes): hdr += struct.pack('<ii', start - hdr_addr, fde - hdr_addr)
    return bytes(hdr)


def make_elf():
    base = 0x400000
    text = b'\x90' * 0x40
    names = b'\0.text\0.eh_frame_hdr\0.eh_frame\0.data\0.shstrtab\0'
    layout, offset = {}, 0x200
    for name, size in (('.text', 0x40), ('.eh_frame_hdr', 0x40), ('.eh_frame', 0x80), ('.data', 0x20)):
        layout[name] = offset; offset += size
    frame, fdes = eh_frame(base + layout['.eh_frame'], [(base + 0x200, 0x10), (base + 0x220, 0x20)])
    hdr = eh_frame_hdr(base + layout['.eh_frame_hdr'], base + layout['.eh_frame'], fdes)
    bodies = {'.text': text, '.eh_frame_hdr': hdr, '.eh_frame': frame, '.data': bytes(0x20)}
    blob = bytearray(offset)
    for name, at in layout.items(): blob[at:at + len(bodies[name])] = bodies[name]
    shstr = len(blob); blob += names
    shoff = len(blob)
    flags = {'.text': 6, '.eh_frame_hdr': 2, '.eh_frame': 2, '.data': 3}
    sections = [b'\0' * 64]
    for name in ('.text', '.eh_frame_hdr', '.eh_frame', '.data'):
        sections.append(struct.pack('<IIQQQQIIQQ', names.index(name.encode() + b'\0'), 1, flags[name],
                                    base + layout[name], layout[name], len(bodies[name]), 0, 0, 1, 0))
    sections.append(struct.pack('<IIQQQQIIQQ', names.index(b'.shstrtab\0'), 3, 0, 0, shstr, len(names), 0, 0, 1, 0))
    blob += b''.join(sections)
    head = bytearray(b'\x7fELF\x02\x01\x01' + bytes(9))
    head += struct.pack('<HHIQQQIHHHHHH', 2, 62, 1, base + 0x200, 64, shoff, 0, 64, 56, 1, 64, len(sections), len(sections) - 1)
    head += struct.pack('<IIQQQQQQ', 1, 5, 0, base, base, len(blob), len(blob), 0x1000)
    blob[0:len(head)] = head
    return bytes(blob), base, layout


class Elf(unittest.TestCase):
    def test_eh_frame_table_gives_function_ranges(self):
        frame, fdes = eh_frame(0x5000, [(0x1000, 0x30), (0x1040, 0x8)])
        hdr = eh_frame_hdr(0x4000, 0x5000, fdes)
        self.assertEqual(m.parse_eh_frame(hdr, 0x4000, frame, 0x5000), [(0x1000, 0x1030), (0x1040, 0x1048)])
        with self.assertRaises(ValueError): m.parse_eh_frame(bytes([1, 0x1b, 0x03, 0x1b]) + hdr[4:], 0x4000, frame, 0x5000)

    def test_minimal_executable(self):
        data, base, layout = make_elf()
        elf = m.ElfImage(data)
        self.assertEqual(elf.functions, [(base + 0x200, base + 0x210), (base + 0x220, base + 0x240)])
        self.assertEqual(elf.function_of(base + 0x225), (base + 0x220, base + 0x240))
        self.assertIsNone(elf.function_at(base + 0x225))
        self.assertEqual(elf.read(base + 0x200, 2), b'\x90\x90')
        self.assertEqual(elf.data_class(base + layout['.data']), 'rw')
        self.assertEqual((elf.dynsym, elf.plt, elf.build_id()), ({}, {}, None))


class Facts(unittest.TestCase):
    def test_calls_refs_widths_fp_and_libm(self):
        insns = [(0x100, 5, 'call', '0x500'), (0x105, 6, 'cmp', 'byte ptr [rip + 0x1000], 0'),
                 (0x10b, 6, 'call', 'qword ptr [rip + 0x10]'), (0x111, 4, 'divss', 'xmm0, xmm1'),
                 (0x115, 4, 'vrsqrtss', 'xmm0, xmm0, xmm1'), (0x119, 4, 'cvtss2sd', 'xmm0, xmm0'),
                 (0x11d, 5, 'vfmadd213sd', 'xmm0, xmm1, xmm2'), (0x122, 7, 'mov', 'rax, qword ptr [0x9000]'),
                 (0x129, 5, 'mov', 'edi, 0x9008'), (0x12e, 5, 'jmp', '0x600'), (0x133, 4, 'ldmxcsr', 'dword ptr [rsp]')]
        f = m.facts(insns, [(0x9000, 0x9100), (0x1100, 0x1200)], {0x600: 'sincosf', 0x500: 'memcpy'})
        self.assertEqual(f['calls'], [(0x100, 'call', 0x500), (0x10b, 'call*', 0x121), (0x12e, 'jmp', 0x600)])
        self.assertEqual(f['refs'], [(0x105, 'cmp', 0x110b, 1), (0x122, 'mov', 0x9000, 8), (0x129, 'mov', 0x9008, None)])
        self.assertEqual(f['fp'], {'div': 1, 'approx': 1, 'widen': 1, 'fma': 1, 'fpenv': 1})
        self.assertEqual(f['libm'], {'sincosf': 1})


class Anchors(unittest.TestCase):
    def mapper(self, table):
        return lambda target: table.get(target)

    def test_ordinal_unique_mismatch_and_unmapped(self):
        win = [(0x10, 'call', 0xa), (0x20, 'call', 0xb), (0x30, 'call', 0xa), (0x40, 'jmp', 0xc)]
        lin = [(0x110, 'call', 0x1a), (0x118, 'call', 0x1b), (0x150, 'call', 0x1a), (0x160, 'jmp', 0x1c)]
        mapper = self.mapper({0xa: 0x1a, 0xb: 0x1b, 0xc: 0x1c})
        second = m.call_anchor(0x30, 5, win, lin, mapper)
        self.assertEqual((second['method'], second['lin_site'], second['confidence']), ('call_ordinal', 0x150, 'medium'))
        tail = m.call_anchor(0x3e, 8, win, lin, mapper)  # pinned range covering the tail jmp
        self.assertEqual((tail['lin_site'], tail['confidence'], tail['win_anchor']), (0x160, 'high', 0x40))
        self.assertEqual(m.call_anchor(0x30, 5, win, lin + [(0x170, 'call', 0x1a)], mapper)['method'], 'call_count_mismatch')
        unique = m.call_anchor(0x30, 5, win, lin[1:], mapper)
        self.assertEqual((unique['method'], unique['lin_site'], unique['confidence']), ('call_unique', 0x150, 'low'))
        self.assertEqual(m.call_anchor(0x20, 5, win, lin, self.mapper({}))['method'], 'call_target_unmapped')
        self.assertIsNone(m.call_anchor(0x50, 4, win, lin, mapper))
        tail_as_call = m.call_anchor(0x40, 5, win, lin[:3] + [(0x160, 'call', 0x1c)], mapper)
        self.assertEqual(tail_as_call['lin_site'], 0x160)


class Votes(unittest.TestCase):
    def test_strong_ordinal_votes_need_compatible_width(self):
        lin = {0x1: dict(refs=[(0, 'mov', 0x900, 8), (1, 'mov', 0x908, 1)]),
               0x2: dict(refs=[(0, 'mov', 0x700, 4), (2, 'cmp', 0x908, 1)]),
               0x3: dict(refs=[])}
        shape = {('cmp', 1)}
        vote = m.data_vote(0x50, [(0xa, 0x1, 1, shape), (0xb, 0x2, 1, shape)], lin)
        self.assertEqual((vote['lin_site'], vote['confidence'], vote['strong']), (0x908, 'high', 2))
        one = m.data_vote(0x50, [(0xa, 0x1, 1, shape)], lin)
        self.assertEqual((one['lin_site'], one['confidence']), (0x908, 'medium'))
        wrong_width = m.data_vote(0x50, [(0xa, 0x1, 0, {('cmp', 1)})], lin)
        self.assertEqual(wrong_width['confidence'], 'low')
        self.assertEqual(m.data_vote(0x50, [(0xc, 0x3, 0, shape)], lin)['method'], 'data_no_linux_refs')
        self.assertIsNone(m.data_vote(0x50, [], lin))
        only_ro = m.data_vote(0x50, [(0xa, 0x1, 0, shape)], lin, admissible=lambda v: v != 0x900)
        self.assertEqual(only_ro['lin_site'], 0x908)


class Pairs(unittest.TestCase):
    def known(self):
        rows = [dict(kind='function', win='10', lin='110', name='f', confidence='verified', source='s'),
                dict(kind='not_equivalent', win='20', lin='999', name='wrong', confidence='verified', source='s'),
                dict(kind='inlined', win='30', lin='130', name='step', confidence='verified', source='s'),
                dict(kind='data', win='5000', lin='9000', name='g', confidence='verified', source='s')]
        with tempfile.TemporaryDirectory() as raw:
            path = Path(raw) / 'known.json'
            path.write_text(json.dumps(dict(schema='x4-lockstep-linux-known-pairs-v1', pairs=rows)), encoding='utf-8')
            return m.load_known(path)

    def test_candidates_and_grading(self):
        known = self.known()
        xfer = {0x10: dict(lin=0x110, round='str1', confidence='high', score=9.0),
                0x20: dict(lin=0x999, round='callgraph', confidence='low', score=1.0),
                0x30: dict(lin=0x130, round='callgraph', confidence='high', score=50.0),
                0x40: dict(lin=0x140, round='vtable', confidence='medium', score=3.0)}
        self.assertEqual(m.function_candidate(0x10, known, xfer)[:3], (0x110, 'known', 'verified'))
        self.assertEqual(m.function_candidate(0x20, known, xfer)[:2], (None, 'xfer_contradicted'))
        self.assertEqual(m.function_candidate(0x30, known, xfer)[:2], (None, 'inlined'))
        self.assertEqual(m.function_candidate(0x40, known, xfer)[:3], (0x140, 'xfer:vtable', 'medium'))
        self.assertEqual(m.function_candidate(0x50, known, xfer)[:2], (None, 'unmapped'))
        grades, disagreements = m.grade_xfer(known, xfer)
        self.assertEqual(grades, {'agree_high': 1, 'known_wrong_low': 1, 'inlined_paired_high': 1})
        self.assertEqual(disagreements, [])
        mapper = m.target_mapper(known, xfer)
        self.assertEqual((mapper(0x10), mapper(0x20), mapper(0x40)), (0x110, None, None))

    def test_rows_without_executables(self):
        known = self.known()
        pins = [dict(file='lockstep_gate.c', line=1, kind='hash', expr='X', rva=0x10, length=8, sha256=SHA[0]),
                dict(file='lockstep_gravidar.h', line=2, kind='hash', expr='X', rva=0x10, length=8, sha256=SHA[0])]
        refs = [dict(file='lockstep_gate.c', line=3, kind='ref', expr='G', rva=0x5000, length=None, sha256=None),
                dict(file='lockstep_gate.c', line=4, kind='ref', expr='H', rva=0x6000, length=None, sha256=None)]
        rows = {r['win_rva']: r for r in m.build_rows(pins, refs, known=known)}
        entry = rows[0x10]
        self.assertEqual((entry['family'], entry['lin_site'], entry['site_method'], entry['win_pin']),
                         (['frame_gate', 'gravidar'], 0x110, 'entry', 'unchecked'))
        self.assertEqual((rows[0x5000]['lin_site'], rows[0x5000]['site_method']), (0x9000, 'known_data'))
        self.assertEqual(rows[0x6000]['site_method'], 'data_unmapped')
        summary = m.summarize(list(rows.values()))
        self.assertEqual(summary['families']['gravidar'], {'sites': 1, 'function_verified': 1, 'site_entry': 1, 'site_mapped': 1})
        self.assertEqual(summary['families']['frame_gate']['data_sites'], 2)


class KnownTable(unittest.TestCase):
    def test_committed_known_pairs_are_well_formed_and_cited(self):
        known = m.load_known()
        document = json.loads(m.KNOWN.read_text(encoding='utf-8'))
        self.assertEqual(document['build']['windows_sha256'], m.WINDOWS_BUILD['sha256'])
        self.assertEqual(document['build']['linux_sha256'], m.LINUX_BUILD['sha256'])
        self.assertGreaterEqual(len(known['function']), 20)
        for row in document['pairs']:
            self.assertRegex(row['win'], r'^[0-9a-f]+$'); self.assertRegex(row['lin'], r'^[0-9a-f]+$')
            for path in re.findall(r'(?:^|[\s;(])((?:reports|src)/[\w./-]+\.(?:txt|json|md|c|h|rs))', row['source']):
                self.assertTrue((ROOT / path).is_file(), f'{row["win"]}: missing citation {path}')
        functions = {w for w in known['function']}
        self.assertFalse(functions & set(known['inlined']), 'an inlined body cannot also be a function pair')


if __name__ == '__main__':
    unittest.main()
