"""Local terminal dashboard. Observes the relay without changing the wire protocol."""

from __future__ import annotations

from collections import deque
from datetime import datetime, timezone
import errno
import json
import os
from pathlib import Path
import secrets
import shutil
import socket
import sys
import time
from concurrent.futures import ThreadPoolExecutor

from .host import LEASE_SECONDS, STATE_SECONDS, Relay
from .economy import EconomySession
from .coop import CoopSession


def addresses():
    import subprocess

    try:
        if sys.platform == 'win32':
            command = (
                '[Console]::OutputEncoding = New-Object Text.UTF8Encoding; '
                '@(Get-NetIPAddress -AddressFamily IPv4 | '
                'Where-Object { $_.IPAddress -ne "127.0.0.1" } | '
                'Select-Object InterfaceAlias,IPAddress) | ConvertTo-Json -Compress'
            )
            result = subprocess.run(
                ['powershell.exe', '-NoLogo', '-NoProfile', '-Command', command],
                capture_output=True,
                text=True,
                encoding='utf-8',
                timeout=5,
                check=True,
                creationflags=subprocess.CREATE_NO_WINDOW,
            )
            rows = json.loads(result.stdout or '[]')
            if isinstance(rows, dict):
                rows = [rows]
            return [
                {
                    'interface': r['InterfaceAlias'],
                    'ip': r['IPAddress'],
                    'kind': 'ZeroTier'
                    if 'zerotier' in r['InterfaceAlias'].lower()
                    else 'LAN / other',
                }
                for r in rows
            ]
        result = subprocess.run(
            ['ip', '-j', '-4', 'addr', 'show', 'scope', 'global'],
            capture_output=True,
            text=True,
            timeout=2,
            check=True,
        )
        rows = json.loads(result.stdout)
        return [
            {
                'interface': r['ifname'],
                'ip': a['local'],
                'kind': 'ZeroTier' if r['ifname'].startswith('zt') else 'LAN / other',
            }
            for r in rows
            for a in r.get('addr_info', [])
            if a.get('family') == 'inet'
        ]
    except (OSError, ValueError, subprocess.SubprocessError):
        return []


def peak_memory_mib():
    if sys.platform == 'win32':
        import ctypes
        from ctypes import wintypes

        class Counters(ctypes.Structure):
            _fields_ = [('cb', wintypes.DWORD), ('PageFaultCount', wintypes.DWORD)] + [
                (name, ctypes.c_size_t)
                for name in (
                    'PeakWorkingSetSize',
                    'WorkingSetSize',
                    'QuotaPeakPagedPoolUsage',
                    'QuotaPagedPoolUsage',
                    'QuotaPeakNonPagedPoolUsage',
                    'QuotaNonPagedPoolUsage',
                    'PagefileUsage',
                    'PeakPagefileUsage',
                )
            ]

        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        psapi = ctypes.WinDLL('psapi', use_last_error=True)
        kernel.GetCurrentProcess.restype = wintypes.HANDLE
        psapi.GetProcessMemoryInfo.argtypes = [
            wintypes.HANDLE,
            ctypes.POINTER(Counters),
            wintypes.DWORD,
        ]
        psapi.GetProcessMemoryInfo.restype = wintypes.BOOL
        values = Counters()
        values.cb = ctypes.sizeof(values)
        if psapi.GetProcessMemoryInfo(kernel.GetCurrentProcess(), ctypes.byref(values), values.cb):
            return values.PeakWorkingSetSize / (1024 * 1024)
        return None
    try:
        import resource

        return resource.getrusage(resource.RUSAGE_SELF).ru_maxrss / (
            1024 * 1024 if sys.platform == 'darwin' else 1024
        )
    except ImportError:
        return None


def enable_terminal():
    if sys.platform != 'win32':
        return True
    import ctypes
    from ctypes import wintypes

    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.GetStdHandle.argtypes = [wintypes.DWORD]
    kernel.GetStdHandle.restype = wintypes.HANDLE
    kernel.GetConsoleMode.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
    kernel.SetConsoleMode.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    handle = kernel.GetStdHandle(-11 & 0xFFFFFFFF)
    mode = wintypes.DWORD()
    return bool(
        kernel.GetConsoleMode(handle, ctypes.byref(mode))
        and kernel.SetConsoleMode(handle, mode.value | 0x0004)
    )


MAX_SHOT_ROWS = 32


