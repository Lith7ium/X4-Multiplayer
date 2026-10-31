"""Loopback coordinator for opt-in native FrameTick gates; no engine memory access."""
import argparse
import hmac
import json
import math
from pathlib import Path
import socket
import struct
import time

# Protocol 3 (docs/LOCKSTEP-NSLOT-WIRE.txt): N=2..8 clients, one opaque input
# blob of 0..480 lowercase hex characters (whole bytes) per client and tick.
CLIENTS_MAX = 8
BLOB_HEX_MAX = 480
STATE_MAX = {1: 512, 2: 512, 3: 1024}   # matches GATE_STATE_MAX in lockstep_wire.h (protocol 4 as 3)
# Protocol 4 = the protocol 3 input wire with a permit window of k=1..8 ticks
# (--lookahead; GATE_LOOKAHEAD_MAX in lockstep_wire.h). The permit for tick T
# needs every client's report of tick T-1-k (of tick 0 for T <= k+1) and
# carries the blobs sealed with tick T-1-k (empty for T <= k).
LOOKAHEAD_MAX = 8
# native_gate_tick_hz (src/native/engine/lockstep_tick.h).
TICK_RATES = (60, 40, 30, 20)
HEX = frozenset('0123456789abcdef')
# Observe mode: while digests keep differing, repeat the DESYNC notice at most
# once per this many ticks (a real desync usually differs on every later tick).
DESYNC_REPEAT = 600


def blob_valid(value):
    return len(value) <= BLOB_HEX_MAX and len(value) % 2 == 0 and all(c in HEX for c in value)


def canonical(value):
    return value.isdecimal() and value.isascii() and str(int(value)) == value


def permit_carries_inputs(packet):
    """A protocol 4 permit with any non-empty blob (NGGO|4|token|slot|T|k|blobs...)."""
    return packet.startswith(b'NGGO|4|') and any(packet.split(b'|')[6:])


class LiveInputs:
    """Opt-in loopback command inboxes; only each game proposes its inputs."""
    def __init__(self, token, clients=2):
        self.token = token
        self.rows = {slot: {} for slot in range(1, clients+1)}
        self.captured = {slot: 0 for slot in range(1, clients+1)}
        self.peers = {}

    def receive(self, data, address):
        if address[0] != '127.0.0.1' or len(data) > 256:
            return []
        try:
            f = data.decode('ascii').split('|')
            if len(f) != 6 or f[0] not in ('NGCMD', 'NGPOLL') or f[1] != '1' or not hmac.compare_digest(f[2], self.token):
                return []
            if any(not value.isdecimal() or str(int(value)) != value for value in f[3:5]):
                return []
            slot, sequence = map(int, f[3:5])
            if slot not in self.rows or not 0 <= sequence <= 4096:
                return []
            rows = self.rows[slot]
            if f[0] == 'NGCMD':
                command = f[5]
                if not sequence or command == '00000000' or len(command) != 8 or any(c not in '0123456789abcdef' for c in command):
                    return []
                if sequence in rows:
                    if rows[sequence]['command'] != command:
                        return []  # Never replace a previously queued command.
                elif sequence == len(rows)+1 and len(rows)-self.captured[slot] < 64:
                    rows[sequence] = {'command': command}
                else:
                    return []
                row = rows[sequence]
                reply = (f'NGCAPTURED|1|{self.token}|{slot}|{sequence}|{row["tick"]}'
                         if 'tick' in row else f'NGQUEUED|1|{self.token}|{slot}|{sequence}')
                return [(address, reply.encode())]
            if slot in self.peers and self.peers[slot] != address:
                return []
            if not f[5].isdecimal() or str(int(f[5])) != f[5]:
                return []
            tick = int(f[5])
            if not 0 <= tick <= 18000 or (sequence == 0) != (tick == 0):
                return []
            if sequence > self.captured[slot]+1 or (sequence and sequence not in rows):
                return []
            if sequence:
                row = rows[sequence]
                if 'tick' in row and row['tick'] != tick:
                    return []
                if sequence == self.captured[slot]+1:
                    if sequence > 1 and tick <= rows[sequence-1]['tick']:
                        return []
                    row['tick'] = tick
                    self.captured[slot] = sequence
            self.peers[slot] = address
            following = self.captured[slot]+1
            if following not in rows:
                return []
            reply = f'NGNEXT|1|{self.token}|{slot}|{following}|{rows[following]["command"]}'
            return [(address, reply.encode())]
        except (ValueError, UnicodeError):
            return []


