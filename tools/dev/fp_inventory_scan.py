"""Instruction census of both X4 9.00 executables for the cross-platform FP inventory (offline).

Linear-disassembles every function of the Windows X4.exe (.pdata roots, chained
fragments folded) and of the Linux X4 ELF (.eh_frame FDE ranges) with capstone, in
parallel. Executable bytes no unwind entry covers (MSVC leaf functions, CRT/asm stubs)
are swept too and split at decoded int3 padding (and, on Linux, after ret/jmp) into
('gap', start) pseudo-functions. Per function it caches:
  mn     mnemonic -> count
  calls  [(address, 'call'|'jmp', target)] direct transfers leaving the function
  refs   [(address, mnemonic, target, width)] RIP-relative memory operands (and, on the
         non-PIE ELF, absolute memory displacements >= 0x400000)
  imm    sorted set of 64-bit movabs immediates
  ymm    instructions with a ymm operand
The cache holds addresses, counts and immediates only, never instruction bytes. It is a
private analysis artefact (default .local/fp-inventory/scan.pkl) and is not committed.

  python tools/dev/fp_inventory_scan.py --windows X4.exe --linux X4 [--out cache.pkl]
"""
import argparse
import collections
import multiprocessing
import pickle
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import lockstep_linux_map as lm  # noqa: E402  (PE/ELF readers and build pins)

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUT = ROOT / '.local/fp-inventory/scan.pkl'
RIP = re.compile(r'\[rip ([+-]) (0x[0-9a-f]+)\]')
ABS = re.compile(r'\[(?:[^\]]*\+ )?(0x[0-9a-f]{6,})\]')
WIDTH = re.compile(r'^(byte|word|dword|qword|xmmword|ymmword|tbyte|zmmword) ptr')
_IMAGES = {}


def _init(paths):
    for side, path in paths.items():
        data = Path(path).read_bytes()
        _IMAGES[side] = lm.PeImage(data) if side == 'w' else lm.ElfImage(data)


def _decode(image, begin, end, md):
    """Robust linear sweep of [begin, end): undecodable bytes are skipped one at a time."""
    code = image.read(begin, end - begin) or b''
    at = 0
    while at < len(code):
        last = None
        for insn in md.disasm_lite(code[at:], begin + at):
            yield insn
            last = insn
        at = (last[0] + last[1] - begin) if last else at
        if at < len(code):
            yield (begin + at, 1, '(bad)', '')
            at += 1


def _facts(side, insns, ranges):
    mn = collections.Counter()
    calls, refs, imm = [], [], set()
    ymm = 0
    inside = lambda t: any(b <= t < e for b, e in ranges)
    for address, size, mnemonic, op in insns:
        mn[mnemonic] += 1
        if mnemonic in ('call', 'jmp'):
            if op.startswith('0x'):
                target = int(op, 16)
                if mnemonic == 'call' or not inside(target): calls.append((address, mnemonic, target))
                continue
        if 'ymm' in op: ymm += 1
        if mnemonic == 'movabs':
            tail = op.rsplit(', ', 1)[-1]
            if tail.startswith('0x'): imm.add(int(tail, 16))
        if '[' not in op:
            if side == 'l' and '0x' in op and mnemonic in ('mov', 'lea', 'push'):
                tail = op.rsplit(', ', 1)[-1]
                if tail.startswith('0x'):
                    value = int(tail, 16)
                    if 0x400000 <= value < 0x8000000: refs.append((address, mnemonic, value, ''))
            continue
        mem = next((part for part in op.split(', ') if '[' in part), op)
        width = WIDTH.search(mem)
        width = width.group(1) if width else ''
        rip = RIP.search(op)
        if rip:
            disp = int(rip.group(2), 16)
            target = address + size + (disp if rip.group(1) == '+' else -disp)
            if mnemonic in ('call', 'jmp'): calls.append((address, mnemonic + '*', target))
            else: refs.append((address, mnemonic, target, width))
            continue
        if side == 'l':
            a = ABS.search(op)
            if a:
                value = int(a.group(1), 16)
                if 0x400000 <= value < 0x8000000: refs.append((address, mnemonic, value, width))
    return dict(mn=dict(mn), calls=calls, refs=refs, imm=sorted(imm), ymm=ymm,
                size=sum(e - b for b, e in ranges))