def combat(session, now):
    """Read-only combat state of a co-op relay for human combat checks (D01/D02).

    Sanitized: ship numbers, generations, sequence numbers, percentages, counts and the validated
    weapon digest only; never a lease, nonce or token. Purely observational: it reads the stored
    generation instead of calling health_generation(), which would advance it on a key change.
    """
    health = []
    for i in (1, 2):
        live = session.health_live[i]
        row = {
            'ship': i,
            'generation': session.health_gen[i],
            'current': session.health_key[i]
            == (session.owners[i], session.epochs[i], session.pilot(i)),
            'live': live > 0 and live == session.health_gen[i],
            'last_seen_age': round(now - session.health_seen[i], 2) if live else None,
            'frame': None,
        }
        frame = session.health.get(i)
        if frame:
            hull, shield = (float(v) for v in frame['fields'][3:5])
            row['frame'] = {
                'seq': frame['seq'],
                'hull_percent': round(hull, 3),
                'shield_percent': round(shield, 3),
                'age': round(now - frame['at'], 2),
            }
        health.append(row)
    newest = sorted(session.shots.items(), key=lambda kv: -kv[1]['at'])[:MAX_SHOT_ROWS]
    shots = sorted(
        (
            {
                'shooter': k[0],
                'shooter_gen': k[1],
                'victim': k[2],
                'victim_gen': k[3],
                'hits': v['hits'],
                'seq': v['seq'],
                'weapon': v['fields'][-1],
                'age': round(now - v['at'], 2),
            }
            for k, v in newest
        ),
        key=lambda r: (r['shooter'], r['victim'], r['shooter_gen'], r['victim_gen']),
    )
    result = {'health': health, 'shots': shots, 'shot_rows': len(session.shots)}
    projectiles = getattr(session, 'projectiles', None)
    if projectiles is not None:
        result['projectiles'] = [
            {'ship': e, 'live': len(s.live()), 'latest_id': s.latest}
            for e, s in sorted(projectiles.streams.items())
        ]
    return result


