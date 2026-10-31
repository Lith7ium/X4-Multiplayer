-- Original INTERNAL W01 NPC replication adapter. No third-party framework.
-- Native cargo uses a separate authority adapter; NPC combat remains separate work.
local cfg=X4LAN_CONFIG;local A=X4LAN_BUBBLE_API;local P=X4LAN_UNIVERSE_PROTOCOL
if not cfg or not cfg.world_sync or not A or not A.state.active then return end
local ffi=require('ffi');local C=ffi.C;local s=A.state
local M=assert(X4LAN_MOTION,'motion buffer missing')
assert(type(cfg.test_profile)=='string' and cfg.test_profile:match('^4584[1-8]$') and cfg.build:sub(1,7)=='world1-','world adapter requires isolated W01 build')
assert(cfg.world_mode=='observe' or cfg.world_mode=='replica','select an explicit world test mode')
ffi.cdef[[
uint32_t GetAllFactions(const char**,uint32_t,bool);
uint32_t GetNumAllFactionShips(const char*);
uint32_t GetAllFactionShips(UniverseID*,uint32_t,const char*);
bool IsComponentClass(UniverseID,const char*);
void ActivateObject(UniverseID,bool);
void RemoveComponent(UniverseID);
]]
local u={receiver=P.receiver(),excluded={},identities={},nextId=0,revision=0,interestSeq=0,
 replicas={},replicaKeys={},motionSeq=0,publish={},publishes={},lastScans={},wanted={},motionCursor=1,hot={},frameNo=0}
A.universe=u
local function log(v) DebugError('[X4LAN] UNIVERSE '..v) end
-- NPC replica motion (see reports/NPC-FLICKER-20261025.txt). Replicas are pose-only: the
-- engine holds a replica still between two SetObjectSectorPos calls, so a fast replica must
-- be written every frame, and sparse samples must be interpolated, not frozen and snapped.
-- Host: still/slow objects are republished less often (heartbeat) so movers get the budget.
-- Guest: per-track gap tolerance, adaptive interpolation delay from the observed sample gap,
-- a bounded-rate render clock, and a baseline pose never overrides a live motion track.
local NPC={still=1,slow=20,stillEvery=2,slowEvery=.5,sendStale=2.5,missingEvery=1,
 stale=2.5,cap=50000,hold=3,fast=150,hotBudget=256,maxDelay=.6,maxHorizon=1,jump=100,suppressEvery=2}
u.npcMotion=NPC
-- Diagnostic (config npc_motion_diagnostics: seconds, or true = 1 s; on in internal packages,
-- false in the inert source config). Counts only; nothing is computed when off.
local diagEvery=cfg.npc_motion_diagnostics==true and 1 or tonumber(cfg.npc_motion_diagnostics)
if diagEvery and diagEvery<=0 then diagEvery=nil end
local function feature(name,reason) DebugError('[X4LAN] FEATURE '..name..' DISABLED reason='..reason) end
if not diagEvery and cfg.npc_motion_diagnostics then feature('npc_motion_diagnostics','invalid_value') end
local diag={}
local function count(name,n) if diagEvery then diag[name]=(diag[name] or 0)+(n or 1) end end
local function peak(name,v) if diagEvery and v>(diag[name] or 0) then diag[name]=v end end
local DIAG_FIELDS={'creates','respawns','removes','hides','shows','suppressed','commits','samples',
 'reset_gap','reset_jump','baseline_snap','writes','hot_writes','jumps','max_jump','max_gap','max_delay',
 'sent','sent_fast','sent_slow','sent_still','paused'}
