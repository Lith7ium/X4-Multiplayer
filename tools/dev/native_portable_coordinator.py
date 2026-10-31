"""Bounded coordinator for two declared portable native fixtures.

The runner must separately prove both actual native process identities. This
coordinator never turns protocol peers into evidence of native gameplay.
"""
import argparse
import hmac
import json
import math
import os
from pathlib import Path
import socket
import struct
import time

from native_portable import SCHEMA, SCHEMA_SHA, compare, decode_frame, encode_frame, exact_keys, hexword, sha, validate_contract

MAX_PACKET = 16384


def number(text, maximum):
    if not text.isdecimal() or str(int(text)) != text or int(text) > maximum:
        raise ValueError('noncanonical protocol integer')
    return int(text)


def real_clock(text):
    hexword(text, 16, 'real clock')
    result = struct.unpack('>d', bytes.fromhex(text))[0]
    if not math.isfinite(result) or result < 0:
        raise ValueError('invalid real clock')
    return result


def validate_participants(value):
    exact_keys(value, ('1', '2'), 'participants')
    for slot, platform in (('1', 'windows'), ('2', 'linux')):
        row = value[slot]
        exact_keys(row, ('platform', 'executable_sha256', 'agent_sha256'), 'participant')
        if row['platform'] != platform:
            raise ValueError('requires Windows slot1 and Linux slot2')
        hexword(row['executable_sha256'], 64, 'executable digest')
        hexword(row['agent_sha256'], 64, 'agent digest')