class Monitor:
    def __init__(self, relay, now, version='unversioned'):
        self.relay = relay
        self.version = version
        self.started = self.previous_at = now
        self.previous = {}
        self.totals = (0, 0)
        self.events = deque(maxlen=8)
        self.history = {}
        self.revisions = {}
        self.cpu_at = time.process_time()
        self.event('SERVER STARTED', now)

    def event(self, message, now):
        self.events.append({'at': round(now - self.started, 1), 'message': message})

    def snapshot(self, now, interfaces=()):
        session = self.relay.session
        elapsed = max(now - self.previous_at, 0.001)
        players = []
        for slot in ('p1', 'p2'):
            peer = session.peers.get(slot)
            old = self.previous.get(slot)
            if old and (peer is None or old['lease'] != peer.lease):
                self.history[slot] = {
                    'address': old['address'],
                    'last_seen': old['last_seen'],
                    'joined_at': old['joined_at'],
                }
                self.event(
                    f'{slot.upper()} TIMED OUT: no valid player updates for {LEASE_SECONDS:g}s', now
                )
            if peer is None:
                past = self.history.get(slot)
                players.append(
                    {
                        'slot': slot,
                        'status': 'TIMED OUT' if past else 'WAITING',
                        'address': past['address'] if past else '-',
                        'updates_hz': 0,
                        'age': round(now - past['last_seen'], 1) if past else None,
                        'connected_seconds': None,
                        'pose': 'unavailable',
                    }
                )
                self.previous.pop(slot, None)
                continue
            changed = old is None or old['lease'] != peer.lease
            if changed:
                self.event(
                    f'{slot.upper()} CONNECTED from {peer.address[0]}:{peer.address[1]}', now
                )
            age = max(0, now - peer.last_seen)
            pose = (
                'waiting for first update'
                if peer.seq < 0
                else 'idle / paused'
                if peer.pose is None
                else 'stale'
                if now - peer.state_at >= STATE_SECONDS
                else 'live'
            )
            status = 'NO UPDATES' if age >= 2 else 'CONNECTED'
            if old and not changed and old['status'] != status:
                self.event(f'{slot.upper()} {status}', now)
            count = peer.state_messages - (old['count'] if old and not changed else 0)
            players.append(
                {
                    'slot': slot,
                    'status': status,
                    'address': f'{peer.address[0]}:{peer.address[1]}',
                    'updates_hz': round(count / elapsed, 1) if not changed else 0,
                    'age': round(age, 2),
                    'connected_seconds': round(now - peer.joined_at, 1),
                    'pose': pose,
                }
            )
            if isinstance(session, CoopSession):
                metrics = session.game_metrics.get(peer.lease)
                if metrics:
                    players[-1]['game'] = {
                        k: v for k, v in metrics.items() if k not in ('seq', 'at')
                    }
                    players[-1]['game']['age'] = round(now - metrics['at'], 2)
                ships = [
                    str(i)
                    for i, nav in session.navigation.items()
                    if session.owners[i] == slot and now - nav['at'] < STATE_SECONDS
                ]
                players[-1]['pose'] = (
                    ('live ships ' + ','.join(ships)) if ships else 'no current ship pose'
                )
            probe = getattr(self.relay, 'netprobe', None)
            if probe is not None and probe.enabled and slot in probe.latest:
                players[-1]['net'] = {k: v for k, v in probe.latest[slot].items() if k != 'slot'}
            self.previous[slot] = {
                'lease': peer.lease,
                'count': peer.state_messages,
                'address': players[-1]['address'],
                'last_seen': peer.last_seen,
                'joined_at': peer.joined_at,
                'status': status,
            }
        streams = {}
        for name, revision, owner, available in (
            ('world', session.world_revision, session.world_owner, session.world is not None),
            (
                'economy',
                session.assembly.revision,
                session.economy_owner,
                session.economy is not None,
            ),
        ):
            old = self.revisions.get(name)
            if not available:
                streams[name] = {'revision': None, 'age': None}
                self.revisions.pop(name, None)
            else:
                changed_at = now if old is None or old[:2] != (owner, revision) else old[2]
                self.revisions[name] = (owner, revision, changed_at)
                streams[name] = {'revision': revision, 'age': round(now - changed_at, 1)}
        rx, tx = self.relay.received_packets, self.relay.sent_packets
        cpu_now = time.process_time()
        cpu_percent = max(0, 100 * (cpu_now - self.cpu_at) / elapsed)
        self.cpu_at = cpu_now
        memory_mib = peak_memory_mib()
        snapshot = {
            'schema': 1,
            'pid': os.getpid(),
            'running': True,
            'version': self.version,
            'updated_at': datetime.now(timezone.utc).isoformat(),
            'uptime': round(now - self.started, 1),
            'build': session.build,
            'connection_timeout': LEASE_SECONDS,
            'bubble': session.bubble,
            'sector': session.sector,
            'port': self.relay.socket.getsockname()[1],
            'interfaces': list(interfaces),
            'players': players,
            'connected': len(session.peers),
            'streams': streams,
            'pending_trades': sum(v['result'] == 'PENDING' for v in session.orders.values()),
            'pending_actions': sum(v['result'] == 'PENDING' for v in session.commands.values()),
            'network': {
                'rx_packets': rx,
                'tx_packets': tx,
                'rx_hz': round((rx - self.totals[0]) / elapsed, 1),
                'tx_hz': round((tx - self.totals[1]) / elapsed, 1),
                'send_errors': self.relay.send_errors,
            },
            'events': list(self.events),
            'relay_process': {
                'cpu_percent_one_core': round(cpu_percent, 2),
                'peak_memory_mib': round(memory_mib, 1) if memory_mib is not None else None,
            },
        }
        if isinstance(session, CoopSession):
            snapshot['fleet'] = [
                {
                    'ship': i,
                    'owner': session.owners[i],
                    'pilot': session.pilot(i),
                    'epoch': session.epochs[i],
                }
                for i in (1, 2)
            ]
            snapshot['combat'] = combat(session, now)
            if session.quest.progress and session.quest.owner == getattr(
                session.peers.get('p1'), 'lease', None
            ):
                rev, stage, delivery, destroyed, paid = session.quest.progress
                snapshot['quest'] = dict(
                    revision=rev,
                    stage=stage,
                    delivered=delivery,
                    destroyed=destroyed,
                    paid=bool(paid),
                )
        if hasattr(session, 'universe'):
            b = session.universe.latest
            snapshot['universe'] = {
                'scope': 'experimental NPC scenes',
                'revision': b.revision if b else None,
                'sector': b.sector if b else None,
                'objects': len(b.rows) if b else 0,
                'guest_committed_revision': session.delivered,
                'pending_objects': len(session.universe.pending['rows'])
                if session.universe.pending
                else 0,
            }
        self.previous_at, self.totals = now, (rx, tx)
        return snapshot


def duration(seconds):
    seconds = int(seconds)
    return f'{seconds // 3600:02}:{seconds // 60 % 60:02}:{seconds % 60:02}'


