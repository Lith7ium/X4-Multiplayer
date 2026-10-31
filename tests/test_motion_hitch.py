"""Remote-ship hitches seen natively in 7.6.0 (VPS server, two loaded Windows clients).

Engine doubles only (LuaJIT): native velocity is simulated by advancing the replica by its
last Velocity vector every frame. Synthetic; not native evidence.
"""
from pathlib import Path
import unittest
from test_game_adapter import LuaRuntime
import test_coop_adapter

UI = Path(__file__).resolve().parents[1] / 'src/extension/x4_lan_lab/ui'
COOP_MD = UI.parent / 'md/x4lan_coop.xml'


def closed_window_zeroes(path=COOP_MD):
    """True if the MD Velocity cue can zero native velocity while $dockWindow is false.

    Walks every zero-vector set_object_velocity in the cue with its enclosing branch
    conditions. A branch requires the window only if `$dockWindow` is one of its own
    top-level `and` terms; a do_else only negates earlier branches, which never does.
    """
    import xml.etree.ElementTree as ET
    cue = ET.parse(path).getroot().find(".//cue[@name='Velocity']/actions")

    def requires(node):
        return '$dockWindow' in [t.strip() for t in node.get('value', '').split(' and ')]

    def walk(node, guarded):
        for child in node:
            if child.tag in ('do_if', 'do_elseif'):
                # A do_elseif also negates the earlier branches; only its own terms count.
                if walk(child, guarded or requires(child)):
                    return True
            elif child.tag == 'do_else':
                if walk(child, guarded):
                    return True
            elif child.tag == 'set_object_velocity':
                linear = child.find('linear')
                zero = linear is not None and all(linear.get(a) == '0' for a in 'xyz')
                if zero and not guarded:
                    return True
            elif walk(child, guarded):
                return True
        return False
    return walk(cue, False)


FLIGHT = """
 local c=X4LAN_BUBBLE_API.coop;c.sectors.cluster_14_sector001_macro=22
 SPEED=650
 function lastRequest(name) for i=#requests,1,-1 do if requests[i][2]==name then return requests[i][3] end end end
 function fences(enabled) local n=0;for _,r in ipairs(requests) do if r[2]=='VelocityFence' and r[3].enabled==enabled then n=n+1 end end;return n end
 -- Native velocity double: the last enabled Velocity request moves object 42 each frame.
 function nativeStep(dt)
  local v=lastRequest('Velocity');local fence=lastRequest('VelocityFence')
  if v and fence and fence.enabled and fence.generation==v.generation and positions[42] then
   positions[42].x=positions[42].x+v.x*dt
  end
 end
 seq=0;pending={}
 -- Queue a NAV2 that reaches the client at `arrival` (FIFO, like an OS receive buffer).
 function sample(time,x,arrival)
  seq=seq+1
  local last=pending[#pending];if last and last.arrival>arrival then arrival=last.arrival end
  pending[#pending+1]={arrival=arrival,f={'NAV2','1',client.lease,string.rep('b',32),'2','1',tostring(seq),'cluster_14_sector001_macro',
   tostring(x),'10000','0','0','0','0','0','0',string.format('%.6f',time),tostring(SPEED),'0','0','0','0','0'}}
 end
 function deliver()
  while pending[1] and pending[1].arrival<=now do c.receive(table.remove(pending,1).f,now) end
 end
 -- 20 Hz source, 45 FPS observer; `late(k)` adds arrival delay for sample k.
 function fly(from,to,late,step)
  local frame=1/45;local nextSample=from
  for t=from,to,frame do
   now=10+t
   while nextSample<=t do
    local k=math.floor(nextSample*20+.5);sample(50+k/20,pos and pos(k) or 1000+SPEED*k/20,10+k/20+.02+(late and late(k) or 0))
    nextSample=nextSample+.05
   end
   deliver();nativeStep(frame);frames.onUpdate()
   if step then step(t) end
  end
 end
"""


