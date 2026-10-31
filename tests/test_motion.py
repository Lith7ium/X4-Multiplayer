"""Source-clock movement under jitter/loss; uses the actual LuaJIT renderer."""
from pathlib import Path
import unittest
from test_game_adapter import LuaRuntime


@unittest.skipUnless(LuaRuntime, 'LuaJIT unavailable')
class MotionTests(unittest.TestCase):
    def setUp(self):
        self.lua = LuaRuntime(unpack_returned_tuples=True)
        self.lua.execute((Path(__file__).resolve().parents[1] /
                          'src/extension/x4_lan_lab/ui/motion.lua').read_text())

    def test_regular_motion_continues_between_sparse_packets_then_freezes(self):
        self.lua.execute('''
          local M=X4LAN_MOTION;local t={}
          for i=0,3 do
           assert(M.push(t,i*.2,{i*20,0,0,0,0,0},{100,0,0,0,0,0},100+i*.2))
          end
          local previous=0
          for i=0,59 do
           local now=100.6+i/60;local p=M.render(t,now)
           assert(p[1]>=previous and p[1]<=95.0001)
           previous=p[1]
          end
          assert(math.abs(M.render(t,101)[1]-85)<.0001)
          assert(math.abs(M.render(t,1000)[1]-95)<.0001)
        ''')

    def test_source_time_removes_variable_arrival_spacing(self):
        self.lua.execute('''
          local M=X4LAN_MOTION;local t={}
          local jitter={0,.04,.01,.08,0,.03}
          for i=1,6 do
           local time=(i-1)*.2
           assert(M.push(t,time,{time*120,0,0,0,0,0},{120,0,0,0,0,0},50+time+jitter[i]))
          end
          for i=0,10 do
           local now=50.6+i*.04
           assert(math.abs(M.render(t,now)[1]-(now-50-.15)*120)<.00001)
          end
          assert(not M.push(t,.8,{999,0,0,0,0,0},{0,0,0,0,0,0},52))
        ''')

    def test_rotation_uses_short_arc_and_prediction_wraps(self):
        self.lua.execute('''
          local M=X4LAN_MOTION;local t={};local d=math.pi/180
          M.push(t,1,{0,0,0,179*d,0,0},{0,0,0,20*d,0,0},1)
          M.push(t,1.1,{0,0,0,-179*d,0,0},{0,0,0,20*d,0,0},1.1)
          assert(math.abs(math.abs(M.render(t,1.2)[4])-math.pi)<.00001)
          assert(M.render(t,1.4)[4]<0)
        ''')

    def test_pauses_teleports_and_invalid_numbers_do_not_invent_velocity(self):
        self.lua.execute('''
          local M=X4LAN_MOTION;local t={};local z={0,0,0,0,0,0}
          M.push(t,1,z,z,1)
          M.push(t,1.1,{20000,0,0,0,0,0},z,1.1)
          assert(M.render(t,1.1)[1]==20000 and #t.samples==1)
          M.push(t,5,{20100,0,0,0,0,0},z,5)
          assert(M.render(t,5)[1]==20100 and #t.samples==1)
          assert(not M.push(t,6,{0/0,0,0,0,0,0},z,6))
          assert(not M.push(t,6,z,{math.huge,0,0,0,0,0},6))
          local v=M.velocity({time=1,pose=z},{1,0,0,0,0,0},2)
          assert(v[1]==0)
        ''')

    def test_native_linear_velocity_follows_curve_and_stops_at_horizon(self):
        self.lua.execute('''
          local M=X4LAN_MOTION;local t={};local z={0,0,0,0,0,0}
          M.push(t,0,z,{999,999,999,1,2,3},100)
          M.push(t,.2,{20,-4,6,.1,.2,.3},{120,-30,40,1,2,3},100.2)
          local p,v=M.render(t,100,0);assert(v[1]==0 and v[2]==0 and v[3]==0 and #v==3)
          p,v=M.render(t,100.1,0);assert(math.abs(v[1]-100)<.00001 and v[2]==-20 and v[3]==30)
          local nextp=M.render(t,100.10001,0)
          for i=1,3 do assert(math.abs((nextp[i]-p[i])/.00001-v[i])<.0001) end
          p,v=M.render(t,100.3,0);assert(v[1]==120 and v[2]==-30 and v[3]==40)
          p,v=M.render(t,100.2+M.horizon+1e-9,0);assert(v[1]==0 and v[2]==0 and v[3]==0)
          local frozen=p[1];p,v=M.render(t,1000,0);assert(p[1]==frozen and v[1]==0)
        ''')

    def test_discontinuity_starts_with_zero_physical_velocity(self):
        self.lua.execute('''
          local M=X4LAN_MOTION;local t={};local z={0,0,0,0,0,0}
          M.push(t,0,z,{100,0,0,0,0,0},100)
          M.push(t,.1,{20000,0,0,0,0,0},{100,0,0,0,0,0},100.1)
          local p,v=M.render(t,100.1);assert(p[1]==20000 and v[1]==0)
          M.push(t,2,{20100,0,0,0,0,0},{100,0,0,0,0,0},102)
          p,v=M.render(t,102);assert(p[1]==20100 and v[1]==0)
        ''')

    def test_render_clock_slews_mode_delay_without_backward_jump(self):
        self.lua.execute('''
          local M=X4LAN_MOTION;local t={};local k=0
          local previous,delay=nil,.15
          for i=0,180 do
           local now=100.5+i/60;if i==30 then delay=.3 end
           while 100+k*.05<=now do assert(M.push(t,k*.05,{k*.05*10000,0,0,0,0,0},{10000,0,0,0,0,0},100+k*.05));k=k+1 end
           local target=M.clock(t,now,delay);local p=M.renderAt(t,target,.6)
           if previous then
            local step=p[1]-previous
            -- 10 km/s at 60 FPS is 166.7 m per frame; a raw .15 s delay change would hop 1.5 km back.
            assert(step>0 and math.abs(step-10000/60)<=10000/60*M.slew+.001, step)
           end
           previous=p[1]
          end
          assert(math.abs((t.lag-t.offset)-.3)<1e-9)
          assert(math.abs(M.clock(t,103.5,5)-(103.5-t.offset-5))<1e-9) -- an implausible step snaps
        ''')

    def test_retained_clock_survives_stale_reset_but_default_rebaselines(self):
        self.lua.execute('''
          local M=X4LAN_MOTION;local z={0,0,0,0,0,0}
          for _,keep in ipairs({true,false}) do
           local t={retainClock=keep}
           M.push(t,1,z,z,10)           -- offset 9 (fast packet)
           M.push(t,5,{100,0,0,0,0,0},z,14.2) -- stale gap; this packet was .2 s slower
           assert(#t.samples==1 and t.resets==1)
           assert(math.abs(t.offset-(keep and 9 or 9.2))<1e-9)
          end
        ''')

    def test_offset_leak_tracks_faster_observer_clock_and_fast_samples_pull_back(self):
        self.lua.execute('''
          local M=X4LAN_MOTION;local z={0,0,0,0,0,0}
          local t={leak=M.leak};local plain={}
          -- The observer clock runs 500 ppm fast: arrival-time grows by .5 ms/s.
          for i=0,600 do
           local time=i*.1;local arrival=100+time*1.0005
           M.push(t,time,z,z,arrival);M.push(plain,time,z,z,arrival)
          end
          assert(plain.offset==100)
          local true_offset=100+60*.0005
          assert(t.offset<=true_offset+1e-9 and t.offset>true_offset-.005, t.offset)
          M.push(t,60.1,z,z,100+60.1)   -- an unusually fast packet still lowers the estimate
          assert(math.abs(t.offset-100)<1e-9)
        ''')

    def test_declared_high_speed_mode_extends_discontinuity_rule_only_with_consistent_velocity(self):
        self.lua.execute('''
          local M=X4LAN_MOTION;local z={0,0,0,0,0,0}
          local function run(mode,vnew,d)
           local t={mode=mode}
           M.push(t,0,z,{300,0,0,0,0,0},0)
           M.push(t,.7,{d,0,0,0,0,0},{vnew,0,0,0,0,0},.7)
           return t.resets or 0
          end
          -- Highway entry during a .7 s hitch: 25 km at a sampled 30 km/s.
          assert(run('highway',30000,25000)==0)
          assert(run(nil,30000,25000)==1)        -- normal mode keeps the original rule
          assert(run('travel',30000,25000)==1)   -- above the 20 km/s travel allowance
          -- A real teleport has no consistent sampled velocity (the sender zeroes > 100 km/s).
          assert(run('highway',0,40000)==1)
        ''')

    def test_mode_classification_infers_fast_legacy_streams_with_hysteresis(self):
        self.lua.execute('''
          local M=X4LAN_MOTION;local s={}
          assert(M.classify(s,nil,300,0)=='normal')
          assert(M.classify(s,nil,5000,1)=='fast')
          assert(M.classify(s,nil,1000,2)=='fast')       -- above exit threshold
          assert(M.classify(s,nil,100,2.5)=='fast')      -- held
          assert(M.classify(s,nil,100,3.1)=='normal')
          assert(M.classify(s,'highway',100,4)=='highway')
          assert(M.classify(s,'normal',100,4.5)=='highway')
          local mode,profile=M.classify(s,'normal',100,5.1)
          assert(mode=='normal' and profile.delay==M.delay and profile.horizon==M.horizon)
          assert(M.classify({},'warp',100,0)=='normal')
        ''')

    def test_sparse_highway_npc_samples_freeze_and_snap_by_default_but_not_with_npc_track(self):
        # NPC flicker reproduction: 3 km/s (local highway), one sample per 0.6 s or 0.9 s
        # (a dense sector sharing 640 UMOVE/s), 50 ms latency, 60 fps rendering.
        self.lua.execute('''
          local M=X4LAN_MOTION
          function highway(gap,npc)
           local t={};if npc then t.stale,t.cap,t.retainClock,t.leak=2.5,50000,true,M.leak end
           local nextSample,k=0,0;local frozen,maxStep,back,prev=0,0,0,nil
           for f=0,600 do
            local now=f/60
            while nextSample+.05<=now do
             M.push(t,nextSample,{nextSample*3000,0,0,0,0,0},{3000,0,0,0,0,0},nextSample+.05)
             if npc then t.gap=gap end
             k=k+1;nextSample=k*gap
            end
            if t.samples and f>=180 then
             local p
             if npc then
              local delay=math.min(.6,math.max(M.delay,gap*1.1+.05));local horizon=math.min(1,math.max(M.horizon,gap+.25))
              p=M.renderAt(t,M.clock(t,now,delay),horizon)
             else p=M.render(t,now) end
             if prev then
              local step=p[1]-prev
              if math.abs(step)<1 then frozen=frozen+1 end
              if step<-1 then back=back+1 end
              maxStep=math.max(maxStep,math.abs(step))
             end
             prev=p[1]
            end
           end
           return frozen,maxStep,back,t.resets or 0
          end
        ''')
        for gap in (.6, .9):
            frozen, step, back, resets = self.lua.eval(f'highway({gap},false)')
            self.assertGreater(frozen, 30, gap)      # default: stands still after the horizon
            self.assertGreater(step, 300, gap)       # ... then snaps hundreds of metres
            frozen, step, back, resets = self.lua.eval(f'highway({gap},true)')
            self.assertEqual((frozen, back, resets), (0, 0, 0), gap)
            self.assertLess(step, 3000/60*1.2, gap)  # uniform 50 m per frame at 3 km/s