def render(state, width=100, attached=False):
    width = max(40, width)
    bar = '=' * min(width, 100)
    status = 'RUNNING' if state['running'] else 'STOPPED'
    lines = [
        bar,
        f' X4 LAN LAB {state.get("version", "unversioned")}  |  SERVER {status}  |  {state["connected"]}/2 players',
        f' Uptime {duration(state["uptime"])}   UDP {state["port"]}   Session {state["bubble"]}',
        bar,
    ]
    for p in state['players']:
        role = 'HOST / LINUX' if p['slot'] == 'p1' else 'GUEST / WINDOWS'
        age = '-' if p['age'] is None else f'{p["age"]:.1f}s'
        lines += [
            f' {p["slot"].upper()}  {role:<17} {p["status"]:<12} {p["address"]}',
            f'     Last update: {age:<7} Updates/s: {p["updates_hz"]:<5.1f} Position: {p["pose"]}',
        ]
        game = p.get('game')
        if game:
            lines.append(
                f'     Game callbacks/s: {game["callbacks_hz"]:g}  Max gap: {game["interval_max_ms"]:g} ms  {game["state"]}'
            )
        net = p.get('net')
        if net:
            lines.append(
                f'     RTT probe ms: median {net["rtt_ms_median"]}  p95 {net["rtt_ms_p95"]}  '
                f'jitter p95 {net["jitter_ms_p95"]}  loss {net["loss_pct"]}%'
            )
    process = state.get('relay_process')
    if process:
        lines.append(
            f' Relay CPU: {process["cpu_percent_one_core"]:g}% of one core  Peak RAM: {process["peak_memory_mib"]} MiB'
        )
    lines += ['', ' SHARE ONE OF THESE SERVER ADDRESSES WITH YOUR FRIEND:']
    for a in sorted(state['interfaces'], key=lambda a: a['kind'] != 'ZeroTier'):
        lines.append(f'   {a["kind"]:<13} {a["ip"]:<16} ({a["interface"]})')
    if not state['interfaces']:
        lines.append('   No address detected. Check your LAN / ZeroTier connection.')
    lines += ['', ' HOST SIMULATION']
    for name, stream in state['streams'].items():
        value = (
            'waiting for host game'
            if stream['revision'] is None
            else f'revision {stream["revision"]}, last observed change {stream["age"]:.1f}s ago'
        )
        lines.append(f'   {name.title():<9} {value}')
    for ship in state.get('fleet', []):
        lines.append(f'   Ship {ship["ship"]}: authority {ship["owner"]}, pilot {ship["pilot"]}')
    universe = state.get('universe')
    if universe:
        lines.append(
            f' NPC scene: {universe["objects"]} ships  Host revision {universe["revision"]}  '
            f'Guest committed {universe["guest_committed_revision"]}'
        )
        lines.append(
            f'   Sector: {universe["sector"] or "waiting"}  (W01; interactions are not shared yet)'
        )
    quest = state.get('quest')
    if quest:
        status = (
            'COMPLETED' if quest['stage'] == 3 else 'DELIVERY' if quest['stage'] == 1 else 'COMBAT'
        )
        lines.append(
            f' Shared mission: {status}  Energy Cells {quest["delivered"]}/20  Targets {quest["destroyed"]}/2'
        )
    net = state['network']
    lines += [
        f'   Pending trades: {state["pending_trades"]}   Container actions: {state["pending_actions"]}',
        f' Network packets/s  IN {net["rx_hz"]:.1f}  OUT {net["tx_hz"]:.1f}  Send errors {net["send_errors"]}',
        '',
        ' RECENT EVENTS',
    ]
    for event in state['events'][-6:]:
        lines.append(f'   {duration(event["at"])}  {event["message"]}')
    lines += [
        '',
        ' Updates/s and game callbacks/s are diagnostics, not measured FPS or ping.',
        f' Connection timeout: {state["connection_timeout"]:g}s. Missing updates do not identify the cause.',
        ' Ctrl+C closes this viewer; the server keeps running.'
        if attached
        else ' Ctrl+C stops this server. Keep this window open while playing.',
    ]
    # Strip control characters from external data before writing to a terminal.
    return '\n'.join(''.join(c if c.isprintable() else '?' for c in line)[:width] for line in lines)


class Screen:
    def __init__(self, attached=False):
        self.tty = sys.stdout.isatty() and os.environ.get('TERM') != 'dumb'
        if self.tty:
            self.tty = enable_terminal()
        self.attached = attached
        self.last_plain = None
        if self.tty:
            print('\033[?1049h\033[?25l\033]0;X4 LAN Server\007', end='', flush=True)

    def show(self, state):
        if self.tty:
            columns, rows = shutil.get_terminal_size((100, 35))
            text = render(state, columns, self.attached).splitlines()
            height = max(1, rows - 1)
            if len(text) > height:
                text = text[: max(0, height - 2)] + text[-2:] if height >= 3 else text[:height]
            print(
                '\033[H' + '\n'.join(line + '\033[K' for line in text) + '\033[J',
                end='',
                flush=True,
            )
        else:
            signature = (
                state['running'],
                tuple((p['status'], p['address']) for p in state['players']),
            )
            if signature != self.last_plain:
                print(render(state, 110, self.attached), flush=True)
                self.last_plain = signature

    def close(self):
        if self.tty:
            print('\033[?25h\033[?1049l', end='', flush=True)


