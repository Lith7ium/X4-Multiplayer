"""Find the Jolt functions inside the owned X4.exe (offline, read-only).

X4.exe carries no symbols and its Jolt classes have no RTTI, so the Windows half of the
portable Jolt redirect (native_gate_jolt_portable) needs its own map from our Jolt
build's functions to X4.exe's. Both are MSVC x64 code from the same Jolt 5.5.0 source,
so they are compared directly:

  ours: every Jolt function of our MSVC build (jolt_portable_build.py build --variant
        control, linked whole with a map: names from the map, extents from .pdata)
  X4:   every function of X4.exe (.pdata roots plus frame-less leaf functions)

Features per function: mnemonic sequence, ordered direct calls, virtual call slots,
struct displacements, float and integer constants, strings, imports. Rounds, each
requiring a minimum similarity and a margin over the runner-up:
  str      a string referenced by exactly one function on each side
  const    an identical rare constant/string/slot key, unique on both sides
  vtable   vtables (code-pointer arrays referenced by code) of equal length whose
           slots are similar, anchored by already matched slots
  callee/caller/position  neighbours of matched pairs through the call graph
With --linux-names (qualified names of the Jolt functions the Linux X4 links, from
`nm -DC`), our functions X4 cannot contain (soft bodies, vehicles, ...) are left out.
The result is a candidate map for review: its rows hold names, RVAs and scores only,
never X4 bytes, and the in-game module pins every patched entry by hash.

  python tools/dev/jolt_portable_winmap.py --x4 X4.exe --ours jolt_all.exe \
      [--linux-names names.txt] [--grade x4-re/data/xfer/pairs.tsv x4-re/data/xfer/linux.pkl] \
      --out .local/jolt-portable/winmap
"""
import argparse
import bisect
import collections
import difflib
import hashlib
import json
import math
import os
import pickle
import re
import shutil
import struct
import subprocess
import sys
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lockstep_linux_map import PeImage, WINDOWS_BUILD  # noqa: E402

RIP = re.compile(r'\[rip ([+-]) (0x[0-9a-f]+)\]')
MEMDISP = re.compile(r'\[(r[a-z0-9]+|e[a-z]+)(?: \+ [a-z0-9]+(?:\*\d)?)? ([+-]) (0x[0-9a-f]+)\]')
ICALL = re.compile(r'^qword ptr \[(r[a-z0-9]+) \+ (0x[0-9a-f]+)\]$|^qword ptr \[(r[a-z0-9]+)\]$')
IMM = re.compile(r'(?<![\w\[])(0x[0-9a-f]+|\d+)(?![\w\]])')
SIZE = re.compile(r'\b(dword|qword|xmmword) ptr')
BRANCHES = {'jmp', 'je', 'jne', 'jz', 'jnz', 'ja', 'jae', 'jb', 'jbe', 'jg', 'jge', 'jl', 'jle', 'js', 'jns', 'jo', 'jno',
            'jp', 'jnp', 'jrcxz', 'jecxz', 'loop', 'loope', 'loopne'}
FRAME = {'rsp', 'rbp', 'rip', 'esp', 'ebp'}


# ------------------------------------------------------------------ images


