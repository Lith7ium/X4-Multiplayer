"""First native Linux lockstep run of the Linux gate: two X4 9.00 instances on one Linux host.

Two steps, one script (copy only the bundle directory to the host):

  1. bundle  (any machine with this repository; Linux/WSL, Python 3.11+, Zig 0.15.2):
       python3 tools/dev/linux_gate_live.py bundle --zig ZIG --snapshot SAVE_009.xml.gz \
         --extension-base LANE_BASE/src/extension/x4_lan_lab --out DIR
         [--clients N] [--portable-math] [--seed-rng] [--path-determinism 3] [--region-rng key] [--ai-wait-rng] [--md-resume-rng] [--boot-hold] [--event-order [--event-rows TICKS]] [--gravidar [--gravidar-deadlines]] [--travel-snapshot] [--runtime-ids] [--world-hash-watch SAVED_ID ...]
     builds x4lan_gate.so (glibc 2.17 target), the overlay (clock mode, world identities,
     world hash WORLD_IMPORTED_2 every --stride ticks), composes the x4_lan_lab extension
     (src/extension/x4_lan_lab + overlay, as the Windows lane tool does), copies the owned
     snapshot, the relay and this script, and writes DIR/manifest.json (SHA-256 of every file).
     --portable-math adds libx4mp_portable_math.so with its decimal and exact_reciprocal patches
     (the Linux side of the Windows FP keys, reports/PORTABLE-MATH-20261029.txt); --seed-rng keys
     the MoveWorker's RNG each frame; --world-hash-watch logs the hashed bytes of up to 64 objects;
     --clients N runs N instances (above 2 on the protocol 3 wire, as the N-client Windows runs).
  2. run     (the Linux host, as root; the game runs as the Steam user, default x4server):
       copy DIR to ~x4server/x4mp-linux-gate-live-<id>/ and touch .x4mp-linux-gate-live in it, then
       python3 ~x4server/x4mp-linux-gate-live-<id>/linux_gate_live.py run --root ~x4server/x4mp-linux-gate-live-<id> --preflight
       python3 ~x4server/x4mp-linux-gate-live-<id>/linux_gate_live.py run --root ~x4server/x4mp-linux-gate-live-<id>
     Exit 0 = both clients stopped with reason 1 (the relay's end), restored=1, relay verdict
     pass and every WORLD_HASH line equal. Any STOP with another reason ends the run within
     two minutes. Results: ROOT/runs/<name>/ (census layout:
     client1/engine-output.log, client1/installed/x4_lan_lab-config.lua, relay.json,
     run-result.json); copy that directory back and run tools/dev/census_world.py on it.

Host requirements (checked before anything starts; the run refuses otherwise):
  - X4 Foundations 9.00 Linux (Steam build 23660954, X4 SHA-256 113f73d7...045f20) at
    ~USER/x4/game (the VPS dedicated-server layout), all seven DLCs as installed.
  - The Steam client of USER already running and logged in (~USER/.steam/steam.pid);
    it is never started, stopped or copied; its identity must not change during the run.
  - No other X4 process on the host (the dedicated x4mp-server must be stopped).
  - Memory: two instances need about 2 x 6 GiB (Windows lanes use ~6 GiB each; Linux with
    software Vulkan is not measured yet): MemAvailable >= --min-memory-gib (default 6.5 per instance).
  - No GPU needed: each instance gets its own Xvfb display (800x600) and Mesa lavapipe
    (/usr/share/vulkan/icd.d/lvp_icd.json). With /opt/x4mp/vklayer present the X4MP
    discard layer is enabled (as for the dedicated server, ~23 fps instead of ~1 fps).
  - Tools: bwrap (bubblewrap), runuser, Xvfb, xauth, python3 >= 3.9.
  - Disk: about 2 GiB under ROOT (two profiles, logs).
Isolation (the pattern of tools/dev/linux_native_admission_run.py, run live on the VPS):
each instance gets a private HOME/XDG tree, profile 45841 with autosave off, the staged
snapshot as save_009, read-only game files, an extension view of the DLCs plus this run's
x4_lan_lab only, a private X4 single-instance lock and a private Xvfb; both share the
running Steam client. The relay listens on 127.0.0.1 only. No service, unit or other
user (tpf2server) is touched; nothing outside ROOT is written, except an empty
/tmp/EGOSOFT_X4.lock when absent (the mount point for the private locks; removed after).
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import signal
import socket
import subprocess
import sys
import time

EXE_SHA = '113f73d74570084f6ce18a659b365009eccf579fb66dbabb5dad2b1718045f20'
MARKER = '.x4mp-linux-gate-live'
SLOT_SAVE = 'save_009.xml.gz'
PORTABLE_MATH = 'libx4mp_portable_math.so'
PORTABLE_MATH_ACTIVE = re.compile(r'X4MP_PORTABLE_MATH ACTIVE version=1 kat=([0-9a-f]{16}) symbols=28 decimal=1 exact_reciprocal=1185/1185 ')
# A client past its STOP line runs free, so nothing waits long after one (seconds).
FAILED_STOP_SETTLE = 5   # after the last STOP, for its trailing lines (WORLD_IDENTITY_STOP, ...)
FAILED_STOP_PEER = 120   # for the peer's own STOP after a failed one
PEER_STOP = 300          # for the peer's STOP after a clean one
RELAY_VERDICT = 120      # for the relay's verdict after both stopped


def sha(path):
    with open(path, 'rb') as stream:
        h = hashlib.sha256()
        for block in iter(lambda: stream.read(1 << 20), b''):
            h.update(block)
        return h.hexdigest()


# ------------------------------------------------------------------ bundle


def insert_ui_files(text, wanted):
    """Add <file name=.../> entries right after ui/config.lua in ui.xml (lane tool rule)."""
    match = re.search(r'<file\s+name="ui/config\.lua"\s*/>', text)
    if not match:
        raise SystemExit('ui.xml has no ui/config.lua entry')
    line_start = text.rfind('\n', 0, match.start()) + 1
    indent = text[line_start:match.start()]
    indent = indent if not indent.strip() else '    '
    return text[:match.end()] + ''.join(f'\n{indent}<file name="{name}" />' for name in wanted) + text[match.end():]


def apply_overlay(target, overlay):
    """Plain files replace, <path>.append appends, ui.xml.files lists UI files (lane tool rules)."""
    ui_files = []
    for item in sorted(Path(overlay).rglob('*')):
        if not item.is_file():
            continue
        rel = item.relative_to(overlay).as_posix()
        if '/' not in rel and rel.startswith('x4lane-'):
            continue  # overlay metadata (x4lane-manifest.json), not extension content
        if rel == 'ui.xml.files':
            ui_files += [l.strip() for l in item.read_text().splitlines() if l.strip() and not l.startswith('#')]
        elif rel.endswith('.append'):
            dest = target / rel[:-len('.append')]
            old = dest.read_bytes()
            add = item.read_bytes()
            dest.write_bytes(old + (b'' if not old or old.endswith(b'\n') else b'\n') + add + (b'' if add.endswith(b'\n') else b'\n'))
        else:
            dest = target / rel
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(item, dest)
    if ui_files:
        ui = target / 'ui.xml'
        ui.write_text(insert_ui_files(ui.read_text(), ui_files))


def bundle(args):
    repo = Path(__file__).resolve().parents[2]
    out = args.out
    if out.exists():
        raise SystemExit(f'{out} exists; use a new directory')
    snapshot_sha = sha(args.snapshot)
    out.mkdir(parents=True)
    work = out / '.build'
    work.mkdir()
    subprocess.run([sys.executable, str(repo / 'tools/dev/build_native_gate_linux.py'), '--zig', str(args.zig), '--out', str(work)], check=True)
    overlay = work / 'overlay'
    command = [sys.executable, str(repo / 'tools/dev/build_native_gate_overlay.py'), '--gate-so', str(work / 'x4lan_gate.so'),
               '--snapshot', str(args.snapshot), '--mode', 'clock', '--world-identities',
               '--world-hash-stride', str(args.stride), '--world-hash-schema', 'WORLD_IMPORTED_2', '--out', str(overlay)]
    if args.seed_rng:
        command.append('--seed-rng')
    if args.clients > 2:
        command.append('--session-clients')
    if args.path_determinism:
        command += ['--path-determinism', str(args.path_determinism)]
    if args.region_rng:
        command += ['--region-rng', args.region_rng]
    if args.ai_wait_rng:
        command.append('--ai-wait-rng')
    if args.md_resume_rng:
        command.append('--md-resume-rng')
    if args.boot_hold:
        command.append('--boot-hold')
    if args.event_order:
        command.append('--event-order')
    if args.event_rows is not None:
        command += ['--event-rows', str(args.event_rows)]
    if args.gravidar:
        command.append('--gravidar')
    if args.gravidar_deadlines:
        command.append('--gravidar-deadlines')
    if args.travel_snapshot:
        command.append('--travel-snapshot')
    if args.runtime_ids:
        command.append('--runtime-ids')
    if args.runtime_id_callers:
        command.append('--runtime-id-callers')
    if args.script_rng:
        command += ['--script-rng', args.script_rng]
    if args.script_rng_mask is not None:
        command += ['--script-rng-mask', str(args.script_rng_mask)]
    if args.script_rng_rows:
        command += ['--script-rng-rows', str(args.script_rng_rows)]
    if args.script_rng_natural:
        command.append('--script-rng-natural')
    if args.script_rng_unmapped:
        command.append('--script-rng-unmapped')
    if args.md_timed_rng:
        command.append('--md-timed-rng')
    if args.find_order:
        command += ['--find-order', args.find_order]
    if args.loadout_order:
        command += ['--loadout-order', args.loadout_order]
    if args.attention_all:
        command.append('--attention-all')
    if args.step_trace:
        command += ['--step-trace', *args.step_trace, '--step-trace-ticks', str(args.step_trace_ticks)]
    if args.fp_redirect:
        command += ['--fp-redirect', args.fp_redirect, '--fp-redirect-ticks', str(args.fp_redirect_ticks)]
        if args.fp_redirect_watch:
            command += ['--fp-redirect-watch', *args.fp_redirect_watch]
    if args.coverage:
        command += ['--coverage', str(args.coverage), '--coverage-list', str(args.coverage_list)]
    if args.loadout_order_rows:
        command += ['--loadout-order-rows', *(str(t) for t in args.loadout_order_rows)]
    if args.gravidar_wide:
        command += ['--gravidar-wide', args.gravidar_wide]
    if args.nav_cadence:
        command += ['--nav-cadence', args.nav_cadence]
    if args.nav_params_init:
        command.append('--nav-params-init')
    if args.nav_key:
        command.append('--nav-key')
    if args.nav_probe_init:
        command.append('--nav-probe-init')
    if args.created_identities:
        command.append('--created-identities')
    if args.created_load:
        command.append('--created-load')
    if args.portable_created:
        command.append('--portable-created')
    if args.spawn_rng:
        command += ['--spawn-rng', args.spawn_rng]
    if args.serial_pathing or (args.path_determinism or 0) & 1:
        command.append('--serial-pathing')
    for saved in args.world_hash_watch or []:
        command += ['--world-hash-watch', saved]
    subprocess.run(command, check=True)
    extension = out / 'extension/x4_lan_lab'
    shutil.copytree(args.extension_base or repo / 'src/extension/x4_lan_lab', extension)
    apply_overlay(extension, overlay)
    if args.portable_math:
        subprocess.run([sys.executable, str(repo / 'tools/dev/build_portable_math_preload.py'), '--zig', str(args.zig)], check=True)
        (out / 'portable_math').mkdir()
        shutil.copy2(repo / '.local/portable-math/preload' / PORTABLE_MATH, out / 'portable_math' / PORTABLE_MATH)
    missing = [name for name in ('ui/lockstep.lua', 'ui/sha256.lua') if not (extension / name).is_file()]
    if missing:
        raise SystemExit(f'the extension lacks {missing}: pass --extension-base with the lane base, the same Lua as Windows')
    config = (extension / 'ui/config.lua').read_text()
    for token in ('@X4LANE_PORT@', '@X4LANE_TOKEN@', '@X4LANE_SLOT@'):
        if token not in config:
            raise SystemExit(f'composed config lacks {token}')
    if snapshot_sha not in config:
        raise SystemExit('composed config does not pin the snapshot')
    (out / 'snapshot').mkdir()
    shutil.copy2(args.snapshot, out / 'snapshot' / SLOT_SAVE)
    shutil.copy2(repo / 'tools/dev/native_gate_relay.py', out / 'native_gate_relay.py')
    shutil.copy2(Path(__file__), out / 'linux_gate_live.py')
    shutil.rmtree(work)
    commit = subprocess.run(['git', '-C', str(repo), 'rev-parse', 'HEAD'], capture_output=True, text=True).stdout.strip()
    files = {p.relative_to(out).as_posix(): sha(p) for p in sorted(out.rglob('*')) if p.is_file()}
    manifest = {'commit': commit, 'ticks': args.ticks, 'stride': args.stride, 'snapshot_sha256': snapshot_sha,
                'gate_so_sha256': files['extension/x4_lan_lab/native/x4lan_gate.so'], 'files': files,
                'portable_math_sha256': files.get(f'portable_math/{PORTABLE_MATH}'), 'seed_rng': bool(args.seed_rng),
                'path_determinism': args.path_determinism, 'region_rng': args.region_rng, 'ai_wait_rng': args.ai_wait_rng, 'md_resume_rng': args.md_resume_rng, 'boot_hold': args.boot_hold, 'event_order': args.event_order, 'event_rows': args.event_rows, 'gravidar': args.gravidar, 'gravidar_deadlines': args.gravidar_deadlines, 'travel_snapshot': args.travel_snapshot, 'runtime_ids': args.runtime_ids, 'runtime_id_callers': args.runtime_id_callers, 'script_rng': args.script_rng, 'script_rng_mask': args.script_rng_mask, 'script_rng_rows': args.script_rng_rows, 'md_timed_rng': args.md_timed_rng, 'find_order': args.find_order, 'loadout_order': args.loadout_order, 'loadout_order_rows': args.loadout_order_rows, 'attention_all': args.attention_all, 'nav_key': args.nav_key, 'step_trace': args.step_trace, 'step_trace_ticks': args.step_trace_ticks, 'fp_redirect': args.fp_redirect, 'fp_redirect_watch': args.fp_redirect_watch, 'fp_redirect_ticks': args.fp_redirect_ticks, 'coverage': args.coverage, 'gravidar_wide': args.gravidar_wide, 'spawn_rng': args.spawn_rng, 'created_identities': args.created_identities, 'created_load': args.created_load, 'portable_created': args.portable_created, 'nav_cadence': args.nav_cadence, 'nav_params_init': args.nav_params_init, 'nav_probe_init': args.nav_probe_init, 'clients': args.clients,
                'world_hash_watch': args.world_hash_watch or []}
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'bundle {out}: {len(files)} files, gate {manifest["gate_so_sha256"][:16]}, snapshot {snapshot_sha[:16]}, commit {commit[:8]}')


# ------------------------------------------------------------------ run


def processes():
    result = {}
    for path in Path('/proc').glob('[0-9]*'):
        try:
            stat = (path / 'stat').read_text().rsplit(')', 1)[1].split()
            result[int(path.name)] = {'parent': int(stat[1]), 'start': int(stat[19]), 'uid': path.stat().st_uid,
                                      'name': (path / 'comm').read_text().strip()}
        except (OSError, ValueError):
            pass
    return result


def x4_executables():
    """(pid, executable) of every running X4. X4 renames its main thread (comm is 'Main()'),
    so only /proc/<pid>/exe identifies it."""
    found = []
    for path in Path('/proc').glob('[0-9]*'):
        try:
            link = os.readlink(path / 'exe')
        except OSError:
            continue
        link = link[:-len(' (deleted)')] if link.endswith(' (deleted)') else link
        if Path(link).name == 'X4':
            found.append((int(path.name), link))
    return sorted(found)


def stop_error(reasons, stopped_at, now, clients=2):
    """The error that ends the run now, or None to keep waiting. reasons and stopped_at map
    the slots seen stopping to their reason (the text of a gate error) and to when they were seen."""
    failed = {slot: reason for slot, reason in reasons.items() if reason != 1}
    if failed:
        settled = len(reasons) == clients and now - max(stopped_at.values()) >= FAILED_STOP_SETTLE
        if settled or now - min(stopped_at[slot] for slot in failed) >= FAILED_STOP_PEER:
            return '; '.join(f'client {slot}: ' + (reason if isinstance(reason, str) else f'NATIVE_GATE STOP reason={reason}')
                             for slot, reason in sorted(failed.items()))
        return None
    if reasons and len(reasons) < clients and now - min(stopped_at.values()) >= PEER_STOP:
        return f'only client(s) {sorted(reasons)} stopped; no peer STOP within {PEER_STOP} s'
    return None


def start_ticks(pid):
    return int(Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()[19])


def memory_available_gib():
    for line in Path('/proc/meminfo').read_text().splitlines():
        if line.startswith('MemAvailable:'):
            return int(line.split()[1]) / (1024 * 1024)
    return 0.0


def steam_identity(userhome, uid):
    pid = int((userhome / '.steam/steam.pid').read_text().strip())
    row = processes().get(pid)
    if not row or row['uid'] != uid or row['name'] != 'steam':
        raise RuntimeError('the running Steam client of the game user is required')
    return {'pid': pid, 'start': row['start'], 'uid': uid}


def bounded_tail(path, limit=1 << 20):
    try:
        with open(path, 'rb') as stream:
            stream.seek(0, os.SEEK_END)
            size = stream.tell()
            stream.seek(max(0, size - limit))
            return stream.read().decode(errors='replace')
    except FileNotFoundError:
        return ''


class LogWatch:
    """Reads a growing X4 log incrementally (it can grow by megabytes per second with
    -debug scripts) and remembers the gate's milestone lines once seen."""

    def __init__(self, path):
        self.path, self.offset, self.carry = path, 0, b''
        self.armed = self.stopped = False
        self.stop_reason = self.failure = None

    def poll(self):
        try:
            with open(self.path, 'rb') as stream:
                stream.seek(self.offset)
                data = stream.read(64 << 20)
        except FileNotFoundError:
            return
        self.offset += len(data)
        lines = (self.carry + data).split(b'\n')
        self.carry = lines.pop()
        for line in lines:
            if b'NATIVE_GATE' not in line:
                continue
            text = line.decode(errors='replace')
            if 'NATIVE_GATE ARMED' in text:
                self.armed = True
            if 'NATIVE_GATE STOP ' in text:
                self.stopped = True
                reason = re.search(r'NATIVE_GATE STOP .*?reason=(\d+)', text)
                self.stop_reason = int(reason.group(1)) if reason else -1
            if self.failure is None and re.search(r'NATIVE_GATE (?:REFUSED|ERROR)', text):
                self.failure = text[text.index('NATIVE_GATE'):][:300]


