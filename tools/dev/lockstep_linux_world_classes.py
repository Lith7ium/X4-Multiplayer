"""Generate the class table of the Linux world hash (offline, read-only).

The Windows world hash (src/native/engine/lockstep_world_hash.h) writes each imported
object's vtable RVA into every domain hash and each movement controller's vtable RVA into
the motion hash, and derives class flags (destructible 36, positional 77, object 73) from
the X4.exe IsRealClass stub. A Linux client must hash the same values, so its gate maps
its own vtable addresses to the Windows ones:

  src/native/engine/linux/gate_world_classes.h
    {Linux vtable VA, Windows vtable RVA, Windows class flags, controller family, Linux top}

Classes are paired by RTTI name (MSVC type descriptor <-> Itanium typeinfo): every class
deriving from U::Component (imported objects) or U::MovementControllerInterface (their
movement controllers). Flags come from the Windows stub exactly as world_class_flags
computes them (0x80000000 when the stub is not the verified pure form); the Linux stub
(vtable+0x11e8) is decoded too and must give the same bits for every pair. Family: nav,
dock, undock by exact Windows vtable; static/movement by the Windows vtable+0x30 getter.
The output holds addresses and flags only, never executable bytes.

What an object's controller pointer holds is a MovementControllerInterface*, so its vtable
is the one of that subobject. MSVC places the interface at offset 0 of every controller;
the Itanium layout of most of them starts with the value source and has the interface at
+0x58 (+0x10 for the indirect ones), where a secondary vtable lives. So a controller's row
is the vtable of its interface subobject, top is that vtable's offset-to-top (the way back
to the complete object, which the field offsets of gate_world.h are relative to), and an
object's row is its primary vtable, U::Component being at offset 0 on both.

The interface's type getter gives an independent check of each controller pair: Windows
vtable slot 0 and Linux vtable slot 7 must return the same type ID.

The run prints every class the mapping does not pair 1:1 and fails on any class that could
exist as an object on one platform and has no partner on the other. --census also checks a
Windows client log holding a full WORLD_OBJECT tick against the table.

  python tools/dev/lockstep_linux_world_classes.py --windows X4.exe --linux X4 [--check]
"""
import argparse
from collections import Counter
import hashlib
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lockstep_linux_map import ElfImage, PeImage, WINDOWS_BUILD, LINUX_BUILD  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'src/native/engine/linux/gate_world_classes.h'
BASE = 0x140000000
WIN_STUB_SHA = '95552e650460a140f624748f17306613061330a17be9794f9e1e1d21abfb2fa0'  # lockstep_world_hash.h
WIN_STUB_SLOT, LIN_STUB_SLOT = 0x11e0, 0x11e8
WIN_GETTER_SLOT = 0x30
FAMILY = {'none': 0, 'nav': 1, 'dock': 2, 'undock': 3, 'static': 4, 'movement': 5, 'other': 6}
WIN_NAV, WIN_DOCK, WIN_UNDOCK = 0x2c51158, 0x2c3fdc0, 0x2c3e568
WIN_STATIC_GETTER, WIN_MOVEMENT_GETTER = 0x10ba20, 0x46ff70
CLASSES = (36, 77, 73)
ROOTS = {'.?AVComponent@U@@': 'object', '.?AVMovementControllerInterface@U@@': 'controller'}
LINUX_ROOTS = {'N1U9ComponentE': 'object', 'N1U27MovementControllerInterfaceE': 'controller'}
LINUX_ROOT = {'object': 'N1U9ComponentE', 'controller': 'N1U27MovementControllerInterfaceE'}
WINDOWS_ROOT = {'object': '.?AVComponent@U@@', 'controller': '.?AVMovementControllerInterface@U@@'}
VALUE_SOURCE = 'N4XLib11ValueSourceIN4Math6PosRotEEE'
VALUE_GETTER = 0xd57f00  # lea rax,[rdi+0x10]; ret: the value of a ValueSource<PosRot>, at +0x10 of it
LIN_TYPE_SLOT = 7  # the interface's type getter (Windows slot 0)