class Image:
    """A PE image with its functions (pdata roots + leaf functions) and features."""

    def __init__(self, path):
        self.path = Path(path)
        self.data = self.path.read_bytes()
        self.pe = PeImage(self.data)
        pe_at = struct.unpack_from('<I', self.data, 0x3c)[0]
        self.base = struct.unpack_from('<Q', self.data, pe_at + 24 + 24)[0]
        self.text = [(va, va + size) for name, va, size, *_ in self.pe.sections if name == '.text']
        self.rdata = [(va, va + size) for name, va, size, *_ in self.pe.sections if name in ('.rdata', '_RDATA')]
        self.rwdata = [(va, va + size) for name, va, size, *_ in self.pe.sections if name == '.data']
        self.imports = self._imports()

    def in_text(self, rva):
        return any(a <= rva < b for a, b in self.text)

    def in_rdata(self, rva):
        return any(a <= rva < b for a, b in self.rdata)

    def _imports(self):
        pe_at = struct.unpack_from('<I', self.data, 0x3c)[0]
        opt = pe_at + 24
        rva, size = struct.unpack_from('<II', self.data, opt + 112 + 8)
        out = {}
        if not size:
            return out
        at = rva
        while True:
            desc = self.pe.read(at, 20)
            if not desc:
                break
            lookup, _, _, name_rva, iat = struct.unpack('<IIIII', desc)
            if not iat:
                break
            dll = self.cstr(name_rva) or '?'
            slot = 0
            while True:
                entry = self.pe.read((lookup or iat) + 8 * slot, 8)
                if not entry:
                    break
                value = struct.unpack('<Q', entry)[0]
                if not value:
                    break
                out[iat + 8 * slot] = f'{dll}!ord{value & 0xffff}' if value >> 63 else f'{dll}!{self.cstr((value & 0x7fffffff) + 2)}'
                slot += 1
            at += 20
        return out

    def cstr(self, rva, limit=256):
        out = bytearray()
        while len(out) < limit:
            c = self.pe.read(rva + len(out), 1)
            if not c or c == b'\0':
                return out.decode('latin-1') if c == b'\0' else None
            out += c
        return None

    def qword(self, rva):
        q = self.pe.read(rva, 8)
        return struct.unpack('<Q', q)[0] if q else None

    def code_pointer(self, rva):
        """RVA of the code a qword at rva points to, or None."""
        q = self.qword(rva)
        if q is None or q < self.base:
            return None
        target = q - self.base
        return target if self.in_text(target) else None


def string_at(image, rva):
    """A printable C string of at least 4 characters at rva, or None."""
    s = image.cstr(rva, 200)
    if s and len(s) >= 4 and all(32 <= ord(c) < 127 for c in s):
        return s
    return None


_IMAGE = None


def _init(path):
    global _IMAGE, _MD
    from capstone import Cs, CS_ARCH_X86, CS_MODE_64
    _IMAGE = Image(path)
    _MD = Cs(CS_ARCH_X86, CS_MODE_64)


def _sweep(chunk):
    """[(start, [(begin, end), ...])] -> [(start, features)]"""
    image, md = _IMAGE, _MD
    out = []
    for start, ranges in chunk:
        mn, calls, tails, slots, imports, drefs, crefs, fconst, imms, disps = [], [], [], [], [], [], [], set(), set(), collections.Counter()
        own = lambda t: any(a <= t < b for a, b in ranges)  # noqa: E731
        size = 0
        for begin, end in ranges:
            size += end - begin
            code = image.pe.read(begin, end - begin) or b''
            for address, length, mnemonic, ops in md.disasm_lite(code, begin):
                mn.append(mnemonic)
                after = address + length
                rip = RIP.search(ops)
                target = None
                if rip:
                    disp = int(rip.group(2), 16)
                    target = after + disp if rip.group(1) == '+' else after - disp
                if mnemonic == 'call' or mnemonic in BRANCHES:
                    if target is not None:
                        name = image.imports.get(target)
                        if name:
                            imports.append(name)
                        else:
                            drefs.append(target)
                        continue
                    if ops.startswith('0x'):
                        t = int(ops, 16)
                        if mnemonic == 'call':
                            calls.append(t)
                        elif not own(t) and image.in_text(t):
                            tails.append(t)
                        continue
                    if mnemonic == 'call':
                        m = ICALL.match(ops)
                        if m:
                            slots.append(int(m.group(2), 16) if m.group(2) else 0)
                    continue
                if target is not None:
                    if image.in_text(target):
                        crefs.append(target)
                    else:
                        drefs.append(target)
                        size_m = SIZE.search(ops)
                        if size_m and image.in_rdata(target) and mnemonic != 'lea':
                            width = {'dword': 4, 'qword': 8, 'xmmword': 16}[size_m.group(1)]
                            raw = image.pe.read(target, width)
                            if raw and (mnemonic.endswith(('ss', 'sd', 'ps', 'pd')) or mnemonic.startswith(('movs', 'comis', 'ucomis', 'cvt', 'vmov', 'vcomis', 'vucomis', 'vcvt', 'movaps', 'movups', 'movdqa', 'movdqu'))):
                                fconst.add((width, raw))
                    continue
                for m in MEMDISP.finditer(ops):
                    if m.group(1) not in FRAME:
                        disps[int(m.group(3), 16) if m.group(2) == '+' else -int(m.group(3), 16)] += 1
                stripped = re.sub(r'\[[^\]]*\]', '', ops)
                for value in IMM.findall(stripped):
                    v = int(value, 0)
                    if 0x100 <= v and v not in (0xffffffff, 0xffffffffffffffff, 0x7fffffff, 0x80000000):
                        imms.add(v)
        strs = sorted({s for s in (string_at(image, r) for r in drefs if image.in_rdata(r)) if s})
        out.append((start, dict(ranges=ranges, size=size, mn=mn, calls=calls, tails=tails, slots=slots, imports=imports,
                                drefs=drefs, crefs=crefs, fconst=frozenset(fconst), imms=frozenset(imms),
                                disps=disps, strs=strs)))
    return out


