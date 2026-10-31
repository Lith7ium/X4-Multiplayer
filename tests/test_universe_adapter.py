"""W01 engine doubles: scope, atomicity and protected player objects."""
from pathlib import Path
import unittest
from test_game_adapter import MOCK, LuaRuntime
from test_bubble_adapter import EXTRA

UI=Path(__file__).resolve().parents[1]/'src/extension/x4_lan_lab/ui'

@unittest.skipUnless(LuaRuntime,'LuaJIT unavailable')
class UniverseAdapterTests(unittest.TestCase):
    def make(self,slot='p1',mode='replica',setup=''):
        l=LuaRuntime(unpack_returned_tuples=True);l.execute(MOCK+EXTRA)
        l.globals().X4LAN_CONFIG.slot=slot;l.globals().X4LAN_CONFIG.world_mode=mode
        l.execute('''
          X4LAN_CONFIG.coop=true;X4LAN_CONFIG.world_sync=true
          X4LAN_CONFIG.test_profile='45841';X4LAN_CONFIG.build='world1-test'
          ConvertIDTo64Bit=function(v) return tonumber(v) end
          local old=require
          require=function(name)
           local module=old(name)
           if name=='ffi' then
            local alloc=module.new
            module.new=function(name,v) if name:find('%[') then return {} else return alloc(name,v) end end
           end
           return module
          end
          sectors={[11]=22,[42]=22,[90]=22,[101]=22,[102]=22,[103]=23,[104]=22}
          for _,id in ipairs({90,101,102,103,104}) do positions[id]={x=id,y=200,z=300,yaw=0,pitch=0,roll=0} end
          api.GetContextByClass=function(id) return sectors[id] or 22 end
          api.GetAllFactions=function(out,cap,hidden) out[0]='argon';out[1]='player';return 2 end
          api.GetNumAllFactionShips=function(f) return f=='argon' and 5 or 2 end
          api.GetAllFactionShips=function(out,cap,f)
           local values=f=='argon' and {90,101,102,103,104} or {11,42}
           for i,id in ipairs(values) do out[i-1]=id end;return #values
          end
          api.IsComponentClass=function() return true end
          GetComponentData=function(id,field)
           id=tonumber(id)
           if id==22 then return 'cluster_14_sector001_macro' end
           if id==23 then return 'cluster_01_sector001_macro' end
           return 'ship_arg_s_scout_01_a_macro',id==104 and 'player' or 'argon'
          end
          local nextReplica=200;spawns={};activated={}
          api.SpawnObjectAtPos2=function(macro,sector,p,owner)
           local id
           if X4LAN_BUBBLE_API and X4LAN_BUBBLE_API.state.proxy then id=nextReplica;nextReplica=nextReplica+1
           else id=42 end
           positions[id]=p;sectors[id]=sector;spawns[#spawns+1]={id=id,macro=macro,owner=owner};return id
          end
          api.SetObjectSectorPos=function(id,sector,p) positions[id]=p;writes[#writes+1]={id=id,sector=sector,pose=p} end
          api.ActivateObject=function(id,on) activated[#activated+1]={id=id,on=on} end
        ''')
        l.execute(setup)
        for name in ('bubble_game.lua','motion.lua','coop_game.lua','universe_protocol.lua','universe_game.lua'):
            l.execute((UI/name).read_text())
        l.execute('''
          events['X4LAN.Loaded']();now=10;frames.onUpdate()
          events['X4LAN_Bubble.Ready']();frames.onUpdate();events['X4LAN_Coop.Ready']()
          frames.onUpdate();events['X4LAN_Universe.Exclude'](nil,90);events['X4LAN_Universe.Ready']()
          U=X4LAN_BUBBLE_API.universe;P=X4LAN_UNIVERSE_PROTOCOL
          function tick(n) for i=1,n do now=now+.05;frames.onUpdate() end end
          function baseline(revision,rows,complete)
           local b=P.baseline(string.rep('a',32),revision,'cluster_14_sector001_macro',rows)
           for i=0,#rows do U.receive(P.split(P.packet(b,client.lease,i),'|'),now) end
           if complete then U.receive(P.split(P.packet(b,client.lease,#rows+1),'|'),now) end
           return b
          end
          testRow='1,ship,ship_arg_s_scout_01_a_macro,argon,1500,200,300,0,0,0,none,0'
        ''')
        return l

    def test_host_enumerates_npcs_excludes_player_fixture_and_other_sector(self):
        l=self.make();l.execute('''
          U.receive({'UINTEREST','1',client.lease,'1','cluster_14_sector001_macro'},now)
          tick(20);assert(U.sender and #U.sender.baseline.rows==2)
          assert(U.sender.baseline.rows[1]:find(',101,200,300,',1,true))
          assert(U.sender.baseline.rows[2]:find(',102,200,300,',1,true))
          assert(not U.failed and #removed==0)
        ''')

    def test_host_captures_each_active_guest_sector(self):
        l=self.make();l.execute('''
          X4LAN_BUBBLE_API.coop.sectors.cluster_01_sector001_macro=23
          U.receive({'UINTEREST','1',client.lease,'1','cluster_14_sector001_macro',string.rep('b',32)},now)
          U.receive({'UINTEREST','1',client.lease,'1','cluster_01_sector001_macro',string.rep('c',32)},now)
          tick(20);U.sender.done=true;tick(100)
          assert(U.lastScans.cluster_14_sector001_macro and U.lastScans.cluster_01_sector001_macro)
          assert(U.publishes.cluster_14_sector001_macro and U.publishes.cluster_01_sector001_macro)
          assert(U.revision>=2 and not U.failed)
          assert(#U.publish==3) -- two Argon Prime ships plus one Grand Exchange ship
        ''')

    def test_partial_baseline_never_spawns_or_suppresses_npcs(self):
        l=self.make('p2');l.execute('''
          tick(1);baseline(1,{testRow},false);tick(20)
          assert(#spawns==1 and #removed==0 and U.latest==nil and not U.failed)
        ''')

    def test_changing_fleet_retries_capture_without_committing_empty_state(self):
        l=self.make();l.execute('''
          local real=api.GetAllFactionShips;local once=true
          api.GetAllFactionShips=function(out,cap,f)
           local count=real(out,cap,f)
           if once then once=false;return count-1 end
           return count
          end
          U.receive({'UINTEREST','1',client.lease,'1','cluster_14_sector001_macro'},now)
          tick(5);assert(not U.sender and not U.failed)
          tick(70);assert(U.sender and #U.sender.baseline.rows==2 and not U.failed)
        ''')

    def test_new_baseline_replaces_stale_motion_after_packet_loss(self):
        l=self.make('p2');l.execute('''
          tick(1);baseline(1,{testRow},true);tick(1)
          U.receive({'UPOS','1',client.lease,string.rep('a',32),'1','2','cluster_14_sector001_macro','1600','200','300','0','0','0'},now)
          tick(20);assert(positions[200].x==1600)
          baseline(2,{(testRow:gsub('1500','1700'))},true);tick(2)
          assert(positions[200].x==1700 and not U.failed)
        ''')

    def test_complete_baseline_replicates_and_only_suppresses_unprotected_local_npcs(self):
        l=self.make('p2');l.execute('''
          tick(1);baseline(1,{testRow},true);tick(20)
          assert(not U.failed and #spawns==2 and spawns[2].owner=='argon')
          assert(positions[200].x==1500 and #activated>0)
          assert(#removed==2 and positions[101]==nil and positions[102]==nil)
          for _,id in ipairs({11,42,90,103,104,200}) do assert(positions[id]) end
          baseline(2,{},true);tick(1);assert(positions[200]==nil)
        ''')

    def test_observe_mode_never_changes_engine_world(self):
        l=self.make('p2','observe');l.execute('''
          tick(1);baseline(1,{testRow},true);tick(20)
          assert(U.latest and #spawns==1 and #removed==0 and #activated==0)
        ''')

    def test_pause_defers_engine_application_and_duplicate_commit_does_not_respawn(self):
        l=self.make('p2');l.execute('''
          tick(1);paused=true;local b=baseline(1,{testRow},true);tick(4)
          assert(#spawns==1 and #removed==0)
          paused=false;tick(1);assert(#spawns==2)
          U.receive(P.split(P.packet(b,client.lease,2),'|'),now);tick(2);assert(#spawns==2)
        ''')

    def test_player_boarding_a_replica_stops_before_removal(self):
        l=self.make('p2');l.execute('''
          tick(1);baseline(1,{testRow},true);tick(1);container=200
          baseline(2,{},true);tick(1)
          assert(U.failed and positions[200] and not X4LAN_BUBBLE_API.state.failed)
        ''')

    def test_motion_is_epoch_checked_and_unknown_objects_do_not_spawn(self):
        l=self.make('p2');l.execute('''
          tick(1);baseline(1,{testRow},true);tick(1)
          local f={'UPOS','1',client.lease,string.rep('a',32),'1','2','cluster_14_sector001_macro','1600','200','300','0','0','0'}
          U.receive(f,now);tick(3);assert(positions[200].x==1600)
          f[6]='1';f[8]='9000';U.receive(f,now);tick(3);assert(positions[200].x==1600)
          f[6]='3';f[4]=string.rep('f',32);U.receive(f,now);tick(3);assert(positions[200].x==1600)
          f[4]=string.rep('a',32);f[5]='999';U.receive(f,now);tick(3);assert(#spawns==2)
        ''')


