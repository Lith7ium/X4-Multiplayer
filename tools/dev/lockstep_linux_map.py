"""Map the lockstep gate's pinned Windows sites to Linux candidates (offline, read-only).

Every site the Windows gate pins (hash_ok(image+RVA, length, sha) and {RVA, length,
"sha"} pin tables in src/native/engine) is attributed to its Windows function through
.pdata and mapped to a Linux X4 function candidate from, in order:
  1. tools/dev/lockstep_linux_known_pairs.json (pairs stated by earlier reports),
  2. an x4-re function pair table (data/xfer/pairs.tsv; --pairs), with its own
     round/confidence. Pairs contradicted by a known "not_equivalent" row are dropped.
With --disasm (needs the capstone package) it also
  - anchors call/jmp sites: the k-th call to T in the Windows function becomes the k-th
    call to T' in the Linux function when T<->T' is a known or high-confidence pair,
  - votes data/global RVAs through the data references of paired functions,
  - compares floating-point instruction profiles of each mapped function pair,
  - grades both methods against the known site/data pairs.
Both executables are optional and are only read; with them the tool verifies the pinned
X4 9.00 build (23660954 / 611726) and every Windows pin hash. Output rows hold
addresses, lengths and hashes only, never engine bytes. A mapping is a candidate for a
human port: Linux struct layouts, the System V ABI, TLS addressing and prologues differ,
so each hook still needs its own Linux pin and review (docs/ENGINE-INSTRUMENTATION.txt).

  python tools/dev/lockstep_linux_map.py --windows X4.exe --linux X4 \
      --pairs x4-re/data/xfer/pairs.tsv --disasm --tsv map.tsv --json map.json
"""
import argparse
import ast
import bisect
import collections
import functools
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'src/native/engine'
KNOWN = Path(__file__).resolve().with_name('lockstep_linux_known_pairs.json')
WINDOWS_BUILD = {'size': 55412768,
                 'sha256': '19750a6563889a970f434b5566eb396c6b2dc29ff814bd3e336f838176ad6891'}
LINUX_BUILD = {'size': 60459280,
               'sha256': '113f73d74570084f6ce18a659b365009eccf579fb66dbabb5dad2b1718045f20',
               'build_id': '77cf7ac0ff32d88ffd3db7eb6ea2e025aecb711d'}

# ----------------------------------------------------------------- gate sources

COMMENT = re.compile(r'/\*.*?\*/|//[^\n]*', re.S)
MACRO = re.compile(r'^[ \t]*#[ \t]*define[ \t]+([A-Za-z_]\w*)[ \t]+([^\n]+?)[ \t]*$', re.M)
ARG = r'((?:[^,(){};"]|\([^(){};"]*\))+)'
HASH_CALL = re.compile(r'hash_ok\(\s*image\s*\+\s*' + ARG + r',\s*' + ARG +
                       r',\s*(?:"([0-9a-f]{64})"|([A-Za-z_]\w*))\s*\)')
TABLE_ROW = re.compile(r'\{\s*' + ARG + r',\s*' + ARG + r',\s*"([0-9a-f]{64})"\s*\}')
TERM = r'(?:0x[0-9a-fA-F]+|\d+)[uUlL]*|[A-Z_][A-Z0-9_]*'
IMAGE_REF = re.compile(r'\bimage\s*\+\s*((?:' + TERM + r')(?:\s*[+-]\s*(?:' + TERM + r'))*)(?![\w>])')
SUFFIX = re.compile(r'\b(0x[0-9a-fA-F]+|\d+)(?:[uU](?:ll|LL|l|L)?|(?:ll|LL|l|L)[uU]?)\b')
CAST = re.compile(r'\(\s*(?:unsigned(?:\s+(?:int|long|long\s+long))?|int|size_t|uintptr_t|'
                  r'u?int(?:8|16|32|64)_t|ULONG|DWORD|ULONGLONG|LONG64|SIZE_T)\s*\)')
OPS = {ast.Add: lambda a, b: a + b, ast.Sub: lambda a, b: a - b, ast.Mult: lambda a, b: a * b,
       ast.LShift: lambda a, b: a << b, ast.RShift: lambda a, b: a >> b,
       ast.BitOr: lambda a, b: a | b, ast.BitAnd: lambda a, b: a & b}


def strip_comments(text):
    """Blank out C comments, keeping every newline so match offsets give line numbers."""
    return COMMENT.sub(lambda m: re.sub(r'[^\n]', ' ', m.group(0)), text)


def collect_macros(texts):
    """name -> list of raw replacement texts (all definitions across the gate)."""
    macros = collections.defaultdict(list)
    for text in texts:
        for m in MACRO.finditer(text):  # function-like macros never match (no blank after the name)
            macros[m.group(1)].append(m.group(2).strip())
    return dict(macros)


def evaluate(expr, macros, depth=0):
    """Integer value of a C constant expression over gate macros, or None.
    Only literals, + - * << >> | &, parentheses, simple integer casts and macros
    whose every definition has the same value are accepted."""
    if depth > 12: return None
    text = CAST.sub('', SUFFIX.sub(r'\1', expr.strip()))
    try:
        tree = ast.parse(text, mode='eval')
    except SyntaxError:
        return None

    def walk(node):
        if isinstance(node, ast.Expression): return walk(node.body)
        if isinstance(node, ast.Constant) and type(node.value) is int: return node.value
        if isinstance(node, ast.BinOp) and type(node.op) in OPS:
            left, right = walk(node.left), walk(node.right)
            return None if left is None or right is None else OPS[type(node.op)](left, right)
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, (ast.USub, ast.UAdd)):
            value = walk(node.operand)
            return None if value is None else (-value if isinstance(node.op, ast.USub) else value)
        if isinstance(node, ast.Name) and node.id in macros:
            values = {evaluate(raw, macros, depth + 1) for raw in macros[node.id]}
            return values.pop() if len(values) == 1 else None
        return None
    return walk(tree)


def macro_string(name, macros):
    values = {raw.strip('"') for raw in macros.get(name, ())}
    value = values.pop() if len(values) == 1 else None
    return value if value and re.fullmatch(r'[0-9a-f]{64}', value) else None


