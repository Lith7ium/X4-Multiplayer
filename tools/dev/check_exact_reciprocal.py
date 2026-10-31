"""Offline proof for native_gate_exact_reciprocal: decode every emitted trampoline.

Input: the export of test_lockstep_gate.exe <X4.exe> with X4LAN_XRCP_EXPORT set (the
trampoline region built over a relocated copy of X4.exe) and the executable itself, or
for the Linux preload (X4MP_EXACT_RECIPROCAL=1) the export of test_preload_reciprocal
(the region installed over the Linux X4 mapped at its fixed addresses) and the ELF.
For every window the trampoline must consist of, in order, for each original
instruction of the window either
  - the exact recipe for RCPSS d,s:   lea rsp,[rsp-D]; movss [rsp],s; andps d,[mask];
                                       orps d,[one]; divss d,[rsp]; lea rsp,[rsp+D]
                                       (D = 0x10 on Windows, 0x90 on Linux: beyond the
                                       System V red zone)
  - the exact recipe for RSQRTSS d,s: the same with sqrtss d,[rsp]; movss [rsp],d after the
                                       first store
  - for RSQRTSS d,s opening an rcp(rsqrt(x)) chain: sqrtss d,s (register form, the same
    bytes with opcode 51); the chain is re-derived here from the original code: the
    first later instruction of the same block that touches d (xmm/ymm/zmm) must be
    RCPSS d,d, and that instruction must appear in a window with nothing emitted for it
    (d then holds the correctly rounded sqrt(s), lanes 1-3 kept). An RCPSS is dropped
    only as such a chain end, and every chain end must be dropped; or that first
    instruction is a whole-register copy MOVAPS/MOVUPS/MOVAPD/MOVUPD/MOVDQA/MOVDQU c,d and
    the next one touching d or c is RCPSS c,d (a copied chain), whose trampoline must then
    be the exact RCPSS recipe for d,d (d[0] = 1/d[0]: c keeps sqrt(s), d the exact
    1/sqrt(s) of the one-by-one recipe); or that first instruction is RCPSS e,d (e != d,
    MSVC's form), whose trampoline must be MOVSS e,d (register form) and then the same
    recipe on d,d;
  - or the original instruction byte for byte (RIP displacement re-targeted to the same
    absolute address),
followed by jmp rel32 to the end of the window, with mask/one the region's constants.
(That no branch enters a chain is the generator's proof, gen_exact_reciprocal.py.)
No RCPSS, RSQRTSS, RCPPS or RSQRTPS remains, and the only arithmetic added is DIVSS and
SQRTSS, which IEEE 754 defines as correctly rounded and every x86-64 CPU implements
exactly, so the rewritten sites give the same bits on Intel and AMD under the same
MXCSR. Needs capstone.

  python tools/dev/check_exact_reciprocal.py --windows <X4.exe> --export <xrcp-export.bin>
  python tools/dev/check_exact_reciprocal.py --linux <X4> --export <xrcp-linux-export.bin>
"""
import argparse
from pathlib import Path
import struct
import sys

import capstone
from capstone import x86_const as X

sys.path.insert(0, str(Path(__file__).resolve().parent))
import lockstep_linux_map as lm  # noqa: E402