def probe(config):
    nonce = secrets.token_hex(16)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.settimeout(0.5)
        sock.connect(('127.0.0.1', config['port']))
        sock.send(f'PING|1|{config["token"]}|{nonce}'.encode())
        try:
            return sock.recv(769) == f'PONG|1|{nonce}|{config["build"]}|{config["bubble"]}'.encode()
        except OSError:
            return False


def attach(status_path):
    if not status_path.exists() or time.time() - status_path.stat().st_mtime > 3:
        raise RuntimeError(
            'A matching server is already running, but its live status is unavailable. '
            'Use its original terminal. To upgrade from v3, stop the old server first.'
        )
    screen = Screen(attached=True)
    try:
        while True:
            try:
                state = json.loads(status_path.read_text())
                if time.time() - status_path.stat().st_mtime > 3:
                    raise RuntimeError(
                        'Server status stopped updating. Check the original server window.'
                    )
                screen.show(state)
                if not state['running']:
                    break
            except FileNotFoundError:
                raise RuntimeError('Server status file disappeared.') from None
            time.sleep(0.5)
    except KeyboardInterrupt:
        pass
    finally:
        screen.close()
    print('Status viewer closed. It did not stop the server.')


def run(config, logs: Path, version='unversioned'):
    logs.mkdir(parents=True, exist_ok=True)
    status_path = logs / 'server-status.json'
    try:
        from .host import require_two_players

        require_two_players(config)
        session_type = CoopSession if config.get('coop') else EconomySession
        if config.get('world_sync'):
            from .universe import UniverseSession

            session_type = UniverseSession
        relay = Relay(
            '0.0.0.0',
            config['port'],
            session_type(config['token'], config['build'], config['bubble']),
        )
    except OSError as exc:
        if exc.errno != errno.EADDRINUSE and getattr(exc, 'winerror', None) != 10048:
            raise
        if probe(config):
            return attach(status_path)
        raise RuntimeError(
            f'UDP port {config["port"]} is used by another server or package pair. '
            'Close that server before starting this one.'
        ) from None
    now = time.monotonic()
    monitor = Monitor(relay, now, version)
    screen = Screen()
    path = logs / ('server-' + datetime.now().strftime('%Y%m%d-%H%M%S-%f') + '.jsonl')
    next_refresh = next_addresses = 0
    probe_log = None
    interfaces = []
    executor = ThreadPoolExecutor(max_workers=1)
    address_job = None

    def publish(state, stream):
        data = json.dumps(state, ensure_ascii=True, allow_nan=False)
        temporary = status_path.with_suffix('.tmp')
        temporary.write_text(data + '\n')
        temporary.replace(status_path)
        stream.write(data + '\n')
        screen.show(state)

    try:
        with path.open('w', buffering=1) as log:
            try:
                while True:
                    now = time.monotonic()
                    relay.step(now)
                    lines = relay.netprobe.report(relay.session.peers, now)
                    if lines:
                        if probe_log is None:
                            # Created only once a client actually sends RTTPING (net_probe=true).
                            probe_log = (
                                logs / ('netprobe-' + path.stem.removeprefix('server-') + '.log')
                            ).open('a', buffering=1)
                        stamp = datetime.now(timezone.utc).isoformat(timespec='milliseconds')
                        probe_log.write(''.join(f'{stamp} {line}\n' for line in lines))
                    if now >= next_refresh:
                        if address_job is not None and address_job.done():
                            interfaces = address_job.result()
                            address_job = None
                        if now >= next_addresses and address_job is None:
                            address_job = executor.submit(addresses)
                            next_addresses = now + 10
                        publish(monitor.snapshot(now, interfaces), log)
                        next_refresh = now + 0.5
                    time.sleep(0.005)
            except KeyboardInterrupt:
                pass
            finally:
                now = time.monotonic()
                monitor.event('SERVER STOPPED', now)
                state = monitor.snapshot(now, interfaces)
                state['running'] = False
                publish(state, log)
    finally:
        if probe_log is not None:
            probe_log.close()
        relay.close()
        screen.close()
        executor.shutdown(wait=True)
    print('Server stopped. Session log:', path)
