"""Windows/Linux floating-point and ordering inventory of X4 9.00 (offline, read-only).

Reads the per-function instruction census written by fp_inventory_scan.py, both pinned
executables (for constants and targeted disassembly), the x4-re function pair table and,
optionally, the x4-re labels, and writes:
  --json  summary: instruction classes, CRT/libm entry tables and call sites by domain,
          FP-environment writers, rcp/rsqrt sites, compare/parity statistics, paired
          function FP profiles, reciprocal substitution, decimal parsers, libstdc++
          ordering helpers, Jolt build identity
  --tsv   per-function / per-site rows (addresses, counts, domains; no bytes)

Domains ('physics', 'movement', 'combat', 'economy', 'ai_script', 'world' = simulation;
'nonsim'; 'thirdparty'; unknown) come from Windows RTTI/label names (x4-re symbols.csv,
labels/merged.tsv, labels/lockstep.tsv), Linux .dynsym names, the pair table, and a
caller-majority propagation to unnamed functions. They are a triage aid, not proof.

  python tools/dev/fp_inventory.py --scan .local/fp-inventory/scan.pkl \
      --windows X4.exe --linux X4 --pairs x4-re/data/xfer/pairs.tsv --x4re x4-re \
      --json out.json --tsv out.tsv
"""
import argparse
import collections
import csv
import json
import pickle
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import lockstep_linux_map as lm  # noqa: E402

# ------------------------------------------------------------------ fixed facts (X4 9.00)

# Windows static-CRT math entries (RVA). Identified by unicorn execution of each entry on
# sample inputs (fp_libm_compare.py), by the FMA3 flag users below, and by pair votes
# against Linux libm call sites. 'fma3' = dispatches on __use_fma3_lib.
WINDOWS_CRT_MATH = {
    0x1a19480: ('sinf', 'fma3'), 0x1a19b80: ('cosf', 'fma3'), 0x1a240d0: ('tanf', 'fma3'),
    0x1a1a790: ('powf', 'fma3'), 0x1b43ed0: ('expf', 'fma3'), 0x1b44940: ('logf', 'fma3'),
    0x1b44460: ('log10f', 'fma3'), 0x1a26f80: ('sin', 'fma3'), 0x1a260d0: ('cos', 'fma3'),
    0x1a274f0: ('tan', 'fma3'), 0x1a26650: ('exp', 'fma3'), 0x1a26a50: ('log', 'fma3'),
    0x1a38060: ('log10', 'fma3'), 0x1a1e840: ('pow', 'fma3'),
    0x1b43ce0: ('asinf', 'plain'), 0x1a19250: ('acosf', 'plain'), 0x1a23e64: ('atanf', 'plain'),
    0x1a1a398: ('atan2f', 'plain'), 0x1a11848: ('exp2f', 'plain'), 0x1a10798: ('log2f', 'plain'),
    0x1a10928: ('log2f', 'thunk'), 0x1a25460: ('asin', 'plain'), 0x1a251c0: ('acos', 'plain'),
    0x1a256e4: ('atan', 'plain'), 0x1a258fc: ('atan2', 'plain'), 0x1a250cc: ('exp2', 'plain'),
    0x1a24744: ('log2', 'plain'),
    0x1a1a080: ('fmodf', 'exact'), 0x1b44180: ('fmod', 'exact'), 0x1b44d38: ('sqrtf', 'exact'),
    0x1a1a2cc: ('sqrt', 'exact'), 0x1a1c794: ('roundf', 'exact'), 0x1a051ac: ('round', 'exact'),
    # compiler sincos helpers over the packed sine kernels (nonstandard lane ABI)
    0x1750210: ('sincosf', 'packed: xmm0=x -> xmm0=[sin x, cos x]; cos = sin(|x|+pi/2) unless |x| tiny'),
    0x1750253: ('sincosf', 'packed: xmm0=a, xmm1=b -> xmm0=[sin a, cos a, sin b, cos b]; cos = sin(x+pi/2)'),
    0x1b42f20: ('sincos', 'packed: xmm0=x -> xmm0=[sin x, cos x] (binary64 lanes)'),
}
WINDOWS_DISPATCH = {
    'use_fma3_lib': 0x38edec0, 'crt_init_fma3': 0x1a310b0,
    'isa_available': 0x2f3f220, 'isa_available_init': 0x17502c0,
    'packed_sinf_kernel_dispatch': 0x17501f0, 'packed_sin_kernel_dispatch': 0x1b42f00,
}
LINUX_MATH = ('acos acosf asin asinf atan atan2 atan2f atanf cos cosf exp exp2 exp2f expf floor floorf fmod '
              'fmodf frexpf log log10 log10f log2 log2f logf pow powf round roundf sin sincos sincosf sinf '
              'sqrt sqrtf tan tanf').split()
EXACT = frozenset('sqrt sqrtf fmod fmodf floor floorf round roundf frexpf'.split())
OTHER_LIBC = ('strtod __isoc99_sscanf ecvt qsort rand srand snprintf sprintf vsnprintf newlocale uselocale '
              'freelocale _ZNSt6thread20hardware_concurrencyEv sysconf').split()
DECIMAL_PARSERS = {(0x14c3090, 0x1a3b1a0): 'binary64 decimal parser (Windows scale *= 0.1, Linux scale /= 10.0)',
                   (0x14c2bd0, 0x1a3ac40): 'binary32-result decimal parser (same difference, then cvtsd2ss)'}