def gate_lines(path):
    """The [X4LAN] NATIVE_GATE lines of a finished log, streamed."""
    with open(path, 'rb') as stream:
        for raw in stream:
            if b'NATIVE_GATE' in raw:
                yield raw.decode(errors='replace').rstrip('\r\n')


def namespace_command(case, game, dlc_binds, lock, env, program, user):
    """bwrap view as tools/dev/linux_native_admission_run.py: read-only host, private case,
    game read-only at case/game, extension directory replaced by this case's allowlist."""
    game = game.resolve(strict=True)
    command = ['runuser', '-u', user, '--', 'bwrap', '--ro-bind', '/', '/', '--proc', '/proc', '--dev-bind', '/dev', '/dev',
               '--bind', str(case), str(case), '--ro-bind', str(game), str(case / 'game')]
    for source, view in dlc_binds:
        command += ['--ro-bind', str(source), str(view)]
    command += ['--ro-bind', str(case / 'extensions'), str(case / 'game/extensions'),
                '--ro-bind', str(case / 'extensions'), str(game / 'extensions'), '--bind', str(lock), '/tmp/EGOSOFT_X4.lock',
                '--die-with-parent', '--chdir', str(case / 'game'), '--clearenv']
    for key, value in env.items():
        command += ['--setenv', key, value]
    return command + ['--', *program]


