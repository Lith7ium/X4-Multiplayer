"""On-foot avatars (walk_game.lua, x4lan_walk.xml) under LuaJIT engine doubles.

Synthetic peers only: these check WALK2 parsing, interpolation, context identity and the
per-slot avatar lifecycle the adapter asks MD for, not that X4 draws or animates them.
"""
from pathlib import Path
import unittest
import xml.etree.ElementTree as ET
from test_game_adapter import MOCK, LuaRuntime
from test_bubble_adapter import EXTRA

ROOT = Path(__file__).resolve().parents[1]
UI = ROOT / 'src/extension/x4_lan_lab/ui'
MD = ROOT / 'src/extension/x4_lan_lab/md/x4lan_walk.xml'
STATION = "'station','0','cluster_14_sector001_macro','ABC-123','station_gen_factory_base_01_macro'"

HELPERS = '''
function remote(slot,seq,stime,x,opts)
  opts=opts or {}
  local lease=opts.lease or string.rep(string.char(96+slot),32)
  local f={'WALK2','1',client.lease,lease,'p'..slot,tostring(seq),opts.state or '1'}
  local ctx=opts.ctx or {'ship','2','-','-','-'}
  for _,v in ipairs(ctx) do f[#f+1]=v end
  for _,v in ipairs({opts.room or 'room_a_macro',tostring(x),'0',tostring(opts.z or 0),tostring(opts.yaw or 90),'1.4',tostring(stime),tostring(opts.transfers or 0)}) do f[#f+1]=v end
  return client:receiveExtension(f,now)
end
function moves(slot)
  local r={};for _,q in ipairs(requests) do if q[2]=='Move' and q[3].slot==slot then r[#r+1]=q[3] end end;return r
end
function hides(slot,reason)
  local n=0;for _,q in ipairs(requests) do if q[2]=='Hide' and q[3].slot==slot and (not reason or q[3].reason==reason) then n=n+1 end end;return n
end
function walkSent()
  local r={};for _,p in ipairs(sent) do if p:sub(1,6)=='WALK2|' then r[#r+1]=p end end;return r
end
function logged(text) for _,l in ipairs(logs) do if l:find(text,1,true) then return true end end;return false end
function step(t) now=t;frames.onUpdate() end
'''