local function diagFlush(now,role,extra)
 if not diagEvery then return end
 if not u.diagAt then u.diagAt=now;diag={};return end
 if now-u.diagAt<diagEvery then return end
 local out={'NPC_DIAG role='..role,string.format('dt=%.2f',now-u.diagAt),extra}
 for _,name in ipairs(DIAG_FIELDS) do
  local v=diag[name]
  if v then out[#out+1]=name..'='..(v%1==0 and tostring(v) or string.format('%.3f',v)) end
 end
 log(table.concat(out,' '));u.diagAt=now;diag={}
end
local function key(object) return tostring(object):gsub('ULL$','') end
local function send(p) s.transport:send(p) end
local function protected(object)
 local k=key(object)
 return u.excluded[k] or u.replicaKeys[k] or A.coop.entity(object)>0 or
  object==C.GetPlayerControlledShipID() or object==C.GetPlayerOccupiedShipID() or object==C.GetPlayerContainerID()
end
local function currentSector()
 local object=C.GetPlayerContainerID()
 if object==0 then return nil end
 local sector=C.GetContextByClass(object,'sector',false)
 if sector==0 then return nil end
 local name=GetComponentData(A.id(sector),'macro')
 if not P.atom(name,true) then return nil end
 A.coop.sectors[name]=sector;return name,sector
end
local function beginScan(name,sector)
 local factions=ffi.new('const char*[256]')
 local count=tonumber(C.GetAllFactions(factions,256,true))
 assert(count>0 and count<256,'faction enumeration unavailable or exceeded limit; refusing an incomplete baseline')
 local names={};for i=0,count-1 do names[#names+1]=ffi.string(factions[i]) end
 return {name=name,sector=sector,factions=names,faction=1,index=0,seen={},objects={},total=0}
end
local function scanStep(scan)
 -- Bound per-frame enumeration; metadata/positions are read only for this sector.
 for _=1,512 do
  if not scan.buffer or scan.index>=scan.count then
   local faction=scan.factions[scan.faction]
   if not faction then return true end
   scan.faction=scan.faction+1
   local count=tonumber(C.GetNumAllFactionShips(faction))
   assert(count<=250000,'faction fleet exceeds internal scan limit')
   -- Headroom plus post-read count catches growth/truncation during enumeration.
   local cap=count+64;scan.buffer=ffi.new('UniverseID[?]',cap)
   scan.count=tonumber(C.GetAllFactionShips(scan.buffer,cap,faction));scan.index=0
   if scan.count>=cap or tonumber(C.GetNumAllFactionShips(faction))>scan.count then
    -- A changing fleet invalidates this capture, not the entire world adapter.
    scan.retry=true;return true
   end
   return false
  end
  local object=scan.buffer[scan.index];scan.index=scan.index+1
  local k=key(object)
  if not scan.seen[k] then
   scan.seen[k]=true;scan.total=scan.total+1
   assert(scan.total<=250000,'universe fleet exceeds internal scan limit')
   if C.IsComponentOperational(object) and not protected(object) and C.IsComponentClass(object,'ship') and
      C.GetContextByClass(object,'sector',false)==scan.sector then
    local macro,owner=GetComponentData(A.id(object),'macro','owner')
    assert(P.atom(macro,true) and P.atom(owner),'NPC descriptor unavailable; refusing incomplete baseline')
    -- player_factions: a host ship owned only by the host's human replicates as its pool faction.
    if owner=='player' and cfg.slot=='p1' and A.players then owner=A.players.personalOwner(object) or owner end
    if owner~='player' then
     scan.objects[#scan.objects+1]={object=object,key=k,macro=macro,owner=owner}
     assert(#scan.objects<=P.limit,'sector object limit exceeded; refusing a truncated baseline')
    end
   end
  end
 end
 return false
end
local function pose(object)
 local p=C.GetObjectPositionInSector(object);return {p.x,p.y,p.z,p.yaw,p.pitch,p.roll}
end
local function poseFields(p)
 local out={};for _,v in ipairs(p) do out[#out+1]=string.format('%.9g',v) end;return table.concat(out,',')
end
local function captured(scan)
 local rows={};local objects={}
 for _,entry in ipairs(scan.objects) do
  if C.IsComponentOperational(entry.object) and C.GetContextByClass(entry.object,'sector',false)==scan.sector then
   local identity=u.identities[entry.key]
   local code=A.npcCombat and A.npcCombat.code(entry.object)
   if not identity or identity.retired or identity.macro~=entry.macro or A.npcCombat and
    (identity.code~=code or identity.owner~=entry.owner or identity.sector~=scan.name) then
    u.nextId=u.nextId+1;assert(u.nextId<=2147483647,'world ID space exhausted')
    identity={id=u.nextId,macro=entry.macro,code=code,owner=entry.owner,sector=scan.name};u.identities[entry.key]=identity
   end
   local row=identity.id..',ship,'..entry.macro..','..entry.owner..','..poseFields(pose(entry.object))..',none,0'
   assert(P.record(row),'invalid captured NPC state')
   objects[#objects+1]={id=identity.id,object=entry.object,sector=scan.name,identity=identity}
   rows[#rows+1]={id=identity.id,row=row}
  end
 end
 if A.cargo then for _,row in ipairs(A.cargo.rows(scan.sector)) do rows[#rows+1]=row end end
 assert(#rows<=P.limit,'combined sector object limit exceeded')
 u.lastScan=scan;u.lastScans[scan.name]=scan;u.cargoDirty=false
 table.sort(rows,function(a,b) return a.id<b.id end)
 local ordered={};for i,r in ipairs(rows) do ordered[i]=r.row end
 u.revision=u.revision+1;u.publishes[scan.name]=objects;u.publish={};u.motionCursor=1
 for _,entries in pairs(u.publishes) do for _,entry in ipairs(entries) do u.publish[#u.publish+1]=entry end end
 u.sender=P.sender(P.baseline(s.client.lease,u.revision,scan.name,ordered))
 log('CAPTURE revision='..u.revision..' sector='..scan.name..' objects='..#ordered)
end
local function removeReplica(r)
 local object=r.object
 if object then count('removes') end
 if object and C.IsComponentOperational(object) then
  -- Recheck live player IDs; a player may have boarded since creation.
  if A.coop.entity(object)>0 or object==C.GetPlayerControlledShipID() or object==C.GetPlayerOccupiedShipID() or object==C.GetPlayerContainerID() then
   error('player entered an NPC replica; end this internal scene test')
  end
  C.RemoveComponent(object)
 end
 if object then u.replicaKeys[key(object)]=nil end
end
local function accept(b,now)
 local name=currentSector();if b.sector~=name then return end
 u.acceptedAt=now;u.latest=b
 if cfg.world_mode=='observe' then log('OBSERVED revision='..b.revision..' sector='..b.sector..' objects='..#b.rows);return end
 local wanted={};u.renderIds={};u.renderCursor=1
 for _,row in ipairs(b.rows) do
  local entity=assert(P.record(row));assert(entity.kind=='ship' or A.cargo,'cargo adapter missing')
  wanted[entity.id]=entity
  u.renderIds[#u.renderIds+1]=entity.id
 end
 for id,r in pairs(u.replicas) do
  local next=wanted[id]
  if not next or next.macro~=r.entity.macro or next.owner~=r.entity.owner or next.kind~=r.entity.kind or next.ware~=r.entity.ware or next.amount~=r.entity.amount or b.sector~=r.sector then
   removeReplica(r);u.replicas[id]=nil
  end
 end
 for id,entity in pairs(wanted) do
  local r=u.replicas[id]
  if not r then r={entity=entity,sector=b.sector,seq=-1};u.replicas[id]=r
  else
   r.entity=entity
   -- A baseline pose is older than its capture plus transfer time. It replaces live motion
   -- only after the motion feed has been silent for NPC.hold seconds (packet loss/recovery);
   -- otherwise a moving replica would jump back to the capture position on every baseline.
   if not r.at or now-r.at>=(r.modern and NPC.hold or M.stale) then
    if r.track then count('baseline_snap') end
    r.track=nil;r.target=nil
   end
  end
 end
 count('commits')
 log('COMMITTED revision='..b.revision..' sector='..b.sector..' objects='..#b.rows)
end
function u.receive(f,now)
 if f[1]=='UINTEREST' then
  if cfg.slot=='p1' and (#f==5 or #f==6) and P.integer(f[4]) and P.atom(f[5],true) then u.wanted[f[5]]=now end
  return true
 elseif f[1]=='UACK' then
  local rev=P.integer(f[5]);local index=f[6]=='-1' and -1 or P.integer(f[6])
  if cfg.slot=='p1' and #f==6 and rev and index and u.sender then u.sender:ack(f[4],rev,index) end
  return true
 elseif f[1]=='UPOS' or f[1]=='UMOVE' then
  local modern=f[1]=='UMOVE'
  if cfg.slot=='p1' or #f~=(modern and 20 or 13) or not u.latest or f[4]~=u.latest.epoch then return true end
  local id,seq=P.integer(f[5]),P.integer(f[6]);local r=id and u.replicas[id]
  if not r or not seq or seq<=r.seq or f[7]~=r.sector then return true end
  local start=modern and 9 or 8
  local row=id..',ship,'..r.entity.macro..','..r.entity.owner..','..table.concat(f,',',start,start+5)..',none,0'
  local parsed=P.record(row);if not parsed then return true end
  local velocity={0,0,0,0,0,0};local time=now
  if modern then
   time=tonumber(f[8]);for i=1,6 do velocity[i]=tonumber(f[14+i]) end
  end
  local track=r.track
  if not track then
   track={}
   -- Host source time is GetCurRealTime of one process: the clock mapping survives a gap.
   if modern then track.stale,track.cap,track.retainClock,track.leak=NPC.stale,NPC.cap,true,M.leak end
  end
  if not modern and not r.track then M.push(track,now-.1,r.entity.pose,velocity,now-.1) end
  local samples=track.samples;local last=samples and samples[#samples];local resets=track.resets or 0
  if not M.push(track,time,parsed.pose,velocity,now) then return true end
  count('samples')
  if (track.resets or 0)>resets then count('reset_'..(track.lastReset or 'gap'))
  elseif modern and last then
   -- Observed source spacing of this replica's samples; rises fast, decays slowly.
   local gap=time-last.time;local old=track.gap or gap
   track.gap=gap>old and old*.5+gap*.5 or old*.8+gap*.2
   peak('max_gap',gap)
  end
  r.track=track;r.target=parsed.pose;r.at=now;r.seq=seq;r.modern=modern
  return true
 elseif f[1]=='UBEGIN' or f[1]=='UOBJECT' or f[1]=='UCOMMIT' then
  if cfg.slot=='p1' or u.failed then return true end
  local ack,b=u.receiver:receive(f,now)
  if b then u.incoming=b end -- Engine changes belong to an unpaused frame.
  if ack then send('UACK|1|'..s.client.lease..'|'..f[4]..'|'..f[5]..'|'..ack) end
  return true
 end
 return false
end
-- Interpolation delay/horizon from the observed sample spacing: sparse but regular samples
-- are interpolated (delay just above the gap) instead of predicted 0.35 s and then frozen.
local function profile(track)
 local gap=track.gap or 0
 return math.min(NPC.maxDelay,math.max(M.delay,gap*1.1+.05)),math.min(NPC.maxHorizon,math.max(M.horizon,gap+.25))
end
local function apply(r,now,sector,hot)
 if r.entity.kind=='drop' then A.cargo.ensure(r,sector);return end
 if A.npcCombat and A.npcCombat.hidden(r) then
  if not r.hidden then r.hidden=true;count('hides') end
  A.npcCombat.hide(r,now);return
 end
 if r.hidden then r.hidden=nil;count('shows') end
 local p=r.entity.pose;local speed=0
 if r.track then
  if r.modern then
   local delay,horizon=profile(r.track);peak('max_delay',delay)
   local q,v=M.renderAt(r.track,M.clock(r.track,now,delay),horizon)
   if q then p=q;speed=M.speed(v) end
  else p=M.render(r.track,now,.1) or p end
 end
 local value=ffi.new('UIPosRot',{x=p[1],y=p[2],z=p[3],yaw=p[4]*180/math.pi,pitch=p[5]*180/math.pi,roll=p[6]*180/math.pi})
 if not r.object or not C.IsComponentOperational(r.object) then
  count(r.object and 'respawns' or 'creates')
  r.object=C.SpawnObjectAtPos2(r.entity.macro,sector,value,r.entity.owner)
  assert(r.object~=0,'NPC replica spawn failed');u.replicaKeys[key(r.object)]=true
  C.ActivateObject(r.object,false);r.applied=nil
 end
 assert(r.object~=C.GetPlayerContainerID() and r.object~=C.GetPlayerControlledShipID(),'player in NPC replica')
 local changed=not r.applied
 if r.applied then for i=1,6 do if math.abs(p[i]-r.applied[i])>(i<=3 and .005 or .00001) then changed=true end end end
 if changed then
  if diagEvery and r.applied and r.appliedAt then
   local d=math.sqrt((p[1]-r.applied[1])^2+(p[2]-r.applied[2])^2+(p[3]-r.applied[3])^2)
   local expected=math.max(speed,r.speed or 0)*(now-r.appliedAt)
   if d>NPC.jump+2*expected then count('jumps');peak('max_jump',d) end
  end
  C.SetObjectSectorPos(r.object,sector,value);r.applied=p;r.appliedAt=now
  count(hot and 'hot_writes' or 'writes')
 end
 r.speed=speed;r.frame=u.frameNo
 if speed>=NPC.fast then u.hot[#u.hot+1]=r end
end
local function render(now,sector)
 local ids=u.renderIds or {};if #ids==0 then return end
 u.frameNo=u.frameNo+1
 -- A pose-only replica stands still until the next write. Fast movers are therefore
 -- written every frame (bounded, rotating when more are fast than the budget).
 local hot=u.hot;u.hot={}
 local n=#hot;local start=n>0 and (u.hotCursor or 0)%n or 0;local used=0
 for i=1,n do
  local r=hot[(start+i-1)%n+1]
  if u.replicas[r.entity.id]==r and r.sector==u.interest then
   if used<NPC.hotBudget then used=used+1;apply(r,now,sector,true)
   else u.hot[#u.hot+1]=r end
  end
 end
 u.hotCursor=start+used
 for _=1,math.min(128,#ids) do
  local r=u.replicas[ids[u.renderCursor]];u.renderCursor=u.renderCursor%#ids+1
  if r and r.sector==u.interest and r.frame~=u.frameNo and (not r.nextApply or now>=r.nextApply) then
   r.nextApply=now+.015
   apply(r,now,sector,false)
  end
 end
end
local function frame(now)
 if u.failed or not s.client or not s.client.lease or not A.coop.ready then return end
 if not u.setup then u.setup=true;AddUITriggeredEvent('X4LAN_Universe','Setup');return end
 if not u.ready or A.paused() then return end
 local name,sector=currentSector();if not name then return end
 if cfg.slot~='p1' then
  if u.interest~=name then
   u.interest=name;u.scan=nil;u.receiver.pending=nil;u.latest=nil;u.nextScan=0
   for _,r in pairs(u.replicas) do removeReplica(r) end;u.replicas={}
  end
  if now>=(u.nextInterest or 0) then
   u.nextInterest=now+1;u.interestSeq=u.interestSeq+1
   send('UINTEREST|1|'..s.client.lease..'|'..u.interestSeq..'|'..name)
  end
  if u.incoming then local incoming=u.incoming;u.incoming=nil;accept(incoming,now) end
  if cfg.world_mode=='replica' and u.latest and now-u.acceptedAt>=15 then
   count('paused')
   if not u.renderPaused then u.renderPaused=true;feature('npc_replicas','baseline_stale age='..string.format('%.1f',now-u.acceptedAt)) end
  elseif u.renderPaused and u.latest then u.renderPaused=nil;log('FEATURE_RESUMED npc_replicas') end
  if cfg.world_mode=='replica' and u.latest and now-u.acceptedAt<15 then
   render(now,sector)
   if not u.scan and now>=(u.nextScan or 0) then u.scan=beginScan(name,sector) end
   if u.scan and scanStep(u.scan) then
    if u.scan.retry then u.scan=nil;u.nextScan=now+1;return end
    local n=0
    for _,entry in ipairs(u.scan.objects) do
     if C.IsComponentOperational(entry.object) and not protected(entry.object) and C.GetContextByClass(entry.object,'sector',false)==sector then
      C.RemoveComponent(entry.object);n=n+1
     end
    end
    -- Locally spawned/arriving NPCs stay visible until this pass; keep the window short.
    count('suppressed',n)
    if n>0 or now>=(u.nextSuppressLog or 0) then
     u.nextSuppressLog=now+30;log('LOCAL_NPC_SUPPRESSED count='..n..' sector='..name)
    end
    u.scan=nil;u.nextScan=now+NPC.suppressEvery
   end
  end
  if diagEvery then
   local live=0;for _ in pairs(u.replicas) do live=live+1 end
   diagFlush(now,'guest','sector='..name..' replicas='..live..' hot='..#u.hot..
    ' baseline_age='..(u.acceptedAt and string.format('%.1f',now-u.acceptedAt) or '-'))
  end
  return
 end
 for name,at in pairs(u.wanted) do if now-at>5 then u.wanted[name]=nil end end
 if u.cargoDirty and (not u.sender or u.sender.done) then
  for name,scan in pairs(u.lastScans) do if u.wanted[name] then captured(scan);break end end
 end
 if u.sender and not u.sender.done then
  for _,p in ipairs(u.sender:packets(s.client.lease,now)) do send(p) end
 elseif not u.scan and now>=(u.nextScan or 0) then
  local names={};for name in pairs(u.wanted) do names[#names+1]=name end;table.sort(names)
  if #names>0 then
   u.scanChoice=(u.scanChoice or 0)%#names+1
   local name=names[u.scanChoice];local target=A.coop.sectorId(name)
   if target then u.scan=beginScan(name,target) end
  end
 end
 if u.scan and scanStep(u.scan) then
  if not u.scan.retry then captured(u.scan) end
  u.scan=nil;u.nextScan=now+2
 end
 if #u.publish>0 and now>=(u.nextMotion or 0) then
  u.nextMotion=now+.05
  -- 32 records per tick are shared by the sector. Movers are always due; slow and still
  -- objects (docked, parked) are republished every NPC.slowEvery/NPC.stillEvery seconds, so
  -- a dense sector no longer stretches every mover's sample spacing past the motion buffer.
  local total=#u.publish;local sent,visited=0,0
  while sent<32 and visited<math.min(total,512) do
   local entry=u.publish[u.motionCursor];u.motionCursor=u.motionCursor%total+1;visited=visited+1
   local identity=entry.identity
   if now>=(identity.due or 0) then
    local target=A.coop.sectors[entry.sector]
    if C.IsComponentOperational(entry.object) and C.GetContextByClass(entry.object,'sector',false)==target then
     u.motionSeq=u.motionSeq+1;sent=sent+1
     local p=pose(entry.object);local previous=identity.motion
     local velocity=M.velocity(previous,p,now,NPC.sendStale)
     local rate=math.huge
     if previous and now-previous.time>.001 then
      local dt=now-previous.time;local d=0
      for i=1,3 do d=d+(p[i]-previous.pose[i])^2 end
      local turn=0;for i=4,6 do turn=math.max(turn,math.abs(M.angle(p[i]-previous.pose[i]))) end
      rate=math.max(math.sqrt(d),turn*200)/dt
     end
     if rate<NPC.still then identity.due=now+NPC.stillEvery;count('sent_still')
     elseif rate<NPC.slow then identity.due=now+NPC.slowEvery;count('sent_slow')
     else identity.due=nil;count('sent_fast') end
     count('sent');identity.motion={time=now,pose=p}
     send('UMOVE|1|'..s.client.lease..'|'..s.client.lease..'|'..entry.id..'|'..u.motionSeq..'|'..entry.sector..'|'..
      string.format('%.6f',now)..'|'..poseFields(p):gsub(',','|')..'|'..poseFields(velocity):gsub(',','|'))
    else identity.due=now+NPC.missingEvery end
   end
  end
 end
 diagFlush(now,'host','sector='..tostring(u.wanted)..' published='..#u.publish)
end
function u.frame(now)
 local ok,err=pcall(frame,now)
 if not ok then u.failed=true;log('STOPPED '..tostring(err));feature('world_sync','adapter_stopped') end
end
RegisterEvent('X4LAN_Universe.Exclude',function(_,object) u.excluded[key(ConvertIDTo64Bit(object))]=true end)
RegisterEvent('X4LAN_Universe.Ready',function() u.ready=true;log('READY mode='..cfg.world_mode..' slot='..cfg.slot..' npc_motion_diagnostics='..tostring(diagEvery or false)) end)
log('ARMED internal world replication; native cargo and shared identity adapters load separately; NPC combat remains experimental')
