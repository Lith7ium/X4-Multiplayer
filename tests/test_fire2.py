"""FIRE2 relay validation and LuaJIT playback doubles; native two-PC visual checks remain required."""
import unittest
from pathlib import Path
from x4lan.coop import CoopSession
from test_host import TOKEN, P1, P2, FOREIGN, hello
from test_game_adapter import MOCK, LuaRuntime
from test_bubble_adapter import EXTRA

UI = Path(__file__).resolve().parents[1] / 'src/extension/x4_lan_lab/ui'


class Fire2RelayTests(unittest.TestCase):
    def setUp(self):
        self.s = CoopSession(TOKEN, 'test', 'bubble')
        self.a = self.s.receive(hello(), P1, 0)[0].decode().split('|')[3]
        self.b = self.s.receive(hello('p2', '2' * 32), P2, 0)[0].decode().split('|')[3]

    def fire(self, seq=1, time='10', groups=('1', '1'), shots=('0', '0'), epoch=1, entity=1, address=P1, lease=None, now=.2):
        lease = lease or self.a
        packet = f'FIRE2|1|{lease}|{entity}|{epoch}|{seq}|{time}|{groups[0]}|{groups[1]}|{shots[0]}|{shots[1]}'
        return self.s.receive(packet.encode(), address, now)

    def forwarded(self, now=.3, to=P2):
        return [p.decode().split('|') for a, p in self.s.snapshots(now) if a == to and p.startswith(b'FIRE2|')]

    def test_owner_frame_is_forwarded_with_source_lease_and_fields(self):
        self.fire(shots=('3', '1'))
        rows = self.forwarded()
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0][3], self.a)
        self.assertEqual(rows[0][4:], ['1', '1', '1', '10', '1', '1', '3', '1'])
        self.assertEqual(self.forwarded(to=P1), [])

    def test_foreign_wrong_owner_epoch_and_bad_fields_are_dropped(self):
        self.fire(address=FOREIGN)
        self.fire(entity=2)
        self.fire(epoch=2)
        for bad in (dict(groups=('0', '1')), dict(groups=('1', '5')), dict(time='nan'), dict(time='-1'),
                    dict(shots=('-1', '0')), dict(shots=('x', '0'))):
            self.fire(**bad)
        self.assertFalse(self.s.fire2)

    def test_sequence_time_and_counters_never_go_backwards(self):
        self.fire(seq=2, time='10', shots=('4', '4'))
        old = dict(self.s.fire2[1])
        for bad in (dict(seq=2, time='11', shots=('5', '4')), dict(seq=3, time='10', shots=('5', '4')),
                    dict(seq=3, time='11', shots=('3', '4'))):
            self.fire(**bad)
            self.assertEqual(self.s.fire2[1], old)
        self.fire(seq=3, time='11', shots=('5', '4'))
        self.assertEqual(self.s.fire2[1]['seq'], 3)

    def test_stale_or_superseded_epoch_is_not_forwarded(self):
        self.fire()
        self.assertEqual(len(self.forwarded(.3)), 1)
        self.assertEqual(self.forwarded(1.0), [])
        self.fire(seq=2, time='11', now=1.1)
        self.s.epochs[1] = 2
        self.assertEqual(self.forwarded(1.2), [])