@unittest.skipUnless(LuaRuntime, 'LuaJIT unavailable')
class WalkAdapterTests(unittest.TestCase):
    def make(self, slot='p3', players=4, dedicated=True, ready=True, enabled=True):
        l = LuaRuntime(unpack_returned_tuples=True)
        l.execute(MOCK + EXTRA)
        g = l.globals().X4LAN_CONFIG
        g.slot, g.walking_avatars = slot, enabled
        if dedicated:
            g.dedicated_session, g.max_players = True, players
        l.execute((UI / 'bubble_game.lua').read_text())
        l.execute((UI / 'life_game.lua').read_text())
        l.execute((UI / 'walk_game.lua').read_text())
        l.execute(HELPERS)
        l.execute("events['X4LAN.Loaded'](); step(10); events['X4LAN_Bubble.Ready'](); step(10)")
        if ready and l.eval('X4LAN_BUBBLE_API.walk ~= nil'):
            l.execute("assert(countRequest('Setup')>=1); events['X4LAN_Walk.Ready']()")
        return l

    def test_disabled_by_default_and_on_the_dedicated_server(self):
        for value in (None, False):
            l = self.make(enabled=value)
            l.execute("assert(X4LAN_BUBBLE_API.walk==nil and events['X4LAN_Walk.Pose2']==nil)")
        l = self.make(slot='p1')
        l.execute("assert(X4LAN_BUBBLE_API.walk==nil and logged('WALK_DISABLED server'))")
        l = self.make(slot='p1', dedicated=False, players=2)  # a two-player host walks
        l.execute("assert(X4LAN_BUBBLE_API.walk~=nil)")

    def test_remote_pose_validation(self):
        l = self.make()
        l.execute(f'''
          local W=X4LAN_BUBBLE_API.walk
          local function f(slot,ctx,extra)
            local t={{'WALK2','1',client.lease,string.rep('b',32),slot,'1','1'}}
            for _,v in ipairs(ctx) do t[#t+1]=v end
            for _,v in ipairs(extra or {{'room_a_macro','1','2','3','90','1','5','0'}}) do t[#t+1]=v end
            return t
          end
          local p=assert(W.parseRemote(f('p2',{{{STATION}}})))
          assert(p.slot==2 and p.kind=='station' and p.code=='ABC-123' and p.z==3 and p.stime==5)
          assert(W.parseRemote(f('p4',{{'ship','3','-','-','-'}})).ship==3)
          local bad={{
            {{f('p3',{{{STATION}}}),'own'}}, {{f('p5',{{{STATION}}}),'slot'}}, {{f('p9',{{{STATION}}}),'slot'}},
            {{f('p2',{{'ship','4','-','-','-'}}),'ship'}}, {{f('p2',{{'ship','0','-','-','-'}}),'ship'}},
            {{f('p2',{{'station','0','-','ABC-123','m_macro'}}),'station'}}, {{f('p2',{{'deck','0','-','-','-'}}),'kind'}},
            {{f('p2',{{'station','0','s macro','ABC-123','m_macro'}}),'sector'}},
            {{f('p2',{{{STATION}}},{{'room_a_macro','nan','2','3','90','1','5','0'}}),'number'}},
            {{f('p2',{{{STATION}}},{{'room_a_macro','100001','2','3','90','1','5','0'}}),'range'}},
            {{f('p2',{{{STATION}}},{{'room_a_macro','1','2','3','361','1','5','0'}}),'range'}},
            {{f('p2',{{{STATION}}},{{'','1','2','3','90','1','5','0'}}),'room'}},
          }}
          for i,c in ipairs(bad) do local ok,why=W.parseRemote(c[1]);assert(ok==nil and why==c[2],i..':'..tostring(why)) end
          local short=f('p2',{{{STATION}}});short[20]=nil;assert(select(2,W.parseRemote(short))=='format')
          local idle=f('p2',{{'none','0','-','-','-'}});idle[7]='0';assert(W.parseRemote(idle).state==0)
          idle[7]='1';assert(select(2,W.parseRemote(idle))=='state')
          -- Every rejected packet is consumed and logged once per reason, never forwarded to MD.
          client:receiveExtension(bad[1][1],now);assert(countRequest('Move')==0 and logged('WALK_DROP reason=own'))
          client:receiveExtension({{'WALK','1',client.lease,string.rep('b',32),'1','2','1','2','3','90','0','0'}},now)
          assert(logged('WALK_DROP reason=legacy_walk'))
        ''')

    def test_interpolates_behind_the_newest_pose_with_yaw_wrap(self):
        l = self.make()
        l.execute('''
          local W=X4LAN_BUBBLE_API.walk
          now=20;remote(2,1,100,0,{yaw=170});now=20.1;remote(2,2,100.1,1,{yaw=-170});now=20.2;remote(2,3,100.2,2,{yaw=-150})
          local r=W.remotes[2];assert(math.abs(r.offset-(-80))<1e-9)
          local p=W.sample(r,20.25)                    -- render time 100.05: half way between seq 1 and 2
          assert(math.abs(p.x-.5)<1e-9 and math.abs(math.abs(p.yaw)-180)<1e-9,tostring(p.yaw))
          assert(W.sample(r,25).x==2 and W.sample(r,19).x==0) -- never extrapolated
          -- Late delivery: a larger delay raises the offset only slowly.
          now=20.9;remote(2,4,100.3,3);assert(r.offset>-80 and r.offset<-79.99)
          -- A new context or a transporter ride never blends with the previous place.
          now=21;remote(2,5,100.4,50,{ctx={'ship','1','-','-','-'}});assert(#r.samples==1)
          now=21.1;remote(2,6,100.5,51,{ctx={'ship','1','-','-','-'},transfers=1});assert(#r.samples==1)
          now=21.1;assert(remote(2,6,100.6,99)) ;assert(r.samples[1].x==51 and logged('WALK_DROP reason=order'))
        ''')

    def test_one_avatar_per_remote_slot_lifecycle(self):
        l = self.make()
        l.execute(f'''
          now=30;remote(2,1,10,1,{{ctx={{{STATION}}}}});remote(4,1,10,4,{{ctx={{'ship','3','-','-','-'}}}})
          step(30.01)
          local m2,m4=moves(2),moves(4)
          assert(#m2==1 and #m4==1 and m2[1].kind=='station' and m2[1].code=='ABC-123' and m4[1].ship==3)
          assert(m2[1].mode=='move' and m2[1].x==1 and logged('WALK_SHOW slot=p2'))
          -- Rate: one placement per avatar per 0.2 s; unchanged poses are not re-sent until refresh.
          step(30.1);assert(#moves(2)==1)
          now=30.15;remote(2,2,10.5,3,{{ctx={{{STATION}}}}});step(30.25)
          assert(#moves(2)==2 and moves(2)[2].x>1 and moves(2)[2].speed>=.3 and moves(2)[2].speed<=7)
          -- p4 keeps sending, p2 goes silent: only p2 is hidden as stale.
          for i=1,10 do now=30.25+i*.1;remote(4,1+i,10+i*.1,4);step(now) end
          assert(hides(2,'stale')==1 and hides(4)==0)
          -- p4 boards its ship: hidden as not on foot.
          now=31.4;remote(4,20,12,0,{{state='0',ctx={{'none','0','-','-','-'}}}});step(31.7)
          assert(hides(4,'not_on_foot')==1)
          -- p4 reconnects with a new lease: history restarts at any sequence.
          now=32;remote(4,1,1,7,{{lease=string.rep('f',32),ctx={{'ship','3','-','-','-'}}}});step(32.3)
          assert(moves(4)[#moves(4)].x==7 and logged('WALK_REMOTE slot=p4 new connection'))
          -- Transporter ride: same place, new transfer count -> one transport request.
          now=32.4;remote(4,2,1.5,20,{{lease=string.rep('f',32),ctx={{'ship','3','-','-','-'}},transfers=1,room='room_b_macro'}});step(32.7)
          assert(moves(4)[#moves(4)].mode=='transport')
          -- Disconnect: forgotten after the 30 s lease timeout, then a fresh record.
          step(63);assert(X4LAN_BUBBLE_API.walk.remotes[2]==nil and hides(2,'forget')==1)
          assert(X4LAN_BUBBLE_API.walk.remotes[4]==nil)
        ''')

    def test_no_placement_until_md_confirms_and_fail_closed_after_timeout(self):
        l = self.make(ready=False)
        l.execute('''
          now=11;remote(2,1,1,1);step(11.1);assert(#moves(2)==0)
          step(20.5);assert(logged('WALK_STOPPED'))
          events['X4LAN_Walk.Ready']();now=21;remote(2,2,2,1);step(21.1);assert(#moves(2)==0)
        ''')

    def test_local_pose_publishing_rate_and_identity(self):
        l = self.make()
        l.execute('''
          local W=X4LAN_BUBBLE_API.walk
          events['X4LAN_Walk.Pose2'](nil,'1|station|0|cluster_14_sector001_macro|ABC-123|station_gen_factory_base_01_macro|room_a_macro|1.5|0|2|45|0|ok')
          step(10.05);local s=walkSent();assert(#s==1,#s)
          local f={};for v in s[1]:gmatch('[^|]+') do f[#f+1]=v end
          assert(#f==18 and f[3]==client.lease and f[5]=='1' and f[6]=='station' and f[9]=='ABC-123' and f[11]=='room_a_macro')
          assert(f[12]=='1.500' and f[15]=='45.00' and f[16]=='0.00' and f[18]=='0')
          -- Standing: every 0.5 s. Walking: every 0.1 s.
          step(10.3);assert(#walkSent()==1);step(10.56);assert(#walkSent()==2)
          for i=1,5 do now=10.56+i*.1;events['X4LAN_Walk.Pose2'](nil,'1|station|0|cluster_14_sector001_macro|ABC-123|station_gen_factory_base_01_macro|room_a_macro|'..(1.5+i*.15)..'|0|2|45|0|ok');frames.onUpdate() end
          assert(#walkSent()>=6,#walkSent())
          -- A change of place goes out at once.
          local before=#walkSent();now=11.12;events['X4LAN_Walk.Pose2'](nil,'0|none|0|-|-|-|-|0|0|0|0|0|pilot');frames.onUpdate()
          assert(#walkSent()==before+1 and walkSent()[#walkSent()]:find('|0|none|0|-|-|-|-|',1,true))
          -- A station without an ID code fails closed to "not on foot".
          now=11.2;events['X4LAN_Walk.Pose2'](nil,'1|station|0|cluster_14_sector001_macro||station_gen_factory_base_01_macro|-|1|0|2|45|0|ok')
          assert(W.localPose.state==0 and W.localPose.reason=='nocode')
          events['X4LAN_Walk.Pose2'](nil,'garbage');assert(logged('WALK_LOCAL_INVALID reason=format'))
          -- A local pose MD stopped refreshing is never re-published.
          local n=#walkSent();step(13);assert(#walkSent()==n)
          -- Paused: nothing is sent.
          paused=true;now=13.1;events['X4LAN_Walk.Pose2'](nil,'0|none|0|-|-|-|-|0|0|0|0|0|pilot');step(13.2);assert(#walkSent()==n)
        ''')