def portable_math_env(case):
    """The preload of one instance: the Linux side of the Windows FP keys. It stops X4 (exit 97) on
    any mismatch; its log line says what is active."""
    return {'LD_PRELOAD': str(case / PORTABLE_MATH), 'X4MP_PORTABLE_DECIMAL': '1', 'X4MP_EXACT_RECIPROCAL': '1',
            'X4MP_PORTABLE_MATH_LOG': str(case / 'portable-math.log')}


def portable_math_state(case):
    """(active, log lines) of one instance's preload. Every process X4 starts inherits LD_PRELOAD
    and appends its own line (dbus-launch is refused with exit 97, not being X4), so only the
    lines of this instance's X4 say whether the patches are active."""
    try:
        lines = (case / 'portable-math.log').read_text(errors='replace').splitlines()
    except OSError:
        return False, []
    own = [line for line in lines if line.endswith(f' exe={case}/game/X4')]
    active = any(PORTABLE_MATH_ACTIVE.search(line) for line in own) and not any('REFUSED' in line for line in own)
    return active, [line[:300] for line in lines[:16]]


def stage_case(root, run, slot, game, userhome, port, token, clients=2):
    """One private instance: HOME, profile 45841 (autosave off), snapshot, extensions."""
    case = run / f'case{slot}'
    profile = case / 'home/.config/EgoSoft/X4/45841'
    for path in (profile / 'save', case / 'tmp', case / 'cache', case / 'data', case / 'runtime', case / 'game',
                 case / 'extensions', case / 'home/.steam'):
        path.mkdir(parents=True, exist_ok=True)
    (case / 'runtime').chmod(0o700)
    (profile / 'config.xml').write_text('<?xml version="1.0"?><root><autosave>false</autosave><uisafemode>false</uisafemode>'
                                        '<fullscreen>false</fullscreen><borderless>false</borderless><res_width>800</res_width>'
                                        '<res_height>600</res_height><antialiasing>0</antialiasing><vsync>false</vsync></root>\n')
    shutil.copyfile(root / 'snapshot' / SLOT_SAVE, profile / 'save' / SLOT_SAVE)
    for name in ('root', 'steam.pid', 'sdk32', 'sdk64'):
        source = userhome / '.steam' / name
        if source.exists():
            (case / 'home/.steam' / name).symlink_to(source)
    (case / 'home/.steam/steam').symlink_to('root')
    dlc_binds = []
    for extension in sorted((game / 'extensions').glob('ego_dlc_*')):
        if extension.is_dir():
            view = case / 'dlc-source' / extension.name
            view.mkdir(parents=True)
            dlc_binds.append((extension.resolve(strict=True), view))
            (case / 'extensions' / extension.name).symlink_to(view)
    lab = case / 'extensions/x4_lan_lab'
    shutil.copytree(root / 'extension/x4_lan_lab', lab)
    config = lab / 'ui/config.lua'
    text = (config.read_text().replace('@X4LANE_PORT@', str(port)).replace('@X4LANE_TOKEN@', token)
            .replace('@X4LANE_SLOT@', f'p{slot}').replace('@X4LANE_CLIENTS@', str(clients)))
    config.write_text(text)
    ids = sorted(p.name for p in (case / 'extensions').iterdir())
    if any(not (n.startswith('ego_dlc_') or n == 'x4_lan_lab') for n in ids):
        raise RuntimeError('unexpected extension in the private view')
    (profile / 'content.xml').write_text('<?xml version="1.0"?><content>' + ''.join(
        f'<extension id="{n}" enabled="true" />' for n in ids) + '</content>\n')
    if (root / 'portable_math' / PORTABLE_MATH).is_file():
        shutil.copyfile(root / 'portable_math' / PORTABLE_MATH, case / PORTABLE_MATH)
    lock = case / 'x4.lock'
    lock.touch()
    return case, profile, dlc_binds, lock, ids