def leaf_functions(image, roots):
    """Frame-less functions have no .pdata entry: find them as direct call targets and as
    code pointers in .rdata outside every known range; end at the last ret/jmp."""
    from capstone import Cs, CS_ARCH_X86, CS_MODE_64
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    covered = sorted((b, e) for b, e, _ in image.pe.entries)
    lows = [b for b, _ in covered]

    def is_covered(x):
        i = bisect.bisect_right(lows, x) - 1
        return i >= 0 and covered[i][0] <= x < covered[i][1]
    candidates = set()
    for a, b in image.rdata:
        blob = image.pe.read(a, b - a) or b''
        for off in range(0, len(blob) - 7, 8):
            v = struct.unpack_from('<Q', blob, off)[0] - image.base
            if image.in_text(v) and not is_covered(v):
                candidates.add(v)
    for a, b in image.text:
        code = image.pe.read(a, b - a) or b''
        for m in re.finditer(rb'\xe8', code):
            if m.start() + 5 > len(code):
                continue
            t = a + m.start() + 5 + struct.unpack_from('<i', code, m.start() + 1)[0]
            if t % 16 == 0 and image.in_text(t) and not is_covered(t):
                candidates.add(t)
    found = []
    for start in sorted(candidates):
        code = image.pe.read(start, 0x400) or b''
        reach, end = start, None
        for address, length, mnemonic, ops in md.disasm_lite(code, start):
            after = address + length
            if mnemonic.startswith('j') and ops.startswith('0x'):
                t = int(ops, 16)
                if start < t < start + 0x400 and t > address:
                    reach = max(reach, t)
            if mnemonic in ('ret', 'jmp') and after > reach:
                end = after
                break
            if mnemonic in ('int3', 'call') and mnemonic == 'int3':
                break
        if end and not is_covered(end - 1) and not is_covered(start):
            found.append((start, [(start, end)]))
    return found


def functions(image):
    roots = [(root, image.pe.ranges(root)) for root in sorted(image.pe._ranges)]
    roots = [(r, sorted(rs, key=lambda x: (x[0] != r, x[0]))) for r, rs in roots if image.in_text(r)]
    leaves = leaf_functions(image, roots)
    known = {r for r, _ in roots}
    return sorted(roots + [leaf for leaf in leaves if leaf[0] not in known])


def extract(path, cache, workers):
    key = hashlib.sha256(Path(path).read_bytes()).hexdigest()[:16]
    cached = Path(cache) / f'features-{key}.pkl'
    if cached.exists():
        return pickle.loads(cached.read_bytes())
    image = Image(path)
    funcs = functions(image)
    chunks = [funcs[i:i + 300] for i in range(0, len(funcs), 300)]
    feats = {}
    with ProcessPoolExecutor(workers, initializer=_init, initargs=(str(path),)) as pool:
        for result in pool.map(_sweep, chunks):
            feats.update(result)
    out = dict(path=str(path), base=image.base, funcs=feats)
    Path(cache).mkdir(parents=True, exist_ok=True)
    cached.write_bytes(pickle.dumps(out, protocol=4))
    return out