def gate_files(source):
    source = Path(source)
    return sorted(source.glob('lockstep_*.h')) + sorted(source.glob('lockstep_gate.c'))


def extract_sites(files):
    """Pins and image references of the gate.
    Returns (pins, refs, unresolved): pins are dicts file/line/kind/expr/rva/length/sha256,
    refs are image+EXPR uses without a hash at that expression."""
    texts = {Path(f).name: strip_comments(Path(f).read_text(encoding='utf-8', errors='replace'))
             for f in files}
    macros = collect_macros(texts.values())
    pins, refs, unresolved = [], [], []

    def line(text, pos): return text.count('\n', 0, pos) + 1
    for name, text in texts.items():
        spans = []
        for m in HASH_CALL.finditer(text):
            sha = m.group(3) or macro_string(m.group(4), macros)
            rva, length = evaluate(m.group(1), macros), evaluate(m.group(2), macros)
            row = dict(file=name, line=line(text, m.start()), kind='hash', expr=m.group(1).strip(),
                       rva=rva, length=length, sha256=sha)
            (pins if None not in (rva, length, sha) else unresolved).append(row)
            spans.append(m.span())
        for m in TABLE_ROW.finditer(text):
            rva, length = evaluate(m.group(1), macros), evaluate(m.group(2), macros)
            row = dict(file=name, line=line(text, m.start()), kind='table', expr=m.group(1).strip(),
                       rva=rva, length=length, sha256=m.group(3))
            (pins if None not in (rva, length) else unresolved).append(row)
        for m in IMAGE_REF.finditer(text):
            if any(a <= m.start() < b for a, b in spans): continue
            rva = evaluate(m.group(1), macros)
            if rva is None:
                unresolved.append(dict(file=name, line=line(text, m.start()), kind='ref',
                                       expr=m.group(1).strip(), rva=None, length=None, sha256=None))
            else:
                refs.append(dict(file=name, line=line(text, m.start()), kind='ref',
                                 expr=m.group(1).strip(), rva=rva, length=None, sha256=None))
    return pins, refs, unresolved


FAMILIES = (
    ('infrastructure', r'thunk|wire$|^state$|^params$|getup_diagnostic|world_hash|world_trace'),
    ('frame_gate', r'^gate$|boot_hold|timer_resolution|^seed$'),
    ('tls_rng', r'thread_rng|boot_rng_anchor|job_rng|render_rng|player_rng|npc_rng|camera_perturb'),
    ('gravidar', r'gravidar|radar|ai_schedule'),
    ('pathing', r'path_determinism|travel_|agent|actor_all|solver|spline|flight'),
    ('events', r'event_order|find_order|interrupt'),
    ('md', r'md_resume_rng|registry_md|script_rng_md'),
    ('script_rng', r'script_rng|ai_wait_rng|region_rng|world_find_rng|world_position_rng|world_wait_rng|ai$'),
    ('bullets_physics', r'bullet|weapon|damage|death|contact|shape|body|mass|ship$|dock'),
    ('nav', r'nav|velocity|world_safe_position|world_position'),
    ('attention', r'attention|view_points'),
    ('created_ids', r'creat|world_identity|world_unknown|registry'),
    ('pilot_player', r'pilot|player|request|transition'),
    ('npc', r'npc_|nested_action'),
    ('world_queue', r'world_'),
    ('rotation_portable', r'rotation|portable|snapshot'),
)


def family(filename):
    stem = re.sub(r'^lockstep_|\.(h|c)$', '', Path(filename).name)
    for name, pattern in FAMILIES:
        if re.search(pattern, stem): return name
    return 'other'

# ----------------------------------------------------------------- Windows PE


class PeImage:
    """Read-only PE32+ view: sections, RVA reads and .pdata functions."""

    def __init__(self, data):
        self.data = data
        if data[:2] != b'MZ': raise ValueError('not a PE image')
        pe = struct.unpack_from('<I', data, 0x3c)[0]
        if data[pe:pe + 4] != b'PE\0\0': raise ValueError('missing PE signature')
        machine, count, self.timestamp = struct.unpack_from('<HHI', data, pe + 4)
        optional_size = struct.unpack_from('<H', data, pe + 20)[0]
        opt = pe + 24
        if machine != 0x8664 or struct.unpack_from('<H', data, opt)[0] != 0x20b:
            raise ValueError('not an x64 PE32+ image')
        self.image_size = struct.unpack_from('<I', data, opt + 56)[0]
        dirs = struct.unpack_from('<I', data, opt + 108)[0]
        self.exception = struct.unpack_from('<II', data, opt + 112 + 3 * 8) if dirs > 3 else (0, 0)
        self.sections = []
        for i in range(count):
            at = opt + optional_size + 40 * i
            name = data[at:at + 8].rstrip(b'\0').decode('ascii', 'replace')
            vsize, va, raw_size, raw = struct.unpack_from('<IIII', data, at + 8)
            flags = struct.unpack_from('<I', data, at + 36)[0]
            self.sections.append((name, va, max(vsize, raw_size), raw, raw_size, flags))
        self._functions()

    def section(self, rva):
        for name, va, size, *_ in self.sections:
            if va <= rva < va + size: return name
        return None

    def is_code(self, rva):
        return any(va <= rva < va + size and flags & 0x20000000
                   for _, va, size, _, _, flags in self.sections)

    def read(self, rva, length):
        for _, va, _, raw, raw_size, _ in self.sections:
            if va <= rva and rva + length <= va + raw_size: return self.data[raw + rva - va:raw + rva - va + length]
        return None

    def _functions(self):
        rva, size = self.exception
        table = self.read(rva, size) if size else None
        if table is None:
            pdata = [s for s in self.sections if s[0] == '.pdata']
            table = self.data[pdata[0][3]:pdata[0][3] + pdata[0][4]] if pdata else b''
        entries = []
        for i in range(0, len(table) - 11, 12):
            begin, end, unwind = struct.unpack_from('<III', table, i)
            if begin == 0 and end == 0: break
            entries.append((begin, end, unwind))
        self.entries = sorted((b, e, self._root(b, u)) for b, e, u in entries)
        self._starts = [b for b, _, _ in self.entries]
        self._ranges = collections.defaultdict(list)
        for b, e, r in self.entries: self._ranges[r].append((b, e))

    def _root(self, begin, unwind):
        """Begin of the primary function of a (possibly chained) unwind entry."""
        for _ in range(32):
            if unwind & 1:  # indirect: points at another RUNTIME_FUNCTION
                ref = self.read(unwind & ~1, 12)
                if not ref: return begin
                begin, _, unwind = struct.unpack('<III', ref)
                continue
            head = self.read(unwind, 4)
            if not head or not (head[0] >> 3) & 4: return begin  # UNW_FLAG_CHAININFO
            chained = self.read(unwind + 4 + ((head[2] + 1) & ~1) * 2, 12)
            if not chained: return begin
            begin, _, unwind = struct.unpack('<III', chained)
        return begin

    def function_of(self, rva):
        """(root, fragment begin, fragment end) of the unwind range holding rva, or None."""
        i = bisect.bisect_right(self._starts, rva) - 1
        if i >= 0 and self.entries[i][0] <= rva < self.entries[i][1]:
            b, e, r = self.entries[i]
            return r, b, e
        return None

    def ranges(self, root):
        return sorted(self._ranges.get(root, ()))

    def data_ranges(self):
        return [(va, va + size) for name, va, size, *_ in self.sections if name in ('.rdata', '.data', '_RDATA')]

    def data_class(self, rva):
        """'ro' (.rdata: constants, strings, vtables) or 'rw' (.data/.bss globals)."""
        name = self.section(rva)
        return 'ro' if name in ('.rdata', '_RDATA') else 'rw' if name == '.data' else None

