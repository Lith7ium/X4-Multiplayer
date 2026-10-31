"""Client datagram budget: XB bundles, and NPC health/identity sent on change.

Synthetic relays and Lua harnesses; no native X4 or Internet measurement.
"""
from pathlib import Path
import unittest

from x4lan.host import MAX_PACKET, Session, bundle

try:
    from lupa.luajit21 import LuaRuntime
except ImportError:
    LuaRuntime = None

ROOT = Path(__file__).resolve().parents[1]
UI = ROOT / 'src/extension/x4_lan_lab/ui'
TOKEN = 'a' * 32
P1, P2 = ('192.0.2.1', 1000), ('192.0.2.2', 2000)


def joined():
    session = Session(TOKEN, 'build', 'bubble')
    leases = []
    for slot, address, nonce in (('p1', P1, '1'), ('p2', P2, '2')):
        hello = f'HELLO|1|{TOKEN}|build|sector_macro|bubble|{slot}|{nonce * 32}'.encode()
        leases.append(session.receive(hello, address, 0)[0].decode().split('|')[3])
    return session, leases


def unbundle(packet, lease):
    """Reference decoder (client.lua unbundle)."""
    header = f'XB|1|{lease}|'.encode()
    assert packet.startswith(header)
    rest, out = packet[len(header):], []
    while rest:
        digits, _, rest = rest.partition(b':')
        body, rest = rest[:int(digits)], rest[int(digits):]
        kind, _, after = body.partition(b'|')
        out.append(kind + b'|1|' + lease.encode() + b'|' + after)
    return out


class BundleTests(unittest.TestCase):
    def test_xbcap_needs_lease_address_and_version(self):
        s, (a, b) = joined()
        s.receive(f'XBCAP|1|{a}|1'.encode(), P2, .1)
        s.receive(f'XBCAP|1|{b}|2'.encode(), P2, .1)
        s.receive(f'XBCAP|1|{b}|1|x'.encode(), P2, .1)
        self.assertFalse(any(p.bundle for p in s.peers.values()))
        self.assertEqual(s.receive(f'XBCAP|1|{b}|1'.encode(), P2, .1), [])
        self.assertTrue(s.peers['p2'].bundle)
        self.assertFalse(s.peers['p1'].bundle)
        # A capability never changes the snapshot itself: without capable peers, identity.
        packets = s.snapshots(.2)
        s.peers['p2'].bundle = False
        self.assertEqual(bundle(packets, s.peers), packets)

    def test_bundles_restore_every_message_in_order_within_the_transport_limit(self):
        s, (a, b) = joined()
        s.peers['p2'].bundle = True
        messages = [f'UMOVE|1|{b}|{a}|{i}|{i}|cluster_macro|10.5|{i}.25|2|3|0|0.5|0|100|0|0|0|0|0'.encode()
                    for i in range(1, 101)]
        foreign = [(P1, f'AUTH|1|{a}|1'.encode())]
        out = bundle(foreign + [(P2, m) for m in messages], s.peers)
        self.assertEqual(out[0], foreign[0])
        to_p2 = [p for addr, p in out if addr == P2]
        self.assertTrue(all(len(p) <= MAX_PACKET for p in to_p2))
        self.assertLess(len(to_p2) * 3, len(messages))
        restored = [m for p in to_p2 for m in (unbundle(p, b) if p.startswith(b'XB|') else [p])]
        self.assertEqual(restored, messages)

    def test_unbundlable_packets_pass_whole_and_keep_order(self):
        s, (_, b) = joined()
        s.peers['p2'].bundle = True
        big = f'BIG|1|{b}|'.encode() + b'x' * (MAX_PACKET - 40)
        packets = [f'A|1|{b}|1'.encode(), f'B|1|{b}|2'.encode(), b'RESULT|1|0123|OK',
                   f'C|1|{b}|3'.encode(), big, f'D|1|{b}|4'.encode(), f'lower|1|{b}|5'.encode()]
        out = [p for _, p in bundle([(P2, p) for p in packets], s.peers)]
        self.assertEqual(out, [f'XB|1|{b}|3:A|13:B|2'.encode(), b'RESULT|1|0123|OK', f'C|1|{b}|3'.encode(),
                               big, f'D|1|{b}|4'.encode(), f'lower|1|{b}|5'.encode()])