class PortableBarrier:
    def __init__(self, token, contract, participants, persist, *, ticks=12, hold_tick=3,
                 hold_seconds=3, started=0, startup_timeout=600, peer_timeout=120):
        hexword(token, 32, 'token')
        self.contract_sha = validate_contract(contract)
        validate_participants(participants)
        if type(ticks) is not int or not 1 <= hold_tick < ticks <= 120 or not 0 <= hold_seconds <= 10:
            raise ValueError('invalid bounded run')
        self.token, self.contract, self.participants, self.persist = token, contract, participants, persist
        self.limit, self.hold_tick, self.hold_seconds = ticks, hold_tick, hold_seconds
        self.started, self.startup_timeout, self.peer_timeout = started, startup_timeout, peer_timeout
        self.peers, self.hellos, self.frames, self.last_seen, self.common_at = {}, {}, {}, {}, {}
        self.state_seen = {}  # Only an accepted immutable frame proves this tick is still held.
        self.terminal, self.hold_observations, self.hold_release_observations = {}, [], {}
        self.released, self.failure, self.stopping, self.done = -1, None, False, False
        self.first_state_at, self.last_progress = None, started
        self.first_difference = None

    def summary(self):
        return {'schema': SCHEMA, 'contract_sha256': self.contract_sha,
                'scope': 'declared twelve-ship portable subset; native identity verified separately by runner',
                'native_processes_verified_by_coordinator': False,
                'verdict': 'fail' if self.failure else 'pass' if self.done else 'incomplete',
                'failure': self.failure, 'first_difference': self.first_difference,
                'released': self.released, 'ticks': self.limit, 'participants': self.participants,
                'hello_slots': sorted(self.hellos), 'frames': self.frames, 'terminal': self.terminal,
                'peer_endpoints': {slot: {'transport': 'ssh_virtual' if address == ('127.0.0.1', 1) else 'loopback_udp',
                                          'address': list(address)} for slot, address in self.peers.items()},
                'hold_observations': self.hold_observations,
                'hold_tick': self.hold_tick, 'hold_required_seconds': self.hold_seconds,
                'hold_release_observations': self.hold_release_observations,
                'hold_seconds': (self.common_at.get(('released', self.hold_tick), 0)
                                 - self.common_at.get(self.hold_tick, 0))}

    def packet(self, kind, slot, tick, *fields):
        return '|'.join((kind, '1', self.token, str(slot), str(tick), *fields)).encode('ascii')

    def stop(self, slot, tick):
        return self.packet('NGPSTOP', slot, tick, '1' if self.failure else '0')

    def stop_all(self):
        result = []
        for slot, address in self.peers.items():
            tick = max((t for t, rows in self.frames.items() if slot in rows), default=0)
            result.append((address, self.stop(slot, tick)))
        return result

    def durable(self):
        try:
            self.persist(self.summary())
            return True
        except (OSError, RuntimeError):
            self.failure = 'evidence_persistence_failed'
            self.stopping = True
            return False

    def fail(self, reason):
        self.failure = self.failure or reason
        self.stopping = True
        self.durable()
        return self.stop_all()

    def poll(self, now):
        if self.done or self.failure:
            return []
        # One native game may finish loading much earlier than its peer. Its
        # immutable frame0 must stay held within the same bounded startup budget;
        # it must not silently switch the other game's load to the runtime clock.
        if self.released < 0 and not self.stopping:
            if now - self.started > self.startup_timeout:
                return self.fail('startup_timeout')
        elif now - self.last_progress > self.peer_timeout:
            return self.fail('missing_peer_or_terminal_ack')
        return []

    def receive(self, data, address, now):
        if address[0] != '127.0.0.1' or not isinstance(data, bytes) or len(data) > MAX_PACKET:
            return []
        try:
            parts = data.decode('ascii').split('|')
            if len(parts) < 4 or parts[1] != '1' or not hmac.compare_digest(parts[2], self.token):
                return []
            slot = number(parts[3], 2)
            if slot not in (1, 2) or (slot in self.peers and self.peers[slot] != address):
                return []
        except (ValueError, UnicodeError):
            return []
        try:
            if parts[0] == 'NGPHELLO':
                expected = self.participants[str(slot)]
                fields = [expected['platform'], expected['executable_sha256'], expected['agent_sha256'],
                          self.contract_sha, SCHEMA_SHA]
                if parts[4:] != fields:
                    return self.fail('participant_capability_or_artifact_mismatch')
                if self.stopping:
                    return [(address, self.stop(slot, 0))]
                self.peers[slot] = address; self.hellos[slot] = fields; self.last_seen[slot] = now
                return []
            if slot not in self.hellos:
                return []
            self.last_seen[slot] = now
            if parts[0] == 'NGPDONE':
                if len(parts) != 8 or not self.stopping:
                    return self.fail('unexpected_terminal_ack')
                tick, calls, returns, restored = (number(v, 121) for v in parts[4:])
                expected = {'tick': self.limit, 'calls': self.limit + 1, 'returns': self.limit + 1, 'restored': 1}
                row = dict(tick=tick, calls=calls, returns=returns, restored=restored)
                if self.failure:
                    return [(address, self.stop(slot, tick))]
                if row != expected or (slot in self.terminal and self.terminal[slot] != row):
                    return self.fail('terminal_count_or_restoration_mismatch')
                self.terminal[slot] = row
                self.done = len(self.terminal) == 2
                if not self.durable():
                    return self.stop_all()
                return [(address, self.packet('NGPCLOSED', slot, tick))]
            if parts[0] != 'NGPSTATE' or len(parts) != 7:
                return self.fail('unknown_portable_message')
            real_clock(parts[4]); hexword(parts[5], 64, 'diagnostic digest')
            if len(parts[6]) > 8192 or len(parts[6]) % 2 or any(c not in '0123456789abcdef' for c in parts[6]):
                raise ValueError('invalid bounded frame hex')
            frame = decode_frame(bytes.fromhex(parts[6]), self.contract)
            tick = frame['tick']
            if tick > self.limit or tick > self.released + 1:
                return self.fail('future_or_unpermitted_frame')
            row = {'state': frame, 'real_bits': parts[4], 'diagnostic_sha256': parts[5]}
            rows = self.frames.setdefault(tick, {})
            if slot in rows and rows[slot] != row:
                return self.fail('changed_frame_while_waiting')
            rows[slot] = row
            self.state_seen[tick, slot] = now
            if self.first_state_at is None:
                self.first_state_at = now; self.last_progress = now
            if self.stopping:
                return [(address, self.stop(slot, tick))]
            if tick <= self.released:
                return [(address, self.packet('NGPGO', slot, tick + 1, '00000000', '00000000'))]
            if len(rows) != 2 or len(self.hellos) != 2:
                return []
            self.common_at.setdefault(tick, now)
            comparison = compare(rows[1]['state'], rows[2]['state'], self.contract)
            if not comparison['equal']:
                self.first_difference = {'tick': tick, **comparison}
                return self.fail('portable_state_mismatch')
            for peer in (1, 2):
                baseline = self.frames[0][peer]
                elapsed = real_clock(rows[peer]['real_bits']) - real_clock(baseline['real_bits'])
                if abs(elapsed - tick / 60) > 1e-8:
                    return self.fail('relative_real_clock_mismatch')
                if tick:
                    previous = self.frames[tick - 1][peer]['state']
                    game = real_clock(rows[peer]['state']['game_bits'])
                    if abs(game - real_clock(previous['game_bits']) - 1 / 60) > 1e-8:
                        return self.fail('game_clock_step_mismatch')
            if tick == self.limit:
                self.stopping = True; self.last_progress = now
                self.durable()
                return self.stop_all()
            if tick == self.hold_tick:
                if len(self.hold_observations) < 256:
                    self.hold_observations.append({'slot': slot, 'elapsed': now - self.common_at[tick],
                                                   'game_bits': frame['game_bits'], 'real_bits': parts[4]})
                if any(self.state_seen[tick, p] - self.common_at[tick] < self.hold_seconds for p in (1, 2)):
                    return []
                # Preserve the exact two observations authorizing release even
                # when the bounded diagnostic sample list filled before them.
                self.hold_release_observations = {
                    peer: {'elapsed': self.state_seen[tick, peer] - self.common_at[tick],
                           'game_bits': rows[peer]['state']['game_bits'],
                           'real_bits': rows[peer]['real_bits'],
                           'frame_sha256': sha(encode_frame(rows[peer]['state'], self.contract))}
                    for peer in (1, 2)}
            self.released = tick; self.last_progress = now
            self.common_at['released', tick] = now
            if not self.durable():
                return self.stop_all()
            return [(self.peers[p], self.packet('NGPGO', p, tick + 1, '00000000', '00000000')) for p in (1, 2)]
        except (ValueError, UnicodeError, KeyError, struct.error):
            return self.fail('malformed_or_unsupported_portable_state')


