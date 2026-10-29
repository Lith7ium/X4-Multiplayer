"""HEALTH relay generations and LuaJIT doubles for victim-pilot health; native acceptance remains required."""
import unittest
from pathlib import Path
from xml.etree import ElementTree
from x4lan.coop import CoopSession
from test_host import TOKEN, P1, P2, FOREIGN, hello
from test_game_adapter import MOCK, LuaRuntime
from test_bubble_adapter import EXTRA

ROOT = Path(__file__).resolve().parents[1]
UI = ROOT / 'src/extension/x4_lan_lab/ui'
WORLD = [100, 100, 0, 64.5, 30, 0, 100, 100, 100, 100, 200, 1, 1, 0, 0, 0, 0]


class HealthRelayTests(unittest.TestCase):
    def setUp(self):
        self.s = CoopSession(TOKEN, 'test', 'bubble')
        self.a = self.s.receive(hello(), P1, 0)[0].decode().split('|')[3]
        self.b = self.s.receive(hello('p2', '2' * 32), P2, 0)[0].decode().split('|')[3]
        self.s.receive(f'WORLD|1|{self.a}|1|{"|".join(map(str, WORLD))}'.encode(), P1, .05)
        self.crew(1, 2)
        self.s.snapshots(.12); self.g = self.s.health_gen[2]     # the generation p2 now pilots under

    def crew(self, seq, ship, now=.1):
        self.s.receive(f'CREW|1|{self.b}|{seq}|{ship}|{ship}'.encode(), P2, now)

    def health(self, seq=1, hull='80', shield='50', entity=2, gen=None, address=P2, now=.2):
        gen = self.g if gen is None else gen
        return self.s.receive(f'HEALTH|1|{self.b}|{entity}|{gen}|{seq}|{hull}|{shield}'.encode(), address, now)

    def ack(self, gen=None, entity=2, now=.15):
        gen = self.g if gen is None else gen
        self.s.receive(f'HACK|1|{self.b}|{entity}|{gen}'.encode(), P2, now)

    def rows(self, kind, now=.3, to=P1):
        return [p.decode().split('|') for a, p in self.s.snapshots(now) if a == to and p.startswith(kind + b'|')]

    def test_baseline_is_the_host_world_and_repeats_until_acknowledged(self):
        self.assertEqual(self.rows(b'HBASE', to=P2), [['HBASE', '1', self.b, '2', str(self.g), '64.500', '30.000']])
        self.assertEqual(self.rows(b'HBASE', to=P1), [])
        self.health()                                           # before the acknowledgement
        self.assertEqual(self.rows(b'HEALTH'), [])
        self.ack(gen=self.g + 1)                                # a wrong generation
        self.assertEqual(len(self.rows(b'HBASE', to=P2)), 1)
        self.ack()
        self.assertEqual(self.rows(b'HBASE', to=P2), [])
        self.health(seq=2)
        self.assertEqual(self.rows(b'HEALTH'), [['HEALTH', '1', self.a, self.b, '2', str(self.g), '2', '80', '50']])
        self.assertEqual(self.rows(b'HEALTH', to=P2), [])

    def test_a_stale_pilot_stream_ends_the_generation_and_rebases_on_host_world(self):
        # A:primary fixture: gen HEALTH hull 80, stale, host WORLD hull 50, same-gen HEALTH hull 80.
        self.ack(); self.health(seq=1, hull='80')
        self.rows(b'HEALTH', now=1.2)                           # 1.0 s without a pilot frame
        world = list(WORLD); world[3] = 50
        self.s.receive(f'WORLD|1|{self.a}|2|{"|".join(map(str, world))}'.encode(), P1, 1.4)
        self.health(seq=2, hull='80', now=1.5)
        self.assertEqual(self.rows(b'HEALTH', now=1.55), [])
        self.assertEqual(self.rows(b'HBASE', now=1.6, to=P2), [['HBASE', '1', self.b, '2', str(self.g + 1), '50.000', '30.000']])

    def test_host_stale_report_ends_only_the_current_generation(self):
        self.ack(); self.health(seq=1)
        self.s.receive(f'HSTALE|1|{self.b}|2|{self.g}'.encode(), P2, .25)   # not the host: ignored
        self.assertEqual(self.s.health_gen[2], self.g)
        self.s.receive(f'HSTALE|1|{self.a}|2|{self.g}'.encode(), P1, .3)
        self.s.receive(f'HSTALE|1|{self.a}|2|{self.g}'.encode(), P1, .32)   # repeat: already ended
        self.health(seq=2, now=.35)
        self.assertEqual((self.s.health_gen[2], self.rows(b'HEALTH', now=.4)), (self.g + 1, []))

    def test_hgen_names_the_generations_of_the_current_authority_revision(self):
        rows = self.rows(b'HGEN', to=P2)
        self.assertEqual(rows, [['HGEN', '1', self.b, str(self.s.authority_revision), str(self.s.health_gen[1]), str(self.g)]])
        before = self.s.authority_revision
        self.crew(2, 0, now=.3)
        row = self.rows(b'HGEN', now=.35, to=P1)[0]
        self.assertGreater(int(row[3]), before); self.assertEqual(row[5], str(self.g + 1))

    def test_frames_need_owner_pilot_live_generation_order_and_valid_values(self):
        self.ack()
        self.health(entity=1)
        self.health(address=FOREIGN)
        for hull, shield in (('nan', '0'), ('-1', '0'), ('101', '0'), ('x', '0')):
            self.health(hull=hull, shield=shield)
        self.assertEqual(self.rows(b'HEALTH'), [])
        self.health(seq=3, hull='70'); self.health(seq=3, hull='10'); self.health(seq=2, hull='10')
        self.assertEqual(self.rows(b'HEALTH')[0][7], '70')
        self.assertEqual(self.rows(b'HEALTH', now=1.2), [])     # stale after 0.75 s

    def test_leave_and_reenter_without_a_snapshot_still_closes_the_old_generation(self):
        self.ack(); self.health(seq=5)
        self.crew(2, 0, now=.30); self.crew(3, 2, now=.31)
        self.health(seq=6, now=.32)                             # generation 1 must be over
        self.assertEqual(self.rows(b'HEALTH', now=.33), [])
        self.assertEqual(self.s.health_gen[2], self.g + 2)

    def test_same_owner_leave_and_reenter_starts_a_new_generation(self):
        self.ack(); self.health(seq=5)
        self.crew(2, 0, now=.3)                                 # leaves the pilot seat
        self.assertEqual(self.rows(b'HEALTH', now=.35), [])
        self.crew(3, 2, now=.4)                                 # same owner and epoch, back aboard
        self.health(seq=6, now=.45)                             # old generation: refused
        self.assertEqual(self.rows(b'HEALTH', now=.5), [])
        self.assertEqual(self.rows(b'HBASE', now=.5, to=P2)[0][4], str(self.g + 2))
        self.ack(gen=self.g + 2, now=.55); self.health(seq=1, gen=self.g + 2, now=.6)
        self.assertEqual(self.rows(b'HEALTH', now=.65)[0][5], str(self.g + 2))


