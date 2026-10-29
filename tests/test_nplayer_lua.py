"""N-player client adapters under LuaJIT engine doubles (synthetic; no X4, no native evidence).

Covers the pure client (one transform track per remote lease, slot admission) and the
co-op chain of a dedicated four-player session seen from client p4: ship mapping, one
proxy per remote client, AUTH with one group per ship, XSHIP and per-remote spacesuits.
"""
from pathlib import Path
import unittest
from xml.etree import ElementTree
from test_game_adapter import MOCK, LuaRuntime
from test_bubble_adapter import EXTRA

UI = Path(__file__).resolve().parents[1] / 'src/extension/x4_lan_lab/ui'
A, B, C, OWN = 'a' * 32, 'b' * 32, 'c' * 32, 'e' * 32


@unittest.skipUnless(LuaRuntime, 'LuaJIT unavailable')
class ClientRemotesTests(unittest.TestCase):
    def runtime(self):
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.execute((UI / 'client.lua').read_text())
        return lua

    def test_one_track_per_remote_lease_in_the_same_tick(self):
        lua = self.runtime()
        lua.execute('''
          local cfg={token=string.rep('a',32),build='test',bubble='bubble',sector='s',slot='p2',dedicated_session=true,max_players=4}
          client=X4LAN_CLIENT.new(cfg,{},string.rep('1',32))
          local own=string.rep('e',32)
          client:receive('WELCOME|1|'..string.rep('1',32)..'|'..own..'|20',0)
          for tick=1,3 do
            for i,lease in ipairs({string.rep('a',32),string.rep('b',32),string.rep('c',32)}) do
              client:receive('PEER|1|'..own..'|'..tick..'|'..lease..'|'..tick..'|'..(i*100+tick)..'|0|0|0|0|0',tick*.05)
            end
          end
          client:receive('PEER|1|'..own..'|3|'..string.rep('a',32)..'|9|999|0|0|0|0|0',.2) -- same tick, same remote
          client:receive('PEER|1|'..own..'|3|'..own..'|9|999|0|0|0|0|0',.2)                -- own lease never a remote
          local all=client:renderAll(.2);local n=0
          for lease,pose in pairs(all) do n=n+1;assert(pose and pose[1]<400) end
          assert(n==3 and client.remotes[string.rep('a',32)].seq==3)
          assert(client:render(.2)) -- mirrors the latest remote
          client:receive('EMPTY|1|'..own..'|4',.25)
          assert(next(client.remotes)==nil and client:render(.25)==nil)
          client:receive('PEER|1|'..own..'|4|'..string.rep('a',32)..'|10|1|0|0|0|0|0',.26) -- after EMPTY of tick 4
          assert(next(client.remotes)==nil)
        ''')

    def test_slot_admission_follows_max_players(self):
        lua = self.runtime()
        lua.execute('''
          local function make(slot,dedicated,players)
            local cfg={token=string.rep('a',32),build='test',bubble='bubble',sector='s',slot=slot,dedicated_session=dedicated,max_players=players}
            return pcall(X4LAN_CLIENT.new,cfg,{},string.rep('1',32))
          end
          assert(make('p1',false,nil) and make('p2',false,nil))
          assert(not make('p3',false,nil) and not make('p3',false,3))
          assert(make('p3',true,nil) and not make('p4',true,nil))      -- legacy dedicated: three slots
          assert(make('p4',true,4) and make('p8',true,8))
          assert(not make('p5',true,4) and not make('p9',true,8) and not make('p2',true,9))
        ''')