FPENV_ROLES = {0x14dd0e0: 'engine: sets MXCSR.FTZ (bit 15) on the main thread; called from X4::GameClient::Init 0x1225ee7',
               0x1a1a080: 'CRT fmodf: saves/restores MXCSR', 0x1a1a790: 'CRT powf: saves/restores MXCSR',
               0x1a31b20: 'CRT FP status/control helper', 0x1a37bb0: 'CRT FP control helper',
               0x1a37ce8: 'CRT FP control helper'}
JOLT_VERSION_ID = 0x400050500  # JPH_VERSION_ID: features 0x400 (ObjectStream) << 24 | 5.5.0
CRT_RANGE = (0x19e0000, 0x1b50000)  # Windows static CRT / libm code
SIM = frozenset('physics movement combat economy ai_script world'.split())

DOMAIN_RULES = [
    ('physics', r'^JPH::|^XPhys::|Jolt|Physics|Collision|RigidBody|^U::.*Shape'),
    ('movement', r'NavigationController|^U::Path::|FlightControl|MovementController|^U::NavContext|Highway|'
                 r'^U::MT::|MassTraffic|Steering|Spline|Docking|^U::Engine::|Formation|Autopilot|Travel|'
                 r'Pathing|SetVelocity|::Move$'),
    ('combat', r'^U::Weapon|^U::Bullet|^U::Turret|Missile|^U::Bomb|^U::Mine|Damage|^U::Destructible|'
               r'^U::Defensible|Shield|Explosi|ShootController|Target|Countermeasure'),
    ('economy', r'^U::Station|^U::Container|ProductionModule|BuildModule|StorageModule|Trade|Ware|'
                r'^U::BuildStorage|ProcessingModule|BuildProcessor|Dismantle|Economy|Production|'
                r'Construction|^U::Account|Cargo|Lockbox|MiningNode|Recyclable|ResourceProbe|Collectable'),
    ('ai_script', r'^AI::|^Scripts::|^MD::|^Mission|^U::NPC|^U::Regions|Radar|Gravidar|Order|^U::Region|'
                  r'Faction|^U::Player::|Patrol'),
    ('world', r'^U::'),
    ('nonsim', r'^UI::|^Lua|^glslang|^spvtools|^spv|^SLNet|^Online|^XGfx|^XSfx|^VR|^Cutscenes|^XIn|^XNet|'
               r'^Network|^fm::|^JPM|HoloMap|MapBehavior|UIElement|^FCD|^FU|^X509|^SSL|^EVP|^ossl|^OSSL|'
               r'^BN_|^ASN1|^d2i|^i2d|^PEM|^BIO|^EC_|^tls|^ssl|^RSA|^curl|^xml|^U::Animation|Cutscene|'
               r'^X4::GameRender|Render|Shader|Texture|Sound|Audio|^XLib::XMLNode'),
]
FAMILY_DOMAIN = {'pathing': 'movement', 'nav': 'movement', 'pilot_player': 'movement', 'rotation_portable': 'movement',
                 'bullets_physics': 'combat', 'gravidar': 'ai_script', 'script_rng': 'ai_script', 'md': 'ai_script',
                 'events': 'ai_script', 'npc': 'ai_script'}

# ------------------------------------------------------------------ pure helpers (unit-tested)

ARITH = re.compile(r'^v?(add|sub|mul|div|sqrt|min|max|rcp|rsqrt)(ss|sd|ps|pd)$')
SSE41 = frozenset('blendps blendpd blendvps blendvpd dpps dppd insertps extractps pinsrb pinsrd pinsrq pextrb '
                  'pextrd pextrq pmulld pminsb pminsd pminuw pminud pmaxsb pmaxsd pmaxuw pmaxud ptest roundss '
                  'roundsd roundps roundpd pmovsxbw pmovsxbd pmovsxbq pmovsxwd pmovsxwq pmovsxdq pmovzxbw '
                  'pmovzxbd pmovzxbq pmovzxwd pmovzxwq pmovzxdq mpsadbw packusdw pcmpeqq phminposuw pblendw '
                  'pblendvb movntdqa pmuldq'.split())
FPENV = frozenset('ldmxcsr stmxcsr vldmxcsr vstmxcsr fldcw fnstcw fldenv fnstenv fxrstor fxsave xrstor xsave'.split())
COMPARES = frozenset('ucomiss ucomisd comiss comisd vucomiss vucomisd vcomiss vcomisd'.split())
PARITY = frozenset('jp jnp setp setnp cmovp cmovnp'.split())


def insn_class(mnemonic):
    """Coarse FP class of one capstone mnemonic, or None."""
    m = mnemonic
    if m.startswith(('vfmadd', 'vfmsub', 'vfnmadd', 'vfnmsub')): return 'fma'
    b = m[1:] if m.startswith('v') else m
    if b in ('rcpss', 'rcpps', 'rsqrtss', 'rsqrtps'): return 'approx'
    if m in FPENV: return 'fpenv'
    if m.startswith('f') and m != 'femms': return 'x87'
    if m in SSE41: return 'sse41'
    if ARITH.match(m): return 'arith'
    if b.startswith('cvt'): return 'cvt'
    return None