@unittest.skipUnless(LuaRuntime, 'LuaJIT unavailable')
class MotionHitchTests(unittest.TestCase):
    def make(self, setup=''):
        harness = test_coop_adapter.CoopAdapterTests('test_absolute_far_pose_and_sector_are_sent_without_old_bubble_limit')
        l = harness.make(setup=setup)
        l.execute(FLIGHT)
        return l

    def test_steady_flight_drift_is_frame_travel_not_target_error(self):
        # Native: request/drift about 13-15 m at 650 m/s and 45 FPS. That is speed * frame
        # time, the travel between two writes; the real target error stays near zero.
        l = self.make('X4LAN_CONFIG.motion_diagnostics=.5')
        l.execute('fly(0,3)')
        rows = [v for v in l.eval('logs').values() if 'MOTION_DIAG' in v]
        self.assertTrue(rows)
        last = rows[-1]
        fields = dict(part.split('=', 1) for part in last.split('MOTION_DIAG ')[1].split())
        self.assertAlmostEqual(float(fields['drift']), 650 / 45, delta=2.5)
        self.assertLess(float(fields['error']), 1.0, last)
        self.assertLess(float(fields['maxError']), 3.0, last)

    def test_one_frame_without_refresh_keeps_native_velocity_without_a_fence(self):
        # Native: fences climbed (21->24 in 10 s). A frame whose dock pairing or apply step
        # is skipped used to fence: MD zeroes the replica's velocity until the next vector.
        l = self.make('X4LAN_CONFIG.coop_docks=true')
        l.execute('''
         local c=X4LAN_BUBBLE_API.coop;allow=true
         c.dockPoseAllowed=function() return allow end
         fly(0,1)
         local before=fences(false);local x=positions[42].x
         fly(1,1+1/45,nil,function() allow=false end);allow=true
         assert(fences(false)==before, 'a one-frame refresh gap must not fence')
         assert(positions[42].x>x+10, 'the replica keeps moving through the gap')
         fly(1+2/45,1.5)
         assert(fences(false)==before)
         -- A real boundary still fences at once: losing the pose for longer than the hold.
         allow=false;fly(1.5,1.9);assert(fences(false)==before+1)
        ''')

    def test_queueing_bursts_do_not_freeze_the_replica(self):
        # Native: NETRTT median 160-477 ms, p95 up to 642 ms, 28-48 % loss on the loaded PC.
        # FIFO bursts that hold samples for up to 0.7 s pushed the render target past the
        # 0.35 s prediction horizon: the replica froze (velocity zero) and then jumped. The
        # adaptive delay widens interpolation from the observed excess latency.
        l = self.make()
        l.execute('''
         function late(k) local j=k%30;if j<14 then return .7-j*.05 end;return 0 end
         fly(0,2)
         local stalls,jerk=0,0;local prev,prevStep=nil,nil
         fly(2,8,late,function(t)
          local x=positions[42].x
          if prev then local step=x-prev;if t>5 and step<SPEED/45*.5 then stalls=stalls+1 end
           if t>5 and prevStep then jerk=math.max(jerk,math.abs(step-prevStep)) end;prevStep=step end
          prev=x
         end)
         STALLS,JERK=stalls,jerk
        ''')
        self.assertEqual(l.eval('STALLS'), 0)
        self.assertLess(l.eval('JERK'), 5.0)

    SECOND_BOUNDARY = '''
     local c=X4LAN_BUBBLE_API.coop
     c.dockPoseAllowed=function() return true end
     c.attachmentGeneration=function() return 1 end
     -- Dock rows are read by MD every 0.05 s and stamped with the wall-clock second.
     rowAt,rowStamp=-1,nil
     c.attachmentWindow=function() return rowStamp,rowAt end
     function wall(t) return t+.37 end
     -- MD double: requests queued by one Lua update execute in the next frame. The dock
     -- window is open only while the current wall second equals the command's stamp.
     native,seen,zeroFrames,movingFrames=nil,0,0,0
     function nativeStep(dt)
      for k=seen+1,#requests do
       local r=requests[k]
       if r[2]=='VelocityFence' and r[3].ship==2 then native=nil
       elseif r[2]=='Velocity' and r[3].ship==2 then
        if math.floor(wall(now))==r[3].dockStamp then native=r[3].x
        elseif CLOSED_ZEROES then native=nil end
       end
      end
      seen=#requests
      if now>=rowAt+.05 then rowAt=now;rowStamp=math.floor(wall(now)) end
      if native then positions[42].x=positions[42].x+native*dt end
     end
     fly(0,4,nil,function(t)
      if t>1 then movingFrames=movingFrames+1;if not native or native==0 then zeroFrames=zeroFrames+1 end end
     end)
    '''

    def test_dock_window_second_boundary_never_zeroes_a_moving_replica(self):
        # Native 7.6.x: a remote ship flying at a constant 650 m/s showed target speed 0 and
        # a snapping lead reticle for single frames. MOTION_DIAG rows with velocity=native and
        # unchanged fences had drift=0 and error=request (the engine did not move the
        # replica between two pose writes) in 6-7 % of rows: about one stall per second.
        # The MD Velocity cue zeroed native velocity whenever its dock window was closed,
        # and the window's systemtime stamp closes across every wall-clock second.
        self.assertFalse(closed_window_zeroes(),
                         'a closed dock window must skip the command, not zero native velocity')
        for zeroes, expect_stalls in ((closed_window_zeroes(), False), (True, True)):
            l = self.make('X4LAN_CONFIG.coop_docks=true')
            l.execute('CLOSED_ZEROES=%s' % ('true' if zeroes else 'false'))
            l.execute(self.SECOND_BOUNDARY)
            self.assertGreater(l.eval('movingFrames'), 100)
            stalls = l.eval('zeroFrames')
            if expect_stalls:
                self.assertGreater(stalls, 0, 'the double must reproduce the old stalls')
            else:
                self.assertEqual(stalls, 0, 'native velocity was zero for %d frames' % stalls)

    def test_watchdog_still_zeroes_on_physical_boundaries(self):
        import xml.etree.ElementTree as ET
        root = ET.parse(COOP_MD).getroot()
        guard = root.find(".//cue[@name='VelocityWatchdog']/actions/do_all/do_if/do_if").get('value')
        for condition in ('not $ship.isoperational', '$ship.dock', 'player.occupiedship == $ship',
                          '$ship.parent != $velocityParent.{$i}', '$ship.sector != $velocitySector.{$i}',
                          'player.age gt $velocityDeadline.{$i}'):
            self.assertIn(condition, guard)

    def test_client_drains_a_busy_relay_queue_in_one_update(self):
        # Native: rx ~1000 packets/s, NETRTT median up to 477 ms with 28-48 % loss while the
        # client read only 32 datagrams per frame. A frame must drain the queue.
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.execute((UI / 'client.lua').read_text())
        lua.execute('''
         local queue={}
         for i=1,200 do queue[i]='EMPTY|1|'..string.rep('b',32)..'|'..i end
         local transport={send=function() return true end,receive=function() return table.remove(queue,1) end}
         local cfg={token=string.rep('a',32),build='test',bubble='bubble',sector='s',slot='p2'}
         local client=X4LAN_CLIENT.new(cfg,transport,string.rep('1',32))
         client:receive('WELCOME|1|'..string.rep('1',32)..'|'..string.rep('b',32)..'|20',0)
         assert(client:update(0.1,nil))
         assert(#queue==0 and client.drained==200, 'left '..#queue)
        ''')


if __name__ == '__main__':
    unittest.main()