class Barrier:
    def __init__(self, token, ticks=120, hold_tick=10, hold_seconds=3, compare=True, relative_real=False, live_inputs=False,
                 observe=False, clients=2, remote=False, lookahead=0, tick_hz=60):
        # remote: the caller already restricts sources (--allow-from); state reports may then
        # come from non-loopback addresses. Peers are still pinned per slot and authenticated.
        self.remote = remote
        if len(token) != 32 or any(c not in '0123456789abcdef' for c in token):
            raise ValueError('invalid token')
        if type(clients) is not int or not 2 <= clients <= CLIENTS_MAX:
            raise ValueError('clients must be 2..8')
        if type(lookahead) is not int or not 0 <= lookahead <= LOOKAHEAD_MAX:
            raise ValueError('lookahead must be 0..8')
        if lookahead and live_inputs:
            raise ValueError('live inputs carry protocol 2 commands; lookahead needs protocol 4')
        if tick_hz not in TICK_RATES:
            raise ValueError('tick rate must be 60, 40, 30 or 20')
        # native_gate_tick_hz of every client: each tick advances game and real time by 1/hz.
        self.tick_hz = tick_hz
        # 0: protocols 1-3, unchanged. k>0: only protocol 4 with the same k; any
        # other authenticated report fails the session (lookahead_mismatch_slot_S).
        self.lookahead = lookahead
        # Protocol 2 carries exactly two 8-hex commands; protocols 1, 3 and 4 admit N.
        self.clients, self.slots = clients, tuple(range(1, clients+1))
        self.token, self.limit = token, ticks
        self.hold_tick, self.hold_seconds, self.compare = hold_tick, hold_seconds, compare
        self.peers, self.states, self.arrivals = {}, {}, {}
        self.released, self.failed, self.done = -1, None, False
        # Timestamps from time.perf_counter: time.monotonic is the 15.6 ms tick on Windows
        # before Python 3.13, which made every frame period a multiple of it.
        self.released_at = {}
        self.relative_real = relative_real
        self.version = None
        self.live_inputs = LiveInputs(token, clients) if live_inputs else None
        # Census mode records every differing declared-state tick and keeps
        # releasing frames, so one run shows how divergence spreads. Clock,
        # timestep and transport checks still stop the session.
        self.observe = observe
        self.mismatches = []
        self.mismatch_slots = {}   # tick -> slots whose digest differs from slot 1
        # Observe-mode notices for people watching a session (desync indicator):
        # NATIVE_GATE DESYNC on the first differing tick of a run (repeated every
        # DESYNC_REPEAT ticks while it lasts) and NATIVE_GATE RESYNC when the
        # digests agree again. No wire change; the main loop prints them.
        self.diverged = False
        self.notices = []
        self.last_notice = None

    def receive(self, data, address, now):
        if self.live_inputs and data.startswith((b'NGCMD|', b'NGPOLL|')):
            return self.live_inputs.receive(data, address) if not self.done and not self.failed else []
        if len(data) > STATE_MAX[3 if data.startswith((b'NGSTATE|3|', b'NGSTATE|4|')) else 1] or (address[0] != '127.0.0.1' and not self.remote):
            return []
        try:
            f = data.decode('ascii').split('|')
            if len(f) not in (8, 9, 10, 11) or f[0] != 'NGSTATE' or f[1] not in ('1', '2', '3', '4') or not hmac.compare_digest(f[2], self.token):
                return []
            version = int(f[1])
            # A relay serves protocol 4 (--lookahead k>0) or protocols 1-3, never both.
            mismatch = (version == 4) != (self.lookahead > 0)
            if len(f) != 7+version or (self.version not in (None, version) and not mismatch):
                return []
            if version == 2 and (self.clients != 2 or len(f[8]) != 8 or any(c not in '0123456789abcdef' for c in f[8])):
                return []
            # Protocols 3 and 4: the client declares its configured N; refuse any other.
            if version >= 3 and (f[8] != str(self.clients) or not blob_valid(f[-1]) or not canonical(f[3]) or not canonical(f[4])):
                return []
            if version == 4:
                if not canonical(f[9]) or not 1 <= int(f[9]) <= LOOKAHEAD_MAX:
                    return []
                mismatch = mismatch or int(f[9]) != self.lookahead
            slot, tick = int(f[3]), int(f[4])
            if slot not in self.slots or not 0 <= tick <= self.limit or tick > self.permitted():
                return []
            if any(len(value) != size or any(c not in '0123456789abcdef' for c in value)
                   for value, size in ((f[5], 16), (f[6], 16), (f[7], 64))):
                return []
            game, real = (struct.unpack('>d', bytes.fromhex(value))[0] for value in f[5:7])
            if not math.isfinite(game) or not math.isfinite(real):
                return []
            if slot in self.peers and self.peers[slot] != address:
                return []
            if mismatch:
                # Old and new transports never mix: a client with another window
                # (or none) would apply other ticks' inputs. Stop the session.
                self.failed = self.failed or f'lookahead_mismatch_slot_{slot}'
                return [(address, f'NGSTOP|{version}|{self.token}|{slot}|{tick}'.encode('ascii'))]
            self.peers[slot] = address
            self.version = version
            row = {'game': game, 'real': real, 'digest': f[7]}
            if version == 2:
                row['command'] = f[8]
            if version >= 3:
                row['blob'] = f[-1]
            rows = self.states.setdefault(tick, {})
            if slot in rows and rows[slot] != row:
                self.failed = self.failed or f'changed_state_while_waiting_tick_{tick}'
            else:
                rows[slot] = row
            self.arrivals.setdefault(tick, now)
            if self.failed or self.done:
                return [(address, self.packet('NGSTOP', slot, tick))]
            if tick <= self.released:
                # A retransmitted report: answer the permits its tick released.
                return [(address, self.packet('NGGO', slot, permit)) for permit in self.unlocked(tick)]
            return self.advance(now)
        except (ValueError, UnicodeError, OverflowError):
            return []

    def permitted(self):
        """Highest permit issued; reports of later ticks cannot exist yet."""
        return min(self.limit, self.released+1+self.lookahead) if self.released >= 0 else 0

    def unlocked(self, tick):
        """Permits released once every client's report of tick passed its checks."""
        permits = range(1, self.lookahead+2) if tick == 0 else (tick+1+self.lookahead,)
        return [permit for permit in permits if permit <= self.limit]

    def advance(self, now):
        """Check complete ticks in order; release their permits or stop everyone."""
        out = []
        while not self.failed and not self.done:
            tick = self.released+1
            if tick > self.limit or len(self.states.get(tick, {})) != self.clients:
                break
            self.check(tick)
            if self.failed or tick == self.limit:
                self.done = not self.failed
                return out+[(self.peers[s], self.packet('NGSTOP', s, tick)) for s in self.slots]
            if tick == self.hold_tick and now-self.arrivals[tick] < self.hold_seconds:
                break
            self.released = tick
            self.released_at[tick] = now
            out += [(self.peers[s], self.packet('NGGO', s, permit)) for permit in self.unlocked(tick) for s in self.slots]
        return out

    def check(self, tick):
        """Timestep, declared digest and clock checks of one complete tick."""
        rows = self.states[tick]
        if tick > 0:
            for s in self.slots:
                delta = rows[s]['game']-self.states[tick-1][s]['game']
                if abs(delta-1/self.tick_hz) > 1e-8:
                    self.failed = f'native_dt_mismatch_player_{s}_tick_{tick}'
        differing = [s for s in self.slots if rows[s]['digest'] != rows[1]['digest']]
        if self.compare and differing:
            self.mismatch_slots[tick] = differing
            if self.observe:
                if tick not in self.mismatches:
                    self.mismatches.append(tick)
                    if not self.diverged or tick-self.last_notice >= DESYNC_REPEAT:
                        self.notices.append(f'NATIVE_GATE DESYNC tick={tick} slots={",".join(map(str, differing))} '
                                            f'first={self.mismatches[0]} mismatches={len(self.mismatches)}')
                        self.last_notice = tick
                    self.diverged = True
            else:
                self.failed = f'battle_state_mismatch_tick_{tick}'
        elif self.compare and self.observe and self.diverged:
            self.diverged = False
            self.notices.append(f'NATIVE_GATE RESYNC tick={tick} mismatches={len(self.mismatches)}')
        real_equal = all(rows[s]['real'] == rows[1]['real'] for s in self.slots)
        if self.relative_real:
            # Loading duration is process-local. Keep raw clocks as evidence
            # and require the same elapsed real ticks from the admission point.
            elapsed = [rows[s]['real']-self.states[0][s]['real'] for s in self.slots]
            real_equal = all(abs(elapsed[0]-t) <= 1e-8 for t in elapsed) and all(abs(t-tick/self.tick_hz) <= 1e-8 for t in elapsed)
        if not self.failed and (any(rows[s]['game'] != rows[1]['game'] for s in self.slots) or not real_equal):
            self.failed = f'native_clock_mismatch_tick_{tick}'

    def packet(self, kind, slot, tick):
        data = f'{kind}|{self.version or 1}|{self.token}|{slot}|{tick}'
        if kind == 'NGGO' and self.version == 2:
            data += ''.join('|'+self.states[tick-1][s]['command'] for s in (1, 2))
        if kind == 'NGGO' and self.version == 3:
            # Every client's sealed proposal of tick-1 in slot order: the exact
            # blobs each client applies at this tick.
            data += ''.join('|'+self.states[tick-1][s]['blob'] for s in self.slots)
        if kind == 'NGGO' and self.version == 4:
            # The window k, then every client's proposal sealed with tick-1-k
            # (none before tick 0): the exact blobs each client applies at tick.
            source = tick-1-self.lookahead
            data += f'|{self.lookahead}'+''.join('|'+(self.states[source][s]['blob'] if source >= 0 else '') for s in self.slots)
        return data.encode('ascii')

    def status(self, now):
        """Small, fixed-size live status for --status-file (desync indicator)."""
        last = self.mismatches[-1] if self.mismatches else None
        return {'tick': self.released, 'desync': bool(self.mismatches), 'diverged_now': self.diverged,
                'first_mismatch': self.mismatches[0] if self.mismatches else None, 'last_mismatch': last,
                'mismatches': len(self.mismatches), 'slots': self.mismatch_slots.get(last, []) if last is not None else [],
                'failure': self.failed, 'done': self.done, 'observe': self.observe, 'updated': round(now, 3),
                **({'lookahead': self.lookahead, 'permitted': self.permitted()} if self.lookahead else {})}

    def take_notices(self):
        notices, self.notices = self.notices, []
        return notices

    def summary(self):
        common = [tick for tick, rows in self.states.items() if len(rows) == self.clients]
        return {'scope': 'native FrameTick gate and declared state subset',
                'protocol_version': self.version, 'clients': self.clients, 'tick_hz': self.tick_hz,
                'real_time_domain': 'elapsed from snapshot admission' if self.relative_real else 'absolute',
                'verdict': 'fail' if self.failed else 'diverged' if self.mismatches else 'pass' if self.done else 'incomplete',
                'failure': self.failed, 'common_ticks': common,
                'observe': self.observe,
                'first_digest_mismatch': self.mismatches[0] if self.mismatches else None,
                'digest_mismatch_ticks': self.mismatches,
                'digest_mismatch_slots': self.mismatch_slots,
                # Wall-clock monotonic seconds when each tick was released: frame cost.
                'released_at': {tick: round(at, 6) for tick, at in self.released_at.items()},
                'hold_seconds': self.released_at.get(self.hold_tick, 0)-self.arrivals.get(self.hold_tick, 0),
                'states': self.states,
                'live_inputs': self.live_inputs.rows if self.live_inputs else None,
                # Protocol 4 only: the permit window; released_at then names the
                # tick whose reports released the permit for tick+1+lookahead.
                **({'lookahead': self.lookahead} if self.lookahead else {})}