def profile(mn):
    """FP profile of a mnemonic counter: op counts by operation, precision and form."""
    s = collections.Counter()
    for m, c in mn.items():
        a = ARITH.match(m)
        if a:
            op, ty = a.groups()
            s[op] += c
            s['double' if ty in ('sd', 'pd') else 'single'] += c
            s['packed' if ty in ('ps', 'pd') else 'scalar'] += c
            continue
        b = m[1:] if m.startswith('v') else m
        if b in ('cvtss2sd', 'cvtps2pd'): s['widen'] += c
        elif b in ('cvtsd2ss', 'cvtpd2ps'): s['narrow'] += c
        elif b.startswith('cvtt'): s['truncate'] += c
        elif b.startswith('round'): s['round'] += c
        elif b in ('dpps', 'dppd', 'haddps', 'haddpd'): s['horizontal'] += c
        elif m.startswith(('vfmadd', 'vfmsub', 'vfnmadd', 'vfnmsub')): s['fma'] += c
    return s


def profile_tags(pw, pl, calls_w, calls_l):
    """Difference classes between the Windows and Linux profile of one function pair."""
    tags = set()
    if calls_w or calls_l: tags.add('libm_call')
    if pw['div'] < pl['div']: tags.add('fewer_div_windows')
    if pw['div'] > pl['div']: tags.add('more_div_windows')
    if pw['rcp'] + pw['rsqrt'] != pl['rcp'] + pl['rsqrt']: tags.add('approx_diff')
    if pw['sqrt'] != pl['sqrt']: tags.add('sqrt_diff')
    if pw['widen'] != pl['widen'] or pw['narrow'] != pl['narrow'] or pw['double'] != pl['double']: tags.add('precision_diff')
    if pw['packed'] != pl['packed']: tags.add('vectorization_diff')
    if any(pw[o] != pl[o] for o in ('add', 'sub', 'mul')): tags.add('arith_count_diff')
    if pw['min'] + pw['max'] != pl['min'] + pl['max']: tags.add('minmax_diff')
    if pw['round'] != pl['round'] or pw['truncate'] != pl['truncate']: tags.add('rounding_diff')
    if not tags: tags.add('same_profile')
    return tags


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


def is_pow2(c):
    import math
    m, _ = math.frexp(abs(c))
    return m == 0.5


def divisor_constants(insns, read_const):
    """Constant divisors of div{ss,sd,ps,pd}: memory operands, or registers loaded from a
    constant and not overwritten before the division. insns: [(address, size, mnemonic,
    op_str)]; read_const(address, size, op_str) -> ('s'|'d', value) or None."""
    found, regs = [], {}
    for a, s, mn, op in insns:
        b = mn[1:] if mn.startswith('v') else mn
        parts = op.split(', ')
        dst = parts[0] if parts else ''
        const = read_const(a, s, op) if 'rip' in op else None
        if b in ('divss', 'divsd', 'divps', 'divpd'):
            src = parts[-1] if len(parts) > 1 else ''
            if const is not None: found.append((a, const))
            elif src in regs: found.append((a, regs[src]))
            regs.pop(dst, None)
            continue
        if b in ('movss', 'movsd', 'movaps', 'movups') and const is not None and dst.startswith('xmm'):
            regs[dst] = const
        elif dst.startswith('xmm') and b not in COMPARES:
            regs.pop(dst, None)
    return found


def reciprocal_matches(linux_divisors, windows_divisors, windows_constants):
    """Classify each Linux constant division: 'substituted' when the Windows twin uses the
    rounded reciprocal constant and does not divide by c; 'exact' for powers of two."""
    out = []
    for a, (kind, c) in linux_divisors:
        if c == 0 or c != c or abs(c) == float('inf'): continue
        if is_pow2(c): out.append((a, kind, c, 'exact_pow2')); continue
        if (kind, c) in windows_divisors: out.append((a, kind, c, 'windows_divides')); continue
        recip = (kind, f32(1.0 / c) if kind == 's' else 1.0 / c)
        out.append((a, kind, c, 'substituted' if recip in windows_constants else 'unmatched'))
    return out


def name_domain(name, rules=None):
    if not name: return None
    for domain, pattern in (rules or _RULES):
        if pattern.search(name): return domain
    return None


_RULES = [(d, re.compile(p)) for d, p in DOMAIN_RULES]


def propagate(functions, seed, rounds=3, exclude=frozenset()):
    """Give unnamed functions the domain held by >= 60% of their classified callers."""
    callers = collections.defaultdict(list)
    for address, facts in functions.items():
        for _, _, target in facts['calls']:
            if target != address: callers[target].append(address)
    domain, source = dict(seed), {a: 'name' for a in seed}
    for r in range(rounds):
        new = {}
        for address in functions:
            if address in domain or address in exclude: continue
            votes = collections.Counter(domain[c] for c in callers.get(address, ()) if c in domain)
            if votes:
                d, n = votes.most_common(1)[0]
                if n >= 0.6 * sum(votes.values()): new[address] = d
        for a, d in new.items(): domain[a], source[a] = d, f'callers{r + 1}'
    return domain, source

# ------------------------------------------------------------------ loading


def load_scan(path):
    with Path(path).open('rb') as f: data = pickle.load(f)
    out = {}
    for side in ('w', 'l'):
        out[side] = {(k[1] if isinstance(k, tuple) else k): dict(v, gap=isinstance(k, tuple)) for k, v in data[side].items()}
    return out, data['meta']


