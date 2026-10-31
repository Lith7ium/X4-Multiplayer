"""LuaJIT engine doubles: protocol integration and command lifecycle, not engine compatibility."""
from pathlib import Path
import unittest
from test_game_adapter import MOCK,LuaRuntime
SCRIPT=Path(__file__).resolve().parents[1]/'src/extension/x4_lan_lab/ui/bubble_game.lua'
EXTRA='''
X4LAN_CONFIG.bubble_actions=true
GetComponentData=function() return 'cluster_14_sector001_macro' end
sent,requests={},{}
AddUITriggeredEvent=function(screen,control,value) requests[#requests+1]={screen,control,value} end
X4LAN_TRANSPORT={open=function() return {close=function() closeCount=closeCount+1 end,
 send=function(self,packet) sent[#sent+1]=packet end} end}
X4LAN_CLIENT={new=function()
 client={lease=string.rep('a',32),update=function(self,time,pose) updates=(updates or 0)+1;lastPose=pose;return not timeout end,render=function() return remotePose end}
 return client
end}
function command(request,verb)
 client:receiveExtension({'COMMAND','1',client.lease,request,'p2',verb},now)
end
function countRequest(name)
 local n=0;for _,r in ipairs(requests) do if r[2]==name then n=n+1 end end;return n
end
'''
@unittest.skipUnless(LuaRuntime,'lupa.luajit21 not installed')
class BubbleAdapterTests(unittest.TestCase):
 def make(self,setup='',ready=True):
  lua=LuaRuntime(unpack_returned_tuples=True);lua.execute(MOCK+EXTRA+setup);lua.execute(SCRIPT.read_text())
  if ready:lua.execute("events['X4LAN.Loaded'](); now=10;frames.onUpdate(); events['X4LAN_Bubble.Ready'](); frames.onUpdate()")
  return lua
 def test_setup_waits_and_original_profile_is_inactive(self):
  l=self.make(ready=False);l.execute("frames.onUpdate(); assert(countRequest('SetupHost')==0); events['X4LAN.Loaded'](); now=10;frames.onUpdate(); assert(countRequest('SetupHost')==1)")
  l=self.make("api.GetSaveFolderPath=function() return '/X4/original/save' end",False);self.assertIsNone(l.eval('frames.onUpdate'))
 def test_dedicated_authority_does_not_teleport_into_shared_ship(self):
  import xml.etree.ElementTree as ET
  l=self.make("X4LAN_CONFIG.dedicated=true;X4LAN_CONFIG.ship_macro='ship_test'")
  l.execute("local found=false;for _,r in ipairs(requests) do if r[2]=='PrepareShips' then found=true;assert(r[3].teleport==false) end end;assert(found)")
  root=ET.parse(Path(__file__).resolve().parents[1]/'src/extension/x4_lan_lab/md/x4lan_bubble.xml')
  teleport=root.find(".//cue[@name='PrepareShips']/actions/do_else/do_if[@value='$args.$teleport']/teleport_player")
  self.assertIsNotNone(teleport)

 def test_commands_deduplicated_before_engine_and_after_result(self):
  l=self.make();l.execute("r=string.rep('b',32);command(r,'PICKUP');command(r,'PICKUP');assert(countRequest('Command')==1);events['X4LAN_Bubble.Result'](nil,r..'|OK');command(r,'PICKUP');assert(countRequest('Command')==1);assert(sent[#sent]=='DONE|1|'..client.lease..'|'..r..'|OK')")
 def test_pause_defers_commands_and_trade_done_is_forwarded(self):
  l=self.make();l.execute("paused=true;command(string.rep('b',32),'TRADE');assert(countRequest('Command')==0);paused=false;command(string.rep('b',32),'TRADE');assert(countRequest('Command')==1);events['X4LAN_Bubble.TradeFinished']();assert(countRequest('TradeFinished')==1)")
 def test_repeated_fire_packet_stops_and_does_not_count_local_shots(self):
  l=self.make();l.execute("f={'FIRE','1',client.lease,string.rep('c',32),'10'};client:receiveExtension(f,0);assert(countRequest('FireOn')==1);now=10.6;client:receiveExtension(f,now);frames.onUpdate();assert(countRequest('FireOff')==1);assert(sent[#sent]:match('|0$'))")
 def test_world_guest_apply_dock_prevents_position_writes(self):
  l=self.make("X4LAN_CONFIG.slot='p2'");l.execute("f={'WORLD','1',client.lease,string.rep('c',32),'1'};for i=1,17 do f[#f+1]='0' end;f[5+14]='1';client:receiveExtension(f,0);assert(countRequest('Apply')==1);assert(requests[#requests][3].values[14]==1);client:receiveExtension(f,0);assert(countRequest('Apply')==1);n=#writes;frames.onUpdate();assert(#writes==n)")
 def test_peer_pose_never_moves_local_player(self):
  l=self.make();l.execute("frames.onUpdate();assert(positions[11].x==100);assert(positions[42].y==10000);assert(math.abs(positions[42].yaw-3.1*180/math.pi)<.001)")

 def test_docking_locks_remote_pose_even_before_next_world_update(self):
  l=self.make();l.execute("r=string.rep('b',32);command(r,'TRADE');n=#writes;frames.onUpdate();assert(#writes==n);events['X4LAN_Bubble.Result'](nil,r..'|REJECTED');frames.onUpdate();assert(#writes==n+1)")

 def test_market_guest_never_applies_legacy_docking_before_market_ready(self):
  l=self.make("X4LAN_CONFIG.slot='p2';X4LAN_CONFIG.trading=true");l.execute('''
f={'WORLD','1',client.lease,string.rep('c',32),'1'};for i=1,17 do f[#f+1]='0' end;f[5+15]='1'
client:receiveExtension(f,0);assert(countRequest('Apply')==0)
X4LAN_BUBBLE_API.trade={state={ready=false},receive=function() return false end}
client:receiveExtension(f,0);assert(countRequest('Apply')==0)
X4LAN_BUBBLE_API.trade.state.ready=true
client:receiveExtension(f,0);assert(countRequest('Apply')==1)
''')
 def test_free_legacy_station_transfer_is_disabled_in_market_mode(self):
  l=self.make("X4LAN_CONFIG.trading=true");l.execute("command(string.rep('d',32),'TRADE');assert(countRequest('Command')==0);assert(sent[#sent]:match('|REJECTED$'))")

 def test_leaving_seat_keeps_original_ship_and_return_preserves_client(self):
  for slot in ('p1','p2'):
   with self.subTest(slot=slot):
    l=self.make("X4LAN_CONFIG.slot='"+slot+"'");l.execute('''
local original=client;local before=updates
controlled=0;now=11;frames.onUpdate()
assert(updates==before+1 and client==original and closeCount==0)
assert(lastPose[1]==100 and lastPose[2]==-9800)
controlled=11;now=12;frames.onUpdate()
assert(updates==before+2 and client==original and not X4LAN_BUBBLE_API.state.failed)
assert(countRequest('SetupHost')+countRequest('SetupGuest')==1)
''')
 def test_initial_on_foot_waits_for_ship_before_setup(self):
  l=self.make('controlled=0',False);l.execute("events['X4LAN.Loaded']();now=10;frames.onUpdate();assert(countRequest('SetupHost')==0 and client==nil);controlled=11;frames.onUpdate();assert(countRequest('SetupHost')==1)")
 def test_pause_on_foot_keeps_idle_updates_and_no_position_writes(self):
  l=self.make();l.execute("controlled=0;paused=true;local before=updates;local moved=#writes;now=11;frames.onUpdate();assert(updates==before+1 and lastPose==nil and #writes==moved and closeCount==0)")
 def test_ship_death_visiting_and_leaving_sector_keep_network_alive(self):
  for change in ('controlled=0;positions[11]=nil','controlled=42','controlled=0;container=42','sector=33','positions[42]=nil'):
   with self.subTest(change=change):
    l=self.make();l.execute("local original=client;local before=updates;"+change+";now=11;frames.onUpdate();assert(not X4LAN_BUBBLE_API.state.failed and closeCount==0 and client==original and updates==before+1)")
 def test_boarded_proxy_is_not_warped_under_the_local_player(self):
  l=self.make();l.execute("container=42;controlled=0;local n=#writes;now=11;frames.onUpdate();assert(#writes==n and closeCount==0)")