def pinned(path, build):
    data = Path(path).read_bytes()
    if len(data) != build['size'] or hashlib.sha256(data).hexdigest() != build['sha256']:
        raise SystemExit(f'{path} is not the pinned X4 9.00 build')
    return data


# ------------------------------------------------------------------ Windows


def win_cstr(pe, rva, limit=400):
    raw = pe.read(rva, limit) or b''
    return raw.split(b'\0')[0].decode('ascii', 'replace')


def windows_classes(pe):
    """Primary vtables (COL offset 0): RVA -> (MSVC name, {base name: offset of its first occurrence})."""
    rdata = [s for s in pe.sections if s[0] == '.rdata'][0]
    _, va, _, raw, raw_size, _ = rdata
    blob = pe.data[raw:raw + raw_size]
    cols = {}
    for at in range(0, len(blob) - 24, 4):
        sig, off, _, td, chd, self_rva = struct.unpack_from('<IIIIII', blob, at)
        if sig == 1 and self_rva == va + at and off == 0:
            name = win_cstr(pe, td + 16)
            if name.startswith('.?A'):
                cols[BASE + va + at] = (name, chd)
    out = {}
    for at in range(0, len(blob) - 8, 8):
        q = struct.unpack_from('<Q', blob, at)[0]
        if q in cols:
            name, chd = cols[q]
            _, _, count, array = struct.unpack('<IIII', pe.read(chd, 16))
            bases = {}
            for i in range(count):
                bcd = struct.unpack('<I', pe.read(array + 4 * i, 4))[0]
                descriptor, _, mdisp = struct.unpack('<IIi', pe.read(bcd, 12))
                bases.setdefault(win_cstr(pe, descriptor + 16), mdisp)
            out[va + at + 8] = (name, bases)
    return out


def win_qword(pe, rva):
    raw = pe.read(rva, 8)
    return struct.unpack('<Q', raw)[0] if raw else None