# ----------------------------------------------------------------- Linux ELF


def _uleb(data, at):
    value = shift = 0
    while True:
        byte = data[at]; at += 1
        value |= (byte & 0x7f) << shift; shift += 7
        if byte < 0x80: return value, at


def _encoded(data, at, encoding, address):
    """Decode one DW_EH_PE value; returns (value, next offset)."""
    fmt = encoding & 0x0f
    size, code = {0x00: (8, '<Q'), 0x02: (2, '<H'), 0x03: (4, '<I'), 0x04: (8, '<Q'),
                  0x0a: (2, '<h'), 0x0b: (4, '<i'), 0x0c: (8, '<q')}[fmt]
    value = struct.unpack_from(code, data, at)[0]
    if encoding & 0x70 == 0x10: value += address + at  # pcrel
    elif encoding & 0x70 not in (0x00,): raise ValueError(f'unsupported pointer encoding {encoding:#x}')
    return value, at + size


def parse_eh_frame(hdr, hdr_addr, frame, frame_addr):
    """Function ranges [(start, end)] from .eh_frame_hdr's binary search table and the
    pc_range of each FDE it points to (CIE augmentation parsed for the FDE encoding)."""
    if hdr[0] != 1 or hdr[1] & 0x0f not in (0x03, 0x0b) or hdr[2] != 0x03 or hdr[3] != 0x3b:
        raise ValueError('unsupported .eh_frame_hdr encoding')
    count = struct.unpack_from('<I', hdr, 8)[0]
    cie_encoding, ranges = {}, []
    for i in range(count):
        loc, fde = struct.unpack_from('<ii', hdr, 12 + 8 * i)
        start, at = loc + hdr_addr, fde + hdr_addr - frame_addr
        if struct.unpack_from('<I', frame, at)[0] == 0xffffffff: raise ValueError('64-bit DWARF FDE')
        cie_at = at + 4 - struct.unpack_from('<I', frame, at + 4)[0]
        if cie_at not in cie_encoding: cie_encoding[cie_at] = _cie_fde_encoding(frame, cie_at)
        enc = cie_encoding[cie_at]
        begin, nxt = _encoded(frame, at + 8, enc, frame_addr)
        length, _ = _encoded(frame, nxt, enc & 0x0f, frame_addr)
        if begin != start: raise ValueError(f'FDE {start:#x} begins at {begin:#x}')
        ranges.append((start, start + length))
    return sorted(ranges)


def _cie_fde_encoding(frame, at):
    if struct.unpack_from('<I', frame, at + 4)[0] != 0: raise ValueError('FDE does not reference a CIE')
    version, p = frame[at + 8], at + 9
    end = frame.index(b'\0', p)
    augmentation, p = frame[p:end].decode('ascii'), end + 1
    _, p = _uleb(frame, p)  # code alignment
    _, p = _uleb(frame, p)  # data alignment (sign irrelevant here)
    p = p + 1 if version == 1 else _uleb(frame, p)[1]
    if not augmentation.startswith('z'): return 0x00
    _, p = _uleb(frame, p)
    for ch in augmentation[1:]:
        if ch == 'R': return frame[p]
        if ch == 'L': p += 1
        elif ch == 'P':
            enc = frame[p]; _, p = _encoded(frame, p + 1, enc & 0x0f, 0)
        elif ch in 'SB': continue
        else: raise ValueError(f'unsupported CIE augmentation {augmentation!r}')
    return 0x00