# ------------------------------------------------------------------ our names


MAP_LINE = re.compile(r'^\s*[0-9a-f]{4}:[0-9a-f]{8}\s+(\S+)\s+([0-9a-f]{16})\s+(f\s+)?(i\s+)?(\S+)\s*$')


def read_map(path, base):
    names, objects, data = collections.defaultdict(list), {}, collections.defaultdict(list)
    for line in Path(path).read_text(encoding='latin-1').splitlines():
        m = MAP_LINE.match(line)
        if not m:
            continue
        name, va, function, _, obj = m.groups()
        rva = int(va, 16) - base
        if function:
            names[rva].append(name)
            objects.setdefault(rva, obj)
        else:
            data[rva].append(name)
    return names, objects, data


def undecorate(names, cache):
    """Decorated MSVC name -> qualified name without parameters (undname.exe, cached)."""
    cache = Path(cache)
    known = json.loads(cache.read_text()) if cache.exists() else {}
    todo = sorted({n for n in names if n not in known})
    if todo:
        undname = shutil.which('undname') or next(iter(sorted(Path(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)')).glob(
            'Microsoft Visual Studio/*/*/VC/Tools/MSVC/*/bin/Hostx64/x64/undname.exe'))), None)
        if not undname:
            raise SystemExit('undname.exe (Visual Studio Build Tools) not found')
        listing = cache.with_suffix('.txt')
        listing.write_text('\n'.join(todo) + '\n')
        lines = [line for line in subprocess.run([str(undname), str(listing)], capture_output=True, text=True).stdout.splitlines() if line.strip()]
        if len(lines) != len(todo):
            raise SystemExit('undname output does not line up with its input')
        known.update(zip(todo, (qualified(line) for line in lines)))
        cache.write_text(json.dumps(known))
    return known


def qualified(text):
    """'public: virtual float __cdecl JPH::SphereShape::GetVolume(void)const' -> 'JPH::SphereShape::GetVolume'."""
    depth = 0
    for i, ch in enumerate(text):
        if ch == '<':
            depth += 1
        elif ch == '>':
            depth -= 1
        elif ch == '(' and depth == 0 and i > 0:
            head, cut, d = text[:i], 0, 0
            for j, c in enumerate(head):
                if c == '<':
                    d += 1
                elif c == '>':
                    d -= 1
                elif c == ' ' and d == 0:
                    cut = j + 1
            return head[cut:].strip()
    return text.strip()


DELETING = re.compile(r"::`(?:scalar|vector) deleting destructor'$")


def comparable(name):
    """Qualified name comparable across MSVC and Itanium: template arguments dropped, MSVC
    deleting destructors named like the destructor they wrap."""
    name = strip_templates(name)
    if DELETING.search(name):
        name = DELETING.sub('', name)
        name = f'{name}::~{name.rsplit("::", 1)[-1]}'
    return name


def strip_templates(name):
    out, depth = [], 0
    for ch in name:
        if ch == '<':
            depth += 1
        elif ch == '>':
            depth -= 1
        elif depth == 0:
            out.append(ch)
    return ''.join(out)


# ------------------------------------------------------------------ similarity


def ratio(a, b):
    return min(a, b) / max(a, b) if max(a, b) else 1.0


def jaccard(a, b):
    a, b = set(a), set(b)
    return len(a & b) / len(a | b) if a or b else 1.0


def counter_cosine(a, b):
    if not a and not b:
        return 1.0
    dot = sum(v * b.get(k, 0) for k, v in a.items())
    na, nb = math.sqrt(sum(v * v for v in a.values())), math.sqrt(sum(v * v for v in b.values()))
    return dot / (na * nb) if na and nb else 0.0