def load_windows_names(x4re):
    names, families = {}, {}
    if not x4re: return names, families
    root = Path(x4re)
    with (root / 'symbols.csv').open(encoding='utf-8') as f:
        for r in csv.DictReader(f):
            if r['kind'] == 'function': names.setdefault(int(r['rva'], 16), r['name'])
    for label in ('labels/merged.tsv', 'labels/lockstep.tsv'):
        path = root / label
        if not path.exists(): continue
        for line in path.open(encoding='utf-8'):
            c = line.rstrip('\n').split('\t')
            if line.startswith('#') or len(c) < 3 or c[2] != 'function': continue
            try: a = int(c[0], 16)
            except ValueError: continue
            names.setdefault(a, c[1])
            fams = {lm.family(m) for m in re.findall(r'src/native/engine/(lockstep_\w+\.[ch])', '\t'.join(c[6:]))}
            if fams: families[a] = fams
    return names, families


def classify(scan, pe_names, families, elf, pairs):
    """Windows and Linux function -> domain."""
    w_seed = {}
    for a in scan['w']:
        d = name_domain(pe_names.get(a))
        if d is None and a in families:
            d = next((FAMILY_DOMAIN[f] for f in sorted(families[a]) if f in FAMILY_DOMAIN), 'world')
        if d: w_seed[a] = d
    wd, _ = propagate(scan['w'], w_seed)
    l_seed = {}
    for a, n in elf.dynsym.items():
        if n.startswith(('_ZN3JPH', '_ZNK3JPH')): l_seed[a] = 'physics'
        elif n.startswith(('_ZNSt', '_ZSt', '_ZNKSt', '_ZN9__gnu_cxx')): l_seed[a] = 'std'
        else: l_seed[a] = 'thirdparty'
    for w, l in pairs.items():
        if l not in l_seed and w in wd: l_seed[l] = wd[w]
    ld, _ = propagate(scan['l'], l_seed, exclude=frozenset(elf.plt))
    return wd, ld


def group(domain):
    return 'sim' if domain in SIM else (domain or 'unknown')

# ------------------------------------------------------------------ analyses


def census(scan, domains):
    out = {}
    for side in ('w', 'l'):
        tot, fns, sim = collections.Counter(), collections.Counter(), collections.Counter()
        bad_x87 = 0
        fp_fns, fp_ops = collections.Counter(), collections.Counter()
        for a, f in scan[side].items():
            n = sum(c for m, c in f['mn'].items() if ARITH.match(m))
            if n: fp_fns[group(domains[side].get(a))] += 1; fp_ops[group(domains[side].get(a))] += n
            seen = set()
            for m, c in f['mn'].items():
                k = insn_class(m)
                keys = [k] if k else []
                if m.startswith('v') and k != 'fma' and m not in ('verr', 'verw'): keys.append('vex')
                if m in ('cpuid', 'xgetbv'): keys.append(m)
                if m == 'fprem': keys.append('x87_fprem')
                for key in keys:
                    tot[key] += c; seen.add(key)
                    if group(domains[side].get(a)) == 'sim': sim[key] += c
            if f['ymm']: tot['ymm'] += f['ymm']; seen.add('ymm')
            if 'x87' in seen and f['mn'].get('(bad)'): bad_x87 += 1
            for key in seen: fns[key] += 1
        out[side] = dict(instructions=dict(sorted(tot.items())), functions=dict(sorted(fns.items())),
                         in_sim_domains=dict(sorted(sim.items())), x87_functions_with_undecodable_bytes=bad_x87,
                         fp_arithmetic_functions=dict(fp_fns), fp_arithmetic_ops=dict(fp_ops))
    return out


def callers_of(functions, targets):
    sites = collections.defaultdict(list)
    for a, f in functions.items():
        for site, mn, t in f['calls']:
            if t in targets and not mn.endswith('*'): sites[t].append((a, site))
    return sites


def math_tables(scan, domains, elf, rows):
    w_sites = callers_of(scan['w'], WINDOWS_CRT_MATH)
    windows = []
    for rva, (name, kind) in sorted(WINDOWS_CRT_MATH.items()):
        sites = [(c, s) for c, s in w_sites.get(rva, ()) if not CRT_RANGE[0] <= c < CRT_RANGE[1]]
        f = scan['w'].get(rva, {})
        reads_flag = any(t == WINDOWS_DISPATCH['use_fma3_lib'] for _, _, t, _ in f.get('refs', ()))
        dom = collections.Counter(group(domains['w'].get(c)) for c, _ in sites)
        windows.append(dict(rva=f'{rva:#x}', name=name, kind=kind, fma3_flag_read=reads_flag, call_sites=len(sites),
                            callers=len({c for c, _ in sites}), sites_by_domain=dict(dom)))
        for c, n in collections.Counter(c for c, _ in sites).items():
            rows.append(('libm_caller', 'windows', f'{c:#x}', name, n, domains['w'].get(c, ''), f'entry {rva:#x}'))
    fma3_users = sorted(f'{a:#x}' for a, f in scan['w'].items()
                        if any(t == WINDOWS_DISPATCH['use_fma3_lib'] for _, _, t, _ in f['refs']))
    stubs = {n: a for a, n in elf.plt.items()}
    l_sites = callers_of(scan['l'], set(elf.plt))
    linux = []
    for name in LINUX_MATH + OTHER_LIBC:
        stub = stubs.get(name)
        if stub is None: continue
        code = elf.read(stub, 16) or b''
        k = 4 if code[:4] == b'\xf3\x0f\x1e\xfa' else 0
        got = stub + k + 6 + struct.unpack_from('<i', code, k + 2)[0] if code[k:k + 2] == b'\xff\x25' else None
        sites = l_sites.get(stub, [])
        dom = collections.Counter(group(domains['l'].get(c)) for c, _ in sites)
        linux.append(dict(name=name, plt=f'{stub:#x}', got=f'{got:#x}' if got else None, call_sites=len(sites),
                          callers=len({c for c, _ in sites}), sites_by_domain=dict(dom),
                          kind='exact' if name in EXACT else 'transcendental' if name in LINUX_MATH else 'libc'))
        if name in LINUX_MATH:
            for c, n in collections.Counter(c for c, _ in sites).items():
                rows.append(('libm_caller', 'linux', f'{c:#x}', name, n, domains['l'].get(c, ''), f'plt {stub:#x}'))
    def total(sel):
        return dict(entries=len(sel), sites=sum(t['call_sites'] for t in sel),
                    sim_sites=sum(t['sites_by_domain'].get('sim', 0) for t in sel),
                    callers=sum(t['callers'] for t in sel))
    return dict(windows_entries=windows, windows_fma3_flag_users=fma3_users, windows_dispatch=
                {k: f'{v:#x}' for k, v in WINDOWS_DISPATCH.items()}, linux_imports=linux,
                totals=dict(windows_transcendental=total([t for t in windows if t['kind'] != 'exact']),
                            windows_exact=total([t for t in windows if t['kind'] == 'exact']),
                            linux_transcendental=total([t for t in linux if t['kind'] == 'transcendental']),
                            linux_exact=total([t for t in linux if t['kind'] == 'exact'])))