class ElfImage:
    """Read-only ELF64 x86-64 view: segments, sections, FDE functions, dynsym, PLT names."""

    def __init__(self, data):
        self.data = data
        if data[:4] != b'\x7fELF' or data[4] != 2 or data[5] != 1: raise ValueError('not an ELF64 LE image')
        self.type, machine = struct.unpack_from('<HH', data, 16)
        if machine != 62: raise ValueError('not x86-64')
        phoff, shoff = struct.unpack_from('<QQ', data, 32)
        phentsize, phnum, shentsize, shnum, shstrndx = struct.unpack_from('<HHHHH', data, 54)
        self.segments = []
        for i in range(phnum):
            p_type, _, offset, vaddr, _, filesz, memsz, _ = struct.unpack_from('<IIQQQQQQ', data, phoff + i * phentsize)
            if p_type == 1: self.segments.append((vaddr, offset, filesz, memsz))
        raw = [struct.unpack_from('<IIQQQQIIQQ', data, shoff + i * shentsize) for i in range(shnum)]
        names = raw[shstrndx][4] if shnum else 0
        self.sections = {}
        for name, s_type, flags, addr, offset, size, link, info, _, entsize in raw:
            label = data[names + name:data.index(b'\0', names + name)].decode('ascii', 'replace')
            self.sections[label] = dict(type=s_type, flags=flags, addr=addr, offset=offset, size=size,
                                        link=link, entsize=entsize)
        hdr, frame = self.sections.get('.eh_frame_hdr'), self.sections.get('.eh_frame')
        self.functions = parse_eh_frame(self.section_bytes('.eh_frame_hdr'), hdr['addr'],
                                        self.section_bytes('.eh_frame'), frame['addr']) if hdr and frame else []
        self._starts = [s for s, _ in self.functions]
        self.start_set = set(self._starts)
        self.dynsym, self.plt = self._symbols()

    def section_bytes(self, name):
        s = self.sections[name]
        return self.data[s['offset']:s['offset'] + s['size']]

    def read(self, va, length):
        for vaddr, offset, filesz, _ in self.segments:
            if vaddr <= va and va + length <= vaddr + filesz: return self.data[offset + va - vaddr:offset + va - vaddr + length]
        return None

    def build_id(self):
        note = self.sections.get('.note.gnu.build-id')
        if not note: return None
        blob = self.section_bytes('.note.gnu.build-id')
        namesz, descsz, _ = struct.unpack_from('<III', blob, 0)
        return blob[12 + ((namesz + 3) & ~3):][:descsz].hex()

    def function_of(self, va):
        i = bisect.bisect_right(self._starts, va) - 1
        if i >= 0 and self.functions[i][0] <= va < self.functions[i][1]: return self.functions[i]
        return None

    def function_at(self, va):
        i = bisect.bisect_left(self._starts, va)
        return self.functions[i] if i < len(self._starts) and self._starts[i] == va else None

    def data_ranges(self):
        return [(s['addr'], s['addr'] + s['size']) for name, s in self.sections.items()
                if s['addr'] and name.startswith(('.rodata', '.data', '.bss'))]

    def data_class(self, va):
        """'ro' (.rodata, .data.rel.ro: constants, strings, vtables) or 'rw' (.data/.bss)."""
        for name, s in self.sections.items():
            if s['addr'] and s['addr'] <= va < s['addr'] + s['size']:
                if name.startswith(('.rodata', '.data.rel.ro')): return 'ro'
                if name.startswith(('.data', '.bss')): return 'rw'
        return None

    def _symbols(self):
        if '.dynsym' not in self.sections: return {}, {}
        strtab = self.sections['.dynstr']
        names, table = [], self.section_bytes('.dynsym')
        dynsym = {}
        for i in range(0, len(table), 24):
            name, info, _, shndx, value, _ = struct.unpack_from('<IBBHQQ', table, i)
            text = self.data[strtab['offset'] + name:self.data.index(b'\0', strtab['offset'] + name)].decode('ascii', 'replace')
            names.append(text)
            if shndx and info & 0xf == 2 and value: dynsym.setdefault(value, text)
        slots = {}
        for section in ('.rela.plt', '.rela.dyn'):
            if section not in self.sections: continue
            rela = self.section_bytes(section)
            for i in range(0, len(rela), 24):
                offset, info, _ = struct.unpack_from('<QQq', rela, i)
                if info >> 32: slots[offset] = names[info >> 32]
        plt = {}
        for section, stride in (('.plt', 16), ('.plt.got', 8), ('.plt.sec', 16)):
            if section not in self.sections: continue
            s, blob = self.sections[section], self.section_bytes(section)
            for at in range(0, len(blob) - 5, stride):
                for k in (0, 4):  # plain stub or endbr64-prefixed stub
                    if blob[at + k:at + k + 2] == b'\xff\x25':
                        slot = s['addr'] + at + k + 6 + struct.unpack_from('<i', blob, at + k + 2)[0]
                        if slot in slots: plt[s['addr'] + at] = slots[slot]
                        break
        return dynsym, plt

# ----------------------------------------------------------------- pair tables


def load_xfer(path):
    """x4-re data/xfer/pairs.tsv: win_rva, lin_va, round, confidence, score, evidence."""
    pairs = {}
    with Path(path).open(encoding='utf-8') as source:
        for line in source:
            if not line.strip() or line.startswith('#'): continue
            cells = line.rstrip('\n').split('\t')
            pairs[int(cells[0], 16)] = dict(lin=int(cells[1], 16), round=cells[2], confidence=cells[3],
                                            score=float(cells[4]) if len(cells) > 4 and cells[4] else 0.0)
    return pairs


KINDS = ('function', 'not_equivalent', 'inlined', 'site', 'data')


def load_known(path=KNOWN):
    document = json.loads(Path(path).read_text(encoding='utf-8'))
    if document.get('schema') != 'x4-lockstep-linux-known-pairs-v1': raise ValueError('unknown known-pairs schema')
    known = {kind: {} for kind in KINDS}
    for row in document['pairs']:
        if row['kind'] not in KINDS or row.get('confidence') not in ('verified', 'inferred') or not row.get('source'):
            raise ValueError(f'invalid known pair {row}')
        entry = dict(row, win=int(row['win'], 16), lin=int(row['lin'], 16))
        bucket = known[row['kind']]
        if row['kind'] == 'not_equivalent': bucket.setdefault(entry['win'], []).append(entry)
        elif entry['win'] in bucket: raise ValueError(f'duplicate known {row["kind"]} pair {row["win"]}')
        else: bucket[entry['win']] = entry
    return known