def _scan(task):
    """One function (key = start) or one uncovered gap (key = ('gap', start)); a gap is
    split into pieces at decoded int3/padding so that MSVC leaf functions stay separate."""
    import capstone
    side, key, ranges = task
    image = _IMAGES[side]
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    if not isinstance(key, tuple):
        insns = [i for b, e in ranges for i in _decode(image, b, e, md)]
        return [(side, key, _facts(side, insns, ranges))]
    out, piece = [], []

    def flush():
        while piece and piece[-1][2] in ('nop', '(bad)'): piece.pop()
        if piece:
            b, e = piece[0][0], piece[-1][0] + piece[-1][1]
            out.append((side, ('gap', b), _facts(side, piece, ((b, e),))))
    for insn in (i for b, e in ranges for i in _decode(image, b, e, md)):
        if insn[2] == 'int3' or (insn[2] in ('nop', '(bad)') and not piece):
            flush(); piece = []
            continue
        piece.append(insn)
        if insn[2] in ('ret', 'jmp') and side == 'l':
            flush(); piece = []
    flush()
    return out


def _gaps(covered, sections):
    """Uncovered [begin, end) spans of executable sections."""
    covered = sorted(covered)
    out = []
    for lo, hi in sections:
        at = lo
        for b, e in covered:
            if e <= at or b >= hi: continue
            if b > at: out.append((at, b))
            at = max(at, e)
        if at < hi: out.append((at, hi))
    return out


def tasks(pe, elf):
    out = []
    if pe:
        roots = sorted({r for _, _, r in pe.entries})
        for root in roots: out.append(('w', root, tuple(pe.ranges(root))))
        text = [(va, va + size) for name, va, size, raw, raw_size, flags in pe.sections if flags & 0x20000000]
        for b, e in _gaps([(b, e) for b, e, _ in pe.entries], text):
            out.append(('w', ('gap', b), ((b, e),)))
    if elf:
        for b, e in elf.functions: out.append(('l', b, ((b, e),)))
        s = elf.sections['.text']
        for b, e in _gaps(elf.functions, [(s['addr'], s['addr'] + s['size'])]):
            out.append(('l', ('gap', b), ((b, e),)))
    return out


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('--windows', type=Path, required=True)
    p.add_argument('--linux', type=Path, required=True)
    p.add_argument('--out', type=Path, default=DEFAULT_OUT)
    p.add_argument('--jobs', type=int, default=max(1, (multiprocessing.cpu_count() or 2) - 2))
    args = p.parse_args(argv)
    pe = lm.PeImage(lm.verify_build(args.windows, lm.WINDOWS_BUILD))
    elf = lm.ElfImage(lm.verify_build(args.linux, lm.LINUX_BUILD))
    if elf.build_id() != lm.LINUX_BUILD['build_id']: raise SystemExit('Linux build ID mismatch')
    work = tasks(pe, elf)
    work.sort(key=lambda t: -sum(e - b for b, e in t[2]))  # big first for load balance
    started = time.time()
    result = {'w': {}, 'l': {}}
    with multiprocessing.Pool(args.jobs, _init, ({'w': str(args.windows), 'l': str(args.linux)},)) as pool:
        for i, pieces in enumerate(pool.imap_unordered(_scan, work, chunksize=64)):
            for side, key, facts in pieces: result[side][key] = facts
            if i % 20000 == 0: print(f'{i}/{len(work)} {time.time() - started:.0f}s', file=sys.stderr)
    meta = dict(windows=lm.WINDOWS_BUILD, linux=lm.LINUX_BUILD, functions={k: len(v) for k, v in result.items()},
                seconds=round(time.time() - started, 1))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open('wb') as out: pickle.dump(dict(meta=meta, **result), out, protocol=pickle.HIGHEST_PROTOCOL)
    print(meta)


if __name__ == '__main__':
    raise SystemExit(main())