def fpenv(scan, domains, rows):
    out = {}
    for side in ('w', 'l'):
        writers = []
        for a, f in scan[side].items():
            n = {m: c for m, c in f['mn'].items() if m in ('ldmxcsr', 'vldmxcsr')}
            if n:
                writers.append(dict(function=f'{a:#x}', count=sum(n.values()), role=FPENV_ROLES.get(a, '')))
                rows.append(('mxcsr_write', 'windows' if side == 'w' else 'linux', f'{a:#x}', 'ldmxcsr',
                             sum(n.values()), domains[side].get(a, ''), FPENV_ROLES.get(a, '')))
        out[side] = sorted(writers, key=lambda r: r['function'])
    return out


def approx_sites(scan, domains, rows):
    out = {}
    for side in ('w', 'l'):
        per = collections.Counter(); fns = collections.Counter(); mn = collections.Counter()
        for a, f in scan[side].items():
            n = {m: c for m, c in f['mn'].items() if insn_class(m) == 'approx'}
            if not n: continue
            g = group(domains[side].get(a))
            per[g] += sum(n.values()); fns[g] += 1; mn.update(n)
            rows.append(('approx', 'windows' if side == 'w' else 'linux', f'{a:#x}',
                         ','.join(f'{k}{v}' for k, v in sorted(n.items())), sum(n.values()), domains[side].get(a, ''), ''))
        out[side] = dict(instructions_by_domain=dict(per), functions_by_domain=dict(fns), by_mnemonic=dict(mn))
    return out


def compare_stats(scan, domains):
    out = {}
    for side in ('w', 'l'):
        c = collections.Counter()
        for a, f in scan[side].items():
            g = group(domains[side].get(a))
            cmp_ = sum(f['mn'].get(m, 0) for m in COMPARES); par = sum(f['mn'].get(m, 0) for m in PARITY)
            c['compares'] += cmp_; c['parity'] += par
            if g == 'sim': c['sim_compares'] += cmp_; c['sim_parity'] += par
        c['parity_per_compare'] = round(c['parity'] / max(1, c['compares']), 4)
        c['sim_parity_per_compare'] = round(c['sim_parity'] / max(1, c['sim_compares']), 4)
        out[side] = dict(c)
    return out


class Disassembler:
    def __init__(self, pe, elf):
        import capstone
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        self.pe, self.elf = pe, elf

    def function(self, side, address):
        if side == 'w':
            ranges = self.pe.ranges(address) or [(address, address + 0x400)]
            image = self.pe
        else:
            fn = self.elf.function_at(address)
            ranges, image = ([fn] if fn else [(address, address + 0x400)]), self.elf
        out = []
        for b, e in ranges:
            out.extend(self.md.disasm_lite(image.read(b, e - b) or b'', b))
        return out

    def const_reader(self, side):
        image = self.pe if side == 'w' else self.elf
        rip = re.compile(r'(dword|qword|xmmword) ptr \[rip ([+-]) (0x[0-9a-f]+)\]')

        def read(a, s, op):
            m = rip.search(op)
            if not m: return None
            t = a + s + int(m.group(3), 16) * (1 if m.group(2) == '+' else -1)
            if m.group(1) == 'qword':
                raw = image.read(t, 8)
                return ('d', struct.unpack('<d', raw)[0]) if raw else None
            raw = image.read(t, 4)
            return ('s', struct.unpack('<f', raw)[0]) if raw else None
        return read