def function_candidate(win, known, xfer):
    """Linux function for a Windows function root: (va, source, confidence, note)."""
    if win in known['function']:
        k = known['function'][win]
        return k['lin'], 'known', k['confidence'], k['name']
    if win in known['inlined']:
        k = known['inlined'][win]
        return None, 'inlined', 'none', f"inlined into Linux {k['lin']:#x}: {k['name']}"
    if win in xfer:
        x = xfer[win]
        wrong = [k for k in known['not_equivalent'].get(win, ()) if k['lin'] == x['lin']]
        if wrong: return None, 'xfer_contradicted', 'none', wrong[0]['name']
        return x['lin'], f"xfer:{x['round']}", x['confidence'], f"score {x['score']:g}"
    return None, 'unmapped', 'none', ''


def grade_xfer(known, xfer):
    """How the automatic pair table fares on the known function pairs."""
    result = collections.Counter(); disagreements = []
    for win, k in known['function'].items():
        if win not in xfer: result['missing'] += 1
        elif xfer[win]['lin'] == k['lin']: result[f"agree_{xfer[win]['confidence']}"] += 1
        else:
            result[f"disagree_{xfer[win]['confidence']}"] += 1
            disagreements.append(dict(win=f'{win:x}', known=f"{k['lin']:x}", xfer=f"{xfer[win]['lin']:x}",
                                      round=xfer[win]['round'], confidence=xfer[win]['confidence']))
    for win, rows in known['not_equivalent'].items():
        for k in rows:
            if win in xfer and xfer[win]['lin'] == k['lin']: result[f"known_wrong_{xfer[win]['confidence']}"] += 1
    for win, k in known['inlined'].items():
        if win in xfer: result[f"inlined_paired_{xfer[win]['confidence']}"] += 1
    return dict(result), disagreements

# ----------------------------------------------------------------- instruction facts (pure)


HEX = re.compile(r'0x[0-9a-f]+')
LIBM = frozenset('acos acosf asin asinf atan atanf atan2 atan2f cos cosf sin sinf sincos sincosf tan tanf '
                 'exp expf exp2 exp2f log logf log2 log2f log10 log10f pow powf cbrt cbrtf hypot hypotf'.split())
RIP = re.compile(r'\[rip ([+-]) (0x[0-9a-f]+)\]')
WIDTH = re.compile(r'\b(byte|word|dword|qword|xmmword|ymmword) ptr')
WIDTHS = {'byte': 1, 'word': 2, 'dword': 4, 'qword': 8, 'xmmword': 16, 'ymmword': 32}


@functools.lru_cache(maxsize=None)
def fp_class(mnemonic):
    m = mnemonic[1:] if mnemonic.startswith('v') and not mnemonic.startswith(('vfm', 'vfnm')) else mnemonic
    if mnemonic.startswith(('vfmadd', 'vfmsub', 'vfnmadd', 'vfnmsub')): return 'fma'
    if re.fullmatch(r'div[sp][sd]', m): return 'div'
    if re.fullmatch(r'sqrt[sp][sd]', m): return 'sqrt'
    if m in ('rcpss', 'rcpps', 'rsqrtss', 'rsqrtps'): return 'approx'
    if m in ('cvtss2sd', 'cvtps2pd'): return 'widen'
    if m in ('cvtsd2ss', 'cvtpd2ps'): return 'narrow'
    if m in ('ldmxcsr', 'stmxcsr', 'fldcw'): return 'fpenv'
    if mnemonic.startswith('f') and mnemonic not in ('femms',) and mnemonic[:3] != 'fxs': return 'x87'
    return None


def facts(insns, data_ranges=(), plt=None):
    """Calls, data references and FP classes of decoded instructions
    [(address, size, mnemonic, op_str)]. Data references (address, mnemonic, target,
    access width or None) are RIP-relative operands, or absolute displacements/immediates
    inside data_ranges (non-PIE ELF)."""
    calls, refs, fp, libm = [], [], collections.Counter(), collections.Counter()
    starts = sorted(data_ranges)
    lows = [a for a, _ in starts]

    def in_data(value):
        i = bisect.bisect_right(lows, value) - 1
        return i >= 0 and starts[i][0] <= value < starts[i][1]
    for address, size, mnemonic, op in insns:
        kind = fp_class(mnemonic)
        if kind: fp[kind] += 1
        if mnemonic in ('call', 'jmp') and re.fullmatch(r'0x[0-9a-f]+', op):
            target = int(op, 16)
            calls.append((address, mnemonic, target))
            if plt and plt.get(target) in LIBM: libm[plt[target]] += 1
            continue
        width = WIDTH.search(op)
        width = WIDTHS[width.group(1)] if width else None
        rip = RIP.search(op)
        if rip:
            disp = int(rip.group(2), 16)
            target = address + size + (disp if rip.group(1) == '+' else -disp)
            if mnemonic in ('call', 'jmp'): calls.append((address, mnemonic + '*', target))
            elif in_data(target) or not starts: refs.append((address, mnemonic, target, width))
            continue
        for value in HEX.findall(op):
            value = int(value, 16)
            if starts and in_data(value): refs.append((address, mnemonic, value, width if '[' in op else None))
    return dict(calls=calls, refs=refs, fp=dict(fp), libm=dict(libm))


def call_anchor(site, length, win_calls, lin_calls, target_map):
    """Linux address of the call/jmp instruction pinned at [site, site+length).
    win_calls/lin_calls: ordered (address, mnemonic, target) of the paired functions;
    target_map: Windows target -> Linux target. Returns dict or None."""
    end = site + max(length or 1, 1)
    anchor = next((c for c in win_calls if site <= c[0] < end and not c[1].endswith('*')), None)
    if anchor is None: return None
    _, mnemonic, target = anchor
    linux_target = target_map(target)
    if linux_target is None: return dict(method='call_target_unmapped', win_anchor=anchor[0], target=target)
    same_win = [c for c in win_calls if c[2] == target and c[1] == mnemonic]
    same_lin = [c for c in lin_calls if c[2] == linux_target and c[1] == mnemonic]
    if not same_lin:  # a tail call may compile to a call and vice versa
        same_win = [c for c in win_calls if c[2] == target and not c[1].endswith('*')]
        same_lin = [c for c in lin_calls if c[2] == linux_target and not c[1].endswith('*')]
    k = next(i for i, c in enumerate(same_win) if c[0] == anchor[0]) if anchor in same_win else 0
    base = dict(win_anchor=anchor[0], target=target, linux_target=linux_target,
                windows_calls=len(same_win), linux_calls=len(same_lin))
    if len(same_lin) == len(same_win) and same_lin:
        return dict(base, method='call_ordinal', lin_site=same_lin[k][0],
                    confidence='high' if len(same_lin) == 1 else 'medium')
    if len(same_lin) == 1:
        return dict(base, method='call_unique', lin_site=same_lin[0][0], confidence='low')
    return dict(base, method='call_count_mismatch')