def launch_order(text, clients):
    """The client numbers in the order they start, from "2,1"; by default 1..clients."""
    if not text:
        return list(range(1, clients + 1))
    order = [int(part) for part in text.split(',')]
    if sorted(order) != list(range(1, clients + 1)):
        raise SystemExit(f'--launch-order must name each of the clients 1..{clients} once: {text}')
    return order


def run(args):
    import pwd
    if os.geteuid() != 0:
        raise SystemExit('run as root (process identity checks and owned cleanup); the game runs as --user')
    try:
        user = pwd.getpwnam(args.user)
    except KeyError:
        raise SystemExit(f'REFUSED: no user {args.user} on this host (--user names the Steam user)') from None
    userhome = Path(user.pw_dir)
    root = args.root.resolve(strict=True)
    if not (root / MARKER).is_file() or root.parent != userhome:
        raise SystemExit(f'{root} is not a staged live-test root under {userhome} with {MARKER}')
    manifest = json.loads((root / 'manifest.json').read_text())
    for rel, digest in manifest['files'].items():
        if sha(root / rel) != digest:
            raise SystemExit(f'bundle file changed: {rel}')
    try:
        game = (userhome / 'x4/game').resolve(strict=True)
    except OSError:
        raise SystemExit(f'REFUSED: no X4 installation at {userhome}/x4/game') from None
    problems = []
    if sha(game / 'X4') != EXE_SHA:
        problems.append('X4 is not the pinned Linux 9.00 executable')
    if x4_executables():
        problems.append('another X4 is running (stop the dedicated server first)')
    for tool in ('bwrap', 'runuser', 'Xvfb', 'xauth'):
        if not shutil.which(tool):
            problems.append(f'{tool} missing')
    if not Path('/usr/share/vulkan/icd.d/lvp_icd.json').is_file():
        problems.append('Mesa lavapipe ICD missing (/usr/share/vulkan/icd.d/lvp_icd.json)')
    memory = memory_available_gib()
    clients = manifest.get('clients', 2)
    needed = args.min_memory_gib if args.min_memory_gib is not None else 6.5 * clients
    if memory < needed:
        problems.append(f'MemAvailable {memory:.1f} GiB < {needed} GiB for {clients} instances')
    try:
        steam = steam_identity(userhome, user.pw_uid)
    except (OSError, ValueError, RuntimeError) as error:
        problems.append(str(error))
        steam = None
    if problems:
        raise SystemExit('REFUSED: ' + '; '.join(problems))
    discard = Path('/opt/x4mp/vklayer').is_dir() and not args.no_discard
    if args.preflight:
        print('PREFLIGHT OK ' + json.dumps({'game': str(game), 'memory_available_gib': round(memory, 1), 'steam': steam,
                                            'discard_layer': discard, 'bundle_commit': manifest['commit'],
                                            'portable_math': bool(manifest.get('portable_math_sha256')), 'seed_rng': manifest.get('seed_rng'),
                                            'gate_so_sha256': manifest['gate_so_sha256']}))
        return
    ticks = args.ticks or manifest['ticks']
    name = args.name or time.strftime('live-%Y%m%d-%H%M%S')
    run_dir = root / 'runs' / name
    run_dir.mkdir(parents=True, exist_ok=False)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reserve:
        reserve.bind(('127.0.0.1', 0))
        port = reserve.getsockname()[1]
    token = secrets.token_hex(16)
    order = launch_order(args.launch_order, clients)
    cases = [stage_case(root, run_dir, slot, game, userhome, port, token, clients) for slot in range(1, clients + 1)]
    for path in [run_dir, *run_dir.rglob('*')]:
        if not path.is_symlink():
            os.chown(path, user.pw_uid, user.pw_gid)
    result = {'name': name, 'ticks': ticks, 'port': port, 'memory_available_gib': round(memory, 1), 'discard_layer': discard,
              'manifest_commit': manifest['commit'], 'gate_so_sha256': manifest['gate_so_sha256'], 'steam_before': steam,
              'portable_math_sha256': manifest.get('portable_math_sha256'), 'seed_rng': manifest.get('seed_rng'), 'launch_order': order}
    procs, starts, logs = [], {}, []
    cancelled = []
    # bwrap binds each private lock over this path, and cannot create it in the read-only view.
    lock_target = Path('/tmp/EGOSOFT_X4.lock')
    lock_created = not lock_target.exists()
    if lock_created:
        lock_target.touch()
        os.chown(lock_target, user.pw_uid, user.pw_gid)
    for number in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
        signal.signal(number, lambda *_: cancelled.append(1))

    def launch(label, command, env=None, as_user=False):
        log = (run_dir / f'{label}.log').open('wb')
        logs.append(log)
        extra = {'user': user.pw_uid, 'group': user.pw_gid, 'extra_groups': []} if as_user else {}
        parent = os.getpid()

        def parent_death():
            if ctypes.CDLL(None).prctl(1, signal.SIGKILL) != 0 or os.getppid() != parent:
                os._exit(125)
        proc = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT, start_new_session=True,
                                env=env, preexec_fn=parent_death, **extra)
        starts[proc.pid] = start_ticks(proc.pid)
        procs.append(proc)
        return proc
    try:
        relay = launch('relay', [sys.executable, str(root / 'native_gate_relay.py'), '--port', str(port), '--token', token,
                                 '--ticks', str(ticks), '--report', str(run_dir / 'relay.json'), '--relative-real', '--observe', '--clients', str(clients)],
                       as_user=True)
        deadline = time.monotonic() + 10
        while 'NATIVE_GATE RELAY READY' not in bounded_tail(run_dir / 'relay.log'):
            if relay.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError('relay did not start')
            time.sleep(0.1)
        games = [None] * clients
        for slot in order:
            case, profile, dlc_binds, lock, ids = cases[slot - 1]
            display = next(n for n in range(101, 140) if not Path(f'/tmp/.X{n}-lock').exists() and not Path(f'/tmp/.X11-unix/X{n}').exists())
            auth = case / 'xauthority'
            auth.touch(mode=0o600)
            os.chown(auth, user.pw_uid, user.pw_gid)
            subprocess.run(['runuser', '-u', args.user, '--', 'xauth', '-f', str(auth), 'add', f':{display}', '.', secrets.token_hex(16)],
                           check=True, stdout=subprocess.DEVNULL)
            xvfb = launch(f'xvfb{slot}', ['Xvfb', f':{display}', '-screen', '0', '800x600x24', '-nolisten', 'tcp', '-noreset', '-auth', str(auth)],
                          as_user=True)
            deadline = time.monotonic() + 10
            while not Path(f'/tmp/.X11-unix/X{display}').exists():
                if xvfb.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError('private Xvfb failed')
                time.sleep(0.1)
            env = {'PATH': '/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin', 'HOME': str(case / 'home'),
                   'USER': args.user, 'LOGNAME': args.user, 'DISPLAY': f':{display}', 'XAUTHORITY': str(auth),
                   'XDG_CONFIG_HOME': str(case / 'home/.config'), 'XDG_CACHE_HOME': str(case / 'cache'),
                   'XDG_DATA_HOME': str(case / 'data'), 'XDG_RUNTIME_DIR': str(case / 'runtime'), 'TMPDIR': str(case / 'tmp'),
                   'SteamAppId': '392160', 'SteamGameId': '392160', 'LD_LIBRARY_PATH': str(case / 'game/lib'),
                   'VK_ICD_FILENAMES': '/usr/share/vulkan/icd.d/lvp_icd.json', 'VK_DRIVER_FILES': '/usr/share/vulkan/icd.d/lvp_icd.json',
                   'SDL_AUDIODRIVER': 'dummy', 'ALSOFT_DRIVERS': 'null'}
            if discard:
                env.update(VK_LAYER_PATH='/opt/x4mp/vklayer', VK_INSTANCE_LAYERS='VK_LAYER_X4MP_discard', X4MP_VK_DISCARD='1')
            if manifest.get('portable_math_sha256'):
                env.update(portable_math_env(case))
            program = [str(case / 'game/X4'), '-personalfolderid', '45841', '-skipintro', '-debug', 'scripts',
                       '-logfile', 'engine-output.log', '-nocputhrottle', '-noworkshopsync', '-seed:276944566']
            games[slot - 1] = (launch(f'game{slot}', namespace_command(case, game, dlc_binds, lock, env, program, args.user)), profile)
        result['started'] = time.time()
        deadline = time.monotonic() + args.timeout
        watches = [LogWatch(profile / 'engine-output.log') for _, profile in games]
        stopped_at = {}
        while True:
            if cancelled:
                raise RuntimeError('cancelled')
            if time.monotonic() > deadline:
                raise RuntimeError('timeout (loading or relay); see the client logs')
            if steam_identity(userhome, user.pw_uid) != steam:
                raise RuntimeError('Steam client identity changed')
            for slot, (watch, (proc, _)) in enumerate(zip(watches, games), 1):
                watch.poll()
                if proc.poll() is not None and not watch.stopped and not watch.failure:
                    raise RuntimeError(f'client {slot}: X4 exited before its STOP line')
                if watch.stopped or watch.failure:
                    stopped_at.setdefault(slot, time.monotonic())
            error = stop_error({slot: watches[slot - 1].failure or watches[slot - 1].stop_reason for slot in stopped_at},
                               stopped_at, time.monotonic(), clients)
            if error:
                raise RuntimeError(error)
            if all(w.stopped for w in watches):
                try:
                    verdict = json.loads((run_dir / 'relay.json').read_text()).get('verdict')
                except (OSError, ValueError):
                    verdict = None
                if relay.poll() is not None or (verdict and verdict != 'incomplete'):
                    break
                if time.monotonic() - max(stopped_at.values()) >= RELAY_VERDICT:
                    raise RuntimeError(f'both clients stopped, no relay verdict within {RELAY_VERDICT} s')
            time.sleep(1)
        result['armed'] = [w.armed for w in watches]
    except Exception as error:
        result['error'] = str(error)
    finally:
        for proc in reversed(procs):
            if proc.poll() is None:
                try:
                    if start_ticks(proc.pid) == starts[proc.pid]:
                        os.killpg(proc.pid, signal.SIGTERM)
                    proc.wait(timeout=15)
                except (OSError, subprocess.TimeoutExpired):
                    try:
                        os.killpg(proc.pid, signal.SIGKILL)
                        proc.wait(timeout=10)
                    except (OSError, subprocess.TimeoutExpired) as error:
                        result.setdefault('cleanup_errors', []).append(str(error))
        for log in logs:
            log.close()
        if lock_created:
            lock_target.unlink(missing_ok=True)
        result['steam_after'] = None
        try:
            result['steam_after'] = steam_identity(userhome, user.pw_uid)
        except (OSError, ValueError, RuntimeError) as error:
            result['steam_after'] = {'error': str(error)}
        result['steam_unchanged'] = result['steam_after'] == steam
        # Our instances run from this run's directory; the process groups above normally ended
        # them. Any other X4 is reported, never touched.
        deadline = time.monotonic() + 30
        while any(link.startswith(f'{run_dir}/') for _, link in x4_executables()) and time.monotonic() < deadline:
            time.sleep(0.5)
        for pid, link in x4_executables():
            if link.startswith(f'{run_dir}/'):
                try:
                    os.kill(pid, signal.SIGKILL)
                except OSError as error:
                    result.setdefault('cleanup_errors', []).append(str(error))
        deadline = time.monotonic() + 10
        while any(link.startswith(f'{run_dir}/') for _, link in x4_executables()) and time.monotonic() < deadline:
            time.sleep(0.5)
        result['x4_left'] = [pid for pid, _ in x4_executables()]
    # Census layout and the quick verdict.
    stops, hashes, identities, problems = [], [], [], []
    for slot, (case, profile, *_rest) in enumerate(cases, 1):
        client = run_dir / f'client{slot}'
        (client / 'installed').mkdir(parents=True, exist_ok=True)
        log = profile / 'engine-output.log'
        if log.exists():
            shutil.copyfile(log, client / 'engine-output.log')
        shutil.copyfile(case / 'extensions/x4_lan_lab/ui/config.lua', client / 'installed/x4_lan_lab-config.lua')
        if (case / 'portable-math.log').is_file():
            shutil.copyfile(case / 'portable-math.log', client / 'portable-math.log')
        stop, world, identity, rows = None, {}, None, []
        if (client / 'engine-output.log').exists():
            for line in gate_lines(client / 'engine-output.log'):
                m = re.search(r'NATIVE_GATE STOP slot=p\d completed=(\d+) reason=(\d+) restored=(\d+)', line)
                if m:
                    stop = {'slot': slot, 'completed': int(m.group(1)), 'reason': int(m.group(2)), 'restored': int(m.group(3))}
                m = re.search(r'NATIVE_GATE WORLD_CAPTURE_PROBLEM (.*)', line)
                if m:
                    rows.append(m.group(1))
                m = re.search(r'NATIVE_GATE WORLD_IDENTITY_STOP calls=(\d+) count=(\d+) late=(\d+) restored=(\d+) failures=(\d+)', line)
                if m:
                    identity = dict(zip(('calls', 'count', 'late', 'restored', 'failures'), map(int, m.groups())))
                m = re.search(r'NATIVE_GATE WORLD_HASH tick=(\d+) .*? state=(WORLD_IMPORTED_\d:\S+)', line)
                if m:
                    world[m.group(1)] = m.group(2)
        stops.append(stop)
        identities.append(identity)
        problems.append(rows)
        hashes.append(world)
    try:
        result['relay'] = json.loads((run_dir / 'relay.json').read_text())
    except (OSError, ValueError):
        result['relay'] = None
    if manifest.get('portable_math_sha256'):
        states = [portable_math_state(case) for case, *_rest in cases]
        result['portable_math'] = [lines for _, lines in states]
        result['portable_math_active'] = [active for active, _ in states]
    common = sorted(set.intersection(*map(set, hashes)), key=int)
    result['stops'] = stops
    result['world_identity'] = identities
    result['world_capture_problems'] = problems
    result['world_hash_ticks'] = len(common)
    result['world_hash_first_difference'] = next((int(t) for t in common if any(other[t] != hashes[0][t] for other in hashes[1:])), None)
    ok = (not result.get('error') and not result.get('cleanup_errors') and result['steam_unchanged'] and not result['x4_left'] and
          all(s and s['reason'] == 1 and s['restored'] == 1 for s in stops) and result['relay'] and result['relay'].get('verdict') == 'pass'
          and result['world_hash_ticks'] > 0 and result['world_hash_first_difference'] is None
          and all(result.get('portable_math_active', [True])))
    result['pass'] = ok
    (run_dir / 'run-result.json').write_text(json.dumps(result, indent=2, default=str) + '\n')
    print('LINUX_GATE_LIVE ' + json.dumps({k: result.get(k) for k in ('name', 'pass', 'error', 'stops', 'world_identity', 'world_capture_problems', 'portable_math_active', 'world_hash_ticks',
                                                                    'world_hash_first_difference', 'steam_unchanged', 'x4_left')}))
    print(f'results: {run_dir}')
    raise SystemExit(0 if ok else 1)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='command', required=True)
    b = sub.add_parser('bundle', help='build the gate, overlay and extension; collect the owned snapshot')
    b.add_argument('--zig', type=Path, required=True)
    b.add_argument('--snapshot', type=Path, required=True, help='owned native snapshot save_009.xml.gz (the census snapshot)')
    b.add_argument('--out', type=Path, required=True)
    b.add_argument('--extension-base', type=Path, help='extension to compose on (default src/extension/x4_lan_lab); use the lane base for the same Lua as Windows')
    b.add_argument('--portable-math', action='store_true', help='preload libx4mp_portable_math.so with its decimal and exact_reciprocal patches')
    b.add_argument('--seed-rng', action='store_true', help='key the MoveWorker RNG word each frame (boot option 2)')
    b.add_argument('--serial-pathing', action='store_true', help='join the pathing pass before the frame goes on (boot option 1)')
    b.add_argument('--md-resume-rng', action='store_true', help='load-time MD cue delays on per-cue streams')
    b.add_argument('--runtime-ids', action='store_true', help='the same runtime IDs on every client (set mode)')
    b.add_argument('--runtime-id-callers', action='store_true', help='the table of who allocates runtime IDs (needs --runtime-ids)')
    b.add_argument('--script-rng', choices=('observe', 'key'), help='AI scripts and MD cues on per-script streams')
    b.add_argument('--script-rng-mask', type=int, choices=range(1, 8), metavar='1..7', help='1 AI scripts, 2 MD cues, 4 also boot phase 2 (default 3)')
    b.add_argument('--script-rng-rows', type=int, metavar='TICKS', help='SCRIPT_RNG_ROW lines for the first TICKS ticks')
    b.add_argument('--script-rng-natural', action='store_true', help='rows of outermost unkeyed scopes too')
    b.add_argument('--script-rng-unmapped', action='store_true', help='AI owners without a portable ID are keyed by their rank')
    b.add_argument('--md-timed-rng', action='store_true', help='direct MD cue checks on the per-cue streams')
    b.add_argument('--find-order', choices=('observe', 'sort'), help='find_* candidate lists on saved IDs')
    b.add_argument('--loadout-order', choices=('observe', 'canonical'), help="what a station wants to restock does not depend on the macros' heap addresses")
    b.add_argument('--step-trace', nargs='+', metavar='SAVED_ID', help="the Linear flight model's step, entry and exit, for up to four saved IDs (diagnostic)")
    b.add_argument('--step-trace-ticks', type=int, default=3, help='ticks 1..N of the step trace (default 3)')
    b.add_argument('--fp-redirect', choices=('shadow', 'writer', 'redirect'), help='the flight parameter refresh recomputed by strict code next to the native one (Option B slice 1; writer and redirect write the writer\'s +0x0..+0xc; the spline length hook is Windows-only)')
    b.add_argument('--fp-redirect-watch', nargs='+', metavar='SAVED_ID', help='FP_REDIRECT_ROWs for up to four saved IDs')
    b.add_argument('--fp-redirect-ticks', type=int, default=3, help='ticks 1..N of the FP_REDIRECT_ROWs (default 3)')
    b.add_argument('--coverage', type=int, metavar='TICKS', help='which listed functions run in ticks 1..TICKS (diagnostic)')
    b.add_argument('--coverage-list', type=Path, help='the Linux function list for --coverage (fp_inventory.py --coverage-lists)')
    b.add_argument('--attention-all', action='store_true', help="every portable object's attention timer keyed by (tick, ID), its first due at the first admitted call")
    b.add_argument('--loadout-order-rows', type=int, nargs=2, metavar=('FROM', 'TO'), help='LOADOUT_ROWs in this tick window (at most 600 ticks)')
    b.add_argument('--nav-cadence', choices=('far', 'observe'), help='nav refresh intervals no longer depend on the camera distance')
    b.add_argument('--nav-params-init', action='store_true', help='parameter deadlines reset at the first call')
    b.add_argument('--nav-key', action='store_true', help="the nav refresh's draws keyed per ship (tick, portable ID, ordinal, time)")
    b.add_argument('--nav-probe-init', action='store_true', help='obstacle-probe deadlines reset at the first call')
    b.add_argument('--created-identities', action='store_true', help='portable IDs and census rows for components made after the load')
    b.add_argument('--created-load', action='store_true', help='also for those the load makes after the import')
    b.add_argument('--portable-created', action='store_true', help='ordering and keying modules use created IDs')
    b.add_argument('--gravidar-wide', choices=('sort',), help='the Gravidar\'s zone groups in portable order for the pass (needs --gravidar)')
    b.add_argument('--spawn-rng', choices=('observe', 'key'), help='ship loadouts on the generator\'s explicit state')
    b.add_argument('--travel-snapshot', action='store_true', help='the wingman travel decision reads a per-pass snapshot of the leader flag')
    b.add_argument('--gravidar-deadlines', action='store_true', help='factor-pass deadlines rebased at the first gated Move, low-attention passes keyed')
    b.add_argument('--gravidar', action='store_true', help='the gated Gravidar pass, keyed units, time sync, sector order (implies serial pathing)')
    b.add_argument('--event-rows', type=int, metavar='TICKS', help='one EVENT_ROW per due event in the boot passes and the first TICKS gated ticks (up to 20000; about 50 MB per client for 6000 ticks)')
    b.add_argument('--event-order', action='store_true', help='due events delivered in (time, saved ID) source order')
    b.add_argument('--boot-hold', action='store_true', help='the first AIDirector::Move after the load runs in tick 1')
    b.add_argument('--ai-wait-rng', action='store_true', help='load-time AI Wait durations on per-wait streams')
    b.add_argument('--region-rng', choices=('key', 'observe'), help='hazard region updates on per-region streams')
    b.add_argument('--path-determinism', type=int, choices=(1, 2, 3), help='nav refresh stamped per frame (bit 1, implies --serial-pathing) and no 1 ms pathing budget (bit 2)')
    b.add_argument('--world-hash-watch', nargs='*', metavar='SAVED_ID', help='log the hashed bytes of these saved IDs (16 hex digits, up to 64)')
    b.add_argument('--clients', type=int, default=2, choices=range(2, 9), metavar='2..8', help='instances on the host; above 2 they use the protocol 3 wire')
    b.add_argument('--ticks', type=int, default=600)
    b.add_argument('--stride', type=int, default=60, help='world hash capture every N ticks (and tick 1)')
    r = sub.add_parser('run', help='on the Linux host, as root')
    r.add_argument('--root', type=Path, required=True)
    r.add_argument('--user', default='x4server')
    r.add_argument('--ticks', type=int)
    r.add_argument('--name')
    r.add_argument('--timeout', type=int, default=3600, help='seconds for load and the whole run')
    r.add_argument('--min-memory-gib', type=float, help='default 6.5 per instance')
    r.add_argument('--no-discard', action='store_true', help='render with lavapipe instead of the discard layer')
    r.add_argument('--preflight', action='store_true', help='only check the host requirements and the bundle; start nothing')
    r.add_argument('--launch-order', metavar='2,1', help='the order the clients start in; a difference that follows it is startup timing, one that follows the number is keyed on the slot')
    args = p.parse_args()
    bundle(args) if args.command == 'bundle' else run(args)


if __name__ == '__main__':
    main()
