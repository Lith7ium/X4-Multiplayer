"""NPC authority, loss/reordering and native adapter fencing; no native acceptance claim."""
import unittest
from pathlib import Path
from x4lan.universe import Baseline, UniverseSession
from test_host import TOKEN, P1, P2, FOREIGN
from test_universe import SECTOR, row
from test_shot import WORLD, DIGEST
import test_universe_adapter as ua
from test_game_adapter import LuaRuntime

ROOT = Path(__file__).resolve().parents[1]


class NpcRelayTests(unittest.TestCase):
    def setUp(self):
        self.s = UniverseSession(TOKEN, 'world1-test', 'bubble')
        def join(slot, address, nonce):
            return self.s.receive(f'HELLO|1|{TOKEN}|world1-test|{SECTOR}|bubble|{slot}|{nonce*32}'.encode(), address, 0)[0].decode().split('|')[3]
        self.a, self.b = join('p1', P1, '1'), join('p2', P2, '2')
        self.commit(1, [row()])
        self.send(f'UINTEREST|1|{self.b}|1|{SECTOR}', P2)
        self.send(f'WORLD|1|{self.a}|1|'+ '|'.join(map(str, WORLD)))
        self.send(f'CREW|1|{self.a}|1|1|1')
        self.send(f'CREW|1|{self.b}|1|2|2', P2)
        self.s.snapshots(.11)
        self.sg = self.s.health_gen[2]
        self.send(f'HACK|1|{self.b}|2|{self.sg}', P2)
        self.hp()

    def send(self, packet, address=P1, now=.12):
        return self.s.receive(packet.encode(), address, now)

    def commit(self, revision, rows):
        b = Baseline.create(self.a, revision, SECTOR, rows)
        for i in range(len(rows)+2): self.s.receive(b.packet(self.a, i), P1, .05)

    def hp(self, seq=1, gen=1, hull=100, alive=1, code='NPC-101', address=P1):
        self.send(f'NHP|1|{self.a}|{self.a}|1|{gen}|{seq}|{SECTOR}|{code}|ship_arg_s_scout_01_a_macro|argon|{hull}|80|{alive}', address)

    def claim(self, seq=1, count=2, gen=1, sg=None, digest=DIGEST, address=P2):
        self.send(f'NCLAIM|1|{self.b}|{self.a}|1|{gen}|2|{self.sg if sg is None else sg}|{seq}|{count}|{digest}', address)

    def frames(self, kind, now=.3, address=P1):
        return [p.decode().split('|') for a,p in self.s.snapshots(now) if a == address and p.startswith((kind+'|').encode())]

    def test_cumulative_claim_preserves_hits_when_intermediate_datagrams_are_lost(self):
        self.claim(1, 2); self.claim(2, 5)
        f = self.frames('NCLAIM')[0]
        self.assertEqual(f[2:5], [self.a, self.b, self.a])
        self.assertEqual(f[10], '5')
        self.assertEqual(self.frames('NCLAIM', .41)[0][10], '5')

    def test_only_native_host_ack_completes_and_lost_ack_is_repeated(self):
        self.claim(count=5)
        ack = f'NACK|1|{self.a}|{self.a}|{self.b}|1|1|2|{self.sg}|5'
        self.send(ack, FOREIGN); self.assertEqual(len(self.frames('NCLAIM')), 1)
        self.send(ack)
        self.assertFalse(self.frames('NCLAIM', .41))
        self.assertEqual(self.frames('NACK', .52, P2)[0][-1], '5')
        self.assertEqual(self.frames('NACK', .63, P2)[0][-1], '5')

    def test_bad_epoch_pilot_generation_digest_and_reordering_do_not_raise_total(self):
        self.claim(count=5)
        for kw in (dict(seq=0,count=9), dict(seq=2,count=4), dict(seq=3,count=9,sg=99),
                   dict(seq=4,count=9,gen=2), dict(seq=5,count=9,address=FOREIGN),
                   dict(seq=6,count=9,digest='weapon_gen_s_laser_01_mk2_macro:46.000')):
            self.claim(**kw)
        self.assertEqual(self.frames('NCLAIM')[0][10], '5')
        self.send(f'CREW|1|{self.b}|2|0|0', P2, .31)
        self.assertFalse(self.frames('NCLAIM', .42))

    def test_partial_baseline_and_absence_are_not_health_or_death_authority(self):
        self.claim(); self.commit(2, [])
        self.assertFalse(self.frames('NCLAIM'))
        self.assertTrue(self.s.npc_combat.states[1]['alive'])
        self.hp(seq=2, hull=0, alive=0)
        self.assertFalse(self.s.npc_combat.states[1]['alive'])
        self.commit(3, [row()]); self.hp(seq=3)
        self.assertFalse(self.s.npc_combat.states[1]['alive'])

    def test_identity_and_nonfinite_health_are_refused(self):
        self.hp(seq=2, code='NEW-101'); self.hp(seq=3, hull=float('nan'))
        self.hp(seq=4, hull=-1); self.hp(seq=5, address=FOREIGN)
        self.assertEqual(self.s.npc_combat.states[1]['seq'], 1)

    def test_shrinking_health_list_and_expiry_are_bounded(self):
        self.assertEqual(len(self.frames('NHP',address=P2)),1)
        self.assertEqual(self.frames('NHP',now=6,address=P2),[])

    def test_guest_interest_in_another_sector_does_not_require_host_proximity(self):
        far='cluster_01_sector001_macro'
        b=Baseline.create(self.a,2,far,[row(9)])
        for i in range(3):self.s.receive(b.packet(self.a,i),P1,.15)
        self.send(f'UINTEREST|1|{self.b}|2|{far}',P2,.16)
        self.send(f'NHP|1|{self.a}|{self.a}|9|1|2|{far}|FAR-009|ship_arg_s_scout_01_a_macro|argon|100|80|1',now=.17)
        self.send(f'NCLAIM|1|{self.b}|{self.a}|9|1|2|{self.sg}|1|5|{DIGEST}',P2,.18)
        out = [(a, p.decode().split('|')) for a, p in self.s.snapshots(.3)]
        self.assertEqual([f for a, f in out if a == P1 and f[0] == 'NCLAIM'][0][5], '9')
        self.assertEqual([f for a, f in out if a == P2 and f[0] == 'NHP'][0][7], far)