def data_vote(target, referencing, lin_facts, admissible=None):
    """Linux candidates for a Windows data RVA. referencing: [(win_function, lin_function,
    ordinal of target among the Windows function's distinct data refs of its class,
    Windows access shape {(mnemonic, width)})]; lin_facts: lin_function -> facts;
    admissible: predicate on Linux addresses (same data class). Per referencing pair a
    candidate gains a strong vote at the same ordinal with a compatible access width and
    a weak vote for any reference; the access shape (Jaccard of access mnemonics plus
    width agreement) only breaks ties. Votes are leads: a struct reached through a base register on one side
    and by absolute field address on the other gives no usable vote."""
    if not referencing: return None
    strong, shape, weak = collections.Counter(), collections.Counter(), collections.Counter()
    for _, lin, ordinal, win_shape in referencing:
        refs = lin_facts.get(lin, {}).get('refs', ())
        distinct = [v for v in dict.fromkeys(r[2] for r in refs) if admissible is None or admissible(v)]
        win_mn, win_w = {m for m, _ in win_shape}, {w for _, w in win_shape if w}
        for index, value in enumerate(distinct):
            lin_shape = {(r[1], r[3]) for r in refs if r[2] == value}
            lin_mn, lin_w = {m for m, _ in lin_shape}, {w for _, w in lin_shape if w}
            widths_ok = not win_w or not lin_w or bool(win_w & lin_w)
            weak[value] += 1
            shape[value] += len(win_mn & lin_mn) / len(win_mn | lin_mn) + (1 if win_w and win_w & lin_w else 0)
            if index == ordinal and widths_ok: strong[value] += 1
    if not weak: return dict(method='data_no_linux_refs', voters=len(referencing))
    key = lambda v: (strong[v], weak[v], round(shape[v], 6))
    ranked = sorted(weak, key=lambda v: (tuple(-x for x in key(v)), v))
    best = ranked[0]
    unique = len(ranked) == 1 or key(best) > key(ranked[1])
    agreeing = strong[best]
    confidence = ('high' if unique and agreeing >= 2 and agreeing * 2 > len(referencing) else
                  'medium' if unique and agreeing >= 1 else 'low')
    return dict(method='data_vote', lin_site=best, confidence=confidence, voters=len(referencing),
                strong=agreeing, weak=weak[best])

# ----------------------------------------------------------------- disassembly stage


def _decode(image, ranges, md):
    insns = []
    for begin, end in ranges:
        code = image.read(begin, end - begin)
        if code: insns.extend(md.disasm_lite(code, begin))
    return insns


def _leaf(image, address, md, limit=1024):
    """An MSVC leaf function has no unwind entry: decode to the int3 padding after a ret/jmp."""
    insns = []
    for insn in md.disasm_lite(image.read(address, limit) or b'', address):
        if insn[2] == 'int3': break
        insns.append(insn)
    return insns


def disassemble(pe, elf, functions):
    """facts for Windows roots and Linux starts: {('w', rva)|('l', va): facts}."""
    try:
        import capstone
    except ImportError:
        raise SystemExit('--disasm needs the capstone package (pip install capstone)')
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    win_data, lin_data = pe.data_ranges(), elf.data_ranges()
    out = {}
    for side, address in functions:
        if side == 'w':
            ranges = pe.ranges(address)
            out[(side, address)] = facts(_decode(pe, ranges, md) if ranges else _leaf(pe, address, md), win_data)
        else:
            fn = elf.function_at(address)
            out[(side, address)] = facts(_decode(elf, [fn] if fn else [], md), lin_data, elf.plt)
    return out

# ----------------------------------------------------------------- mapping


def build_rows(pins, refs, pe=None, elf=None, xfer=None, known=None):
    """One row per unique Windows RVA used by the gate."""
    xfer, known = xfer or {}, known or {kind: {} for kind in KINDS}
    by_rva = collections.defaultdict(list)
    for item in pins + refs: by_rva[item['rva']].append(item)
    rows = []
    for rva in sorted(by_rva):
        items = by_rva[rva]
        modules = sorted({i['file'] for i in items})
        pinned = [i for i in items if i['sha256']]
        row = dict(win_rva=rva, family=sorted({family(m) for m in modules}),
                   modules=modules, kinds=sorted({i['kind'] for i in items}),
                   length=max((i['length'] for i in pinned), default=None),
                   pins=len({(i['length'], i['sha256']) for i in pinned}), win_pin=None,
                   section=pe.section(rva) if pe else None, is_code=None, win_function=None, offset=None,
                   lin_function=None, function_source=None, function_confidence='none', lin_start_ok=None,
                   lin_site=None, site_method=None, site_confidence='none', notes='')
        if pe and pinned:
            ok = [hashlib.sha256(pe.read(i['rva'], i['length']) or b'').hexdigest() == i['sha256'] for i in pinned]
            row['win_pin'] = 'ok' if all(ok) else 'mismatch'
        elif pinned: row['win_pin'] = 'unchecked'
        else: row['win_pin'] = 'unpinned'
        if rva in known['data']:
            k = known['data'][rva]
            row.update(lin_site=k['lin'], site_method='known_data', site_confidence=k['confidence'], notes=k['name'])
        code = pe.is_code(rva) if pe else (rva in known['function'] or rva in xfer or rva in known['site']
                                            or rva in known['inlined'])
        row['is_code'] = bool(code)
        if pe and row['section'] in (None, '.reloc', '.rsrc'):
            row.update(site_method='not_a_site', notes='image bound or header arithmetic, not an engine site')
        elif code:
            fn = pe.function_of(rva) if pe else None
            root = fn[0] if fn else (rva if (rva in known['function'] or rva in xfer or rva in known['inlined']) else None)
            if root is None: row.update(site_method='no_function', notes='no unwind entry (MSVC leaf); function start unknown')
            else:
                row.update(win_function=root, offset=rva - root)
                lin, source, confidence, note = function_candidate(root, known, xfer)
                row.update(lin_function=lin, function_source=source, function_confidence=confidence)
                row['notes'] = note
                if lin is not None and elf: row['lin_start_ok'] = elf.function_at(lin) is not None
            if rva in known['site']:
                k = known['site'][rva]
                row.update(lin_site=k['lin'], site_method='known_site', site_confidence=k['confidence'])
            elif row['offset'] == 0 and row['lin_function'] is not None:
                row.update(lin_site=row['lin_function'], site_method='entry', site_confidence=row['function_confidence'])
            elif row['win_function'] is not None:
                row['site_method'] = 'manual'
        elif row['site_method'] is None:
            row['site_method'] = 'data_unmapped'
        rows.append(row)
    return rows


