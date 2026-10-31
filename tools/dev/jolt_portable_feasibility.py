"""Measure how the Jolt Physics code sits inside the owned Linux X4 9.00 executable
(offline, read-only): the numbers behind the portable Jolt verdict
(reports/LOCKSTEP-JOLT-PORTABLE-20261029.txt).

  python tools/dev/jolt_portable_feasibility.py --linux X4 [--json out.json]

Reported (the executable keeps a .dynsym with all Jolt functions and objects):
  jolt        functions named in namespace JPH, templates over JPH types, and unnamed
              functions only Jolt calls (inline copies, cold parts)
  boundary    direct calls from engine code into Jolt: sites, callers, callees
  inlined     engine functions that carry Jolt code inline: they use the Jolt allocator
              hooks, Jolt vtables or typeinfo, Jolt statics or take a Jolt function's
              address; their scalar and packed floating-point instruction counts
  subclasses  engine classes deriving from Jolt classes (Itanium typeinfo), their vtable
              slots that stay Jolt's and the engine overrides with their FP counts
Output holds counts, names and addresses only, never code bytes.
"""
import argparse
import bisect
import collections
import hashlib
import json
import os
import re
import struct
import sys
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lockstep_linux_map import ElfImage, LINUX_BUILD  # noqa: E402

RIP = re.compile(r'\[rip ([+-]) 0x([0-9a-f]+)\]')
ABS = re.compile(r'0x([0-9a-f]{6,8})\b')
FP = re.compile(r'^v?(add|sub|mul|div|sqrt|min|max|rcp|rsqrt|dp|hadd|hsub|fmadd\w*|fmsub\w*|fnmadd\w*|fnmsub\w*)(ss|sd|ps|pd)$')
ALLOCATOR = {'_ZN3JPH8AllocateE', '_ZN3JPH4FreeE', '_ZN3JPH15AlignedAllocateE', '_ZN3JPH11AlignedFreeE', '_ZN3JPH10ReallocateE'}
CXXABI = {'_ZTVN10__cxxabiv117__class_type_infoE': 'class', '_ZTVN10__cxxabiv120__si_class_type_infoE': 'si',
          '_ZTVN10__cxxabiv121__vmi_class_type_infoE': 'vmi'}

_IMG = None


def _init(path):
    global _IMG, _MD
    import capstone
    _IMG = ElfImage(Path(path).read_bytes())
    _MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)


def _scan(chunk):
    """[(begin, end)] -> [(begin, end, calls[(site, target, is_call)], refs[(site, target)], scalar FP, packed FP)]"""
    out = []
    for begin, end in chunk:
        code = _IMG.read(begin, end - begin)
        if not code:
            continue
        calls, refs, scalar, packed = [], [], 0, 0
        for address, size, mnemonic, ops in _MD.disasm_lite(code, begin):
            if mnemonic in ('call', 'jmp') and ops.startswith('0x'):
                calls.append((address, int(ops, 16), mnemonic == 'call'))
                continue
            m = RIP.search(ops)
            if m:
                disp = int(m.group(2), 16)
                refs.append((address, address + size + (disp if m.group(1) == '+' else -disp)))
            elif not mnemonic.startswith('j'):
                for h in ABS.findall(ops):
                    v = int(h, 16)
                    if 0x400000 <= v < 0x8000000:
                        refs.append((address, v))
            f = FP.match(mnemonic)
            if f:
                if f.group(2) in ('ss', 'sd'):
                    scalar += 1
                else:
                    packed += 1
        out.append((begin, end, calls, refs, scalar, packed))
    return out


def symbols(img):
    """All defined .dynsym entries: (value, size, type, name)."""
    strtab = img.sections['.dynstr']
    table = img.section_bytes('.dynsym')
    out = []
    for i in range(0, len(table), 24):
        name, info, _, shndx, value, size = struct.unpack_from('<IBBHQQ', table, i)
        if shndx and value:
            text = img.data[strtab['offset'] + name:img.data.index(b'\0', strtab['offset'] + name)].decode('ascii', 'replace')
            out.append((value, size, info & 0xf, text))
    return out


def relocations(img):
    strtab = img.sections['.dynstr']
    table = img.section_bytes('.dynsym')
    names = []
    for i in range(0, len(table), 24):
        name = struct.unpack_from('<I', table, i)[0]
        names.append(img.data[strtab['offset'] + name:img.data.index(b'\0', strtab['offset'] + name)].decode('ascii', 'replace'))
    out = {}
    blob = img.section_bytes('.rela.dyn')
    for i in range(0, len(blob), 24):
        offset, info, addend = struct.unpack_from('<QQq', blob, i)
        out[offset] = (info & 0xffffffff, names[info >> 32] if info >> 32 else None, addend)
    return out