@unittest.skipUnless(LuaRuntime, 'LuaJIT unavailable')
class NpcAdapterTests(unittest.TestCase):
    def make(self, slot='p2'):
        l = ua.UniverseAdapterTests().make(slot)
        l.execute('''
         X4LAN_CONFIG.npc_combat=true
         codes={};api.GetObjectIDCode=function(id) return codes[id] or 'NAT-'..id end
         A=X4LAN_BUBBLE_API
         A.health={mdStamp='2026-10-25T00:00:00',mdAge=100,mdAt=now,now=now,valid={3,4},closed={0,0},live={entity=2,gen=4},remote={[2]={gen=4}}}
         A.shot={support={[2]={digest='weapon_ter_s_plasma_01_mk1_macro:40.000',strength=40}},stateAck=1,stateRevision=1}
         A.shotCapture={ready=true};A.coop.owners={'p1','p2'};A.coop.pilots={'p1','p2'}
         function step() now=now+.11;A.health.mdAt=now;A.health.now=now;U.frame(now) end
         function actions(name) local t={};for _,v in ipairs(requests) do if v[1]=='X4LAN_NpcCombat' and v[2]==name then t[#t+1]=v[3] end end;return t end
        ''')
        l.execute((ROOT/'src/extension/x4_lan_lab/ui/npc_combat_game.lua').read_text())
        l.execute('''N=A.npcCombat;step();events['X4LAN_NpcCombat.Ready']()
         function hp(seq,hu,alive,code)
          U.receive({'NHP','1',client.lease,string.rep('a',32),'1','1',tostring(seq),'cluster_14_sector001_macro',code or 'HOST-101','ship_arg_s_scout_01_a_macro','argon',tostring(hu or 100),'80',alive or '1'},now)
         end
        ''')
        return l

    def guest(self):
        l=self.make();l.execute('baseline(1,{testRow},true);step();hp(1);step()');return l

    def test_health_needs_committed_baseline_and_native_binding(self):
        l=self.make();l.execute('''
         hp(1);step();assert(#actions('Bind')==0)
         baseline(1,{testRow},false);hp(2);step();assert(#actions('Bind')==0)
         baseline(1,{testRow},true);step();hp(3,70);step()
         local a=actions('Bind');assert(#a==1 and a[1].hull==70 and tonumber(a[1].object)==200 and a[1].code=='NAT-200')
         assert(not N.failed)
        ''')

    def test_host_death_suppresses_replica_and_stale_motion_cannot_respawn(self):
        self.guest().execute('''
         hp(2,0,'0');step();assert(#actions('Destroy')==1)
         positions[200]=nil -- The fenced native Destroy completes asynchronously.
         hp(3,100,'1');step();baseline(2,{testRow},true);step()
         assert(#spawns==2 and not positions[200] and not U.failed)
        ''')

    def test_terminal_health_does_not_depend_on_fresh_motion_or_baseline(self):
        self.guest().execute('''
         U.acceptedAt=now-30;hp(2,0,'0');step()
         assert(#actions('Destroy')==1 and not N.failed)
        ''')

    def test_changed_native_identity_or_owner_blocks_queued_health_bind(self):
        for change in ("codes[200]='NEW-200';codes[101]='NEW-101';codes[102]='NEW-102'", "container=200"):
            with self.subTest(change=change):
                self.guest().execute("requests={};"+change+";step();assert(#actions('Bind')==0)")

    def test_native_cumulative_counts_retransmit_and_ack_stops_them(self):
        self.guest().execute('''
         N.count('1|1|1|2|4|2');N.count('1|1|1|2|4|5');N.count('1|1|1|2|4|3')
         local p=N.claims['1|1|2|4'];assert(p.hits==5);step();assert(p.next)
         U.receive({'NACK','1',client.lease,string.rep('a',32),client.lease,'1','1','2','4','5'},now)
         assert(p.ack==5);A.health.valid[2]=5;step();assert(next(N.claims)==nil)
        ''')

    def test_shrinking_fleet_cannot_index_past_end_or_stop_motion(self):
        self.guest().execute('N.cursor=15;step();assert(not N.failed and not U.failed and #actions("Bind")>0)')

    def test_sector_return_rebinds_new_local_replica_without_reusing_queued_serial(self):
        self.guest().execute('''
         local serial=N.records[1].serial
         baseline(2,{},true);step();baseline(3,{testRow},true);step();hp(2);step()
         assert(N.records[1].object==201 and N.records[1].serial>serial and not N.failed)
         N.count('1|1|'..serial..'|2|4|99');assert(next(N.claims)==nil)
        ''')

    def test_host_uses_new_id_after_native_handle_reuse(self):
        self.make('p1').execute('''
         U.wanted={cluster_14_sector001_macro=now};for i=1,30 do step() end
         local first=U.publish[1].id;U.sender.done=true;U.nextScan=0
         codes[200]='NEW-200';codes[101]='NEW-101';codes[102]='NEW-102'
         for i=1,30 do step() end
         assert(U.publish[1].id>first and not U.failed and not N.failed)
        ''')

    def test_native_retirement_stops_old_readbacks_and_renews_the_world_identity(self):
        self.make('p1').execute('''
         U.wanted={cluster_14_sector001_macro=now};for i=1,30 do step() end
         local id=U.publish[1].id;local r=N.records[id]
         N.retired(id..'|1|'..r.serial);local seq=N.seq
         N.read(id..'|1|'..r.serial..'|100|100|1');assert(N.seq==seq)
         U.sender.done=true;U.nextScan=0;for i=1,30 do step() end
         assert(U.publish[1].id>id and r.retired and not N.failed)
        ''')