@unittest.skipUnless(LuaRuntime, 'LuaJIT unavailable')
class FourPlayerCoopTests(unittest.TestCase):
    def make(self, slot='p4'):
        l = LuaRuntime(unpack_returned_tuples=True)
        l.execute(MOCK + EXTRA)
        g = l.globals().X4LAN_CONFIG
        g.slot, g.coop, g.dedicated_session, g.max_players = slot, True, True, 4
        g.ship_macro, g.ship_loadout, g.ship_weapons = 'ship_test_macro', 'loadout', 6
        l.execute('''
          ConvertIDTo64Bit=tonumber
          api.GetContextByClass=function() return 22 end
          GetComponentData=function() return 'cluster_14_sector001_macro' end
          api.SetObjectSectorPos=function(object,sector,p)
            writes[#writes+1]={object=object,x=p.x}
            positions[object]={x=p.x,y=p.y,z=p.z,yaw=p.yaw*math.pi/180,pitch=p.pitch*math.pi/180,roll=p.roll*math.pi/180}
          end
          nextObject=80
          api.SpawnObjectAtPos2=function(macro,sector,p,owner) local n=nextObject;nextObject=n+1;positions[n]=p;return n end
        ''')
        for name in ('bubble_game.lua', 'motion.lua', 'coop_game.lua', 'life_game.lua'):
            l.execute((UI / name).read_text())
        l.execute('''
          events['X4LAN.Loaded']();now=10;frames.onUpdate()
          for _,n in ipairs({61,62,63}) do positions[n]={x=0,y=0,z=0,yaw=0,pitch=0,roll=0} end
          events['X4LAN_Bubble.PreparedOwn'](nil,61);events['X4LAN_Bubble.PreparedProxy'](nil,62)
          events['X4LAN_Bubble.PreparedExtra.1'](nil,63);events['X4LAN_Bubble.ShipsReady']()
          frames.onUpdate();events['X4LAN_Bubble.Ready']();frames.onUpdate();events['X4LAN_Coop.Ready']()
          function auth(rev,...)
            local f={'AUTH','1',client.lease,tostring(rev)}
            for _,v in ipairs({...}) do f[#f+1]=v end
            client:receiveExtension(f,now)
          end
          function nav2(source,entity,epoch,seq,x)
            local f={'NAV2','1',client.lease,source,tostring(entity),tostring(epoch),tostring(seq),'cluster_14_sector001_macro',
              tostring(x),'10000','0','0','0','0','0','0',string.format('%.6f',seq*.05),'0','0','0','0','0','0'}
            client:receiveExtension(f,now)
          end
        ''')
        return l

    def test_client_p4_owns_ship_three_and_renders_both_remote_clients(self):
        l = self.make()
        l.execute('''
          local s,c=X4LAN_BUBBLE_API.state,X4LAN_BUBBLE_API.coop
          local R=X4LAN_BUBBLE_API.roster
          assert(R.players==4 and R.ships==3 and R.own==3 and c.count==3)
          local p=nil;for _,r in ipairs(requests) do if r[2]=='PrepareShips' then p=r[3] end end
          assert(p.count==3 and p.ownIndex==3)
          -- own at index 3; the proxy and the extra fill 1 and 2 in order
          assert(c.ships[3]==61 and c.ships[1]==62 and c.ships[2]==63, 'ship mapping')
          auth(1,'p3','2','p3','p2','1','p2','p4','2','p4')
          assert(c.owners[3]=='p4' and c.owners[1]=='p3' and c.revision==1)
          auth(2,'p3','2','p3','p2','1','p2')            -- a two-ship AUTH is malformed here
          assert(c.revision==1)
          auth(3,'p3','2','p3','p2','1','p2','p5','2','p5') -- p5 is beyond max_players
          assert(c.revision==1)
          c.sectors['cluster_14_sector001_macro']=22
          for seq=1,3 do now=10+seq*.05;nav2(string.rep('c',32),1,2,seq,100+seq);nav2(string.rep('b',32),2,1,seq,200+seq) end
          now=10.6;frames.onUpdate()
          local moved={}
          for _,w in ipairs(writes) do moved[w.object]=true end
          assert(moved[62] and moved[63], 'both remote clients are rendered')
          assert(not moved[61], 'own ship is never moved by the network')
          -- own ship publishes as entity 3
          local found=false
          for _,pkt in ipairs(sent) do if pkt:find('^NAV2|1|'..client.lease..'|3|2|') then found=true end end
          assert(found, 'NAV2 for own ship 3')
        ''')

    def test_xship_reaches_md_and_server_publishes_extra_ships(self):
        l = self.make()
        l.execute('''
          local f={'XSHIP','1',client.lease,string.rep('a',32),'1','3','40.5','20','0','0'}
          client:receiveExtension(f,now)
          assert(countRequest('ApplyExtra')==1 and requests[#requests][3].ship==3 and requests[#requests][3].hull==40.5)
          client:receiveExtension(f,now);assert(countRequest('ApplyExtra')==1)          -- unchanged, throttled
          f[6]='4';client:receiveExtension(f,now);assert(countRequest('ApplyExtra')==1) -- beyond ship count
          f[6]='2';client:receiveExtension(f,now);assert(countRequest('ApplyExtra')==1) -- fixture rows use WORLD
        ''')
        server = self.make('p1')
        server.execute('''
          events['X4LAN_Bubble.XShip'](nil,'3|55|10|0|0')
          now=11;frames.onUpdate()
          local found=false
          for _,pkt in ipairs(sent) do if pkt:find('^XSHIP|1|'..client.lease..'|%d+|3|55|10|0|0$') then found=true end end
          assert(found)
        ''')

    def test_two_remote_ships_with_jittered_arrival_keep_independent_smooth_tracks(self):
        l = self.make()
        l.execute('''
          local c=X4LAN_BUBBLE_API.coop
          auth(1,'p3','2','p3','p2','1','p2','p4','2','p4')
          c.sectors['cluster_14_sector001_macro']=22
          local function sample(source,entity,epoch,k,x,v,mode)
            local time=50+k*.05
            c.receive({'NMODE','1',client.lease,source,tostring(entity),tostring(epoch),tostring(k+1),tostring(time),mode},now)
            c.receive({'NAV2','1',client.lease,source,tostring(entity),tostring(epoch),tostring(k+1),'cluster_14_sector001_macro',
              tostring(x),'10000','0','0','0','0','0','0',tostring(time),tostring(v),'0','0','0','0','0'},now)
          end
          local p3,p2=string.rep('c',32),string.rep('b',32)
          local prev={[62]=-math.huge,[63]=-math.huge};local jitter={.018,-.012,.004,-.019,.011,0,-.007,.015}
          local velocityBefore=countRequest('Velocity')
          for k=0,80 do
            -- ship 1 (p3) at highway speed, ship 2 (p2) in normal flight; arrivals jittered +-20 ms,
            -- and the two streams arrive in alternating order.
            now=10+k*.05+jitter[k%8+1]
            if k%2==0 then sample(p3,1,2,k,1000+k*500,10000,'highway');sample(p2,2,1,k,5000+k*5,100,'normal')
            else sample(p2,2,1,k,5000+k*5,100,'normal');sample(p3,1,2,k,1000+k*500,10000,'highway') end
            for _=1,2 do
              frames.onUpdate();now=now+.016
              for object,old in pairs(prev) do
                if positions[object] then assert(positions[object].x>=old-1e-6,'ship object '..object..' hopped back at '..k);prev[object]=positions[object].x end
              end
            end
          end
          assert(c.remote[1].mode=='highway' and c.remote[2].mode=='normal')
          assert(c.velocity[1] and math.abs(c.velocity[1].linear[1]-10000)<1, 'highway replica reports its speed')
          assert(countRequest('Velocity')>velocityBefore and c.velocity[2], 'normal flight keeps native velocity')
          for _,r in ipairs(requests) do if r[2]=='Velocity' then assert(r[3].ship==2 or (r[3].ship==1 and r[3].x>=0)) end end
          assert(prev[62]>1000+40*500 and prev[63]>5000+40*5, 'both replicas advanced')
          assert(c.remote[1].track~=c.remote[2].track, 'one motion track per remote ship')
        ''')

    def test_client_p3_accepts_a_remote_dock_observation_for_ship_two(self):
        l = self.make('p3')
        l.globals().X4LAN_CONFIG.coop_docks = True
        l.globals().X4LAN_CONFIG.seed_id = 'd' * 32
        l.execute((UI / 'coop_dock_game.lua').read_text())
        l.execute('''
          local c,d=X4LAN_BUBBLE_API.coop,X4LAN_BUBBLE_API.coopDocks
          d.ready=true
          auth(1,'p3','2','p3','p2','1','p2','p1','1','0')
          local p2=string.rep('b',32)
          nav2(p2,2,1,7,200)
          client:receiveExtension({'CDOCK','1',client.lease,p2,'2','1','7','FREE','0'},now)
          local q=d.goal(2,now)
          assert(q and q.mode=='FREE' and q.lease==p2, 'p3 must see p2 dock state for ship 2')
          -- entity beyond the session ship count is ignored, entity 3 is parsed (count=3)
          client:receiveExtension({'CDOCK','1',client.lease,p2,'4','1','8','FREE','0'},now)
          assert(d.remote[4]==nil)
        ''')

    def test_each_remote_client_gets_its_own_spacesuit(self):
        l = self.make()
        l.execute('''
          X4LAN_BUBBLE_API.coop.sectors['cluster_14_sector001_macro']=22
          for i,lease in ipairs({string.rep('b',32),string.rep('c',32)}) do
            client:receiveExtension({'BODY2','1',client.lease,lease,'1','suit','0',tostring(i),'cluster_14_sector001_macro',tostring(i*10),'0','0','0','0','0'},now)
          end
          now=10.1;frames.onUpdate()
          local life=X4LAN_BUBBLE_API.life;local suits={}
          for _,e in pairs(life.remotes) do assert(e.suit);suits[e.suit]=true end
          local n=0;for _ in pairs(suits) do n=n+1 end
          assert(n==2 and countRequest('RemoteSuit')==2)
        ''')