def atomic_write(path, text):
    # On Windows a reader of the old file (an indexer, a scanner, a tool) makes the replace
    # fail for a moment, and p4-L lost its relay to that at tick 1164. These files are
    # checkpoints, so a session must not end for one: the next checkpoint tries again.
    temporary = path.with_suffix('.tmp')
    try:
        temporary.write_text(text)
        temporary.replace(path)
    except OSError as error:
        print(f'NATIVE_GATE checkpoint not written: {error}', flush=True)
        return False
    return True


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--bind', default='127.0.0.1')
    p.add_argument('--allow-from', default='', metavar='IP[,IP...]',
                   help='required for a non-loopback --bind: only datagrams from these IPv4 source addresses are read')
    p.add_argument('--port', type=int, required=True)
    p.add_argument('--token', required=True)
    p.add_argument('--ticks', type=int, default=120)
    p.add_argument('--hold-tick', type=int, default=10)
    p.add_argument('--hold-seconds', type=float, default=3)
    p.add_argument('--report', type=Path, required=True)
    p.add_argument('--relative-real', action='store_true')
    p.add_argument('--live-inputs', action='store_true', help='accept local per-client tactical input queues')
    p.add_argument('--observe', action='store_true', help='census: record state mismatches and keep running to --ticks')
    p.add_argument('--clients', type=int, default=2, help='full-simulation clients 2..8 (more than 2 needs protocol 1 or 3)')
    p.add_argument('--lookahead', type=int, default=0, metavar='K',
                   help='protocol 4: release the permit for tick T once every client reported tick T-1-K (0..8; '
                        '0 keeps protocols 1-3). Input delays must cover it: pilot >= K+2, orders >= K+1')
    p.add_argument('--tick-hz', type=int, default=60, choices=TICK_RATES,
                   help="the clients' native_gate_tick_hz; the timestep and relative-real checks use 1/hz")
    p.add_argument('--input-journal', type=Path, metavar='FILE',
                   help='append every released tick (its reports: digests, clocks, inputs) as one JSON line, flushed before '
                        'its permits leave. The full --report is then written every 60 s and at the end instead of before each '
                        'release that carries inputs: that rewrite grows with the run (7 ms at 600 ticks, about 1 ms more per '
                        '100 ticks) and every pilot record is an input')
    p.add_argument('--status-file', type=Path, help='observe mode: small live JSON status (desync flag, first/last mismatch), '
                   'rewritten atomically on every notice and at most once a second otherwise')
    args = p.parse_args()
    allowed = set(filter(None, (part.strip() for part in args.allow_from.split(','))))
    if args.bind != '127.0.0.1':
        # A remote relay (e.g. the dedicated server host) serves only the listed client
        # addresses; everything else is dropped before parsing. The token still authenticates.
        try:
            for address in allowed | {args.bind}: socket.inet_aton(address)
        except OSError: p.error('--bind and --allow-from need dotted IPv4 addresses')
        if not allowed: p.error('a non-loopback --bind needs --allow-from')
    if not 2 <= args.clients <= CLIENTS_MAX: p.error('--clients must be 2..8')
    if not 0 <= args.lookahead <= LOOKAHEAD_MAX: p.error('--lookahead must be 0..8')
    if args.lookahead and args.live_inputs: p.error('--live-inputs needs protocol 2; --lookahead needs protocol 4')
    barrier = Barrier(args.token, args.ticks, args.hold_tick, args.hold_seconds, relative_real=args.relative_real, live_inputs=args.live_inputs,
                      observe=args.observe, clients=args.clients, remote=bool(allowed), lookahead=args.lookahead, tick_hz=args.tick_hz)
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind((args.bind, args.port));s.settimeout(.1)
    print('NATIVE_GATE RELAY READY', flush=True)
    last = None
    last_report = 0
    last_status, final_written = 0, False
    def write_status(now):
        if not args.status_file:
            return
        atomic_write(args.status_file, json.dumps(barrier.status(now))+'\n')
    write_status(time.perf_counter())
    # --input-journal: the proposals a permit releases are durable before it is sent, one
    # line per tick, without rewriting the history; the full report becomes a checkpoint.
    journal = args.input_journal.open('w', encoding='ascii') if args.input_journal else None
    journaled = -1
    checkpoint = 60 if journal else 1
    try:
        while True:
            # Larger than any UDP payload: an oversize datagram is read whole
            # and refused by the barrier instead of raising WSAEMSGSIZE here.
            try: data, address = s.recvfrom(65536)
            except (socket.timeout, ConnectionResetError):
                # Windows reports a previous UDP send to a closed peer as an
                # ICMP port-unreachable error here. It supplies no state report;
                # keep serving the other peer and preserve the admission gate.
                continue
            if allowed and address[0] not in allowed:
                continue
            replies = barrier.receive(data, address, time.perf_counter())
            notices = barrier.take_notices()
            for notice in notices:
                print(notice, flush=True)
            # An action can fail on the first admitted UI callback. Preserve its
            # proposals before releasing the clients, even inside the one-second
            # periodic checkpoint interval. Never infer missing commands later.
            command_release = any(packet.startswith((b'NGGO|2|', b'NGGO|3|')) for _,packet in replies) and any(
                row.get('command', '00000000') != '00000000' or row.get('blob', '') != ''
                for row in barrier.states.get(barrier.released, {}).values()) or any(
                permit_carries_inputs(packet) for _,packet in replies)
            if journal and barrier.released > journaled:
                for tick in range(journaled+1, barrier.released+1):
                    journal.write(json.dumps({'tick': tick, 'released_at': round(barrier.released_at.get(tick, 0), 6),
                                              'rows': barrier.states.get(tick, {})}, separators=(',', ':'))+'\n')
                journal.flush()
                journaled = barrier.released
            elif command_release and not journal:
                if atomic_write(args.report, json.dumps(barrier.summary(), indent=2)+'\n'):
                    last_report = time.perf_counter()
            for dest, packet in replies:
                try: s.sendto(packet, dest)
                except ConnectionResetError:
                    # A departed peer must not prevent the other peer's STOP
                    # or retry. Missing reports still cannot advance the gate.
                    continue
            status = (barrier.released, barrier.failed, barrier.done)
            if status != last:
                last = status
                # Rewriting the complete growing history every frame makes long
                # tests quadratic in disk/serialization work. Checkpoint once a
                # second (every 60 s with --input-journal, which holds every
                # released tick), immediately on stop/failure, atomic replace.
                now = time.perf_counter()
                if now-last_report >= checkpoint or barrier.failed or barrier.done:
                    written = atomic_write(args.report, json.dumps(barrier.summary(), indent=2)+'\n')
                    # Nothing follows the last report, so it waits out a reader.
                    for _ in range(20):
                        if written or not (barrier.failed or barrier.done):
                            break
                        time.sleep(0.1)
                        written = atomic_write(args.report, json.dumps(barrier.summary(), indent=2)+'\n')
                    if written:
                        last_report = now
                diverged = f' first_mismatch={barrier.mismatches[0]} mismatches={len(barrier.mismatches)}' if barrier.mismatches else ''
                print(f'NATIVE_GATE tick={barrier.released} failure={barrier.failed or "none"} done={barrier.done}{diverged}', flush=True)
            # Live status for desync watchers: at once on a notice or the end,
            # otherwise at most once a second.
            now = time.perf_counter()
            final = bool(barrier.failed or barrier.done)
            if args.status_file and (notices or (final and not final_written) or now-last_status >= 1):
                write_status(now)
                last_status, final_written = now, final
    finally:
        s.close()
        if journal: journal.close()


if __name__ == '__main__': main()