class Scorer:
    def __init__(self, ours, x4):
        self.o, self.w = ours, x4
        self.mcount = {}

    def counts(self, side, f):
        key = (side, f)
        if key not in self.mcount:
            feats = (self.o if side == 'o' else self.w)[f]
            self.mcount[key] = collections.Counter(feats['mn'])
        return self.mcount[key]

    def score(self, o, w, sequence=True):
        a, b = self.o[o], self.w[w]
        s = 0.2 * ratio(len(a['mn']), len(b['mn']))
        s += 0.2 * counter_cosine(self.counts('o', o), self.counts('w', w))
        s += 0.15 * counter_cosine(a['disps'], b['disps'])
        s += 0.1 * jaccard(a['fconst'], b['fconst']) + 0.05 * jaccard(a['imms'], b['imms'])
        s += 0.05 * jaccard(a['strs'], b['strs']) + 0.05 * counter_cosine(collections.Counter(a['slots']), collections.Counter(b['slots']))
        s += 0.05 * ratio(len(a['calls']) + 1, len(b['calls']) + 1)
        if sequence and len(a['mn']) * len(b['mn']) < 4_000_000:
            s += 0.15 * difflib.SequenceMatcher(None, a['mn'], b['mn'], autojunk=False).ratio()
        else:
            s += 0.15 * ratio(len(a['mn']), len(b['mn']))
        return s


# ------------------------------------------------------------------ matching


class Matcher:
    def __init__(self, ours, x4, jolt, scorer, threshold=0.8, margin=0.05):
        self.O, self.W, self.jolt, self.S = ours, x4, jolt, scorer
        self.threshold, self.margin = threshold, margin
        self.pairs, self.taken = {}, {}
        self.o_callers, self.w_callers = collections.defaultdict(set), collections.defaultdict(set)
        for a, f in ours.items():
            for c in f['calls'] + f['tails']:
                self.o_callers[c].add(a)
        for a, f in x4.items():
            for c in f['calls'] + f['tails']:
                self.w_callers[c].add(a)

    def add(self, o, w, how, score):
        if o in self.pairs or w in self.taken:
            return False
        self.pairs[o] = (w, how, round(score, 3))
        self.taken[w] = o
        return True

    def best(self, o, candidates, how):
        """Pair o with the best of candidates when it clears the threshold and the margin."""
        candidates = [w for w in set(candidates) if w not in self.taken and w in self.W]
        if not candidates or o in self.pairs:
            return False
        if len(candidates) > 60:
            candidates = sorted(candidates, key=lambda w: -self.S.score(o, w, sequence=False))[:20]
        scored = sorted(((self.S.score(o, w), w) for w in candidates), reverse=True)
        top = scored[0]
        if top[0] < self.threshold or (len(scored) > 1 and top[0] - scored[1][0] < self.margin):
            return False
        return self.add(o, top[1], how, top[0])

    def by_strings(self):
        index_o, index_w = collections.defaultdict(set), collections.defaultdict(set)
        for a in self.jolt:
            for s in self.O[a]['strs']:
                index_o[s].add(a)
        for a, f in self.W.items():
            for s in f['strs']:
                if s in index_o:
                    index_w[s].add(a)
        votes = collections.defaultdict(set)
        for s, os_ in index_o.items():
            ws = index_w.get(s, set())
            if len(os_) == 1 and len(ws) == 1:
                votes[next(iter(os_))].add(next(iter(ws)))
        n = 0
        for o, ws in votes.items():
            if len(ws) == 1:
                n += self.best(o, ws, 'str')
        return n

    @staticmethod
    def rare_key(f):
        key = set(f['fconst']) | {('i', v) for v in f['imms']} | {('s', s) for s in f['strs']} | {('v', s) for s in f['slots'] if s >= 0x40}
        return frozenset(key) if len(key) >= 3 else None

    def by_keys(self):
        ko = collections.defaultdict(list)
        for a in self.jolt:
            if a not in self.pairs:
                k = self.rare_key(self.O[a])
                if k:
                    ko[k].append(a)
        kw = collections.defaultdict(list)
        for a, f in self.W.items():
            if a not in self.taken:
                k = self.rare_key(f)
                if k in ko:
                    kw[k].append(a)
        n = 0
        for k, os_ in ko.items():
            if len(os_) == 1 and len(kw.get(k, ())) == 1:
                n += self.best(os_[0], kw[k], 'const')
        return n

    def propagate(self):
        n = 0
        for o, (w, _, _) in list(self.pairs.items()):
            oc = [c for c in self.O[o]['calls'] + self.O[o]['tails'] if c in self.O]
            wc = [c for c in self.W[w]['calls'] + self.W[w]['tails'] if c in self.W]
            if len(oc) == len(wc):
                for x, y in zip(oc, wc):
                    if x in self.jolt and x not in self.pairs:
                        n += self.best(x, [y], 'position')
            for x in dict.fromkeys(oc):
                if x in self.jolt and x not in self.pairs:
                    n += self.best(x, wc, 'callee')
            wcallers = self.w_callers.get(w, set())
            if len(wcallers) <= 400:
                for x in self.o_callers.get(o, ()):
                    if x in self.jolt and x not in self.pairs:
                        n += self.best(x, wcallers, 'caller')
        return n


