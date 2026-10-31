"""RTTPING/RTTPONG probe: relay unit tests, real UDP and a Lua client over a simulated link.

Simulated delays prove the arithmetic and compatibility, not real Internet RTT.
"""
from collections import deque
from pathlib import Path
import re
import socket
import unittest

from x4lan.host import Relay, Session
from x4lan.netprobe import NetProbe, percentile

try:
    from lupa.luajit21 import LuaRuntime
except ImportError:
    LuaRuntime = None

ROOT = Path(__file__).resolve().parents[1]
UI = ROOT / 'src/extension/x4_lan_lab/ui'
TOKEN = 'a' * 32
P1 = ('192.0.2.1', 1000)
P2 = ('192.0.2.2', 2000)


def hello(slot, nonce):
    return f'HELLO|1|{TOKEN}|build|sector_macro|bubble|{slot}|{nonce}'.encode()


def joined():
    session = Session(TOKEN, 'build', 'bubble')
    lease1 = session.receive(hello('p1', '1' * 32), P1, 0)[0].decode().split('|')[3]
    lease2 = session.receive(hello('p2', '2' * 32), P2, 0)[0].decode().split('|')[3]
    return session, lease1, lease2


def ping(lease, seq, client_ms, echo='-', hold='-'):
    return f'RTTPING|1|{lease}|{seq}|{client_ms:.3f}|{echo}|{hold}'.encode()


def fields(line):
    return dict(re.findall(r'(\w+)=(\S+)', line))


class NetProbeUnitTests(unittest.TestCase):
    def test_percentile_nearest_rank(self):
        self.assertIsNone(percentile([], .5))
        self.assertEqual(percentile([5, 1, 4, 2, 3], .5), 3)
        self.assertEqual(percentile([5, 1, 4, 2, 3], .95), 5)
        self.assertEqual(percentile([5, 1, 4, 2, 3], 0), 1)

    def test_answers_only_authenticated_well_formed_pings(self):
        session, lease1, lease2 = joined()
        probe = NetProbe(0)
        reply = probe.handle(ping(lease1, 1, 100), P1, session.peers, 1.0)
        self.assertEqual(reply, [f'RTTPONG|1|{lease1}|1|100.000|1000.000'.encode()])
        for raw, address in ((ping(lease2, 1, 100), P1), (ping(lease1, 1, 100), ('192.0.2.9', 1)),
                             (ping(lease1, 2**31, 100), P1), (ping(lease1, 1, 100)[:-2], P1),
                             (b'RTTPING|1|' + lease1.encode() + b'|1|-5|-|-', P1),
                             (b'RTTPING|2|' + lease1.encode() + b'|1|5|-|-', P1), ('RTTPING|1|é'.encode(), P1)):
            self.assertEqual(probe.handle(raw, address, session.peers, 1.0), [], raw)

    def test_rtt_jitter_loss_and_rates_per_peer(self):
        session, lease1, _ = joined()
        probe = NetProbe(0)
        echo, now = '-', 0.0
        arrivals = [0.0, 0.25, 0.53, 0.75, 1.0]  # sequence 4 is lost upstream
        for seq, arrival in zip((1, 2, 3, 5, 6), arrivals):
            hold = '-' if echo == '-' else '10.000'
            raw = ping(lease1, seq, (seq - 1) * 250, echo, hold)
            probe.count_rx(session.peers, P1, len(raw))
            reply = probe.handle(raw, P1, session.peers, arrival)[0].decode()
            probe.count_tx(session.peers, P1, len(reply))
            echo = reply.split('|')[5]
            # The client receives the pong 40 ms later and holds it for 10 ms before echoing.
        self.assertEqual(probe.report(session.peers, 4.9), [])
        lines = probe.report(session.peers, 5.0)
        self.assertEqual(len(lines), 1)  # p2 never probed and has no traffic stats
        row = fields(lines[0])
        self.assertEqual((row['side'], row['slot'], row['pings'], row['seq_lost']), ('relay', 'p1', '5', '1'))
        self.assertEqual(row['loss_pct'], '16.7')
        # RTT samples: arrival - previous pong time - 10 ms hold.
        self.assertEqual(row['rtt_samples'], '4')
        self.assertEqual(row['rtt_ms_min'], '210.000')
        self.assertEqual(row['rtt_ms_max'], '270.000')
        # Deviations: |0.25-0.25|, |0.28-0.25|, |0.22-0.5|, |0.25-0.25| -> p95 = 280 ms.
        self.assertEqual(row['jitter_ms_p95'], '280.000')
        self.assertEqual(row['rx_pps'], '1.0')
        self.assertEqual(row['tx_pps'], '1.0')
        # The window resets; a disconnected peer is not reported.
        del session.peers['p1']
        self.assertEqual(probe.report(session.peers, 10.0), [])
        self.assertNotIn('p1', probe.latest)

    def test_forged_echo_is_not_an_rtt_sample(self):
        session, lease1, _ = joined()
        probe = NetProbe(0)
        probe.handle(ping(lease1, 1, 0), P1, session.peers, 1.0)
        probe.handle(ping(lease1, 2, 250, '999.000', '0.000'), P1, session.peers, 1.25)
        row = fields(probe.report(session.peers, 5)[0])
        self.assertEqual(row['rtt_samples'], '0')
        self.assertEqual(row['rtt_ms_median'], 'na')

    def test_no_lines_without_any_probe_client(self):
        session, _, _ = joined()
        probe = NetProbe(0)
        probe.count_rx(session.peers, P1, 50)
        self.assertEqual(probe.report(session.peers, 5), [])
        self.assertIn('p1', probe.latest)  # rates still available to the monitor
        self.assertFalse(probe.enabled)