@unittest.skipUnless(LuaRuntime, 'LuaJIT unavailable')
class HealthAdapterTests(unittest.TestCase):
    def make(self, slot, clock=True):
        l = LuaRuntime(unpack_returned_tuples=True); l.execute(MOCK + EXTRA)
        g = l.globals(); g.X4LAN_CONFIG.slot = slot; g.X4LAN_CONFIG.coop = True; g.X4LAN_CONFIG.pilot_health = True
        l.execute('''
         sectors={[11]=22,[42]=22};ConvertIDTo64Bit=function(v) return tonumber(v) end
         api.GetContextByClass=function(object) return sectors[object] or 22 end
         GetComponentData=function() return 'cluster_14_sector001_macro' end
        ''')
        for file in ('bubble_game.lua', 'motion.lua', 'coop_game.lua', 'health_game.lua'):
            l.execute((UI / file).read_text())
        l.execute('''events['X4LAN.Loaded']();now=10;frames.onUpdate();events['X4LAN_Bubble.Ready']();frames.onUpdate();events['X4LAN_Coop.Ready']()
         function auth(rev,e2,p2) client:receiveExtension({'AUTH','1',client.lease,tostring(rev),'p1','1','p1','p2',tostring(e2),p2},now) end
         function hgen(rev,g1,g2) client:receiveExtension({'HGEN','1',client.lease,tostring(rev),tostring(g1),tostring(g2)},now) end
         function base(gen) client:receiveExtension({'HBASE','1',client.lease,'2',tostring(gen),'64.5','30'},now) end
         function own(age,e,gen,hull,shield) events['X4LAN_Health.Own'](nil,table.concat({age,'2026-10-23T23:00:00',e,gen,hull,shield},'|')) end
         function sentKind(kind) local out={};for _,p in ipairs(sent) do if p:sub(1,#kind+1)==kind..'|' then out[#out+1]=p end end;return out end
         function last(name) local r;for _,q in ipairs(requests) do if q[1]=='X4LAN_Health' and q[2]==name then r=q[3] end end;return r end
         function order(...) local want={};for _,n in ipairs({...}) do want[n]=true end
          local out={};for _,q in ipairs(requests) do if q[1]=='X4LAN_Health' and want[q[2]] then out[#out+1]=q[2]..':'..tostring(q[3].gen) end end
          return table.concat(out,',') end
         auth(1,1,'p2')
         now=10.1;frames.onUpdate();assert(countRequest('Setup')>=1);events['X4LAN_Health.Ready']()
        ''')
        if clock:
            l.execute("own('50',0,0,0,0)")
        l.execute('frames.onUpdate()')
        return l

    def test_guest_adopts_the_valid_baseline_once_then_acknowledges_and_reports(self):
        out = self.make('p2').execute('''
         own('50',2,0,90,50);now=10.2;frames.onUpdate();local early=#sentKind('HEALTH')
         base(4);local unannounced=countRequest('Base')            -- not yet valid for this revision
         hgen(1,1,4);base(4);base(4);local b=last('Base');local bases=countRequest('Base')
         events['X4LAN_Health.Based'](nil,'2|4');local a=last('Authority')
         base(4);local acks=sentKind('HACK')
         own('50.1',2,0,64.5,30);now=10.25;frames.onUpdate();local crossed=#sentKind('HEALTH')  -- pre-baseline readback
         own('50.2',2,4,64.5,30);now=10.3;frames.onUpdate()
         own('50.25',2,4,64.5,30);now=10.35;frames.onUpdate()     -- paced
         own('50.3',2,4,64.5,20);now=10.36;frames.onUpdate()      -- shield drop: at once
         local h=sentKind('HEALTH')
         return early,unannounced,b.gen,b.hull,b.deadline,bases,a.gen,a.own,#acks,acks[1],crossed,#h,h[1],h[2]''')
        early, unannounced, gen, hull, deadline, bases, agen, own, nacks, ack, crossed, count, first, second = out
        self.assertEqual((early, unannounced, gen, hull, deadline, bases, agen, own, nacks, crossed, count),
                         (0, 0, 4, 64.5, 51, 1, 4, True, 1, 0, 2))
        self.assertTrue(ack.endswith('|2|4'), ack)
        self.assertTrue(first.endswith('|2|4|1|64.500|30.000'), first)
        self.assertTrue(second.endswith('|2|4|2|64.500|20.000'), second)

    def test_guest_acknowledges_on_based_and_reports_only_after_it(self):
        out = self.make('p2').execute('''
         hgen(1,1,4);base(4);own('50.1',2,0,64.5,30);now=10.15;frames.onUpdate()
         local before=#sentKind('HEALTH')
         events['X4LAN_Health.Based'](nil,'2|4');local acks=sentKind('HACK')
         own('50.2',2,4,64.5,30);now=10.2;frames.onUpdate()
         local first=nil;for i,p in ipairs(sent) do if p:sub(1,5)=='HACK|' and not first then first=i end end
         local health=nil;for i,p in ipairs(sent) do if p:sub(1,7)=='HEALTH|' and not health then health=i end end
         return before,#acks,first<health''')
        self.assertEqual(tuple(out), (0, 1, True))                  # HACK at Based, before any HEALTH

    def test_guest_auth_transition_revokes_at_once_and_never_readopts(self):
        # A:primary: base5/Based, AUTH pilot 0, AUTH pilot p2, delayed base5 queued a second Base5.
        out = self.make('p2').execute('''
         hgen(1,1,5);base(5);events['X4LAN_Health.Based'](nil,'2|5')
         auth(2,1,'0');local revoked=last('Authority');auth(3,1,'p2')
         base(5);hgen(1,1,5);base(5)                               -- delayed frames of the old revision
         events['X4LAN_Health.Based'](nil,'2|5')
         own('51',2,5,10,0);now=10.4;frames.onUpdate()
         local stalled=order('Base');hgen(3,1,7);base(7)
         return revoked.gen,revoked.own,revoked.ended,stalled,order('Base'),#sentKind('HEALTH'),X4LAN_BUBBLE_API.health.live''')
        self.assertEqual(tuple(out), (0, False, 5, 'Base:5', 'Base:5,Base:7', 0, None))   # the revocation names gen 5

    def test_guest_never_readopts_an_older_baseline(self):
        out = self.make('p2').execute('''
         hgen(1,1,5);base(5);events['X4LAN_Health.Based'](nil,'2|5');now=10.2;frames.onUpdate()
         base(4)
         return X4LAN_BUBBLE_API.health.live.gen,X4LAN_BUBBLE_API.health.basing,order('Base','Authority')''')
        self.assertEqual(tuple(out), (5, None, 'Authority:5,Base:5,Authority:5'))
        md = (ROOT / 'src/extension/x4_lan_lab/md/x4lan_health.xml').read_text()
        self.assertTrue('$a.$gen == $arena.$healthAuth.{$a.$entity} and player.timewarp.factor gt 0s' in md, '$a.$gen == $arena.$healthAuth.{$a.$entity} and player.timewarp.factor gt 0s')

    def test_host_applies_valid_generations_in_order_and_releases(self):
        out = self.make('p1').execute('''
         local src=string.rep('c',32)
         function hp(gen,seq,hull) client:receiveExtension({'HEALTH','1',client.lease,src,'2',tostring(gen),tostring(seq),hull,'50'},now) end
         hp(3,1,'90');local early=countRequest('Apply')             -- not yet valid
         hgen(1,1,3);hp(3,2,'80');local a=last('Apply')
         hp(3,2,'10');hp(3,1,'10');hp(2,9,'10')                   -- duplicate, older, older generation
         client:receiveExtension({'HEALTH','1',client.lease,src,'1','1','4','10','0'},now)   -- the host's own ship
         local applies=countRequest('Apply')
         hgen(1,1,4);local superseded=last('Release')             -- a newer generation ends generation 3
         hp(4,1,'1.0');local lethal=last('Apply')
         now=10.5;frames.onUpdate();local fresh=countRequest('Release')
         now=11.6;frames.onUpdate();local stale=last('Release')
         own('51.5',0,0,0,0);hp(4,2,'60');auth(2,2,'p2')            -- the stale generation never resumes
         return early,a.gen,a.hull,a.deadline,applies,superseded.gen,tostring(superseded.closed),lethal.gen,lethal.hull,fresh,stale.gen,tostring(stale.closed),order('Release'),countRequest('Apply')''')
        self.assertEqual(tuple(out), (0, 3, 80, 51, 1, 3, 'true', 4, 1.0, 1, 4, 'true', 'Release:3,Release:4', 2))

    def test_host_auth_transition_without_a_frame_closes_the_generation(self):
        # A:primary: HEALTH gen5 seq1, AUTH pilot 0 then p2 before any frame, delayed gen5 seq2.
        out = self.make('p1').execute('''
         local src=string.rep('c',32)
         local function hp(gen,seq,hull) client:receiveExtension({'HEALTH','1',client.lease,src,'2',tostring(gen),tostring(seq),hull,'0'},now) end
         hgen(1,1,5);hp(5,1,'40');auth(2,1,'0');auth(3,1,'p2');hp(5,2,'1.0');hgen(1,1,5);hp(5,3,'1.0')
         return order('Apply','Release'),tostring(last('Release').closed)''')
        self.assertEqual(tuple(out), ('Apply:5,Release:5', 'true'))
        md = (ROOT / 'src/extension/x4_lan_lab/md/x4lan_health.xml').read_text()
        self.assertTrue('$a.$gen gt $arena.$healthClosed.{$e} and $a.$gen ge $arena.$healthGen.{$e} and player.timewarp.factor gt 0s' in md, '$a.$gen gt $arena.$healthClosed.{$e} and $a.$gen ge $arena.$healthGen.{$e} and player.timewarp.factor gt 0s')
        health_xml = ElementTree.fromstring(md)
        closed_guards = [
            node for node in health_xml.iter('do_if')
            if node.get('value') == 'event.param3.$closed'
        ]
        self.assertTrue(any(
            len(guard) > 0
            and guard[0].tag == 'set_value'
            and guard[0].get('name') == '$arena.$healthClosed.{$e}'
            for guard in closed_guards
        ))
        self.assertTrue('exact="$arena.$healthValue.{$e}.{1}"' in md, 'exact="$arena.$healthValue.{$e}.{1}"')  # release restores the canonical value

    def test_writes_carry_the_window_of_a_fresh_md_readback(self):
        out = self.make('p1').execute('''
         now=10.4;frames.onUpdate();own('50.5',0,0,0,0)
         hgen(1,1,3);client:receiveExtension({'HEALTH','1',client.lease,string.rep('c',32),'2','3','1','80','50'},now)
         local a=last('Apply')
         now=11.0;frames.onUpdate()                               -- readback now 0.6 s old: not writable
         client:receiveExtension({'HEALTH','1',client.lease,string.rep('c',32),'2','3','2','70','50'},now)
         return a.stamp,a.from,a.deadline,countRequest('Apply')''')
        self.assertEqual(tuple(out), ('2026-10-23T23:00:00', 50.5, 51.5, 1))
        md = (ROOT / 'src/extension/x4_lan_lab/md/x4lan_health.xml').read_text()
        fresh = ("player.timewarp.factor gt 0s and player.systemtime.{'%Y-%m-%dT%H:%M:%S'} == $a.$stamp "
                 "and player.age / 1s ge $a.$from and player.age / 1s le $a.$deadline")
        self.assertEqual(md.count(fresh), 3)                       # Base, Apply and Authority(own)
        self.assertTrue('<check_value value="player.timewarp.factor gt 0s"/>' in md, '<check_value value="player.timewarp.factor gt 0s"/>')   # unpaused readback
        self.assertNotIn('Blackboard', (UI / 'health_game.lua').read_text())

    def test_host_closes_natively_even_after_a_stale_release(self):
        out = self.make('p1').execute('''
         hgen(1,1,3);client:receiveExtension({'HEALTH','1',client.lease,string.rep('c',32),'2','3','1','80','50'},now)
         now=11.3;frames.onUpdate();local stale=sentKind('HSTALE')
         own('51.3',0,0,0,0);client:receiveExtension({'HEALTH','1',client.lease,string.rep('c',32),'2','3','2','80','50'},now)
         auth(2,1,'0')
         return order('Apply','Release'),tostring(last('Release').closed),#stale,stale[1]''')
        order, closed, nstale, stale = out
        self.assertEqual((order, closed, nstale), ('Apply:3,Release:3', 'true', 1))   # A:primary: Apply:3,Release:3,Apply:3 before
        self.assertTrue(stale.endswith('|2|3'), stale)
        md = (ROOT / 'src/extension/x4_lan_lab/md/x4lan_health.xml').read_text()
        self.assertTrue('<do_if value="$arena.$healthLive.{$i} and player.age - $arena.$healthAt.{$i} gt 2s">' in md, '<do_if value="$arena.$healthLive.{$i} and player.age - $arena.$healthAt.{$i} gt 2s">')  # watchdog

    def test_guest_retries_an_unconfirmed_base_and_md_writes_it_once(self):
        out = self.make('p2').execute('''
         hgen(1,1,4);base(4);now=10.3;base(4);local early=countRequest('Base')
         now=10.7;own('50.6',0,0,0,0);base(4);return early,countRequest('Base')''')
        self.assertEqual(tuple(out), (1, 2))
        md = (ROOT / 'src/extension/x4_lan_lab/md/x4lan_health.xml').read_text()
        self.assertTrue('<do_if value="$arena.$healthBased.{$a.$entity} != $a.$gen">' in md, '<do_if value="$arena.$healthBased.{$a.$entity} != $a.$gen">')

    def test_guest_reports_each_fresh_reading_once_and_never_while_paused(self):
        out = self.make('p2').execute('''
         hgen(1,1,4);base(4);events['X4LAN_Health.Based'](nil,'2|4')
         own('51',2,4,80,50);for i=1,8 do now=10.1+i*.1;frames.onUpdate() end
         local once=#sentKind('HEALTH')
         X4LAN_BUBBLE_API.paused=function() return true end
         own('52',2,4,70,50);now=11.0;frames.onUpdate();local paused=#sentKind('HEALTH')
         X4LAN_BUBBLE_API.paused=function() return false end
         now=11.5;frames.onUpdate();local stale=#sentKind('HEALTH')
         own('53',2,4,60,50);now=11.6;frames.onUpdate()
         return once,paused,stale,#sentKind('HEALTH'),last('Keep').gen''')
        self.assertEqual(tuple(out), (1, 1, 1, 2, 4))
        md = (ROOT / 'src/extension/x4_lan_lab/md/x4lan_health.xml').read_text()
        self.assertTrue('<do_if value="$arena.$healthOwn.{$i} and player.age - $arena.$healthKeptAt.{$i} gt 2s">' in md)

    def test_md_watchdog_expiry_closes_the_generation_on_both_sides(self):
        host = self.make('p1').execute('''
         hgen(1,1,3);client:receiveExtension({'HEALTH','1',client.lease,string.rep('c',32),'2','3','1','80','50'},now)
         events['X4LAN_Health.Expired'](nil,'2|3');now=10.2;frames.onUpdate()
         own('50.2',0,0,0,0);client:receiveExtension({'HEALTH','1',client.lease,string.rep('c',32),'2','3','2','80','50'},now)
         return order('Apply','Release'),tostring(last('Release').closed),#sentKind('HSTALE')''')
        self.assertEqual(tuple(host), ('Apply:3,Release:3', 'true', 1))
        guest = self.make('p2').execute('''
         hgen(1,1,4);base(4);events['X4LAN_Health.Based'](nil,'2|4');events['X4LAN_Health.Expired'](nil,'2|4')
         base(4);return X4LAN_BUBBLE_API.health.live,last('Authority').gen,countRequest('Base')''')
        self.assertEqual(tuple(guest), (None, 0, 1))
        md = (ROOT / 'src/extension/x4_lan_lab/md/x4lan_health.xml').read_text()
        self.assertTrue('exact="[$arena.$healthClosed.{$i},$arena.$healthGen.{$i}].max"' in md)
        self.assertEqual(md.count("'X4LAN_Health.Expired'"), 2)

    def test_guest_authority_is_fenced_against_a_queued_old_own(self):
        out = self.make('p2').execute('''
         hgen(1,1,4);base(4);events['X4LAN_Health.Based'](nil,'2|4');local a=last('Authority')
         return a.gen,tostring(a.own),a.stamp,a.from,a.deadline''')
        self.assertEqual(tuple(out), (4, 'true', '2026-10-23T23:00:00', 50, 51))
        md = (ROOT / 'src/extension/x4_lan_lab/md/x4lan_health.xml').read_text()
        fresh = ("player.timewarp.factor gt 0s and player.systemtime.{'%Y-%m-%dT%H:%M:%S'} == $a.$stamp "
                 "and player.age / 1s ge $a.$from and player.age / 1s le $a.$deadline")
        own = '$a.$gen gt $arena.$healthClosed.{$e} and $a.$gen == $arena.$healthAuth.{$e} and @$a.$stamp and ' + fresh
        self.assertTrue(own in md, 'own=true needs gen above the closed fence, its own gen and a fresh window')
        self.assertTrue('<do_elseif value="$a.$gen gt $arena.$healthClosed.{$e} and $a.$gen gt $arena.$healthTop.{$e} and not $a.$own">' in md)
        self.assertTrue('exact="[$arena.$healthClosed.{$i},$arena.$healthAuth.{$i}].max"' in md)   # keepalive watchdog fence
        self.assertEqual(md.count(fresh), 3)                       # Authority(own), Base, Apply

    def test_a_failed_guest_closes_at_once_and_never_resumes(self):
        # A:primary guest01: after HEALTH STOPPED, fresh Base/Based re-armed the WORLD guard.
        out = self.make('p2').execute('''
         hgen(1,1,4);base(4);events['X4LAN_Health.Based'](nil,'2|4')
         local H=X4LAN_BUBBLE_API.health;H.frame=function() error('induced') end
         now=10.2;frames.onUpdate();local revoked=last('Authority')
         local n0,s0=#requests,#sent
         hgen(1,1,5);base(5);events['X4LAN_Health.Based'](nil,'2|5');own('51',2,5,60,20)
         client:receiveExtension({'HBASE','1',client.lease,'2','5','64.5','30'},now)
         now=10.3;frames.onUpdate();now=10.4;frames.onUpdate()
         local health=0;for i=n0+1,#requests do if requests[i][1]=='X4LAN_Health' then health=health+1 end end
         local packets=0;for i=s0+1,#sent do local k=sent[i]:match('^(%u+)|');if k=='HACK' or k=='HEALTH' then packets=packets+1 end end
         return H.failed,revoked.gen,revoked.ended,health,packets,H.live''')
        self.assertEqual(tuple(out), (True, 0, 4, 0, 0, None))

    def test_a_failed_host_closes_its_remote_generation_and_applies_nothing(self):
        out = self.make('p1').execute('''
         local src=string.rep('c',32)
         hgen(1,1,3);client:receiveExtension({'HEALTH','1',client.lease,src,'2','3','1','80','50'},now)
         local H=X4LAN_BUBBLE_API.health;H.frame=function() error('induced') end
         now=10.2;frames.onUpdate();local rel=last('Release')
         hgen(1,1,4);client:receiveExtension({'HEALTH','1',client.lease,src,'2','4','1','70','50'},now)
         return rel.gen,tostring(rel.closed),order('Apply'),#sentKind('HSTALE')''')
        self.assertEqual(tuple(out), (3, 'true', 'Apply:3', 0))

    def test_no_queued_write_without_an_md_clock(self):
        out = self.make('p1', clock=False).execute('''
         hgen(1,1,3);client:receiveExtension({'HEALTH','1',client.lease,string.rep('c',32),'2','3','1','80','50'},now)
         return countRequest('Apply')''')
        self.assertEqual(out, 0)

    def test_disabled_flag_registration_and_bubble_guards(self):
        l = LuaRuntime(unpack_returned_tuples=True); l.execute(MOCK + EXTRA)
        l.globals().X4LAN_CONFIG.coop = True
        for file in ('bubble_game.lua', 'motion.lua', 'coop_game.lua', 'health_game.lua'):
            l.execute((UI / file).read_text())
        self.assertIsNone(l.eval('X4LAN_BUBBLE_API.health'))
        ui = (ROOT / 'src/extension/x4_lan_lab/ui.xml').read_text()
        self.assertLess(ui.index('ui/coop_game.lua'), ui.index('ui/health_game.lua'))
        self.assertIn('pilot_health = false', (UI / 'config.lua').read_text())
        bubble = (ROOT / 'src/extension/x4_lan_lab/md/x4lan_bubble.xml').read_text()
        self.assertEqual(bubble.count('@$healthLive.{'), 4)       # Publish: hull and shield of both ships
        bubble_xml = ElementTree.fromstring(bubble)
        own_guards = [
            node for node in bubble_xml.iter('do_if')
            if node.get('value') == 'not @Arena.$healthOwn.{$i}'
        ]
        self.assertTrue(any(
            len(guard) > 0
            and guard[0].tag == 'do_if'
            and guard[0].get('value', '').startswith('($ship.hullpercentage - ')
            for guard in own_guards
        ))  # guest skips its own ship; writes only changes
        change_guards = [
            node for node in bubble_xml.iter('do_if')
            if node.get('value', '').endswith(' gt 0.25')
            and len(node) > 0 and node[0].tag.startswith('set_object_')
        ]
        self.assertEqual(len(change_guards), 6)  # ships, targets and ApplyExtra hull/shield
        self.assertTrue('le (if @Arena.$healthOwn.{$i} then 0 else 1.01)' in bubble, 'le (if @Arena.$healthOwn.{$i} then 0 else 1.01)')
        guards = (ROOT / 'tools/dev/life_guards.py').read_text()          # the generator keeps the same guards
        for text in ('@$healthLive.{%d}', "'not @Arena.$healthOwn.{$i}'", 'le (if @Arena.$healthOwn.{$i} then 0 else 1.01)'):
            self.assertTrue(text in guards, text)


if __name__ == '__main__':
    unittest.main()