def atomic_report(path, report):
    temporary = path.with_suffix(path.suffix + '.tmp')
    with temporary.open('w', encoding='utf-8', newline='\n') as stream:
        json.dump(report, stream, indent=2); stream.write('\n'); stream.flush(); os.fsync(stream.fileno())
    temporary.replace(path)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--contract', type=Path, required=True)
    p.add_argument('--participants', type=Path, required=True)
    p.add_argument('--port', type=int, required=True)
    p.add_argument('--report', type=Path, required=True)
    p.add_argument('--ticks', type=int, default=12)
    p.add_argument('--lifetime', type=int, default=900)
    p.add_argument('--runner-owned', action='store_true', help='retain completed server up to30s for owning runner cleanup')
    args = p.parse_args()
    if not 1024 <= args.port <= 65535 or not 30 <= args.lifetime <= 1800 or args.report.exists():
        p.error('bounded port/lifetime and fresh report required')
    started = time.monotonic()
    barrier = PortableBarrier(os.environ.get('X4MP_PORTABLE_TOKEN', ''),
        json.loads(args.contract.read_text()), json.loads(args.participants.read_text()),
        lambda result: atomic_report(args.report, result), ticks=args.ticks, started=started)
    last = None; terminal_at = None
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind(('127.0.0.1', args.port)); sock.settimeout(.1)
        print('PORTABLE_COORDINATOR READY native_identity_requires_runner=1', flush=True)
        while time.monotonic() - started < args.lifetime:
            now = time.monotonic()
            try:
                data, address = sock.recvfrom(MAX_PACKET + 1)
                replies = barrier.receive(data, address, now)
            except (socket.timeout, ConnectionResetError):
                replies = barrier.poll(now)
            for address, packet in replies:
                try: sock.sendto(packet, address)
                except ConnectionResetError: pass
            status = barrier.released, barrier.failure, barrier.done
            if status != last:
                print(f'PORTABLE_COORDINATOR tick={barrier.released} failure={barrier.failure or "none"} done={barrier.done}', flush=True)
                last = status
            if barrier.done or barrier.failure:
                if terminal_at is None: terminal_at = now
            if terminal_at is not None and now - terminal_at >= (30 if args.runner_owned and barrier.done and not barrier.failure else 2):
                break
        if not barrier.done and not barrier.failure:
            barrier.fail('coordinator_lifetime_expired')
    raise SystemExit(0 if barrier.done and not barrier.failure else 1)


if __name__ == '__main__':
    main()