class RelayUdpTests(unittest.TestCase):
    def test_real_udp_ping_is_answered_without_touching_the_session(self):
        session = Session(TOKEN, 'build', 'bubble')
        relay = Relay('127.0.0.1', 0, session)
        client = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        client.settimeout(1)
        try:
            client.connect(relay.socket.getsockname())
            client.send(hello('p1', '1' * 32))
            relay.step(0)
            lease = client.recv(769).decode().split('|')[3]
            peer = session.peers['p1']
            before = (peer.seq, peer.last_seen, session.tick)
            client.send(ping(lease, 1, 5))
            relay.step(1.0)
            reply = client.recv(769).decode().split('|')
            while reply[0] == 'EMPTY':  # ordinary 20 Hz snapshots may arrive first
                reply = client.recv(769).decode().split('|')
            self.assertEqual(reply[:5], ['RTTPONG', '1', lease, '1', '5.000'])
            self.assertEqual((peer.seq, peer.last_seen), before[:2])
            stats = relay.netprobe.stats['p1']
            self.assertEqual(stats.rx_packets, 1)  # the ping; HELLO preceded the peer
            self.assertGreaterEqual(stats.tx_packets, 2)  # RTTPONG plus snapshots
        finally:
            client.close()
            relay.close()

    def test_monitor_snapshot_and_render_include_probe_summary(self):
        from x4lan.coop import CoopSession
        from x4lan.monitor import Monitor, render
        session = CoopSession(TOKEN, 'build', 'bubble')
        relay = Relay('127.0.0.1', 0, session)
        try:
            lease = session.receive(hello('p1', '1' * 32), P1, 0)[0].decode().split('|')[3]
            monitor = Monitor(relay, 0)
            relay.netprobe.window_start = 0
            relay.netprobe.handle(ping(lease, 1, 0), P1, session.peers, 0.1)
            relay.netprobe.report(session.peers, 5.0)
            state = monitor.snapshot(5.0)
            net = state['players'][0]['net']
            self.assertEqual(net['pings'], 1)
            self.assertIn('RTT probe ms', render(state))
        finally:
            relay.close()