@unittest.skipUnless(LuaRuntime, 'lupa.luajit21 not installed')
class LuaBundleTests(unittest.TestCase):
    def client(self, config=''):
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.execute('''
logs={};DebugError=function(v) logs[#logs+1]=v end
GetCurRealTime=function() return simNow end;simNow=0
X4LAN_MEASURE_CLOCK=function() return simNow end
X4LAN_CONFIG={enabled=true,net_probe=true,slot='p2'}
sent,inbox={},{}
X4LAN_TRANSPORT={open=function() return {send=function(_,p) sent[#sent+1]=p;return true end,
 receive=function() return table.remove(inbox,1) end,close=function() end} end}
''')
        lua.execute((UI / 'client.lua').read_text())
        for name in ('measure_clock.lua', 'net_rtt.lua'):
            lua.execute((UI / name).read_text())
        lua.execute('''
transport=X4LAN_TRANSPORT.open('127.0.0.1',1)
client=X4LAN_CLIENT.new({token=string.rep('a',32),build='build',bubble='bubble',sector='sector_macro',slot='p2'%s},
 transport,string.rep('2',32))
seen={};client.receiveExtension=function(self,f) seen[#seen+1]=table.concat(f,'|');return true end
''' % config)
        return lua

    def run_relay(self, lua, session, until, extra=lambda now: []):
        t = 0.0
        while t < until:
            lua.globals().simNow = t
            lua.execute('client:update(simNow,nil)')
            sent = lua.globals().sent
            inbox = lua.globals().inbox
            for i in range(1, len(sent) + 1):
                for reply in session.receive(sent[i].encode(), P2, t):
                    inbox[len(inbox) + 1] = reply.decode()
            lua.execute('sent={}')
            for addr, packet in bundle(session.snapshots(t) + extra(t), session.peers):
                if addr == P2:
                    inbox[len(inbox) + 1] = packet.decode()
            t += .05

    def test_client_announces_bundles_and_dispatches_each_message(self):
        lua = self.client()
        s, (a, _) = joined()
        lease = lambda: lua.eval('client.lease')
        self.run_relay(lua, s, 1.2)
        b = lease()
        self.assertTrue(s.peers['p2'].bundle, 'XBCAP follows the next HELLO once leased')
        messages = [f'NHP|1|{b}|{a}|{i}|1|{i}|cluster_macro|ARG-{i}|ship_macro|argon|100|50|1' for i in range(1, 30)]
        lua.execute('seen={}')
        self.run_relay(lua, s, .2, lambda now: [(P2, m.encode()) for m in messages] if now < .05 else [])
        seen = [v for v in lua.globals().seen.values() if v.startswith('NHP|')]
        self.assertEqual(seen, messages)
        # A bundle for another lease, or a truncated item, delivers nothing more.
        lua.execute('''seen={};inbox={'XB|1|'..string.rep('f',32)..'|5:A|xyz','XB|1|'..client.lease..'|9:NHP|1'}
         client:update(simNow+.01,nil)''')
        self.assertEqual(len(lua.globals().seen), 0)

    def test_rx_types_count_bundled_messages_and_opt_out_sends_no_xbcap(self):
        lua = self.client()
        s, _ = joined()
        self.run_relay(lua, s, 5.5)
        line = [v for v in lua.globals().logs.values() if ' NETRTT v=1 side=client ' in v][-1]
        self.assertRegex(line, r' rx_msgs=[\d.]+ rx_types=EMPTY:[\d.]+')
        quiet = self.client(",net_bundle=false")
        s2, _ = joined()
        self.run_relay(quiet, s2, 2.5)
        self.assertFalse(s2.peers['p2'].bundle)


@unittest.skipUnless(LuaRuntime, 'lupa.luajit21 not installed')
class HostCadenceTests(unittest.TestCase):
    def test_host_nhp_only_on_change_with_keepalive(self):
        import test_npc_combat as nc
        l = nc.NpcAdapterTests().make('p1')
        l.execute('''
         U.wanted={cluster_14_sector001_macro=now};for i=1,30 do step() end
         local id=U.publish[1].id;local r=N.records[id];local read=id..'|1|'..r.serial
         local seq=N.seq
         N.read(read..'|100|80|1');assert(N.seq==seq+1,'first reading is sent')
         for i=1,5 do step();N.read(read..'|100|80|1') end
         assert(N.seq==seq+1,'unchanged readings are not resent within the keepalive')
         N.read(read..'|90|80|1');assert(N.seq==seq+2,'a change is sent at once')
         for i=1,14 do step();N.read(read..'|90|80|1') end
         assert(N.seq==seq+3,'unchanged state repeats once per keepalive: '..(N.seq-seq))
         U.wanted={cluster_14_sector001_macro=now};N.read(read..'|0|0|0');assert(N.seq==seq+4 and r.dead,'death is sent at once')
         step();assert(N.seq==seq+4,'the death is not resent every poll')
         for i=1,14 do step() end
         assert(N.seq==seq+5,'the death repeats at the keepalive')
         assert(not N.failed)
        ''')

    def test_host_identity_only_on_change_with_keepalive(self):
        l = LuaRuntime(unpack_returned_tuples=True)
        l.execute('''
         sent,contact={},true
         local api={GetObjectIDCode=function(id) return 'ABC-'..id end,GetComponentName=function(id) return 'Ship '..id end,
          IsComponentOperational=function(id) return id~=nil and id~=0 end,
          GetObjectPositionInSector=function() return {x=0,y=0,z=0} end,
          GetContextByClass=function() return 9 end,SetObjectForcedRadarVisible=function() end}
         local ffi={C=api,cdef=function() end,string=function(v) return v end,new=function(_,_,v) return v end}
         require=function() return ffi end
         DebugError=function() end;ConvertIDTo64Bit=tonumber;RegisterEvent=function() end
         AddUITriggeredEvent=function() end
         GetComponentData=function(id,k) if k=='maxradarrange' then return 10000 end;return true,contact end
         X4LAN_CONFIG={world_sync=true,slot='p1'}
         X4LAN_BUBBLE_API={state={client={lease='epoch'},transport={send=function(_,p) sent[#sent+1]=p end}},
          coop={ready=true,ships={11,12}},id=function(v) return v end,paused=function() return false end,
          universe={ready=true,latest={epoch='epoch'},publish={},replicas={},receive=function() end,frame=function() end}}
        ''')
        l.execute((UI / 'universe_protocol.lua').read_text())
        l.execute((UI / 'identity_game.lua').read_text())
        l.execute('''
         local U=X4LAN_BUBBLE_API.universe
         local function count(from) local n=0;for i=from or 1,#sent do if sent[i]:find('|P|1|',1,true) then n=n+1 end end;return n end
         U.frame(0);assert(count()==1)
         for t=1,10 do U.frame(t/10) end;assert(count()==1,'unchanged identity waits for the keepalive')
         U.frame(1.2);assert(count()==2,'keepalive')
         contact=false;U.frame(1.3);assert(count()==3,'a contact change is sent at once')
         assert(sent[#sent]:sub(-4)=='|1|0' or sent[#sent-1]:sub(-4)=='|1|0')
        ''')


if __name__ == '__main__':
    unittest.main()