def target_mapper(known, xfer, minimum=('verified', 'inferred', 'high')):
    def mapped(target):
        if target in known['function']: return known['function'][target]['lin']
        x = xfer.get(target)
        if x and x['confidence'] in minimum and not any(k['lin'] == x['lin'] for k in known['not_equivalent'].get(target, ())):
            return x['lin']
        return None
    return mapped


def deepen(rows, pe, elf, xfer, known):
    """--disasm: call anchors, data votes, FP profiles, and grading against known pairs."""
    pairs = {w: x['lin'] for w, x in xfer.items() if x['confidence'] in ('high', 'medium')}
    for w, k in known['function'].items(): pairs[w] = k['lin']
    for w, rows_ in known['not_equivalent'].items():
        if any(pairs.get(w) == k['lin'] for k in rows_): pairs.pop(w, None)
    wanted = {('w', w) for w in pairs} | {('l', l) for l in pairs.values()}
    wanted |= {('w', r['win_function']) for r in rows if r['win_function'] is not None}
    wanted |= {('l', r['lin_function']) for r in rows if r['lin_function'] is not None}
    info = disassemble(pe, elf, sorted(wanted))
    mapper = target_mapper(known, xfer)
    # data references of every paired function, for votes; ordinals count only references
    # of the same class (read-only versus writable) on each side
    lin_class = functools.lru_cache(maxsize=None)(elf.data_class)
    referencing = collections.defaultdict(list)
    inferred = {w for w, k in known['function'].items() if k['confidence'] == 'inferred'}
    for w, l in pairs.items():
        if w in inferred: continue  # same role, different body: reference positions do not align
        distinct = list(dict.fromkeys(r[2] for r in info.get(('w', w), {}).get('refs', ())))
        wrefs = info.get(('w', w), {}).get('refs', ())
        for cls in ('ro', 'rw'):
            same = [t for t in distinct if pe.data_class(t) == cls]
            for ordinal, target in enumerate(same):
                referencing[target].append((w, l, ordinal, {(r[1], r[3]) for r in wrefs if r[2] == target}))
    lin_facts = {l: f for (side, l), f in info.items() if side == 'l'}

    def admissible_for(win_rva):
        cls = pe.data_class(win_rva)
        return lambda v: lin_class(v) == cls
    for row in rows:
        w, l = row['win_function'], row['lin_function']
        if w is not None and l is not None:
            wf, lf = info.get(('w', w), {}), info.get(('l', l), {})
            row['fp_win'] = profile(wf.get('fp', {}))
            row['fp_lin'] = profile(lf.get('fp', {}), lf.get('libm', {}))
            row['fp_same'] = all(wf.get('fp', {}).get(k, 0) == lf.get('fp', {}).get(k, 0)
                                 for k in ('div', 'sqrt', 'approx', 'widen', 'narrow', 'fma'))
            if row['site_method'] == 'manual':
                anchor = call_anchor(row['win_rva'], row['length'], wf.get('calls', ()), lf.get('calls', ()), mapper)
                if anchor:
                    row['site_method'] = anchor['method']
                    if 'lin_site' in anchor:
                        row.update(lin_site=anchor['lin_site'], site_confidence=anchor['confidence'])
                        if anchor['win_anchor'] != row['win_rva']:
                            row['notes'] = (row['notes'] + '; ' if row['notes'] else '') + \
                                f"anchored at Windows call {anchor['win_anchor']:#x}"
        if row['site_method'] == 'data_unmapped':
            vote = data_vote(row['win_rva'], referencing.get(row['win_rva'], ()), lin_facts, admissible_for(row['win_rva']))
            if vote:
                row['site_method'] = vote['method']
                if 'lin_site' in vote: row.update(lin_site=vote['lin_site'], site_confidence=vote['confidence'])
    grades = collections.Counter(); misses = []
    for w, k in known['site'].items():
        fn = pe.function_of(w)
        if not fn or fn[0] not in pairs: grades['site_function_unpaired'] += 1; continue
        anchor = call_anchor(w, 1, info.get(('w', fn[0]), {}).get('calls', ()),
                             info.get(('l', pairs[fn[0]]), {}).get('calls', ()), mapper)
        if not anchor or 'lin_site' not in anchor: grades['site_not_a_call_or_unanchored'] += 1
        elif anchor['lin_site'] == k['lin']: grades[f"site_agree_{anchor['confidence']}"] += 1
        else:
            grades[f"site_disagree_{anchor['confidence']}"] += 1
            misses.append(dict(kind='site', win=f'{w:x}', known=f"{k['lin']:x}", got=f"{anchor['lin_site']:x}"))
    for w, k in known['data'].items():
        vote = data_vote(w, referencing.get(w, ()), lin_facts, admissible_for(w))
        if not vote or 'lin_site' not in vote: grades['data_no_vote'] += 1
        elif vote['lin_site'] == k['lin']: grades[f"data_agree_{vote['confidence']}"] += 1
        else:
            grades[f"data_disagree_{vote['confidence']}"] += 1
            misses.append(dict(kind='data', win=f'{w:x}', known=f"{k['lin']:x}", got=f"{vote['lin_site']:x}"))
    fp = collections.Counter()
    for (side, address), f in info.items():
        if side == 'w' and address in pairs and ('l', pairs[address]) in info:
            g = info[('l', pairs[address])]
            dims = ('div', 'sqrt', 'approx', 'widen', 'narrow', 'fma')
            if any(f['fp'].get(d) or g['fp'].get(d) for d in dims):
                fp['fp_pairs'] += 1
                fp['fp_same'] += all(f['fp'].get(d, 0) == g['fp'].get(d, 0) for d in dims)
                fp['fewer_div_on_windows'] += f['fp'].get('div', 0) < g['fp'].get('div', 0)
                fp['more_div_on_windows'] += f['fp'].get('div', 0) > g['fp'].get('div', 0)
            fp['libm_calls_linux'] += sum(g['libm'].values())
    return dict(grades), misses, dict(fp)