class NPlayerMdContractTests(unittest.TestCase):
    """Static MD contract only; the scripts still need a native X4 run."""
    MD = Path(__file__).resolve().parents[1] / 'src/extension/x4_lan_lab/md'

    def test_bubble_prepares_ordered_ships_and_counts_shots_for_every_ship(self):
        import xml.etree.ElementTree as ET
        root = ET.parse(self.MD / 'x4lan_bubble.xml')
        names = {c.get('name') for c in root.iter('cue')}
        for name in ['Shots%d' % k for k in range(1, 8)] + ['ApplyExtra', 'PrepareShips']:
            self.assertIn(name, names)
        text = (self.MD / 'x4lan_bubble.xml').read_text()
        self.assertIn('md.X4LAN_Bubble.PrepareShips.$ordered.clone', text)
        self.assertIn("'X4LAN_Bubble.PreparedExtra.%s'.[$k]", text)
        self.assertIn("'X4LAN_Bubble.XShip'", text)
        for k in range(3, 8):
            self.assertIn('groupname="$weapons%d"' % k, text)
        self.assertNotIn('[false,false]', text)          # per-ship lists follow the ship count

    def test_health_and_shot_lists_cover_every_shared_ship(self):
        # Native Linux p4 (ship 3): X4LAN_Health.Own read $healthOwn.{3} on a two-entry list
        # 10 times a second (2,535 MD errors). Every arena list indexed by a shared ship index
        # must follow the ship count; ships 3+ keep the neutral entry.
        loop = '<do_all exact="[$arena.$ships.count,2].max" counter="$i">'
        for name, lists in (('x4lan_health.xml', ('healthOwn', 'healthLive', 'healthValue', 'healthGen', 'healthClosed',
                                                    'healthAuth', 'healthBased', 'healthAt', 'healthKeptAt',
                                                    'healthProxyLost', 'healthTop')),
                            ('x4lan_shot.xml', ('shotStrength', 'shotCurrentGen', 'shotAuthority')),
                            ('x4lan_shot_visual.xml', ('shotVisualMode', 'shotVisualGen', 'shotVisualAt'))):
            text = (self.MD / name).read_text()
            self.assertIn(loop, text, name)
            for key in lists:
                self.assertIn('<append_to_list name="$arena.$%s"' % key, text, key)
                self.assertNotRegex(text, r'<set_value name="\$arena\.\$%s" exact="\[(?:0|false|-?\d+s|\[)' % key)
        capture = (self.MD / 'x4lan_shot_capture.xml').read_text()
        self.assertIn('exact="$ai le 2 and $capRemote.{$ai}"', capture)

    def test_coop_lists_follow_the_ship_count_and_dock_window_spares_extra_ships(self):
        text = (self.MD / 'x4lan_coop.xml').read_text()
        self.assertNotIn('[[0,0],[0,0]]', text)
        self.assertIn('<do_all exact="$count" counter="$i">', text)
        coop_xml = ElementTree.fromstring(text)
        extra_ship_guards = [
            node for node in coop_xml.iter('do_if')
            if node.get('value') == '$v.$ship gt 2'
        ]
        self.assertTrue(any(
            len(guard) == 1
            and guard[0].tag == 'set_value'
            and guard[0].get('name') == '$dockWindow'
            and guard[0].get('exact') == 'true'
            for guard in extra_ship_guards
        ))
        life = (self.MD / 'x4lan_life.xml').read_text()
        self.assertIn('<do_all exact="$arena.$ships.count" counter="$i">', life)
        # The native attachment session starts for every client slot, not only p2
        # (native finding: p3 stopped coop docks and published UNKNOWN, freezing ship 1/2 replicas).
        attachment = (self.MD / 'x4lan_coop_attachment.xml').read_text()
        self.assertIn("(if md.X4LAN_Bubble.Arena.$host then $v.$role == 'p1' else $v.$role != 'p1')", attachment)
        for name in ('x4lan_trade.xml', 'x4lan_walk.xml'):
            self.assertNotIn('3 - ', (self.MD / name).read_text(), name)   # ships 3+ make 3-localSlot invalid
        cargo = (self.MD / 'x4lan_cargo.xml').read_text()
        self.assertIn('object="$ships.{2}"', cargo)          # native cargo stays on ships 1/2


if __name__ == '__main__':
    unittest.main()