def pair_analysis(scan, domains, pairs_by_conf, dis, rows):
    """FP profile tags per pair and reciprocal substitution of constant divisors."""
    w_names = {a: n for a, (n, _) in WINDOWS_CRT_MATH.items()}
    summary = collections.defaultdict(collections.Counter)
    recip = collections.Counter(); recip_pairs = set(); recip_sim = set()
    for conf, pairs in pairs_by_conf.items():
        for w, l in pairs.items():
            fw, fl = scan['w'].get(w), scan['l'].get(l)
            if not fw or not fl: continue
            pw, pl = profile(fw['mn']), profile(fl['mn'])
            cw = [t for _, mn, t in fw['calls'] if t in w_names and w_names[t] not in EXACT]
            cl = [t for _, mn, t in fl['calls'] if t in dis.elf.plt and dis.elf.plt[t] in LINUX_MATH and dis.elf.plt[t] not in EXACT]
            if not any(pw[o] or pl[o] for o in ('add', 'sub', 'mul', 'div', 'sqrt', 'min', 'max', 'rcp', 'rsqrt')) and not cw and not cl:
                continue
            tags = profile_tags(pw, pl, cw, cl)
            g = group(domains['w'].get(w) or domains['l'].get(l))
            if pl['div']:
                ld = divisor_constants(dis.function('l', l), dis.const_reader('l'))
                if ld:
                    wi = dis.function('w', w); wr = dis.const_reader('w')
                    wd = {c for _, c in divisor_constants(wi, wr)}
                    wc = {wr(a, s, op) for a, s, mn, op in wi if 'rip' in op} - {None}
                    for site, kind, c, verdict in reciprocal_matches(ld, wd, wc):
                        recip[verdict] += 1
                        if verdict == 'substituted':
                            recip_pairs.add((w, l)); tags.add('reciprocal_substitution')
                            if g == 'sim': recip_sim.add((w, l))
                            rows.append(('reciprocal', 'pair', f'{w:#x}', f'{l:#x}', 1, domains['w'].get(w, ''),
                                         f'Linux {site:#x} divides by {c!r} ({kind}); Windows multiplies by its rounded reciprocal'))
            for scope in (f'{conf}:all', f'{conf}:{g}'):
                summary[scope]['pairs'] += 1
                for t in tags: summary[scope][t] += 1
                if 'libm_call' in tags or 'reciprocal_substitution' in tags or 'approx_diff' in tags:
                    summary[scope]['proven_different'] += 1
                if tags & {'reciprocal_substitution', 'approx_diff'}:
                    summary[scope]['proven_codegen_different'] += 1
            if g == 'sim' and 'same_profile' not in tags:
                rows.append(('pair_profile', 'pair', f'{w:#x}', f'{l:#x}', conf, domains['w'].get(w, ''), ','.join(sorted(tags))))
    return dict(profiles={k: dict(v) for k, v in sorted(summary.items())},
                reciprocal=dict(sites=dict(recip), functions=len(recip_pairs), sim_functions=len(recip_sim)))


def decimal_parsers(scan, dis):
    out = []
    for (w, l), note in DECIMAL_PARSERS.items():
        cw, cl = callers_of(scan['w'], {w}), callers_of(scan['l'], {l})
        out.append(dict(windows=f'{w:#x}', linux=f'{l:#x}', note=note, windows_call_sites=len(cw[w]),
                        windows_callers=len({c for c, _ in cw[w]}), linux_call_sites=len(cl[l]),
                        linux_callers=len({c for c, _ in cl[l]})))
    holders = {}
    for side in ('w', 'l'):
        image = dis.pe if side == 'w' else dis.elf
        users = set()
        for a, f in scan[side].items():
            for site, mn, t, width in f['refs']:
                if width == 'qword' and mn.endswith(('subsd', 'movsd')):
                    raw = image.read(t, 8)
                    if raw and struct.unpack('<d', raw)[0] == 48.0: users.add(a)
        holders[side] = sorted(f'{a:#x}' for a in users)
    return dict(parsers=out, functions_with_digit_accumulation=holders)


LIBSTDCXX_HELPERS = {
    '_ZSt11_Hash_bytesPKvmm': 'std::_Hash_bytes (std::hash of strings/bytes, Murmur-based)',
    '_ZNKSt8__detail20_Prime_rehash_policy14_M_need_rehashEmmm': 'unordered_* insert (prime bucket policy)',
    '_ZNKSt8__detail20_Prime_rehash_policy11_M_next_bktEm': 'unordered_* rehash/reserve',
    '_ZSt29_Rb_tree_insert_and_rebalancebPSt18_Rb_tree_node_baseS0_RS_': 'std::map/set insert',
    '_ZSt18_Rb_tree_incrementPKSt18_Rb_tree_node_base': 'std::map/set iteration (const)',
    '_ZSt18_Rb_tree_incrementPSt18_Rb_tree_node_base': 'std::map/set iteration',
    '_ZNSt8__detail15_List_node_base7_M_hookEPS0_': 'std::list insert',
}
IMM = re.compile(r'^(\w+), (0x[0-9a-f]+|\d+)$')


def introsort_like(insns, start):
    """libstdc++ __introsort_loop shape: calls itself and, near the top, compares a pointer
    difference against 16 elements (16 * sizeof(T) bytes). Heuristic."""
    if not any(mn == 'call' and op == hex(start) for _, _, mn, op in insns): return False
    head = insns[:40]
    for i, (_, _, mn, op) in enumerate(head):
        m = IMM.match(op) if mn == 'cmp' else None
        if m and int(m.group(2), 0) in {16 * s for s in (1, 2, 4, 8, 12, 16, 24, 32, 40, 48, 64)} and                 any(mn2 == 'sub' and op2.startswith(m.group(1) + ',') for _, _, mn2, op2 in head[:i]):
            return True
    return False