def profile(fp, libm=None):
    parts = [f'{k}{fp[k]}' for k in ('div', 'sqrt', 'approx', 'widen', 'narrow', 'fma', 'fpenv', 'x87') if fp.get(k)]
    if libm: parts.append('libm' + str(sum(libm.values())))
    return ','.join(parts) or '-'

# ----------------------------------------------------------------- output


COLUMNS = ('family', 'modules', 'kinds', 'win_rva', 'length', 'pins', 'win_pin', 'section', 'win_function',
           'offset', 'lin_function', 'function_source', 'function_confidence', 'lin_start_ok', 'lin_site',
           'site_method', 'site_confidence', 'fp_win', 'fp_lin', 'fp_same', 'notes')


def cell(name, value):
    if value is None: return ''
    if name in ('win_rva', 'win_function', 'lin_function', 'lin_site', 'offset') and isinstance(value, int):
        return f'{value:#x}'
    if isinstance(value, list): return ','.join(value)
    return str(value)


def summarize(rows):
    """Per family (a site used by several families counts in each): sites, functions by
    mapping confidence, site methods and data mappings."""
    by_family = collections.defaultdict(collections.Counter)
    for r in rows:
        if r['site_method'] == 'not_a_site': continue
        for name in r['family']:
            f = by_family[name]
            f['sites'] += 1
            if r['win_pin'] == 'ok': f['pinned'] += 1
            if not r['is_code']:
                f['data_sites'] += 1
                f[f"data_{r['site_confidence']}"] += 1
                continue
            f[f"function_{r['function_confidence']}"] += 1
            f[f"site_{r['site_method']}"] += 1
            if r['lin_site'] is not None: f['site_mapped'] += 1
    functions = collections.defaultdict(set)
    for r in rows:
        if r['win_function'] is not None: functions[r['function_confidence']].add(r['win_function'])
    return dict(families={k: dict(v) for k, v in sorted(by_family.items())},
                functions={k: len(v) for k, v in functions.items()},
                distinct_functions=len({r['win_function'] for r in rows if r['win_function'] is not None}))


def verify_build(path, expected):
    data = Path(path).read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if len(data) != expected['size'] or digest != expected['sha256']:
        raise SystemExit(f'{path}: not the pinned X4 9.00 build (size {len(data)}, sha256 {digest})')
    return data


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('--source', type=Path, default=SOURCE, help='gate sources (default: src/native/engine)')
    p.add_argument('--windows', type=Path, help='pinned Windows X4.exe (read only)')
    p.add_argument('--linux', type=Path, help='pinned Linux X4 ELF (read only)')
    p.add_argument('--pairs', type=Path, help='x4-re data/xfer/pairs.tsv')
    p.add_argument('--known', type=Path, default=KNOWN)
    p.add_argument('--disasm', action='store_true', help='call anchors, data votes and FP profiles (capstone)')
    p.add_argument('--tsv', type=Path); p.add_argument('--json', type=Path)
    args = p.parse_args(argv)
    pins, refs, unresolved = extract_sites(gate_files(args.source))
    pe = PeImage(verify_build(args.windows, WINDOWS_BUILD)) if args.windows else None
    elf = ElfImage(verify_build(args.linux, LINUX_BUILD)) if args.linux else None
    if elf and elf.build_id() != LINUX_BUILD['build_id']: raise SystemExit('Linux build ID mismatch')
    xfer = load_xfer(args.pairs) if args.pairs else {}
    known = load_known(args.known)
    rows = build_rows(pins, refs, pe, elf, xfer, known)
    report = dict(schema='x4-lockstep-linux-map-v1', source_pins=len(pins), source_refs=len(refs),
                  unresolved=[dict(u, rva=None) for u in unresolved], unique_rvas=len(rows),
                  windows_pins={k: sum(1 for r in rows if r['win_pin'] == k) for k in ('ok', 'mismatch', 'unchecked', 'unpinned')},
                  summary=summarize(rows))
    if xfer: report['xfer_vs_known'], report['xfer_disagreements'] = grade_xfer(known, xfer)
    if args.disasm:
        if not (pe and elf): raise SystemExit('--disasm needs --windows and --linux')
        report['disasm_grades'], report['disasm_misses'], report['fp_profiles'] = deepen(rows, pe, elf, xfer, known)
        report['summary'] = summarize(rows)
    if args.tsv:
        with args.tsv.open('w', encoding='utf-8', newline='\n') as out:
            out.write('\t'.join(COLUMNS) + '\n')
            for r in rows: out.write('\t'.join(cell(c, r.get(c)) for c in COLUMNS) + '\n')
    if args.json:
        report['rows'] = [{c: cell(c, r.get(c)) for c in COLUMNS} for r in rows]
        args.json.write_text(json.dumps(report, indent=1) + '\n', encoding='utf-8')
        del report['rows']
    json.dump(report, sys.stdout, indent=1); print()
    return 1 if report['windows_pins']['mismatch'] else 0


if __name__ == '__main__':
    raise SystemExit(main())