class WalkMdTests(unittest.TestCase):
    def test_md_cues_and_lua_events_match(self):
        root = ET.parse(MD).getroot()
        names = {c.get('name') for c in root.iter('cue')}
        self.assertTrue({'Session', 'Publish', 'Move', 'Hide', 'Transported', 'TransportDone', 'TriggerDiag'} <= names)
        controls = {e.get('control') for e in root.iter('event_ui_triggered')}
        self.assertEqual(controls, {"'Setup'", "'Move'", "'Hide'"})
        raised = {e.get('name') for e in root.iter('raise_lua_event')}
        self.assertEqual(raised, {"'X4LAN_Walk.Ready'", "'X4LAN_Walk.Pose2'"})
        lua = (UI / 'walk_game.lua').read_text()
        for event in ('X4LAN_Walk.Ready', 'X4LAN_Walk.Pose2'):
            self.assertIn(f"RegisterEvent('{event}'", lua)
        # The local payload has the 13 fields walk_game.parseLocal expects.
        for e in root.iter('raise_lua_event'):
            if e.get('name') == "'X4LAN_Walk.Pose2'":
                self.assertEqual(e.get('param').split('.[')[0].count('|'), 12, e.get('param'))
        # Unresolved macros never reach find_station/find_sector unguarded.
        move = next(c for c in root.iter('cue') if c.get('name') == 'Move')
        text = ET.tostring(move, encoding='unicode')
        self.assertIn('@$sm.id == $v.$sector and @$cm.id == $v.$macro', text)
        self.assertNotIn('teleport_player', ET.tostring(root, encoding='unicode'))


if __name__ == '__main__':
    unittest.main()