def ordering(scan, elf, domains, dis):
    by_name = {n: a for a, n in elf.dynsym.items() if n in LIBSTDCXX_HELPERS}
    sites = callers_of(scan['l'], set(by_name.values()))
    lib = {}
    for n, a in by_name.items():
        s = sites.get(a, [])
        lib[LIBSTDCXX_HELPERS[n]] = dict(address=f'{a:#x}', sites=len(s), callers=len({c for c, _ in s}),
                                         by_domain=dict(collections.Counter(group(domains['l'].get(c)) for c, _ in s)))
    consts = {}
    for side in ('w', 'l'):
        consts[side] = dict(fnv1a_offset_basis=sum(1 for f in scan[side].values() if 0xcbf29ce484222325 in f['imm']),
                            murmur64_multiplier=sum(1 for f in scan[side].values() if 0xc6a4a7935bd1e995 in f['imm']))
    sorts = []
    for a, f in scan['l'].items():
        if f['gap'] or not any(t == a and mn == 'call' for _, mn, t in f['calls']): continue
        if introsort_like(dis.function('l', a), a): sorts.append(a)
    plt = {n: a for a, n in elf.plt.items()}
    qs = callers_of(scan['l'], {plt.get('qsort'), plt.get('rand')})
    return dict(libstdcxx_helpers=lib, hash_constant_functions=consts,
                linux_introsort_like_functions=dict(count=len(sorts), by_domain=dict(collections.Counter(
                    'thirdparty' if a in elf.dynsym else group(domains['l'].get(a)) for a in sorts))),
                linux_qsort_sites=dict(sites=len(qs.get(plt.get('qsort'), [])), callers=sorted(
                    f'{c:#x}' for c in {c for c, _ in qs.get(plt.get('qsort'), [])})),
                linux_rand_sites=len(qs.get(plt.get('rand'), [])))


def jolt(scan, elf):
    holders = sorted(f'{a:#x}' for a, f in scan['l'].items() if JOLT_VERSION_ID in f['imm'])
    win = sorted(f'{a:#x}' for a, f in scan['w'].items() if any(v & 0xffffff == 0x050500 for v in f['imm']))
    named = [a for a, n in elf.dynsym.items() if n.startswith(('_ZN3JPH', '_ZNK3JPH'))]
    traits = collections.Counter()
    for a in named:
        f = scan['l'].get(a)
        if not f: continue
        for m, c in f['mn'].items():
            k = insn_class(m)
            if k in ('approx', 'sse41', 'fma'): traits[k] += c
        for _, mn, t in f['calls']:
            if t in elf.plt and elf.plt[t] in LINUX_MATH: traits['libm_' + elf.plt[t]] += 1
    return dict(version_id=f'{JOLT_VERSION_ID:#x}', version='5.5.0', features='0x400 (ObjectStream only)',
                linux_functions_with_version_constant=holders, windows_functions_with_version_constant=win,
                linux_named_functions=len(named), linux_named_traits=dict(traits))

# Thread/FP-environment anchors (X4 9.00). Windows from x4-re labels (rng/time reports);
# Linux from this inventory (call into Random::SeedThreadState, inlined thread-start hook).
FPENV_ANCHORS = {
    'windows_main_thread_ftz': dict(function='0x14dd0e0', ldmxcsr='0x14dd105', caller='0x1223ce0 X4::GameClient::Init',
                                    call_site='0x1225ee7', effect='MXCSR |= 0x8000 (FTZ) on the main thread only'),
    'windows_thread_start_hook': dict(function='0x96350 X4::ThreadStartHook', pointer='0x38f9d58 g_ThreadStartHook',
                                      note='calls Random::SeedThreadState 0x14dd110 and srand; no MXCSR write'),
    'linux_main_thread_seed_call': dict(function='0xfcdbd0 (pair of X4::GameClient::Init)', call_site='0xfcf8c8',
                                        callee='0x1a47fe0 Random::SeedThreadState', note='no MXCSR write anywhere in the ELF'),
    'linux_thread_start_hook': dict(function='0x1029f80', note='inlined seeding (time + SDL thread ID -> FS:-0x2720), '
                                    'srand, rpmalloc_thread_initialize; reached through a pointer; no MXCSR write'),
}


def patch_targets(report):
    """Actionable replacement list for the portable math / FP-environment work."""
    math = report['math']
    win = [dict(rva=e['rva'], name=e['name'], kind=e['kind'], call_sites=e['call_sites'],
                action='keep (exact operation)' if e['kind'] == 'exact' else
                'replace whole entry (jmp to portable implementation; MS x64: args xmm0/xmm1, result xmm0)'
                if not e['kind'].startswith('packed') else 'replace whole entry, keep lane ABI: ' + e['kind'])
           for e in math['windows_entries']]
    lin = [dict(symbol=e['name'], plt=e['plt'], got=e['got'], call_sites=e['call_sites'],
                action='keep (exact operation)' if e['kind'] == 'exact' else 'interpose (LD_PRELOAD symbol or GOT slot)')
           for e in math['linux_imports'] if e['kind'] != 'libc']
    return dict(schema='x4-crossplatform-patch-targets-v1', math=dict(windows=win, linux=lin,
                windows_dispatch_flags=math['windows_dispatch']),
                decimal_parsers=report['decimal']['parsers'], fp_environment=FPENV_ANCHORS,
                approx_instructions=report['approx'], qsort=report['ordering']['linux_qsort_sites'],
                windows_qsort_candidate='0x1a111a0 (pair votes; MSVC CRT qsort shape)')