@unittest.skipUnless(LuaRuntime, 'lupa.luajit21 not installed')
class LuaClientProbeTests(unittest.TestCase):
    """client.lua + net_rtt.lua against the Python relay through a simulated one-way delay."""

    def simulate(self, relay_supports_probe=True, enabled=True, drop_every=0, seconds=11.0, delay=0.03):
        lua = LuaRuntime(unpack_returned_tuples=True)
        session = Session(TOKEN, 'build', 'bubble')
        probe = NetProbe(0)
        link = {'up': deque(), 'down': deque(), 'now': 0.0, 'sent': 0}

        def send(packet):
            link['sent'] += 1
            if packet.startswith('RTTPING|'):
                if drop_every and link['sent'] % drop_every == 0:
                    return True
            link['up'].append((link['now'] + delay, packet.encode()))
            return True

        def receive():
            if link['down'] and link['down'][0][0] <= link['now']:
                return link['down'].popleft()[1].decode()
            return None
        lua.globals().py_send, lua.globals().py_receive = send, receive
        lua.execute('''
logs={};DebugError=function(v) logs[#logs+1]=v end
GetCurRealTime=function() return simNow end;simNow=0
X4LAN_MEASURE_CLOCK=function() return simNow+1e-7*(probeTick or 0) end
X4LAN_CONFIG={enabled=true,net_probe=%s,slot='p2'}
X4LAN_TRANSPORT={open=function() return {send=function(_,p) return py_send(p) end,
 receive=function() return py_receive() end,close=function() end} end}
''' % ('true' if enabled else 'false'))
        lua.execute((UI / 'client.lua').read_text())
        for name in ('measure_clock.lua', 'net_rtt.lua'):
            lua.execute((UI / name).read_text())
        lua.execute('''
transport=X4LAN_TRANSPORT.open('127.0.0.1',1)
client=X4LAN_CLIENT.new({token=string.rep('a',32),build='build',bubble='bubble',sector='sector_macro',slot='p2'},
 transport,string.rep('2',32))
''')
        step, t = 0.016, 0.0
        while t < seconds:
            link['now'] = t
            lua.globals().simNow = t
            lua.execute('client:update(simNow,nil)')
            while link['up'] and link['up'][0][0] <= t:
                _, raw = link['up'].popleft()
                if raw.startswith(b'RTTPING|') and relay_supports_probe:
                    replies = probe.handle(raw, P2, session.peers, t)
                else:
                    replies = session.receive(raw, P2, t)
                for reply in replies:
                    link['down'].append((t + delay, reply))
            t += step
        relay_lines = probe.report(session.peers, t)
        logs = [v for v in lua.globals().logs.values()]
        return logs, relay_lines, lua

    def client_lines(self, logs):
        return [fields(v) for v in logs if ' NETRTT v=1 side=client ' in v]

    def test_client_and_relay_measure_rtt_through_simulated_delay(self):
        logs, relay_lines, lua = self.simulate()
        rows = self.client_lines(logs)
        self.assertEqual(len(rows), 2, logs)
        row = rows[-1]
        self.assertEqual(row['slot'], 'p2')
        self.assertEqual(row['lost'], '0')
        self.assertGreater(int(row['pongs']), 15)
        # 60 ms simulated RTT plus at most one 16 ms frame of UI polling.
        for key in ('rtt_ms_min', 'rtt_ms_median', 'rtt_ms_p95'):
            self.assertGreaterEqual(float(row[key]), 59.9)
            self.assertLessEqual(float(row[key]), 76.1)
        self.assertLessEqual(float(row['jitter_ms_p95']), 16.1)
        self.assertGreater(float(row['tx_pps']), 4)
        relay = fields(relay_lines[0])
        self.assertEqual(relay['slot'], 'p2')
        self.assertGreater(int(relay['rtt_samples']), 10)
        self.assertGreaterEqual(float(relay['rtt_ms_median']), 59.9)
        self.assertLessEqual(float(relay['rtt_ms_p95']), 76.1)
        self.assertEqual(relay['seq_lost'], '0')

    def test_lost_pings_are_counted_and_connection_continues(self):
        logs, relay_lines, lua = self.simulate(drop_every=3)
        row = self.client_lines(logs)[-1]
        self.assertGreater(int(row['lost']), 0)
        self.assertGreater(float(row['loss_pct']), 0)
        self.assertGreater(int(fields(relay_lines[0])['seq_lost']), 0)
        self.assertTrue(lua.eval('client.lease ~= nil'))

    def test_old_relay_ignores_ping_and_session_is_unaffected(self):
        logs, relay_lines, lua = self.simulate(relay_supports_probe=False)
        row = self.client_lines(logs)[-1]
        self.assertEqual(row['pongs'], '0')
        self.assertGreater(int(row['lost']), 0)
        self.assertEqual(row['rtt_ms_median'], 'na')
        self.assertEqual(relay_lines, [])
        self.assertTrue(lua.eval('client.lease ~= nil'))

    def test_disabled_client_sends_no_probe(self):
        logs, relay_lines, lua = self.simulate(enabled=False, seconds=3)
        self.assertEqual(self.client_lines(logs), [])
        self.assertEqual(relay_lines, [])
        self.assertTrue(lua.eval('X4LAN_NET_PROBE == nil and client.lease ~= nil'))


if __name__ == '__main__':
    unittest.main()