def cstr(img, va):
    out = bytearray()
    while len(out) < 512:
        c = img.read(va + len(out), 1)
        if not c or c == b'\0':
            break
        out += c
    return out.decode('ascii', 'replace')


def analyse(path, workers):
    data = Path(path).read_bytes()
    if len(data) != LINUX_BUILD['size'] or hashlib.sha256(data).hexdigest() != LINUX_BUILD['sha256']:
        raise SystemExit(f'{path} is not the pinned Linux X4 9.00')
    img = ElfImage(data)
    syms = symbols(img)
    funcs = img.functions
    chunks = [funcs[i:i + 500] for i in range(0, len(funcs), 500)]
    with ProcessPoolExecutor(workers, initializer=_init, initargs=(str(path),)) as pool:
        scan = [row for part in pool.map(_scan, chunks) for row in part]
    starts = {row[0]: row for row in scan}
    fnames = collections.defaultdict(list)
    for v, s, t, n in syms:
        if t == 2:
            fnames[v].append(n)

    # ---- classify functions
    kind = {}
    for begin, *_ in scan:
        names = fnames.get(begin, [])
        if any(n.startswith(('_ZN3JPH', '_ZNK3JPH', '_ZThn', '_ZTv')) and '3JPH' in n for n in names):
            kind[begin] = 'jolt'
        elif any('3JPH' in n[:60] and n.startswith('_Z') for n in names):
            kind[begin] = 'jolt_template'
        elif names:
            kind[begin] = 'named'
        else:
            kind[begin] = 'unnamed'
    callers = collections.defaultdict(set)
    for begin, end, calls, *_ in scan:
        for _, target, _ in calls:
            if target in starts and target != begin:
                callers[target].add(begin)
    jolt_kinds = {'jolt', 'jolt_template', 'jolt_internal'}
    changed = True
    while changed:
        changed = False
        for f, k in list(kind.items()):
            if k != 'unnamed':
                continue
            cs = callers.get(f)
            if cs and all(kind[c] in jolt_kinds for c in cs):
                kind[f] = 'jolt_internal'
                changed = True
            elif cs and all(kind[c] in ('named', 'library_internal') for c in cs):
                kind[f] = 'library_internal'
                changed = True
    jolt = {f for f, k in kind.items() if k in jolt_kinds}
    engine = {f for f, k in kind.items() if k == 'unnamed'}
    counts = collections.Counter(kind.values())
    sizes = collections.Counter()
    for begin, end, *_ in scan:
        sizes[kind[begin]] += end - begin

    # ---- engine -> Jolt calls
    sites, ecallers, callees = 0, set(), collections.Counter()
    for begin, end, calls, *_ in scan:
        if begin not in engine:
            continue
        for _, target, _ in calls:
            if target in jolt:
                sites += 1
                ecallers.add(begin)
                callees[target] += 1

    # ---- inlined Jolt data signatures in engine functions
    objects = sorted((v, s, n) for v, s, t, n in syms if t == 1 and s)
    lows = [o[0] for o in objects]

    def object_of(va):
        i = bisect.bisect_right(lows, va) - 1
        if i >= 0 and objects[i][0] <= va < objects[i][0] + objects[i][1]:
            return objects[i][2]
        return None
    signature = collections.defaultdict(set)
    hits = collections.Counter()
    for begin, end, calls, refs, *_ in scan:
        if begin not in engine:
            continue
        for _, target in refs:
            name = object_of(target)
            if name and '3JPH' in name:
                cls = ('allocator' if name in ALLOCATOR else 'vtable' if name.startswith('_ZTV') else
                       'typeinfo' if name.startswith(('_ZTI', '_ZTS')) else 'static')
                signature[begin].add(cls)
                hits[cls] += 1
            elif target in jolt:
                signature[begin].add('function_address')
                hits['function_address'] += 1
    with_fp = [f for f in signature if starts[f][4] + starts[f][5]]
    with_scalar = [f for f in signature if starts[f][4]]
    touching = set(signature) | ecallers

    # ---- engine subclasses of Jolt classes
    rel = relocations(img)
    infos = {}
    for offset, (_, name, _) in rel.items():
        if name in CXXABI:
            k = CXXABI[name]
            bases = []
            if k == 'si':
                bases = [offset + 16]
            elif k == 'vmi':
                count = struct.unpack('<I', img.read(offset + 20, 4))[0]
                bases = [offset + 24 + 16 * j for j in range(count)]
            resolved = []
            for slot in bases:
                if slot in rel and rel[slot][1]:
                    resolved.append(rel[slot][1])
                else:
                    resolved.append(struct.unpack('<Q', img.read(slot, 8))[0])
            infos[offset] = (cstr(img, struct.unpack('<Q', img.read(offset + 8, 8))[0]), resolved)
    by_symbol = {n: v for v, s, t, n in syms}
    name_of = {off: n for off, (n, _) in infos.items()}

    def base_names(off, seen=()):
        out = []
        for b in infos.get(off, ('', []))[1]:
            if isinstance(b, str):
                addr = by_symbol.get(b)
                label = b[4:] if b.startswith('_ZTI') else b
                out.append(label)
                if addr in infos and addr not in seen:
                    out += base_names(addr, seen + (off,))
            elif b in infos and b not in seen:
                out.append(name_of[b])
                out += base_names(b, seen + (off,))
        return out
    derived = {}
    for off, (name, _) in infos.items():
        if 'JPH' in name.split('E')[0][:8]:
            continue
        chain = base_names(off)
        if not name.startswith('N3JPH') and any(b.startswith('N3JPH') for b in chain):
            derived[off] = (name, sorted({b for b in chain if b.startswith('N3JPH')}))
    sec = img.sections['.data.rel.ro']
    blob = img.data[sec['offset']:sec['offset'] + sec['size']]
    text = img.sections['.text']
    tlo, thi = text['addr'], text['addr'] + text['size']
    vt_slots = collections.Counter()
    override_fp = collections.Counter()
    classes = []
    for at in range(8, len(blob) - 8, 8):
        q = struct.unpack_from('<Q', blob, at)[0]
        if q in derived:
            p, slots = at + 8, []
            while p + 8 <= len(blob):
                v = struct.unpack_from('<Q', blob, p)[0]
                if not tlo <= v < thi:
                    break
                slots.append(v)
                p += 8
            jolt_slots = sum(1 for s in slots if s in jolt)
            engine_slots = [s for s in slots if s not in jolt and s in starts]
            vt_slots['jolt'] += jolt_slots
            vt_slots['engine'] += len(engine_slots)
            scalar = sum(starts[s][4] for s in set(engine_slots))
            packed = sum(starts[s][5] for s in set(engine_slots))
            override_fp['scalar'] += scalar
            override_fp['packed'] += packed
            classes.append(dict(cls=derived[q][0], jolt_bases=derived[q][1], vtable=hex(sec['addr'] + at - 8), slots=len(slots),
                                jolt_slots=jolt_slots, engine_overrides=len(engine_slots), override_fp_scalar=scalar,
                                override_fp_packed=packed))
    version = [hex(v) for v, s, t, n in syms if n == '_ZN3JPH27VerifyJoltVersionIDInternalEm']

    engine_fp = sum(starts[f][4] + starts[f][5] for f in engine)
    return dict(
        build=dict(size=len(data), sha256=LINUX_BUILD['sha256']),
        functions=dict(total=len(scan), by_kind=dict(counts), bytes_by_kind=dict(sizes)),
        jolt=dict(functions=len(jolt), named=counts['jolt'] + counts['jolt_template'], internal=counts['jolt_internal'],
                  bytes=sum(sizes[k] for k in jolt_kinds), verify_version=version),
        boundary=dict(sites=sites, engine_callers=len(ecallers), jolt_callees=len(callees),
                      callees=sorted((fnames.get(t, [hex(t)])[0], c) for t, c in callees.items())),
        inlined=dict(engine_functions=len(signature), signature_classes=dict(collections.Counter(c for s in signature.values() for c in s)),
                     reference_counts=dict(hits), with_fp=len(with_fp), with_scalar_fp=len(with_scalar),
                     fp_functions=sorted((hex(f), starts[f][4], starts[f][5], sorted(signature[f])) for f in with_fp),
                     fp_scalar=sum(starts[f][4] for f in signature), fp_packed=sum(starts[f][5] for f in signature),
                     touching_functions=len(touching), touching_fp=sum(starts[f][4] + starts[f][5] for f in touching),
                     touching_scalar_fp=sum(starts[f][4] for f in touching), engine_fp_total=engine_fp),
        subclasses=dict(classes=len({c['cls'] for c in classes}), vtables=len(classes), slots=dict(vt_slots),
                        override_fp=dict(override_fp), list=classes),
    )


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--linux', required=True, type=Path)
    p.add_argument('--json', type=Path)
    p.add_argument('--workers', type=int, default=max(1, (os.cpu_count() or 2) - 2))
    args = p.parse_args()
    result = analyse(args.linux, args.workers)
    text = json.dumps(result, indent=1)
    if args.json:
        args.json.write_text(text + '\n')
    brief = {k: v for k, v in result.items() if k not in ('boundary', 'subclasses')}
    brief['boundary'] = {k: v for k, v in result['boundary'].items() if k != 'callees'}
    brief['subclasses'] = {k: v for k, v in result['subclasses'].items() if k != 'list'}
    brief['subclasses']['with_fp_overrides'] = [(c['cls'], c['engine_overrides'], c['override_fp_scalar'], c['override_fp_packed'])
                                                for c in result['subclasses']['list'] if c['override_fp_scalar'] + c['override_fp_packed']]
    print(json.dumps(brief, indent=1))


if __name__ == '__main__':
    main()