NPC_HELPERS = '''
  function umove(id,seq,time,x,vx)
   return {'UMOVE','1',client.lease,string.rep('a',32),tostring(id),tostring(seq),'cluster_14_sector001_macro',
    string.format('%.6f',time),string.format('%.3f',x),'200','300','0','0','0',tostring(vx),'0','0','0','0','0'}
  end
  function npcRow(id,x) return id..',ship,ship_arg_s_scout_01_a_macro,argon,'..x..',200,300,0,0,0,none,0' end
  -- Engine double: zone context changes every 2 km (temp zones at highway speed).
  api.GetContextByClass=function(id,class)
   if class=='zone' then return 5000+math.floor(((positions[id] or {}).x or 0)/2000) end
   return sectors[id] or 22
  end
  function writesFor(object,from) local n=0;for i=from,#writes do if writes[i].id==object then n=n+1 end end;return n end
'''


@unittest.skipUnless(LuaRuntime,'LuaJIT unavailable')
class NpcFlickerTests(unittest.TestCase):
    """Synthetic reproduction of guest NPC flicker at highway speed; engine doubles only."""
    make = UniverseAdapterTests.make

    def guest(self,setup=''):
        l=self.make('p2',setup=setup);l.execute(NPC_HELPERS);return l

    def test_highway_replica_with_sparse_samples_moves_every_frame_without_freeze_or_snap(self):
        l=self.guest();l.execute('''
          tick(1);baseline(1,{testRow},true);tick(2)
          local object=U.replicas[1].object;assert(object==200)
          local t0=now;local nextS=now;local seq=1
          local frozen,back,missed,maxStep,prev=0,0,0,0,nil
          for f=1,600 do
           now=now+1/60
           -- 0.6 s per sample: one mover among ~380 sharing 640 UMOVE/s.
           while nextS+.05<=now do seq=seq+1;U.receive(umove(1,seq,nextS,1500+(nextS-t0)*3000,3000),now);nextS=nextS+.6 end
           local before=#writes+1;frames.onUpdate()
           if f>120 then
            if writesFor(object,before)~=1 then missed=missed+1 end
            local step=positions[object].x-prev
            if math.abs(step)<1 then frozen=frozen+1 end
            if step<-1 then back=back+1 end
            maxStep=math.max(maxStep,math.abs(step))
           end
           prev=positions[object].x
          end
          assert(frozen==0 and back==0 and missed==0,frozen..' '..back..' '..missed)
          assert(maxStep<3000/60*1.25,maxStep)
          assert(U.replicas[1].track.resets==nil and #spawns==2 and not U.failed)
        ''')

    def test_dense_highway_scene_writes_every_fast_replica_every_frame(self):
        l=self.guest();l.execute('''
          tick(1);local rows={};for id=1,200 do rows[#rows+1]=npcRow(id,id*100) end
          baseline(1,rows,true);tick(3)
          local t0=now;local nextS={};local seq=1
          for id=1,200 do nextS[id]=now+(id%20)*.015 end
          local missed,back=0,0;local prev={}
          for f=1,240 do
           now=now+1/60
           for id=1,200 do
            while nextS[id]+.05<=now do
             seq=seq+1;U.receive(umove(id,seq,nextS[id],id*100+(nextS[id]-t0)*3000,3000),now);nextS[id]=nextS[id]+.3
            end
           end
           local before=#writes+1;frames.onUpdate()
           for id=1,200 do
            local object=U.replicas[id].object
            if f>90 then
             if writesFor(object,before)~=1 then missed=missed+1 end
             if positions[object].x<prev[id]-1 then back=back+1 end
            end
            prev[id]=positions[object].x
           end
          end
          assert(missed==0 and back==0,missed..' '..back)
          assert(#U.hot==200 and not U.failed)
        ''')

    def test_baseline_with_older_capture_pose_does_not_snap_moving_replica_back(self):
        l=self.guest();l.execute('''
          tick(1);baseline(1,{testRow},true);tick(2)
          local object=U.replicas[1].object;local t0=now;local seq=1;local x
          local function run(count,send)
           local back=0
           for f=1,count do
            now=now+1/60
            if send and f%54==0 then seq=seq+1;U.receive(umove(1,seq,now-.05,1500+(now-.05-t0)*3000,3000),now) end
            local before=positions[object].x;frames.onUpdate()
            if positions[object].x<before-1 then back=back+1 end
           end
           return back
          end
          -- 0.9 s sample spacing (dense sector); the baseline arrives 0.8 s after a sample.
          assert(run(270,true)==0 and run(48,false)==0)
          -- Baseline captured ~2 s ago (plus transfer): 6 km behind the live track.
          x=positions[object].x;baseline(2,{npcRow(1,math.floor(x-6000))},true)
          assert(run(120,true)==0 and positions[object].x>x)
          -- Motion feed lost for longer than the hold: the baseline recovers the replica.
          run(240,false);baseline(3,{npcRow(1,777)},true);run(2,false)
          assert(positions[object].x==777 and not U.failed)
        ''')

    def test_diagnostics_are_inert_by_default_and_count_churn_when_enabled(self):
        l=self.guest();l.execute('''
          tick(1);baseline(1,{testRow},true);tick(60)
          for _,v in ipairs(logs) do assert(not v:find('NPC_DIAG',1,true)) end
        ''')
        l=self.guest('X4LAN_CONFIG.npc_motion_diagnostics=1');l.execute('''
          tick(1);baseline(1,{testRow},true);tick(30)
          local object=U.replicas[1].object;local seq=1
          for i=1,10 do seq=seq+1;U.receive(umove(1,seq,now,1500+i*30,300),now);tick(1) end
          seq=seq+1;U.receive(umove(1,seq,now,90000,300),now);tick(10)  -- 88 km discontinuity
          positions[object]=nil;tick(30)                               -- engine removed the replica
          rows={};for _,v in ipairs(logs) do if v:find('NPC_DIAG',1,true) then rows[#rows+1]=v end end
          function total(field) local n=0;for _,v in ipairs(rows) do n=n+(tonumber(v:match(' '..field..'=([%d.]+)')) or 0) end;return n end
        ''')
        rows=list(l.eval('rows').values())
        self.assertTrue(4<=len(rows)<=6, rows)
        self.assertTrue(all(r.startswith('[X4LAN] UNIVERSE NPC_DIAG role=guest dt=') for r in rows), rows)
        self.assertIn('sector=cluster_14_sector001_macro replicas=1 hot=0 baseline_age=', rows[0])
        for field, expected in (('creates',1),('respawns',1),('commits',1),('reset_jump',1),('suppressed',2),('samples',11)):
            self.assertEqual(l.eval(f"total('{field}')"), expected, field)
        self.assertGreaterEqual(l.eval("total('jumps')"), 1)
        self.assertGreater(l.eval("total('max_jump')"), 80000)

    def test_host_republishes_still_npcs_rarely_so_movers_keep_a_dense_sample_rate(self):
        l=self.make();l.execute('''
          local ids={};for id=1001,1300 do ids[#ids+1]=id;positions[id]={x=id*10,y=0,z=0,yaw=0,pitch=0,roll=0} end
          api.GetNumAllFactionShips=function(f) return f=='argon' and #ids or 0 end
          api.GetAllFactionShips=function(out,cap,f)
           if f~='argon' then return 0 end;for i,id in ipairs(ids) do out[i-1]=id end;return #ids
          end
          U.receive({'UINTEREST','1',client.lease,'1','cluster_14_sector001_macro'},now)
          for i=1,20 do now=now+.05;frames.onUpdate() end
          assert(#U.publish==300,#U.publish)
          for i=1,40 do now=now+.05;for id=1001,1030 do positions[id].x=positions[id].x+150 end;frames.onUpdate() end
          local start=#sent;local counts={}
          for i=1,60 do  -- 3 s
           now=now+.05;for id=1001,1030 do positions[id].x=positions[id].x+150 end;frames.onUpdate()
          end
          for i=start+1,#sent do
           local f=P.split(sent[i],'|')
           if f[1]=='UMOVE' then local id=tonumber(f[5]);counts[id]=(counts[id] or 0)+1 end
          end
          local moverMin,stillMax=math.huge,0
          for _,e in ipairs(U.publish) do
           local n=counts[e.id] or 0
           if e.object<=1030 then moverMin=math.min(moverMin,n) else stillMax=math.max(stillMax,n) end
          end
          -- Blind round robin gave every object 32*20/300 = 2.1 samples/s.
          assert(moverMin>=45 and stillMax<=2,moverMin..' '..stillMax)
        ''')

    def test_stale_baseline_and_invalid_diagnostic_value_are_logged_as_disabled_features(self):
        l=self.guest("X4LAN_CONFIG.npc_motion_diagnostics='often'");l.execute('''
          tick(1);baseline(1,{testRow},true);tick(320)
          local disabled,invalid=0,0
          for _,v in ipairs(logs) do
           if v:find('FEATURE npc_replicas DISABLED reason=baseline_stale',1,true) then disabled=disabled+1 end
           if v:find('FEATURE npc_motion_diagnostics DISABLED reason=invalid_value',1,true) then invalid=invalid+1 end
           assert(not v:find('NPC_DIAG',1,true))
          end
          assert(disabled==1 and invalid==1,disabled..' '..invalid)
          baseline(2,{testRow},true);tick(2)
          local resumed=false;for _,v in ipairs(logs) do resumed=resumed or v:find('FEATURE_RESUMED npc_replicas',1,true) end
          assert(resumed)
        ''')