# ------------------------------------------------------------------ main


def coverage_lists(out, scan, domains, pairs_by_conf):
    """The E1 lists: one hexadecimal address per line (Windows RVAs, Linux VAs), and the pairing of each Windows entry."""
    out.mkdir(parents=True, exist_ok=True)
    chosen = {}
    for side in ('w', 'l'):
        chosen[side] = sorted(a for a, f in scan[side].items()
                              if not f['gap'] and group(domains[side].get(a)) == 'sim' and sum(c for m, c in f['mn'].items() if ARITH.match(m)))
        (out / ('windows.txt' if side == 'w' else 'linux.txt')).write_text(''.join(f'{a:x}' + chr(10) for a in chosen[side]), encoding='ascii')
    linux = set(chosen['l'])
    with (out / 'pairs.tsv').open('w', encoding='ascii', newline=chr(10)) as f:
        f.write('windows' + chr(9) + 'linux' + chr(9) + 'confidence' + chr(10))
        for a in chosen['w']:
            twin, conf = pairs_by_conf['high'].get(a), 'high'
            if twin is None: twin, conf = pairs_by_conf['medium'].get(a), 'medium'
            f.write(f'{a:x}' + chr(9) + (f'{twin:x}' if twin is not None else '-') + chr(9) + (conf if twin is not None else '-') + chr(9) +
                    ('listed' if twin in linux else '-') + chr(10))
    print('coverage lists', {s: len(v) for s, v in chosen.items()})


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('--scan', type=Path, required=True)
    p.add_argument('--windows', type=Path, required=True)
    p.add_argument('--linux', type=Path, required=True)
    p.add_argument('--pairs', type=Path, required=True)
    p.add_argument('--x4re', type=Path, help='x4-re root for Windows names and lockstep labels')
    p.add_argument('--json', type=Path); p.add_argument('--tsv', type=Path)
    p.add_argument('--patch-json', type=Path, help='actionable replacement targets')
    p.add_argument('--coverage-lists', type=Path, help='directory: windows.txt and linux.txt, the simulation functions with FP arithmetic (entries, not gap pieces), for native_gate_coverage; pairs.tsv with their twins')
    args = p.parse_args(argv)
    scan, meta = load_scan(args.scan)
    if meta['windows'] != lm.WINDOWS_BUILD or meta['linux'] != lm.LINUX_BUILD: raise SystemExit('scan cache is for another build')
    pe = lm.PeImage(lm.verify_build(args.windows, lm.WINDOWS_BUILD))
    elf = lm.ElfImage(lm.verify_build(args.linux, lm.LINUX_BUILD))
    xfer, known = lm.load_xfer(args.pairs), lm.load_known()
    pairs_by_conf = {'high': {}, 'medium': {}}
    for w, x in xfer.items():
        if x['confidence'] in pairs_by_conf: pairs_by_conf[x['confidence']][w] = x['lin']
    for w, k in known['function'].items():
        pairs_by_conf['medium'].pop(w, None); pairs_by_conf['high'][w] = k['lin']
    for w, bad in known['not_equivalent'].items():
        for conf in pairs_by_conf:
            if any(pairs_by_conf[conf].get(w) == b['lin'] for b in bad): pairs_by_conf[conf].pop(w)
    names, families = load_windows_names(args.x4re)
    all_pairs = {**pairs_by_conf['medium'], **pairs_by_conf['high']}
    wd, ld = classify(scan, names, families, elf, all_pairs)
    domains = {'w': wd, 'l': ld}
    rows = []
    dis = Disassembler(pe, elf)
    report = dict(schema='x4-crossplatform-fp-inventory-v1', build=dict(windows=lm.WINDOWS_BUILD, linux=lm.LINUX_BUILD),
                  functions=meta['functions'], domains={s: dict(collections.Counter(group(v) for v in d.values()))
                                                        for s, d in (('windows', wd), ('linux', ld))},
                  census=census(scan, domains), math=math_tables(scan, domains, elf, rows),
                  mxcsr_writers=fpenv(scan, domains, rows), approx=approx_sites(scan, domains, rows),
                  compares=compare_stats(scan, domains),
                  pairs=pair_analysis(scan, domains, pairs_by_conf, dis, rows),
                  decimal=decimal_parsers(scan, dis), ordering=ordering(scan, elf, domains, dis), jolt=jolt(scan, elf))
    if args.json: args.json.write_text(json.dumps(report, indent=1) + '\n', encoding='utf-8')
    if args.coverage_lists:
        coverage_lists(args.coverage_lists, scan, domains, pairs_by_conf)
    if args.patch_json:
        args.patch_json.write_text(json.dumps(patch_targets(report), indent=1) + '\n', encoding='utf-8')
    if args.tsv:
        with args.tsv.open('w', encoding='utf-8', newline='\n') as out:
            out.write('class\tplatform\taddress\tdetail\tcount\tdomain\tnote\n')
            for r in sorted(rows, key=lambda r: (r[0], r[1], r[2], str(r[3]))):
                out.write('\t'.join(str(x) for x in r) + '\n')
    json.dump({k: report[k] for k in ('functions', 'domains')}, sys.stdout, indent=1); print()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