# ------------------------------------------------------------------ vtables


def our_vtables(image, data_names, names):
    """vftable symbol -> [slot function RVAs]"""
    starts = sorted(data_names)
    out = {}
    for i, rva in enumerate(starts):
        tables = [n for n in data_names[rva] if n.startswith('??_7') and '@JPH@@' in n]
        if not tables:
            continue
        limit = starts[i + 1] if i + 1 < len(starts) else rva + 8 * 400
        slots, at = [], rva
        while at < limit:
            target = image.code_pointer(at)
            if target is None:
                break
            slots.append(target)
            at += 8
        out[tables[0]] = slots
    return out


def x4_vtables(image, feats):
    """Code-pointer arrays in .rdata that code references (lea of a vtable): start -> slots."""
    referenced = set()
    for f in feats.values():
        referenced.update(r for r in f['drefs'] if image.in_rdata(r))
    starts = sorted(r for r in referenced if image.code_pointer(r) is not None)
    out = {}
    for i, start in enumerate(starts):
        limit = starts[i + 1] if i + 1 < len(starts) else start + 8 * 400
        slots, at = [], start
        while at < limit:
            target = image.code_pointer(at)
            if target is None:
                break
            slots.append(target)
            at += 8
        if len(slots) >= 2:
            out[start] = slots
    return out


def match_vtables(matcher, ours_vt, x4_vt):
    """Pair vtables of equal length whose already matched slots agree; then pair slots."""
    by_length = collections.defaultdict(list)
    for start, slots in x4_vt.items():
        by_length[len(slots)].append(start)
    vt_pairs, n = {}, 0
    for name, slots in ours_vt.items():
        candidates = by_length.get(len(slots), [])
        best, best_score, second = None, -1, -1
        for start in candidates:
            xs = x4_vt[start]
            agree = sum(1 for a, b in zip(slots, xs) if a in matcher.pairs and matcher.pairs[a][0] == b)
            conflict = sum(1 for a, b in zip(slots, xs) if a in matcher.pairs and matcher.pairs[a][0] != b and b in matcher.taken)
            if conflict or not agree:
                continue
            score = agree
            if score > best_score:
                best, second, best_score = start, best_score, score
            elif score > second:
                second = score
        if best is not None and best_score > second:
            vt_pairs[name] = best
            for a, b in zip(slots, x4_vt[best]):
                if a in matcher.jolt and a not in matcher.pairs and b in matcher.W:
                    if matcher.S.score(a, b) >= matcher.threshold - 0.1 and b not in matcher.taken:
                        n += matcher.add(a, b, 'vtable', matcher.S.score(a, b))
    return vt_pairs, n


# ------------------------------------------------------------------ main