@unittest.skipUnless(LuaRuntime, 'LuaJIT unavailable')
class Fire2AdapterTests(unittest.TestCase):
    def make(self, groups_api=True, fire2=True):
        l = LuaRuntime(unpack_returned_tuples=True)
        l.execute(MOCK + EXTRA)
        l.globals().X4LAN_CONFIG.slot = 'p1'
        l.globals().X4LAN_CONFIG.coop = True
        if fire2 is not None:
            l.execute('X4LAN_CONFIG.fire2=%s' % ('true' if fire2 else 'false'))
        l.execute('noGroups=%s' % ('false' if groups_api else 'true'))
        l.execute('''
         sectors={[11]=22,[42]=22};ConvertIDTo64Bit=function(v) return tonumber(v) end
         api.GetContextByClass=function(object) return sectors[object] or 22 end
         GetComponentData=function() return 'cluster_14_sector001_macro' end
         api.SetObjectSectorPos=function(object,sector,p) positions[object]=p end
         groups={[11]={1,1},[42]={1,1}};groupWrites={}
         api.GetDefensibleActiveWeaponGroup=function(ship,primary) return groups[ship][primary and 1 or 2] end
         api.SetDefensibleActiveWeaponGroup=function(ship,primary,g) groups[ship][primary and 1 or 2]=g;groupWrites[#groupWrites+1]={ship,primary,g} end
         if noGroups then api.GetDefensibleActiveWeaponGroup=nil;api.SetDefensibleActiveWeaponGroup=nil end
        ''')
        for file in ('bubble_game.lua', 'motion.lua', 'coop_game.lua', 'life_game.lua'):
            l.execute((UI / file).read_text())
        l.execute('''events['X4LAN.Loaded']();now=10;frames.onUpdate();events['X4LAN_Bubble.Ready']();frames.onUpdate();events['X4LAN_Coop.Ready']()
         C=X4LAN_BUBBLE_API.coop;C.sectors.cluster_14_sector001_macro=22
         function auth(rev,e2) client:receiveExtension({'AUTH','1',client.lease,tostring(rev),'p1','1','p1','p2',tostring(e2 or 1),'p2'},now) end
         function nav2(seq,time,fire,epoch)
          client:receiveExtension({'NAV2','1',client.lease,string.rep('b',32),'2',tostring(epoch or 1),tostring(seq),'cluster_14_sector001_macro',
           '1000','10000','0','0','0','0','0',tostring(fire or 0),tostring(time),'0','0','0','0','0','0'},now)
         end
         function fire2(seq,time,pg,sg,ps,ss,epoch)
          client:receiveExtension({'FIRE2','1',client.lease,string.rep('b',32),'2',tostring(epoch or 1),tostring(seq),tostring(time),
           tostring(pg),tostring(sg),tostring(ps),tostring(ss)},now)
         end
         function fires(name) local out={};for _,r in ipairs(requests) do if r[2]==name then out[#out+1]=r[3] end end;return out end
         function legacyOn() local n=0;for _,v in ipairs(fires('Fire')) do if v.on then n=n+1 end end;return n end
         function logged(text) for _,v in ipairs(logs) do if v:find(text,1,true) then return true end end;return false end
         function sentFire2(from) for i=from or 1,#sent do if sent[i]:find('^FIRE2|') then return sent[i] end end end
         auth(1)
        ''')
        return l

    # --- mode contract: exactly one fire protocol per session ---

    def test_fire2_mode_sends_classified_triggers_and_never_encodes_unknown_group(self):
        l = self.make()
        l.execute('''
         events['X4LAN_Coop.Shot'](nil,'1|p');events['X4LAN_Coop.Shot'](nil,'1|s');events['X4LAN_Coop.Shot'](nil,'1|s');events['X4LAN_Coop.Shot'](nil,1)
         groups[11]={2,3};now=11;frames.onUpdate()
         local f={};for v in sentFire2():gmatch('[^|]+') do f[#f+1]=v end
         assert(#f==11 and f[4]=='1' and f[5]=='1' and f[8]=='2' and f[9]=='3' and f[10]=='2' and f[11]=='2')
         assert(C.shots[1]==4)
         local n=#sent;groups[11]={0,1};now=12;frames.onUpdate()
         assert(sentFire2(n+1)==nil,'group 0 must not be encoded')
        ''')

    def test_legacy_mode_neither_sends_nor_accepts_fire2(self):
        for flag in (False, None):
            with self.subTest(fire2=flag):
                l = self.make(fire2=flag)
                l.execute('''
                 events['X4LAN_Coop.Shot'](nil,'1|p');now=11;frames.onUpdate();assert(sentFire2()==nil,'legacy mode never sends FIRE2')
                 nav2(1,50,0);fire2(1,50,1,1,0,0);fire2(2,50.05,1,1,1,0)
                 assert(next(C.fire2)==nil,'FIRE2 ignored in legacy mode')
                 now=11.05;nav2(2,50.05,1);frames.onUpdate();assert(legacyOn()==1 and #fires('Fire2')==0)
                ''')

    def test_fire2_mode_never_uses_nav_counter(self):
        l = self.make()
        l.execute('''
         nav2(1,50,0);now=10.05;nav2(2,50.05,1);frames.onUpdate()
         assert(legacyOn()==0 and not C.remote[2].firing,'no NAV fire before any FIRE2')
         fire2(1,50.05,1,1,1,0);now=10.1;nav2(3,50.1,2);fire2(2,50.1,1,1,2,0);now=10.3;frames.onUpdate()
         local f=fires('Fire2');assert(legacyOn()==0 and #f==1 and f[1].primary==true)
        ''')

    def test_fire2_mode_without_group_api_disables_remote_fire_with_diagnostic(self):
        l = self.make(groups_api=False)
        l.execute('''
         assert(logged('FIRE2_UNAVAILABLE'))
         nav2(1,50,0);fire2(1,50,1,1,0,0);now=10.05;nav2(2,50.05,1);fire2(2,50.05,1,1,1,0);now=10.3;frames.onUpdate()
         assert(next(C.fire2)==nil and legacyOn()==0 and #fires('Fire2')==0,'no silent protocol change')
         now=11;frames.onUpdate();assert(sentFire2()==nil)
        ''')

    # --- baseline, playback and reordering ---

    def test_baseline_adopts_counters_without_replay(self):
        l = self.make()
        l.execute('''
         nav2(1,50,3);fire2(1,50,1,1,7,2);now=10.3;frames.onUpdate()
         assert(#fires('Fire2')==0,'baseline must not replay counters')
        ''')

    def test_mixed_trigger_reorder_plays_each_new_shot_once(self):
        l = self.make()
        l.execute('''
         nav2(1,50,0);now=10.05;nav2(2,50.05,1);frames.onUpdate()
         fire2(1,50,1,1,0,0);now=10.10;nav2(3,50.10,2);fire2(2,50.10,1,1,1,1);now=10.26;frames.onUpdate()
         local f=fires('Fire2');assert(legacyOn()==0 and #f==1 and f[1].primary==true and f[1].secondary==true)
         now=10.3;fire2(3,50.3,1,1,1,1);now=10.7;frames.onUpdate()
         f=fires('Fire2');assert(f[#f].primary==false and f[#f].secondary==false,'no replay, released')
        ''')

    def test_trigger_edges_play_at_nav2_source_time_and_apply_groups(self):
        l = self.make()
        l.execute('''
         nav2(1,50,0);fire2(1,50,1,1,0,0)
         now=10.05;nav2(2,50.05,0);fire2(2,50.05,2,1,1,0)
         frames.onUpdate();assert(#fires('Fire2')==0,'edge waits for the motion playback delay')
         now=10.21;frames.onUpdate()
         local f=fires('Fire2');assert(#f==1 and f[1].primary==true and f[1].secondary==false)
         assert(f[1].primaryGroup==2 and f[1].secondaryGroup==1,'MD selects groups after stopping old credit')
         now=10.6;fire2(3,50.6,2,1,1,0);frames.onUpdate()
         f=fires('Fire2');assert(f[#f].primary==false,'trigger released after its deadline')
        ''')

    def test_stale_edges_and_silence_stop_fire_even_while_motion_freezes(self):
        l = self.make()
        l.execute('''
         nav2(1,50,0);fire2(1,50,1,1,0,0);now=10.05;fire2(2,50.05,1,1,0,1);now=10.3;frames.onUpdate()
         local f=fires('Fire2');assert(#f==1 and f[1].secondary==true)
         now=10.75;frames.onUpdate();f=fires('Fire2');assert(f[#f].secondary==false,'silence stops fire')
         local q=C.fire2[2];q.at=now;q.events={{time=40,trigger=1,group=1}};frames.onUpdate()
         assert(#q.events==0 and q.deadline[1]==0,'old queued edge dropped, never fired late')
        ''')

    def test_playback_grants_counter_deltas_including_split_volleys_once(self):
        l = self.make()
        l.execute('''
         nav2(1,50,0);fire2(1,50,1,1,0,0)
         now=10.05;nav2(2,50.05,4);fire2(2,50.05,1,1,4,0)
         now=10.21;frames.onUpdate();local f=fires('Fire2')
         assert(#f==1 and f[1].primaryShots==4 and f[1].secondaryShots==0 and f[1].epoch==1)
         now=10.25;nav2(3,50.25,6);fire2(3,50.25,1,1,6,0)
         now=10.41;frames.onUpdate();f=fires('Fire2')
         assert(#f==2 and f[2].primary and f[2].primaryShots==2,'grant even while already on')
         fire2(3,50.25,1,1,6,0);fire2(4,50.41,1,1,6,0)
         now=10.5;frames.onUpdate();assert(#fires('Fire2')==2,'no duplicate grant')
         now=10.9;frames.onUpdate();f=fires('Fire2')
         assert(#f==3 and not f[3].primary and f[3].primaryShots==0,'timeout cancels, never grants')
        ''')

    def test_group_switch_cancels_old_credit_and_leaves_other_trigger_alone(self):
        l = self.make()
        l.execute('''
         nav2(1,50,0);fire2(1,50,1,1,0,0)
         now=10.05;nav2(2,50.05,12);fire2(2,50.05,1,1,6,6)
         now=10.21;frames.onUpdate()
         now=10.25;nav2(3,50.25,12);fire2(3,50.25,2,1,6,6)
         now=10.41;frames.onUpdate();local f=fires('Fire2');local v=f[#f]
         assert(v.primaryGroup==2 and v.resetPrimary and not v.primary and v.primaryShots==0)
         assert(v.secondary and not v.resetSecondary and v.secondaryShots==0)
         now=10.42;nav2(4,50.42,14);fire2(4,50.42,2,1,8,6)
         now=10.58;frames.onUpdate();v=fires('Fire2')[#fires('Fire2')]
         assert(v.primary and v.primaryShots==2 and not v.secondary,'expired other trigger stays stopped')
        ''')

    def test_pending_budget_is_bounded_and_old_credit_expires_during_continuous_input(self):
        l = self.make()
        l.execute('''
         nav2(1,50,0);fire2(1,50,1,1,0,0)
         now=10.05;nav2(2,50.05,10000);fire2(2,50.05,1,1,10000,0)
         now=10.21;frames.onUpdate();local f=fires('Fire2')
         assert(f[1].primaryShots==256,'large counter jumps are bounded')
         now=10.4;nav2(3,50.4,10001);fire2(3,50.4,1,1,10001,0)
         now=10.57;frames.onUpdate();f=fires('Fire2');local v=f[#f]
         assert(v.resetPrimary and v.primaryShots==1,'expired credit is cancelled before new grant')
        ''')

    # --- pause ---

    def test_pause_drops_queued_active_and_paused_history(self):
        l = self.make()
        l.execute('''
         nav2(1,50,0);fire2(1,50,1,1,0,0)
         now=10.05;fire2(2,50.05,1,1,1,0);paused=true;now=10.12;frames.onUpdate()
         assert(#C.fire2[2].events==0)
         paused=false;now=10.21;frames.onUpdate();assert(#fires('Fire2')==0,'pre-pause edge resumed')
         now=10.3;nav2(2,50.3,0);fire2(3,50.3,1,1,2,0);now=10.5;frames.onUpdate();assert(fires('Fire2')[1].primary==true)
         paused=true;now=10.55;frames.onUpdate();local f=fires('Fire2');assert(f[#f].primary==false,'pause stops fire')
         now=10.6;fire2(4,50.6,1,1,5,0);assert(#C.fire2[2].events==0)
         paused=false;now=10.65;frames.onUpdate();fire2(5,50.62,1,1,5,0);now=10.9;frames.onUpdate()
         assert(#fires('Fire2')==#f,'no fire from paused history')
         now=10.95;nav2(3,50.95,0);fire2(6,50.95,1,1,6,0);now=11.15;frames.onUpdate()
         f=fires('Fire2');assert(f[#f].primary==true,'fresh post-resume shot plays')
        ''')

    def test_pre_pause_packet_arriving_after_resume_never_fires(self):
        l = self.make()
        l.execute('''
         nav2(1,50,0);fire2(1,50,1,1,0,0)
         paused=true;now=10.12;frames.onUpdate()
         paused=false;now=10.21;frames.onUpdate()
         fire2(2,50.05,1,1,1,0);now=10.22;frames.onUpdate();now=10.5;frames.onUpdate()
         assert(#fires('Fire2')==0,'in-flight pre-pause shot must not start fire')
         now=10.6;nav2(2,50.6,0);fire2(3,50.6,1,1,2,0);now=10.8;frames.onUpdate()
         local f=fires('Fire2');assert(#f==1 and f[1].primary==true,'subsequent fresh edge plays')
        ''')

    def test_resume_frame_packet_received_before_frame_never_fires(self):
        l = self.make()
        l.execute('''
         nav2(1,50,0);fire2(1,50,1,1,0,0)
         paused=true;now=10.12;frames.onUpdate()
         -- Resume: the client receives the delayed pre-pause packet before coop.frame runs.
         paused=false;now=10.21;fire2(2,50.05,1,1,1,0);frames.onUpdate();now=10.5;frames.onUpdate()
         assert(#fires('Fire2')==0,'pre-pause edge received in the resume frame must not fire')
         now=10.6;nav2(2,50.6,0);fire2(3,50.6,1,1,2,0);now=10.8;frames.onUpdate()
         assert(#fires('Fire2')==1,'subsequent fresh edge plays')
        ''')

    def test_baseline_created_after_resume_never_replays_old_edges(self):
        l = self.make()
        l.execute('''
         nav2(1,50,0)
         paused=true;now=10.12;frames.onUpdate();paused=false;now=10.21;frames.onUpdate()
         -- The first FIRE2 ever seen arrives after resume, followed by more pre-pause history.
         fire2(1,49.9,1,1,0,0);fire2(2,50.05,1,1,1,0);now=10.5;frames.onUpdate()
         assert(#fires('Fire2')==0,'old edges after a post-resume baseline must not fire')
         now=10.6;nav2(2,50.6,0);fire2(3,50.6,1,1,2,0);now=10.8;frames.onUpdate()
         assert(#fires('Fire2')==1,'fresh edge plays')
        ''')

    # --- authority ---

    def test_delayed_nav_reset_cannot_move_resume_boundary_backwards(self):
        for packet_first in (False, True):
            with self.subTest(packet_before_resumed_frame=packet_first):
                l = self.make()
                l.globals().packet_first = packet_first
                l.execute('''
                 nav2(1,50,0);fire2(1,50,1,1,0,0)
                 paused=true;now=10.12;frames.onUpdate()
                 paused=false;now=13
                 if not packet_first then frames.onUpdate() end
                 nav2(2,51,0) -- Delayed sample after a stale gap: samples reset, but the
                 -- real-time clock mapping of the same source process is retained (-40, not -38).
                 fire2(2,51.05,1,1,1,0);now=13.21;frames.onUpdate()
                 assert(math.abs(C.remote[2].track.offset+40)<.01 and C.remote[2].track.resets==1) -- drift leak only
                 assert(C.resumeCutoffs[2].cutoff==53 and #fires('Fire2')==0)
                 -- Another pause with the delayed mapping cannot lower the cutoff.
                 paused=true;now=13.25;frames.onUpdate();paused=false;now=13.3;frames.onUpdate()
                 -- With the retained mapping the new boundary is the true resume time (~53.29).
                 assert(C.resumeCutoffs[2].cutoff>=53 and C.resumeCutoffs[2].cutoff<53.4)
                 now=13.4;nav2(3,53.4,0);fire2(3,53.4,1,1,2,0)
                 now=13.61;frames.onUpdate()
                 assert(#fires('Fire2')==1 and fires('Fire2')[1].primary)
                ''')

    def test_unknown_resume_mapping_waits_for_nav_baseline_then_allows_new_edges(self):
        l = self.make()
        l.execute('''
         paused=true;now=10.12;frames.onUpdate();paused=false;now=13;frames.onUpdate()
         fire2(1,50,1,1,0,0);fire2(2,50.05,1,1,1,0)
         assert(#C.fire2[2].events==0 and #fires('Fire2')==0)
         now=13.1;nav2(1,53,0);fire2(3,52.9,1,1,2,0);frames.onUpdate()
         assert(C.resumeCutoffs[2].cutoff==53 and #fires('Fire2')==0)
         now=13.3;nav2(2,53.2,0);fire2(4,53.2,1,1,3,0)
         now=13.51;frames.onUpdate();assert(#fires('Fire2')==1)
        ''')

    def test_epoch_change_clears_fire2_and_rebaselines(self):
        l = self.make()
        l.execute('''
         nav2(1,50,0);fire2(1,50,1,1,0,0);now=10.05;fire2(2,50.05,1,1,1,0);now=10.3;frames.onUpdate()
         assert(#fires('Fire2')==1)
         auth(2,2);assert(C.fire2[2]==nil)
         local f=fires('Fire2');assert(f[#f].primary==false,'epoch change stops FIRE2 fire')
         nav2(1,60,0,2);nav2(2,60.05,1,2);fire2(1,60.05,1,1,9,0,2);now=10.6;frames.onUpdate()
         assert(legacyOn()==0 and #fires('Fire2')==#f,'new epoch: no NAV fire, no replay of the new baseline')
        ''')

    def test_resume_cutoff_does_not_leak_into_a_new_owner_clock(self):
        l = self.make()
        l.execute('''
         nav2(1,50,0);fire2(1,50,1,1,0,0)
         paused=true;now=10.12;frames.onUpdate();paused=false;now=13;frames.onUpdate()
         assert(C.resumeCutoffs[2].cutoff==53)
         auth(2,2);now=13.1;nav2(1,3,0,2);fire2(1,3,1,1,5,0,2)
         assert(C.resumeCutoffs[2].epoch==2 and C.resumeCutoffs[2].cutoff==3)
         fire2(2,54,1,1,99,0,1);assert(#fires('Fire2')==0,'old owner rejected')
         now=13.15;nav2(2,3.05,0,2);fire2(2,3.05,1,1,6,0,2)
         now=13.31;frames.onUpdate()
         assert(#fires('Fire2')==1 and fires('Fire2')[1].primary,'new owner clock is independent')
        ''')


if __name__ == '__main__':
    unittest.main()