APPROX = {'rcpss', 'rsqrtss', 'rcpps', 'rsqrtps'}
COPIES = ('movaps', 'movups', 'movapd', 'movupd', 'movdqa', 'movdqu')
CHAIN_REACH = 16
CONSTANTS = bytes([0, 0, 0, 0] + [0xff] * 12 + [0, 0, 0x80, 0x3f] + [0] * 12)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--windows', type=Path)
    group.add_argument('--linux', type=Path)
    parser.add_argument('--export', type=Path, required=True)
    args = parser.parse_args()
    if args.windows:
        pe, drop = lm.PeImage(lm.verify_build(args.windows, lm.WINDOWS_BUILD)), 16
    else:
        pe, drop = lm.ElfImage(lm.verify_build(args.linux, lm.LINUX_BUILD)), 144
    data = args.export.read_bytes()
    if data[:5] != b'XRCP1':
        raise SystemExit('not an exact-reciprocal export')
    image, region = struct.unpack_from('<QQ', data, 5)
    count, size = struct.unpack_from('<II', data, 21)
    rows = [struct.unpack_from('<III', data, 29 + 12 * i) for i in range(count)]
    blob = data[29 + 12 * count:29 + 12 * count + size]
    if blob[:32] != CONSTANTS:
        raise SystemExit('constants differ')
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    mask, one = region, region + 16
    stats = {'windows': 0, 'rcpss': 0, 'rsqrtss': 0, 'copied': 0, 'rip': 0, 'chains': 0, 'copied_chains': 0, 'moved_chains': 0}
    chain_ends, dropped = {}, set()

    def reg(op):
        return op.reg

    def xmm(r):
        name = md.reg_name(r) or ''
        return int(name[3:]) if name[:3] in ('xmm', 'ymm', 'zmm') and name[3:].isdigit() else None

    def chain_end(src):
        """(address, copy register or None) of the RCPSS d,d or c,d that closes the chain opened by
        RSQRTSS src, or None."""
        d, copy, at = xmm(src.operands[0].reg), None, src.address + src.size
        for _ in range(CHAIN_REACH):
            raw = pe.read(at - image, 16)
            ins = next(md.disasm(raw, at, count=1), None) if raw else None
            if ins is None:
                return None
            m = ins.mnemonic
            if m in ('ret', 'int3', 'hlt', 'ud2', 'call', 'loop', 'syscall') or m.startswith(('j', 'vzero', 'xrstor', 'fxrstor')):
                return None
            read, written = ins.regs_access()
            watched = {d} if copy is None else {d, copy}
            if any(xmm(r) in watched for r in list(read) + list(written)):
                regs = [xmm(o.reg) if o.type == X.X86_OP_REG else None for o in ins.operands]
                if m == 'rcpss' and copy is None and regs == [d, d]:
                    return at, None, 'inplace'
                if m == 'rcpss' and copy is not None and regs == [copy, d]:
                    return at, copy, 'copy'
                if m == 'rcpss' and copy is None and len(regs) == 2 and regs[1] == d and regs[0] not in (None, d):
                    return at, regs[0], 'move'
                if m in COPIES and copy is None and len(regs) == 2 and regs[1] == d and regs[0] not in (None, d):
                    copy, at = regs[0], at + ins.size
                    continue
                return None
            at += ins.size
        return None

    for rva, offset, used in rows:
        original = list(md.disasm(pe.read(rva, 32), image + rva))
        tramp = list(md.disasm(blob[offset:offset + used], region + offset))
        if sum(i.size for i in tramp) != used:
            raise SystemExit(f'{rva:#x}: trampoline does not decode completely')
        at, consumed = 0, 0
        window_end = None
        while at < len(tramp):
            ins = tramp[at]
            if ins.mnemonic == 'jmp':
                window_end = ins.operands[0].imm
                if at != len(tramp) - 1:
                    raise SystemExit(f'{rva:#x}: jmp before the end')
                while consumed < len(original) and original[consumed].address in chain_ends and \
                        chain_ends[original[consumed].address][2] == 'inplace' and \
                        original[consumed].address < window_end:   # a chain end closing the window
                    dropped.add(original[consumed].address)
                    consumed += 1
                break
            src = original[consumed]
            consumed += 1
            copy_end = None
            if src.mnemonic == 'rcpss' and src.address in chain_ends:
                d, c, form = chain_ends[src.address]
                if form == 'inplace':
                    if xmm(src.operands[0].reg) != d or xmm(src.operands[1].reg) != d:
                        raise SystemExit(f'{src.address:#x}: chain end is not rcpss d,d')
                    dropped.add(src.address)
                    continue
                if xmm(src.operands[0].reg) != c or xmm(src.operands[1].reg) != d:
                    raise SystemExit(f'{src.address:#x}: {form} chain end is not rcpss c,d')
                if form == 'move':   # movss c,d (register form) first: c[0] = sqrt(s), c's lanes 1-3 kept
                    mv = ins
                    if mv.mnemonic != 'movss' or len(mv.operands) != 2 or any(o.type != X.X86_OP_REG for o in mv.operands) or \
                            xmm(mv.operands[0].reg) != c or xmm(mv.operands[1].reg) != d:
                        raise SystemExit(f'{src.address:#x}: {mv.mnemonic} {mv.op_str} where movss c,d belongs')
                    at += 1
                    stats['moved_chains'] += 1
                copy_end = src.address
            if src.mnemonic == 'rsqrtss' and ins.mnemonic == 'sqrtss' and ins.operands[1].type == X.X86_OP_REG:
                want = bytearray(src.bytes)
                want[-2] = 0x51
                if bytes(ins.bytes) != bytes(want) or src.operands[1].type != X.X86_OP_REG:
                    raise SystemExit(f'{src.address:#x}: {ins.op_str} is not the register sqrtss of {src.op_str}')
                end = chain_end(src)
                if end is None:
                    raise SystemExit(f'{src.address:#x}: sqrtss without an rcpss d,d or c,d closing the chain')
                chain_ends[end[0]] = (xmm(src.operands[0].reg), end[1], end[2])
                stats['chains'] += 1
                stats['copied_chains'] += end[2] == 'copy'
                at += 1
                continue
            if src.mnemonic in ('rcpss', 'rsqrtss'):
                d, s = reg(src.operands[0]), reg(src.operands[1])
                if copy_end is not None:   # the copied chain's end: d[0] = 1/d[0] on the rsqrtss register
                    d = s
                    dropped.add(copy_end)
                want = [('lea', None), ('movss', 'store_s')]
                if src.mnemonic == 'rsqrtss':
                    want += [('sqrtss', 'load_d'), ('movss', 'store_d')]
                want += [('andps', mask), ('orps', one), ('divss', 'load_d'), ('lea', None)]
                for k, (mnemonic, check) in enumerate(want):
                    got = tramp[at + k]
                    if got.mnemonic != mnemonic:
                        raise SystemExit(f'{rva:#x}: {got.mnemonic} {got.op_str} where {mnemonic} belongs')
                    ops = got.operands
                    if mnemonic == 'lea':
                        if ops[0].reg != X.X86_REG_RSP or ops[1].mem.base != X.X86_REG_RSP or ops[1].mem.disp not in (-drop, drop):
                            raise SystemExit(f'{rva:#x}: {got.op_str}')
                    elif check == 'store_s' or check == 'store_d':
                        if ops[0].mem.base != X.X86_REG_RSP or ops[0].mem.disp or ops[1].reg != (s if check == 'store_s' else d):
                            raise SystemExit(f'{rva:#x}: {got.op_str}')
                    elif check == 'load_d':
                        if ops[0].reg != d or ops[1].mem.base != X.X86_REG_RSP or ops[1].mem.disp:
                            raise SystemExit(f'{rva:#x}: {got.op_str}')
                    else:
                        target = got.address + got.size + ops[1].mem.disp
                        if ops[0].reg != d or ops[1].mem.base != X.X86_REG_RIP or target != check:
                            raise SystemExit(f'{rva:#x}: {got.op_str}')
                at += len(want)
                stats[src.mnemonic] += 1
            else:
                if src.mnemonic in APPROX:
                    raise SystemExit(f'{rva:#x}: approximate instruction copied')
                if ins.mnemonic != src.mnemonic or ins.size != src.size:
                    raise SystemExit(f'{rva:#x}: copy {ins.mnemonic} differs from {src.mnemonic}')
                same = ins.bytes == src.bytes
                if not same:
                    if 'rip' not in src.op_str:
                        raise SystemExit(f'{rva:#x}: copy changed')
                    o = src.disp_offset
                    if ins.bytes[:o] != src.bytes[:o] or ins.bytes[o + 4:] != src.bytes[o + 4:]:
                        raise SystemExit(f'{rva:#x}: copy changed outside its displacement')
                    if ins.address + ins.size + ins.disp != src.address + src.size + src.disp:
                        raise SystemExit(f'{rva:#x}: RIP target moved')
                    stats['rip'] += 1
                stats['copied'] += 1
                at += 1
        window_length = sum(original[i].size for i in range(consumed))
        if window_end != image + rva + window_length:
            raise SystemExit(f'{rva:#x}: jmp back to {window_end:#x}, window ends at {image + rva + window_length:#x}')
        stats['windows'] += 1
    if set(chain_ends) != dropped:
        missing = sorted(set(chain_ends) - dropped)
        raise SystemExit(f'chain ends left native: {", ".join(hex(a) for a in missing[:8])}')
    copied = (f'; {stats["copied_chains"]} of them through a register copy, closing RCPSS c,d as the exact d[0] = 1/d[0]'
              if stats['copied_chains'] else '')
    copied += (f'; {stats["moved_chains"]} of them into another register, closing RCPSS e,d as MOVSS e,d and the exact '
               f'd[0] = 1/d[0]' if stats['moved_chains'] else '')
    print(f'{stats["windows"]} trampolines decoded: {stats["rcpss"]} RCPSS and {stats["rsqrtss"]} RSQRTSS as exact '
          f'DIVSS/SQRTSS recipes, {stats["chains"]} rcp(rsqrt(x)) chains as one SQRTSS (each closing RCPSS d,d dropped{copied}), '
          f'{stats["copied"]} original instructions copied ({stats["rip"]} RIP-relative re-targeted), '
          f'no approximate instruction left (stack drop {drop})')


if __name__ == '__main__':
    main()