def win_flags(pe, vtable):
    """world_class_flags of lockstep_world_hash.h, on the file image."""
    function = (win_qword(pe, vtable + WIN_STUB_SLOT) or 0) - BASE
    code = pe.read(function, 36) if function > 0 else None
    if code:
        relative = struct.unpack_from('<i', code, 6)[0]
        normal = code[:6] + b'\0\0\0\0' + code[10:]
        if hashlib.sha256(normal).hexdigest() == WIN_STUB_SHA:
            words = struct.unpack('<8I', pe.read(function + 10 + relative, 32))
            return sum(1 << i for i, k in enumerate(CLASSES) if (words[k // 16] >> (2 * (k % 16))) & 3)
    return 0x80000000


def win_family(pe, vtable):
    if vtable == WIN_NAV:
        return 'nav'
    if vtable == WIN_DOCK:
        return 'dock'
    if vtable == WIN_UNDOCK:
        return 'undock'
    getter = (win_qword(pe, vtable + WIN_GETTER_SLOT) or 0) - BASE
    return {WIN_STATIC_GETTER: 'static', WIN_MOVEMENT_GETTER: 'movement'}.get(getter, 'other')


# ------------------------------------------------------------------ Linux


TEMPLATES = {  # the movement controller template instances of this build
    '.?AV?$MovementControllerBase@V?$ValueSource@VPosRot@Math@@@XLib@@@U@@':
        'N1U22MovementControllerBaseIN4XLib11ValueSourceIN4Math6PosRotEEEEE',
    '.?AV?$MovementControllerBase@V?$IndirectValueSource@VPosRot@Math@@@XLib@@@U@@':
        'N1U22MovementControllerBaseIN4XLib19IndirectValueSourceIN4Math6PosRotEEEEE',
    '.?AV?$NestableMovementControllerBase@V?$ValueSource@VPosRot@Math@@@XLib@@@U@@':
        'N1U30NestableMovementControllerBaseIN4XLib11ValueSourceIN4Math6PosRotEEEEE',
    '.?AV?$NestableMovementControllerBase@V?$IndirectValueSource@VPosRot@Math@@@XLib@@@U@@':
        'N1U30NestableMovementControllerBaseIN4XLib19IndirectValueSourceIN4Math6PosRotEEEEE',
}


def itanium(msvc):
    """MSVC type descriptor name of a plain or nested class -> Itanium typeinfo name
    (templates only from TEMPLATES)."""
    if msvc in TEMPLATES:
        return TEMPLATES[msvc]
    m = re.fullmatch(r'\.\?A[VU]((?:\w+@)+)@', msvc)
    if not m:
        return None
    parts = m.group(1).rstrip('@').split('@')[::-1]
    if len(parts) == 1:
        return f'{len(parts[0])}{parts[0]}'
    return 'N' + ''.join(f'{len(p)}{p}' for p in parts) + 'E'


def linux_rtti(elf):
    """typeinfo address -> {name, bases: [(typeinfo, offset, virtual)]} and name -> vtable entries
    in address order: (VA of the first slot, offset-to-top, first slot)."""
    strtab = elf.sections['.dynstr']
    table = elf.section_bytes('.dynsym')
    names = []
    for i in range(0, len(table), 24):
        n = struct.unpack_from('<I', table, i)[0]
        names.append(elf.data[strtab['offset'] + n:elf.data.index(b'\0', strtab['offset'] + n)].decode('ascii', 'replace'))
    abi = {'_ZTVN10__cxxabiv117__class_type_infoE': 'class', '_ZTVN10__cxxabiv120__si_class_type_infoE': 'si',
           '_ZTVN10__cxxabiv121__vmi_class_type_infoE': 'vmi'}
    typeinfo = {}
    rela = elf.section_bytes('.rela.dyn')
    for i in range(0, len(rela), 24):
        offset, info, _ = struct.unpack_from('<QQq', rela, i)
        if info >> 32 and names[info >> 32] in abi:
            pointer = struct.unpack('<Q', elf.read(offset + 8, 8))[0]
            entry = {'name': (elf.read(pointer, 400) or b'').split(b'\0')[0].decode('ascii', 'replace'), 'bases': []}
            kind = abi[names[info >> 32]]
            if kind == 'si':
                entry['bases'].append((struct.unpack('<Q', elf.read(offset + 16, 8))[0], 0, False))
            elif kind == 'vmi':
                count = struct.unpack('<I', elf.read(offset + 20, 4))[0]
                for k in range(count):
                    base, flags = struct.unpack('<Qq', elf.read(offset + 24 + 16 * k, 16))
                    entry['bases'].append((base, flags >> 8, bool(flags & 1)))
            typeinfo[offset] = entry
    sec = elf.sections['.data.rel.ro']
    blob = elf.data[sec['offset']:sec['offset'] + sec['size']]
    entries = {}
    for at in range(8, len(blob) - 16, 8):
        q = struct.unpack_from('<Q', blob, at)[0]
        if q in typeinfo:
            top = struct.unpack_from('<q', blob, at - 8)[0]
            if -0x100000 < top <= 0:
                entries.setdefault(typeinfo[q]['name'], []).append(
                    (sec['addr'] + at + 8, top, struct.unpack_from('<Q', blob, at + 8)[0]))
    return typeinfo, entries


def complete_vtables(elf, entries):
    """The vtable groups of a class that belong to a complete object. A group starts at an
    entry with offset-to-top 0; a construction vtable (base-in-derived, used only while a
    derived class is being built) and an abstract class's vtable have no destructor there."""
    text = elf.sections['.text']
    lo, hi = text['addr'], text['addr'] + text['size']
    groups = []
    for va, top, first in entries:
        if top == 0:
            groups.append({'complete': lo <= first < hi, 'primary': va, 'by_top': {0: va}})
        elif groups and va - groups[-1]['primary'] < 0x1000:
            groups[-1]['by_top'].setdefault(top, va)
    return [g for g in groups if g['complete']]


def linux_bases(typeinfo, address):
    """(name, offset in the class) of every non-virtual base, depth first."""
    out = []

    def walk(at, base):
        for parent, offset, virtual in typeinfo[at]['bases']:
            if parent in typeinfo and not virtual:
                out.append((typeinfo[parent]['name'], base + offset))
                walk(parent, base + offset)
    walk(address, 0)
    return out


def lin_flags(elf, vtable):
    """The Linux IsRealClass stub (vtable+0x11e8): movsxd rax,esi; lea rdx,[rip+bitmap]; ...
    two bits per class, like Windows. None when the stub is not that form."""
    raw = elf.read(vtable + LIN_STUB_SLOT, 8)
    function = struct.unpack('<Q', raw)[0] if raw else 0
    code = elf.read(function, 32) if function else None
    if not code or code[:6] != bytes.fromhex('4863c6488d15') or code[10:] != bytes.fromhex(
            '83e60f48c1e804488d0c368b0482d3e8a8030f95c0c3'):
        return None
    bitmap = function + 10 + struct.unpack_from('<i', code, 6)[0]
    words = struct.unpack('<8I', elf.read(bitmap, 32))
    return sum(1 << i for i, k in enumerate(CLASSES) if (words[k // 16] >> (2 * (k % 16))) & 3)


# ------------------------------------------------------------------ table


def returned_constant(code):
    """imm32 of mov eax,imm32; ret, or None."""
    return struct.unpack_from('<I', code, 1)[0] if code and code[0] == 0xb8 and code[5] == 0xc3 else None


def controller_type_ids(pe, elf, rva, vtable):
    """(Windows, Linux) value of the interface's type getter."""
    windows = (win_qword(pe, rva) or 0) - BASE
    linux = struct.unpack('<Q', elf.read(vtable + LIN_TYPE_SLOT * 8, 8))[0]
    return (returned_constant(pe.read(windows, 6)) if windows > 0 else None,
            returned_constant(elf.read(linux, 6)) if linux else None)


def value_getter_in_primary(elf, primary):
    """The complete object's value getter: the shared ValueSource one among its first slots."""
    return any(struct.unpack('<Q', elf.read(primary + 8 * i, 8))[0] == VALUE_GETTER for i in range(16))


def build(windows, linux):
    """rows, unmatched [(MSVC name, reason)], mismatched [(name, what)], linux_only [(name, root)]."""
    pe, elf = PeImage(pinned(windows, WINDOWS_BUILD)), ElfImage(pinned(linux, LINUX_BUILD))
    win = windows_classes(pe)
    typeinfo, entries = linux_rtti(elf)
    address_of = {t['name']: a for a, t in typeinfo.items()}
    rows, unmatched, mismatched, paired = [], [], [], set()
    for rva, (name, bases) in sorted(win.items()):
        kind = next((ROOTS[b] for b in bases if b in ROOTS), None)
        if not kind:
            continue
        iname = itanium(name)
        if not iname or iname not in address_of:
            unmatched.append((name, 'no Linux typeinfo of this name'))
            continue
        paired.add(iname)
        groups = complete_vtables(elf, entries.get(iname, []))
        if not groups:
            unmatched.append((name, 'abstract: no complete Linux vtable'))
            continue
        if len(groups) != 1:
            mismatched.append((name, f'{len(groups)} complete Linux vtables'))
            continue
        group = groups[0]
        if bases[WINDOWS_ROOT[kind]] != 0:
            mismatched.append((name, f'Windows {kind} base at {bases[WINDOWS_ROOT[kind]]:#x}'))
            continue
        linux_base = linux_bases(typeinfo, address_of[iname])
        offset = next((o for n, o in linux_base if n == LINUX_ROOT[kind]), None)
        if iname == LINUX_ROOT[kind]:
            offset = 0
        if offset is None or -offset not in group['by_top']:
            mismatched.append((name, f'no Linux vtable at the {kind} base (offset {offset})'))
            continue
        flags = win_flags(pe, rva) if kind == 'object' else 0
        family = win_family(pe, rva) if kind == 'controller' else 'none'
        if kind == 'object':
            if offset != 0:
                mismatched.append((name, f'Linux Component base at {offset:#x}'))
                continue
            linux_flags = lin_flags(elf, group['primary'])
            if (flags & 0x80000000) != (linux_flags is None) or (linux_flags is not None and linux_flags != flags):
                mismatched.append((name, f'class flags {flags:#x} on Windows, {linux_flags} on Linux'))
        if family in ('movement', 'dock', 'undock'):
            # gate_world.h reads these at fixed offsets of the complete object.
            if next((o for n, o in linux_base if n == VALUE_SOURCE), None) != 0:
                mismatched.append((name, f'{family}: the Linux value source is not at offset 0'))
        if family == 'movement' and not value_getter_in_primary(elf, group['primary']):
            mismatched.append((name, 'movement: the Linux value getter is not the shared one'))
        if family == 'static' and (offset != 0 or not value_getter_in_primary(elf, group['primary'])):
            mismatched.append((name, 'static: interface or value getter differs'))
        if family == 'nav' and offset != 0:
            mismatched.append((name, 'nav: the Linux interface is not at offset 0'))
        if kind == 'controller':
            ids = controller_type_ids(pe, elf, rva, group['by_top'][-offset])
            if ids[0] is None or ids[0] != ids[1]:
                mismatched.append((name, f'type ID {ids[0]} on Windows, {ids[1]} on Linux'))
        rows.append((group['by_top'][-offset], rva, flags, FAMILY[family], -offset, name))
    rows.sort()
    if len({r[0] for r in rows}) != len(rows) or len({r[1] for r in rows}) != len(rows):
        raise SystemExit('class table is not one to one')
    linux_only = []
    for address, t in sorted(typeinfo.items()):
        if t['name'] in paired or not complete_vtables(elf, entries.get(t['name'], [])):
            continue
        root = next((LINUX_ROOTS[n] for n, _ in linux_bases(typeinfo, address) if n in LINUX_ROOTS), None)
        if root:
            linux_only.append((t['name'], root))
    return rows, unmatched, mismatched, linux_only


def check_census(log, rows):
    """Every object and controller of one Windows client's first WORLD_OBJECT tick against the
    table. A row without state (flags & 8) has motion = SHA-256 of kind, type, flags, parent, so
    its controller's vtable (kind; 0 for none) can be recovered from the log alone."""
    pattern = re.compile(rb'WORLD_OBJECT tick=(\d+) id=[0-9a-f]{16} type=([0-9a-f]+) flags=([0-9a-f]+) parent=([0-9a-f]{16}) '
                         rb'topology=[0-9a-f]{64} health=[0-9a-f]{64} motion=([0-9a-f]{64})')
    objects = {r[1]: r for r in rows if not r[3]}
    controllers = [r[1] for r in rows if r[3]]
    tick, total, absent, stateless, problems = None, 0, 0, Counter(), Counter()
    seen = [0]
    with open(log, 'rb') as stream:
        for line in stream:
            found = pattern.search(line) if b'WORLD_OBJECT' in line else None
            if not found:
                continue
            if tick is None:
                tick = found.group(1)
            if found.group(1) != tick:
                break
            total += 1
            kind_type, flags, parent = int(found.group(2), 16), int(found.group(3), 16), int(found.group(4), 16)
            if not flags:
                absent += 1
                continue
            row = objects.get(kind_type)
            if not row:
                problems[f'object class {kind_type:#x} not in the table'] += 1
                continue
            if bool(flags & 2) != bool(row[2] & 1) or bool(flags & 4) != bool(row[2] & 2):
                problems[f'object class {row[5]}: flags {flags:#x} against table flags {row[2]:#x}'] += 1
            if flags & 8:
                motion = found.group(5).decode()
                for kind in seen + [c for c in controllers if c not in seen]:
                    if hashlib.sha256(struct.pack('<IIIQ', kind, kind_type, flags, parent)).hexdigest() == motion:
                        stateless[kind] += 1
                        if kind not in seen:
                            seen.append(kind)
                        break
                else:
                    problems[f'object class {row[5]}: controller not in the table'] += 1
    names = {r[1]: r[5] for r in rows}
    print(f'census tick {tick.decode() if tick else None}: {total} rows, {absent} absent, stateless controllers:')
    for kind, count in stateless.most_common():
        print(f'  {count:6} {names.get(kind, "no controller")}')
    for problem, count in problems.most_common():
        print('  PROBLEM', count, problem)
    return not problems and total > 0


def render(rows):
    lines = ['/* Generated by tools/dev/lockstep_linux_world_classes.py from the pinned X4 9.00 builds',
             ' * (Windows X4.exe 19750a65...6891, Linux X4 113f73d7...045f20); do not edit.',
             ' * Linux vtable VA -> Windows vtable RVA, the Windows class flags (bit 0 destructible,',
             ' * 1 positional, 2 object; 0x80000000 unknown), the movement controller family (1 nav,',
             ' * 2 dock, 3 undock, 4 static, 5 movement, 6 other) and the Linux offset-to-top, sorted by',
             ' * Linux VA. Classes: U::Component and U::MovementControllerInterface descendants, by RTTI',
             ' * name. A controller\'s Linux vtable is the one of its interface subobject (what an object',
             ' * points to); top leads from there to the complete object. */',
             f'#define LXW_CLASS_COUNT {len(rows)}u',
             'typedef struct {uint32_t linux_vtable,windows_rva,flags,family;int32_t top;} lxw_class;',
             'static const lxw_class lxw_classes[LXW_CLASS_COUNT]={']
    for lv, rva, flags, family, top, name in rows:
        lines.append(f'    {{{lv:#x},{rva:#x},{flags:#x},{family},{top}}}, /* {name} */')
    lines.append('};')
    return '\n'.join(lines) + '\n'


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--windows', required=True, type=Path)
    p.add_argument('--linux', required=True, type=Path)
    p.add_argument('--check', action='store_true', help='compare with the committed header instead of writing it')
    p.add_argument('--census', type=Path, help='also check one Windows client log with a full WORLD_OBJECT tick (read only)')
    args = p.parse_args()
    rows, unmatched, mismatched, linux_only = build(args.windows, args.linux)
    text = render(rows)
    counts = {f: sum(1 for r in rows if r[3] == v) for f, v in FAMILY.items()}
    print(f'classes {len(rows)} (families {counts}); not 1:1: unmatched {len(unmatched)}, '
          f'mismatched {len(mismatched)}, Linux only {len(linux_only)}')
    print(f'controllers by Linux offset of the interface: {sorted(Counter(-r[4] for r in rows if r[3]).items())}')
    for name, reason in unmatched:
        print('  unmatched', name, reason)
    for name, reason in mismatched:
        print('  mismatched', name, reason)
    for name, root in linux_only:
        print('  Linux only', name, root)
    if mismatched or linux_only or any(not reason.startswith('abstract') for _, reason in unmatched):
        raise SystemExit('the class table does not cover both executables')
    if args.census and not check_census(args.census, rows):
        raise SystemExit('the census shows classes the table does not cover')
    if args.check:
        if OUT.read_text() != text:
            raise SystemExit(f'{OUT} is out of date')
        print('LINUX WORLD CLASSES PASS')
    else:
        OUT.write_text(text, newline='\n')
        print(OUT)


if __name__ == '__main__':
    main()