def grade(pairs, names_of, pairs_tsv, linux_pkl, undec):
    """Agreement with an independent Linux->Windows pair table (x4-re), by qualified name."""
    lin = pickle.loads(Path(linux_pkl).read_bytes())
    ref = {}
    for row in Path(pairs_tsv).read_text().splitlines():
        if row.startswith('#'):
            continue
        w, l, *_ = row.split('\t')
        ref[int(w, 16)] = int(l, 16)
    agree = disagree = 0
    for o, (w, how, _) in pairs.items():
        if w not in ref:
            continue
        lq = {comparable(qualified(lin['dem'].get(n, n))) for n in lin['names'].get(ref[w], ())}
        oq = {comparable(undec[n]) for n in names_of.get(o, ())}
        if not lq:
            continue
        if lq & oq:
            agree += 1
        else:
            disagree += 1
    return agree, disagree


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--x4', required=True, type=Path)
    p.add_argument('--ours', required=True, type=Path, help='our Jolt build linked whole, with its .map next to it')
    p.add_argument('--linux-names', type=Path, help='qualified names of the Jolt functions in the Linux X4 (nm -DC), one per line')
    p.add_argument('--grade', nargs=2, metavar=('PAIRS_TSV', 'LINUX_PKL'))
    p.add_argument('--out', type=Path, default=Path('.local/jolt-portable/winmap'))
    p.add_argument('--workers', type=int, default=max(1, (os.cpu_count() or 2) - 2))
    p.add_argument('--threshold', type=float, default=0.8)
    args = p.parse_args()

    x4_bytes = args.x4.read_bytes()
    if len(x4_bytes) != WINDOWS_BUILD['size'] or hashlib.sha256(x4_bytes).hexdigest() != WINDOWS_BUILD['sha256']:
        raise SystemExit(f'{args.x4} is not the pinned X4.exe 9.00')
    args.out.mkdir(parents=True, exist_ok=True)
    W = extract(args.x4, args.out, args.workers)['funcs']
    ours = extract(args.ours, args.out, args.workers)
    O = ours['funcs']
    names, objects, data = read_map(args.ours.with_suffix('.map'), ours['base'])
    undec = undecorate({n for ns in names.values() for n in ns}, args.out / 'undname.json')
    jolt = {a for a in O if any('@JPH@@' in n for n in names.get(a, ())) or objects.get(a, '').startswith('jolt:')}
    wanted = None
    if args.linux_names:
        wanted = {comparable(qualified(line.strip())) for line in args.linux_names.read_text().splitlines() if line.strip()}
        jolt = {a for a in jolt if any(comparable(undec[n]) in wanted for n in names.get(a, ()))}
    print(f'X4.exe functions {len(W)}; ours {len(O)}, Jolt {len(jolt)}' + (' (linked by the Linux X4)' if wanted else ''))
    matcher = Matcher(O, W, jolt, Scorer(O, W), threshold=args.threshold)
    print('round str', matcher.by_strings())
    print('round const', matcher.by_keys())
    vt_o = our_vtables(Image(args.ours), data, names)
    vt_w = x4_vtables(Image(args.x4), W)
    print(f'vtables: ours {len(vt_o)}, X4 candidates {len(vt_w)}')
    vt_pairs = {}
    for it in range(30):
        n = matcher.propagate()
        found, m = match_vtables(matcher, vt_o, vt_w)
        vt_pairs.update(found)
        n += m + matcher.by_keys()
        print(f'iteration {it}: +{n}, pairs {len(matcher.pairs)}, vtables {len(vt_pairs)}')
        if not n:
            break
    rows = []
    for o, (w, how, score) in sorted(matcher.pairs.items(), key=lambda kv: kv[1][0]):
        rows.append(f'{w:#x}\t{o:#x}\t{how}\t{score}\t{undec[names[o][0]]}\t{names[o][0]}')
    (args.out / 'pairs.tsv').write_text('# x4_rva\tours_rva\tround\tscore\tqualified\tdecorated\n' + '\n'.join(rows) + '\n')
    counts = collections.Counter(how for _, how, _ in matcher.pairs.values())
    summary = dict(x4_functions=len(W), ours_jolt=len(jolt), matched=len(matcher.pairs), rounds=counts,
                   vtables_ours=len(vt_o), vtables_matched=len(vt_pairs))
    if args.grade:
        summary['grade_agree'], summary['grade_disagree'] = grade(matcher.pairs, names, *args.grade, undec)
    (args.out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
