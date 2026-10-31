-- Native gate test only. Lua supplies real post-universe ship samples; networking
-- during a wait is owned by the separate gate DLL on X4's main thread.
local cfg=X4LAN_CONFIG
if not cfg or not cfg.native_gate_test then return end
-- The nav octree input snapshot runs with any nav octree mode; alone it arms 'observe' (no visibility control).
if cfg.native_gate_nav_octree_snapshot and not cfg.native_gate_nav_octree then cfg.native_gate_nav_octree='observe' end
local ffi=require('ffi')
if not pcall(ffi.typeof,'UniverseID') then ffi.cdef('typedef uint64_t UniverseID;') end
if not pcall(ffi.typeof,'UIPosRot') then ffi.cdef('typedef struct {float x,y,z,yaw,pitch,roll;} UIPosRot;') end
ffi.cdef[[
double GetCurrentGameTime(void);
const char* GetSaveFolderPath(void);
UIPosRot GetObjectPositionInSector(UniverseID);
bool IsComponentOperational(UniverseID);
bool IsStartmenu(void);bool IsSaveListLoadingComplete(void);void ReloadSaveList(void);
size_t GetNumUpgradeSlots(UniverseID,const char*,const char*);
UniverseID GetUpgradeSlotCurrentComponent(UniverseID,const char*,size_t);
UniverseID GetPlayerContainerID(void);
UniverseID GetPlayerOccupiedShipID(void);
UniverseID GetPlayerControlledShipID(void);
const char* GetObjectIDCode(UniverseID);
]]
local profile=assert(cfg.test_profile,'test profile required')
local savepath=ffi.string(ffi.C.GetSaveFolderPath()):gsub('\\','/'):gsub('/+$','')
assert(profile=='45841' and savepath:sub(-#('/'..profile..'/save'))=='/'..profile..'/save','native gate outside test profile')
-- Protocol 3 (N=2..8 clients, opaque per-client input blobs) is opt-in through
-- native_gate_clients; without it the gate keeps the two-player protocol 2.
local clients=cfg.native_gate_clients
local wire3=clients~=nil
do
 if wire3 then assert(type(clients)=='number' and clients%1==0 and clients>=2 and clients<=8,'native gate clients must be 2..8') else clients=2 end
 local slot=type(cfg.slot)=='string' and cfg.slot:match('^p([1-8])$')
 assert(slot and tonumber(slot)<=clients,wire3 and 'native gate slot outside its client count' or 'native gate requires two players')
 -- These fixtures bind two players and the 8-hex protocol 2 commands.
 assert(not wire3 or not (cfg.native_gate_command_test or cfg.native_gate_management_test or cfg.native_gate_construction_test or
  cfg.native_gate_player_probe or cfg.native_gate_docking_state or cfg.native_gate_docking_shape_rng or cfg.native_gate_docking_shape_refresh or
  cfg.native_gate_creation_rng_schema or cfg.native_gate_registry_creation_schema),'two-player fixtures require native gate wire 2')
end
-- Protocol 4 (opt-in): a permit window of native_gate_lookahead = k ticks over
-- the protocol 3 input wire (docs/LOCKSTEP-NSLOT-WIRE.txt, LOOKAHEAD). A blob
-- proposed with tick t then arrives with the permit for t+1+k, so the input
-- delays must cover it: pilot records are staged one tick before they apply
-- (delay >= k+2), player orders apply in the admitting tick (delay >= k+1).
do
 local k=cfg.native_gate_lookahead
 assert(k==nil or (type(k)=='number' and k%1==0 and k>=0 and k<=8),'native gate lookahead must be 0..8')
 k=k or 0
 assert(k==0 or wire3,'native gate lookahead needs the protocol 3 input wire (native_gate_clients)')
 if cfg.native_gate_pilot_inputs and cfg.native_gate_pilot_source=='wire' then
  assert((cfg.native_gate_pilot_delay or 3)>=k+2,'pilot delay must be at least native_gate_lookahead+2')
 end
 if cfg.native_gate_player_orders then
  assert((cfg.native_gate_player_orders_delay or 3)>=k+1,'player order delay must be at least native_gate_lookahead+1')
 end
end
assert(cfg.native_gate_hash=='clock' or cfg.native_gate_hash=='battle','invalid gate hash scope')
assert(cfg.native_gate_battle_engage==nil or cfg.native_gate_battle_engage==false,'native_gate_battle_engage accepts only false')
-- Opt-in census scenario fixtures (docs/LOCKSTEP-SCENARIOS.txt): MD events at fixed gated
-- ticks, identical on every client. native_gate_missions.xml / native_gate_highway.xml.
-- Published as X4LAN_NATIVE_SCENARIOS; frame() reads that global (the frame is at
-- LuaJIT's 60-upvalue limit, so it holds no extra locals for this).
do
 local scenario_events,scenario_text
 local missions,highway=cfg.native_gate_missions,cfg.native_gate_highway
 local super,target=cfg.native_gate_highway_super,cfg.native_gate_highway_target
 assert(missions==nil or missions==false or missions==true,'native_gate_missions accepts only true')
 assert(highway==nil or highway==false or highway==true or highway=='warp' or highway=='fly','native_gate_highway accepts true, warp or fly')
 assert(super==nil or super==false or super==true,'native_gate_highway_super accepts only true')
 assert(target==nil or target=='highway' or target=='beacon','native_gate_highway_target accepts highway or beacon')
 if highway==true then highway='warp' end
 assert(highway or not (super or target),'highway options require native_gate_highway')
 target=target or 'highway'
 if missions or highway then
  assert(cfg.native_gate_hash=='battle' and cfg.native_gate_snapshot_sha,'scenario fixtures require the battle snapshot boot')
  assert(not (missions and highway),'run one scenario fixture at a time')
  assert(not cfg.native_gate_player_tour,'scenario fixtures exclude native_gate_player_tour')
 end
 if highway then
  assert(not cfg.native_gate_pilot_inputs and not cfg.native_gate_player_probe and not cfg.native_gate_player_orders,
   'the highway fixture flies the player ship by autopilot; it excludes pilot inputs, player probes and player orders')
 end
 local leave=highway=='fly' and 2430 or 930
 local super_at=highway=='fly' and 3030 or 1230
 scenario_events=function(tick)
  local out={}
  local function add(control,param) param=param or {};param.tick=tick;out[#out+1]={control,param} end
  if missions then
   if tick==32 then add('MissionSetup') end
   if tick==34 then add('MissionOffer') end
   if tick==300 then add('MissionAccept') end
   if tick==320 then add('MissionVisit') end
   if tick==340 then add('MissionHunt') end
   if tick==1500 then add('MissionCleanup') end
   if tick>=120 and tick%120==0 then add('MissionSample') end
  end
  if highway then
   if tick==40 then add('HighwayStart',{mode=highway,target=target}) end
   if tick==100 then add('HighwayEnter') end
   if tick==leave then add('HighwayLeave') end
   if super and tick==super_at then add('HighwaySuper') end
   if super and tick==super_at+60 then add('HighwaySuperEnter') end
   if tick>=120 and tick%60==0 then add('HighwaySample') end
  end
  return out
 end
 if missions or highway then
  scenario_text='missions='..tostring(missions==true)..' highway='..tostring(highway or false)..' super='..tostring(super==true)..' target='..(highway and target or 'none')
 end
 -- native_gate_highway_trace={first,last}: per-tick exact pose of the player ship and the
 -- four convoy ships (sc-highway-fly2: the player's speed differed by tick 240 in one fly
 -- run and not in another), plus the player's autopilot/flight-assist/boost state.
 local trace,trace_window=nil,cfg.native_gate_highway_trace
 if trace_window~=nil then
  assert(highway and type(trace_window)=='table' and #trace_window==2 and type(trace_window[1])=='number' and type(trace_window[2])=='number' and
   trace_window[1]==math.floor(trace_window[1]) and trace_window[2]==math.floor(trace_window[2]) and
   trace_window[1]>=1 and trace_window[1]<=trace_window[2] and trace_window[2]-trace_window[1]<=3600,
   'native_gate_highway_trace needs native_gate_highway and {first,last} ticks, at most 3600 apart')
  for _,line in ipairs({'bool IsAutoPilotActive(void);','bool IsFlightAssistActive(void);','float GetBoostEnergyPercentage(void);'}) do pcall(ffi.cdef,line) end
  local convoy={}
  for i=1,4 do RegisterEvent('X4LAN_NativeGate.HighwayShip'..i,function(_,ship) convoy[i]=ConvertIDTo64Bit(ship) end) end
  local function pose(id)
   if not ffi.C.IsComponentOperational(id) then return 'gone' end
   local ok,p=pcall(ffi.C.GetObjectPositionInSector,id)
   if not ok then return 'unreadable' end
   return string.format('%a,%a,%a/%a,%a,%a',p.x,p.y,p.z,p.yaw,p.pitch,p.roll)
  end
  trace=function(tick)
   if tick<trace_window[1] or tick>trace_window[2] then return end
   local ship=ffi.C.GetPlayerOccupiedShipID()
   local state=string.format('ap=%d fa=%d boost=%a',ffi.C.IsAutoPilotActive() and 1 or 0,ffi.C.IsFlightAssistActive() and 1 or 0,ffi.C.GetBoostEnergyPercentage())
   DebugError('[X4LAN] NATIVE_GATE HIGHWAY_TRACE tick='..tick..' who=player pose='..(ship~=0 and pose(ship) or 'none')..' '..state)
   for i=1,4 do DebugError('[X4LAN] NATIVE_GATE HIGHWAY_TRACE tick='..tick..' who=c'..i..' pose='..(convoy[i] and pose(convoy[i]) or 'pending')) end
  end
 end
 X4LAN_NATIVE_SCENARIOS={events=scenario_events,text=scenario_text,trace=trace}
end
assert(cfg.native_gate_speech_game_time==nil or cfg.native_gate_speech_game_time==false or cfg.native_gate_speech_game_time==true,
 'native_gate_speech_game_time accepts only true')
-- Read-only Gravidar contact watch (lockstep_gravidar.h): up to 8 saved unit IDs.
do
 local gravidar_watch=cfg.native_gate_gravidar_watch
 if gravidar_watch~=nil then
 assert(type(gravidar_watch)=='table' and #gravidar_watch>=1 and #gravidar_watch<=8 and cfg.native_gate_gravidar_key_all,
  'native_gate_gravidar_watch needs 1..8 saved IDs and native_gate_gravidar_key_all')
 local seen={}
 for i=1,#gravidar_watch do
  local id=gravidar_watch[i]
  assert(type(id)=='string' and #id==16 and not id:find('[^0-9a-f]') and id~=string.rep('0',16) and not seen[id],'invalid Gravidar watch ID')
  seen[id]=true
 end
 end
end
local world_hash_schema=cfg.native_gate_world_hash_schema
if world_hash_schema==nil then world_hash_schema='WORLD_IMPORTED_1' end
assert(world_hash_schema=='WORLD_IMPORTED_1' or world_hash_schema=='WORLD_IMPORTED_2','invalid world hash schema')
if world_hash_schema=='WORLD_IMPORTED_2' then
 local stride=cfg.native_gate_world_hash_stride
 assert(type(stride)=='number' and stride==math.floor(stride) and stride>=1 and stride<=18000,'enriched world hash requires census stride1..18000')
end
-- Windows X4.exe loads x4lan_gate_64.dll; the native Linux executable loads x4lan_gate.so (the
-- core subset, src/native/engine/linux/gate_linux.c: configurations needing an unported module refuse).
local gate=assert(package.loadlib('./extensions/x4_lan_lab/native/'..(package.config:sub(1,1)=='\\' and 'x4lan_gate_64.dll' or 'x4lan_gate.so'),
 'luaopen_x4lan_gate'))()
-- Crash note (lockstep_crash_note.h; native_gate_crash_note, default off): an unhandled exception writes one
-- CRASH line beside the debug log of this profile (x4lan-crash.txt, and x4lan-debug.txt when X4 shares it).
if cfg.native_gate_crash_note~=nil then
 assert(cfg.native_gate_crash_note==true,'crash note refused: native_gate_crash_note must be true')
 local dir=savepath:gsub('/save$','')
 assert(gate.crash_note and gate.crash_note(dir..'/x4lan-crash.txt',dir..'/x4lan-debug.txt')==1,'crash note refused')
 DebugError('[X4LAN] NATIVE_GATE CRASH_NOTE_ARMED file='..dir:match('[^/]+$')..'/x4lan-crash.txt')
end
-- Protocol 3 sections (native_gate_inputs_core.lua): pilot records, player orders and hit claims share
-- one blob per slot and tick through the multiplexer X4LAN_NATIVE_INPUTS. Each section keeps the
-- adapter contract for its own payload; an external adapter that is not the multiplexer still owns
-- the wire alone. A UI reload re-registers under the same owner tag.
local function input_section(name,owner,adapter)
 assert(wire3,name..' inputs need the protocol 3 input wire (native_gate_clients)')
 if not X4LAN_NATIVE_INPUTS then
  local core=assert(X4LAN_INPUT_MUX_CORE,'input multiplexer core missing (ui/native_gate_inputs_core.lua)')
  X4LAN_NATIVE_INPUTS=core.mux(function(text) DebugError('[X4LAN] NATIVE_GATE '..text) end)
  if cfg.native_gate_input_priority~=nil then X4LAN_NATIVE_INPUTS.set_priority(cfg.native_gate_input_priority) end
 end
 assert(X4LAN_NATIVE_INPUTS.owner=='mux','input adapter '..tostring(X4LAN_NATIVE_INPUTS.owner)..' owns the wire')
 X4LAN_NATIVE_INPUTS.register(name,owner,adapter)
end
if cfg.native_gate_player_orders then
 assert(wire3,'player orders need the protocol 3 input wire (native_gate_clients)')
 assert(not cfg.native_gate_command_test and not X4LAN_NATIVE_PLAYER_COMMANDS,'player orders exclude the two-player command adapters')
 local orders=assert(X4LAN_NATIVE_PLAYER_ORDERS,'player order adapter missing')
 orders.bind(gate)
 -- apply gets the orders payload of every slot, proposal(tick, budget) stages this client's blob.
 input_section('orders','orders',{apply=function(tick,inputs) return orders.apply(tick,inputs) end,proposal=orders.proposal})
end
-- Hit claims: 'carry' rides scripted claims on the wire and folds the admitted ones into the receipt,
-- never applying a hit (Lua test carrier). true/'shooter'/'victim'/'first' are the native claims
-- (lockstep_hit_native.h): their 'hits' section registers with the pilot section when the pilot
-- slots arm (arm_pilot_section); every other value is refused before loading (pilot arming).
if cfg.native_gate_hit_claims=='carry' then
 local core=assert(X4LAN_INPUT_MUX_CORE,'input multiplexer core missing (ui/native_gate_inputs_core.lua)')
 input_section('hits','hits',core.hit_carrier({slot=tonumber(cfg.slot:sub(2)),script=cfg.native_gate_hit_claims_script,
  log=function(text) DebugError('[X4LAN] NATIVE_GATE '..text) end}))
end
-- Nav octrees 'agreed': this client's build report rides its blob (section 'octree', kind 0x4f, offered
-- last so it waits a tick when the blob is full); every client agrees on the admitted reports and the gate
-- shows an octree at the join of the tick every slot reported it built. The receipt joins the digest.
if cfg.native_gate_nav_octree=='agreed' then
 input_section('octree','octree',{
  apply=function(tick,inputs,n) return assert(gate.nav_octree_agree(tick,n,table.concat(inputs,',')),'nav octree agreement refused') end,
  proposal=function() return gate.nav_octree_report() end})
end
local world_capture=gate.world_capture
if world_hash_schema=='WORLD_IMPORTED_2' then world_capture=gate.world_capture_nav_v2 end
if cfg.native_gate_world_hash_stride then assert(type(world_capture)=='function','configured world hash export unavailable') end
local diagnostic=cfg.native_gate_player_probe=='diagnostic-getup'
if diagnostic then assert(cfg.slot=='p1' and cfg.native_gate_diagnostic_contract=='diagnostic-getup-1','invalid local diagnostic contract') end
local snapshot=cfg.native_gate_snapshot_sha
local fixed
if not snapshot then
 fixed=assert(package.loadlib('./extensions/x4_lan_lab/native/x4lan_fixedtick_64.dll','luaopen_x4lan_fixedtick'))()
else
 assert(type(snapshot)=='string' and #snapshot==64 and snapshot:match('^[a-f0-9]+$'),'invalid snapshot identity')
end
-- Lockstep player identity (native_gate_player_identity.lua): slot factions, ship ownership and
-- relations applied by MD at one gated setup tick; its state joins the declared digest.
local identity
if cfg.native_gate_player_identity then
 assert(snapshot and cfg.native_gate_hash=='battle','player identity needs the battle snapshot boot')
 identity=assert(X4LAN_NATIVE_PLAYER_IDENTITY,'player identity adapter missing')
 identity.bind(gate,wire3 and clients or nil)
end
local owner;local armed,done,refreshed=false,false,false
local native_owned=false -- Successful boot/start owns cleanup before adapters configure.
-- Status stride (native_gate_status_stride=N, default off: every tick is written). A DebugError
-- call costs the frame roughly 30 us (FRAME_TIMING across census configs) and the play config
-- writes ~85 lines per tick. With N, the output of an admitted tick (state rows, drained rows,
-- traces, INPUTS/RNG lines) is collected and written when the tick is one of the first N, a
-- multiple of N or a world capture tick, in calls of up to ~6000 bytes (one line each beyond).
-- Module status lines (ticklog.status) are only built on those ticks; the STOP lines give every
-- module's final status. Other ticks are held: the last ticklog.keep since the last written tick
-- stay in memory and are written (after one STATUS_HELD line) before a stop or failure, so the
-- ticks leading to either keep their state rows and inputs. FRAME_TIMING and player action
-- events (hit claims, refused flight inputs) are always written at once. Only the log changes:
-- every tick is sampled, hashed and reported as before, and the drains still empty the native
-- buffers.
local ticklog={stride=cfg.native_gate_status_stride,keep=16,ring={}}
assert(ticklog.stride==nil or (type(ticklog.stride)=='number' and ticklog.stride%1==0 and ticklog.stride>=1 and ticklog.stride<=3600),
 'native_gate_status_stride must be a tick count 1..3600')
-- Who allocates runtime IDs (RUNTIME_IDS_CALLER), every N ticks instead of 300, so that
-- census_runtime_callers.py --first names the tick and stack where the shared list parts.
do
 local stride=cfg.native_gate_runtime_ids_caller_stride
 assert(stride==nil or (type(stride)=='number' and stride%1==0 and stride>=1 and stride<=3600 and cfg.native_gate_runtime_ids~=nil),
  'native_gate_runtime_ids_caller_stride must be a tick count 1..3600, with native_gate_runtime_ids')
end
function ticklog.write(text) DebugError('[X4LAN] NATIVE_GATE '..text) end
-- Collected lines in order: strings, or functions returning a list of lines (built only when written).
function ticklog.emit(entries)
 local chunk,size={},0
 local function line(text)
  text='[X4LAN] NATIVE_GATE '..text
  if size>0 and size+#text>6000 then DebugError(table.concat(chunk,'\n'));chunk,size={},0 end
  chunk[#chunk+1]=text;size=size+#text+1
 end
 for _,entry in ipairs(entries) do
  if type(entry)=='function' then for _,text in ipairs(entry()) do line(text) end else line(entry) end
 end
 if size>0 then DebugError(table.concat(chunk,'\n')) end
end
-- tick: the admitted tick whose output this frame collects; nil writes at once.
function ticklog.open(tick)
 ticklog.tick=nil
 if not (ticklog.stride and tick) then return end
 local world=cfg.native_gate_world_hash_stride
 local current={tick=tick,write=tick<=ticklog.stride or tick%ticklog.stride==0 or (world and tick%world==0)}
 local ring=ticklog.ring
 if #ring>=ticklog.keep then table.remove(ring,1) end
 ring[#ring+1]=current;ticklog.tick=current
end
-- End of the tick's frame: a written tick is emitted, and the held ticks before it are dropped.
function ticklog.close()
 local current=ticklog.tick
 ticklog.tick=nil
 if current and current.write then ticklog.ring={};ticklog.emit(current) end
end
-- Before a written tick's direct DebugError output (world pages): emit what it collected so far.
function ticklog.drain()
 local current=ticklog.tick
 if current and current.write and #current>0 then ticklog.emit(current);for i=#current,1,-1 do current[i]=nil end end
end
-- Stop or failure: the held ticks and the current one, in order.
function ticklog.flush()
 local ring=ticklog.ring
 ticklog.tick=nil;ticklog.ring={}
 local held={}
 for _,lines in ipairs(ring) do if not lines.write then held[#held+1]=lines.tick end end
 if #held>0 then ticklog.write('STATUS_HELD first='..held[1]..' last='..held[#held]..' ticks='..#held..' stride='..ticklog.stride) end
 for _,lines in ipairs(ring) do ticklog.emit(lines) end
end
local function log(text)
 local current=ticklog.tick
 if current then current[#current+1]=text else ticklog.write(text) end
end
-- A module status line: built only when it will be written (not on held ticks).
function ticklog.status(name,tick,getter,missing)
 local current=ticklog.tick
 if current and not current.write then return end
 local value=getter()
 log(name..' tick='..tick..' '..(missing and assert(value,missing) or tostring(value)))
end
-- Lines built from data the tick keeps anyway (state rows): built when written, also at a flush.
function ticklog.lines(build)
 local current=ticklog.tick
 if current then current[#current+1]=build;return end
 for _,text in ipairs(build()) do ticklog.write(text) end
end
-- Probe profile (native_gate_probe_profile=true, default off): QPC microseconds per section of an
-- admitted tick's frame(), averaged over 60 ticks, one PROBE_PROFILE line each (always written).
-- Sections in order: drains (poll, trace drains, Gravidar trace), inputs, pilot, modules (statuses,
-- rows, view/region frames), sample (fixture events and the declared state; of it actor = gate.actor
-- and its parse, health = position and hull/shield, orders, fleet, bodies = gate.body and its bits),
-- capture (world hash), digest (hash, proposal, report), log (a written tick's batch). The
-- QPC reads cost about 25 us per tick themselves.
local profile
if cfg.native_gate_probe_profile~=nil then
 assert(cfg.native_gate_probe_profile==true or cfg.native_gate_probe_profile==false,'native_gate_probe_profile accepts only true')
 if cfg.native_gate_probe_profile then
  assert(type(gate.qpc_us)=='function','native_gate_probe_profile needs gate.qpc_us')
  profile={sections={'drains','inputs','pilot','modules','sample','capture','digest','log'},parts={'actor','health','orders','fleet','bodies'},
   sum={},n=0,max=0}
  function profile.begin() profile.cur={};profile.at=gate.qpc_us();profile.start=profile.at end
  function profile.mark(name)
   local cur=profile.cur
   if not cur then return end
   local now=gate.qpc_us();cur[name]=(cur[name] or 0)+now-profile.at;profile.at=now
  end
  -- Time inside a section, reported apart (inside sample).
  function profile.part(name,since)
   local cur=profile.cur
   if cur then cur[name]=(cur[name] or 0)+gate.qpc_us()-since end
  end
  function profile.finish(tick)
   local cur=profile.cur
   if not cur then return end
   profile.cur=nil
   local total=profile.at-profile.start
   for name,value in pairs(cur) do profile.sum[name]=(profile.sum[name] or 0)+value end
   profile.sum.total=(profile.sum.total or 0)+total;profile.n=profile.n+1
   if total>profile.max then profile.max=total end
   if profile.n<60 then return end
   local text={'PROBE_PROFILE tick='..tick..' ticks='..profile.n}
   for _,list in ipairs({profile.sections,profile.parts,{'total'}}) do
    for _,name in ipairs(list) do text[#text+1]=string.format('%s_us=%.0f',name,(profile.sum[name] or 0)/profile.n) end
   end
   text[#text+1]=string.format('max_total_us=%.0f',profile.max)
   ticklog.write(table.concat(text,' '))
   profile.sum,profile.n,profile.max={},0,0
  end
 end
end
local function log_world_page(page)
 -- World011 showed the native logger cutting a dense Nav page at 8191 bytes.
 -- Keep complete C rows in bounded calls; never shorten or discard a row.
 assert(type(page)=='string' and #page>0 and #page<12288,'invalid world page size')
 local chunks,chunk,first={},'',1
 while first<=#page do
  local last=page:find('\n',first,true)
  assert(last and last>first,'incomplete or empty world page row')
  local row=page:sub(first,last)
  assert(#row<=6000,'world page row exceeds logging bound')
  if #chunk+#row>6000 then chunks[#chunks+1]=chunk;chunk='' end
  chunk=chunk..row;first=last+1
 end
 if #chunk>0 then chunks[#chunks+1]=chunk end
 -- Validate the complete page before emitting its first chunk; the tick's lines so far precede it.
 ticklog.drain()
 for _,part in ipairs(chunks) do DebugError(part) end
end
local command_error=false
RegisterEvent('X4LAN_NativeGate.CommandRefused',function() command_error=true end)
-- Protocol 3: s.inputs[1..clients] are the blobs admitted for this tick, in
-- slot order and identical on every client. The optional X4LAN_NATIVE_INPUTS
-- adapter applies them (its result joins the declared state digest) and
-- proposes this client's blob for the next tick; see docs/LOCKSTEP-NSLOT-WIRE.txt.
local function apply_inputs(tick,s)
 assert(s.wire_version==3 and s.clients==clients and type(s.inputs)=='table' and #s.inputs==clients,'native input wire 3 mismatch')
 -- Protocol 4: s.inputs are the blobs proposed with tick s.inputs_tick = tick-1-k.
 local k=cfg.native_gate_lookahead or 0
 assert((s.lookahead or 0)==k and (k==0 or (tick<=k and s.inputs_tick==nil) or s.inputs_tick==tick-1-k),'native gate lookahead mismatch')
 local any=false
 for i=1,clients do
  local blob=s.inputs[i]
  assert(type(blob)=='string' and #blob<=480 and #blob%2==0 and not blob:find('[^0-9a-f]'),'invalid native input blob')
  any=any or blob~=''
 end
 if any then log('INPUTS tick='..tick..(k>0 and ' proposed='..s.inputs_tick or '')..' blobs='..table.concat(s.inputs,',')) end
 local adapter=X4LAN_NATIVE_INPUTS
 if adapter and adapter.apply then return adapter.apply(tick,s.inputs,clients,tonumber(cfg.slot:sub(2))) end
 assert(not any,'unexpected native inputs')
end
local actors,ready={},false
local weapons={}
local registry_navigation_seen={}
local creation_trace,creation_rng,registry_creation
local registry_md_seen=0
local ai_schedule_fallback -- last logged AI_SCHEDULE_FALLBACK detail
local function drain_registry_md(s)
 if not cfg.native_gate_registry_md_trace then return end
 assert(type(s.registry_md_count)=='number' and s.registry_md_count>=registry_md_seen,'native MD trace count missing or regressed')
 for i=registry_md_seen+1,s.registry_md_count do log('REGISTRY_MD '..assert(gate.registry_md(i),'native MD row missing')) end
 registry_md_seen=s.registry_md_count
end
local function outside_session()
 local current=gate.poll()
 local ended=current and current.active==0 and
  (armed or (tonumber(current.issued) or 0)>0 or (tonumber(current.reason) or 0)>0)
 return done or ended,current
end
if cfg.native_gate_hash=='battle' then
 for i=1,12 do
  RegisterEvent('X4LAN_NativeGate.Actor'..i,function(_,object)
   local ended,current=outside_session()
   if ended then
    log('ACTOR_OUTSIDE_SESSION id='..i..' reason='..tostring(current and current.reason or 'stopped'))
    return
   end
   local native=ConvertIDTo64Bit(object)
   if cfg.native_gate_snapshot_fixture then
    assert(gate.snapshot_enroll(i,(tostring(native):gsub('ULL$','')))==1,'snapshot actor enrollment mismatch '..i)
    log('SNAPSHOT_ENROLL id='..i..' saved='..cfg.native_gate_snapshot_fixture[i])
   end
   actors[i]=native
  end)
 end
 RegisterEvent('X4LAN_NativeGate.Ready',function(_,count)
  if outside_session() then log('READY_OUTSIDE_SESSION count='..tostring(count));return end
  assert(tonumber(count)==12,'incomplete battle fixture');ready=true
 end)
end
local function drain_bullets()
 if not cfg.native_gate_bullet_diag then return end
 for i=1,assert(gate.bullet(0)) do log('BULLET '..assert(gate.bullet(i))) end
 assert(gate.bullet_clear()==1,'bullet trace clear refused')
end
local function drain_deaths()
 if not cfg.native_gate_death_diag then return end
 for i=1,assert(gate.death(0)) do log('DEATH '..assert(gate.death(i))) end
 assert(gate.death_clear()==1,'death trace clear refused')
end
local function drain_mass()
 if not cfg.native_gate_mass_diag then return end
 for i=1,assert(gate.mass(0)) do log('MASS '..assert(gate.mass(i))) end
 assert(gate.mass_clear()==1,'mass trace clear refused')
end
local function drain_radar()
 if not cfg.native_gate_radar_diag then return end
 for i=1,assert(gate.radar(0)) do log('RADAR '..assert(gate.radar(i))) end
 assert(gate.radar_clear()==1,'radar trace clear refused')
end
local interrupt_fault_cursor=0
-- Trace rows (interrupts, nested actions, NPC lifecycle, speech, triggers) are written at once: a status stride
-- holds a tick's lines and drops them unless the run fails (s2-victim6: 3116 NPC actions recorded, none in the log).
local function drain_interrupts()
 if cfg.native_gate_npc_pilot then
  for i=1,assert(gate.npc_pilot(0)) do ticklog.write('NPC_PILOT '..assert(gate.npc_pilot(i))) end
  assert(gate.npc_pilot_clear()==1,'NPC pilot trace clear refused')
 end
 if not cfg.native_gate_interrupt_diag then return end
 -- What the observers could not record, by site (lockstep_interrupt_faults.h); none of it stops a session.
 while gate.interrupt_fault_row do
  local row=gate.interrupt_fault_row(interrupt_fault_cursor)
  if not row then break end
  ticklog.write('INTERRUPT_FAULT_ROW '..row);interrupt_fault_cursor=interrupt_fault_cursor+1
 end
 for i=1,assert(gate.interrupt(0)) do ticklog.write('INTERRUPT '..assert(gate.interrupt(i))) end
 assert(gate.interrupt_clear()==1,'interrupt trace clear refused')
 for i=1,assert(gate.interrupt_step(0)) do ticklog.write('INTERRUPT_STEP '..assert(gate.interrupt_step(i))) end
 assert(gate.interrupt_step_clear()==1,'interrupt step trace clear refused')
 if cfg.native_gate_nested_actions then
  for i=1,assert(gate.nested_action(0)) do ticklog.write('NESTED_ACTION '..assert(gate.nested_action(i))) end
  assert(gate.nested_action_clear()==1,'nested action trace clear refused')
 end
 if cfg.native_gate_npc_lifecycle then
  for i=1,assert(gate.npc_action(0)) do ticklog.write('NPC_ACTION '..assert(gate.npc_action(i))) end
  assert(gate.npc_action_clear()==1,'NPC action trace clear refused')
 end
 if cfg.native_gate_npc_deadline then
  for i=1,assert(gate.npc_ready(0)) do ticklog.write('NPC_READY '..assert(gate.npc_ready(i))) end
  assert(gate.npc_ready_clear()==1,'NPC readiness trace clear refused')
  for i=1,assert(gate.npc_deadline(0)) do ticklog.write('NPC_DEADLINE '..assert(gate.npc_deadline(i))) end
  assert(gate.npc_deadline_clear()==1,'NPC deadline trace clear refused')
 end
 if cfg.native_gate_npc_trigger then
  for i=1,assert(gate.npc_cause(0)) do ticklog.write('NPC_CAUSE '..assert(gate.npc_cause(i))) end
  for i=1,assert(gate.npc_trigger(0)) do ticklog.write('NPC_TRIGGER '..assert(gate.npc_trigger(i))) end
  if cfg.native_gate_npc_provenance then
   for i=1,assert(gate.npc_provenance(0)) do ticklog.write('NPC_PROVENANCE '..assert(gate.npc_provenance(i))) end
  end
  assert(gate.npc_trigger_clear()==1,'NPC trigger trace clear refused')
 end
 if cfg.native_gate_npc_rng then
  for i=1,assert(gate.npc_exec(0)) do ticklog.write('NPC_EXEC '..assert(gate.npc_exec(i))) end
  assert(gate.npc_exec_clear()==1,'NPC execution trace clear refused')
 end
 if cfg.native_gate_npc_diag then
  for i=1,assert(gate.npc_enqueue(0)) do ticklog.write('NPC_ENQUEUE '..assert(gate.npc_enqueue(i))) end
  assert(gate.npc_enqueue_clear()==1,'NPC enqueue trace clear refused')
  for i=1,assert(gate.npc_speak(0)) do ticklog.write('NPC_SPEAK '..assert(gate.npc_speak(i))) end
  assert(gate.npc_speak_clear()==1,'NPC speech trace clear refused')
  if cfg.native_gate_npc_finish then
   for i=1,assert(gate.npc_finish(0)) do ticklog.write('NPC_FINISH '..assert(gate.npc_finish(i))) end
   assert(gate.npc_finish_clear()==1,'NPC finish trace clear refused')
  end
 end
end
local function drain_damage()
 if not cfg.native_gate_damage_diag then return end
 for i=1,assert(gate.damage(0)) do log('DAMAGE '..assert(gate.damage(i))) end
 assert(gate.damage_clear()==1,'damage trace clear refused')
end
local function drain_contacts()
 if not cfg.native_gate_contact_diag then return end
 for i=1,assert(gate.contact(0)) do
  log('CONTACT '..assert(gate.contact(i)))
  if cfg.native_gate_contact_order then log('CONTACT_ORDER '..assert(gate.contact_order(i))) end
 end
 if cfg.native_gate_contact_properties then
  for i=1,assert(gate.contact_properties(0)) do log('CONTACT_PROPERTIES '..assert(gate.contact_properties(i))) end
 end
 assert(gate.contact_clear()==1,'contact trace clear refused')
end
local function drain_shape_lifetimes()
 if not cfg.native_gate_shape_lifetimes then return end
 for i=1,assert(gate.shape_lifetime(0)) do log('SHAPE_LIFETIME '..assert(gate.shape_lifetime(i))) end
 assert(gate.shape_lifetime_clear()==1,'shape lifetime trace clear refused')
end
-- Who gets built, in the order it is decided (native_gate_build_rows). A few rows a second at most, and they are
-- wanted on the ticks the status stride holds, so they go out at once.
local build_row_cursor=0
local function drain_build_rows()
 if not cfg.native_gate_build_rows then return end
 for _=1,8192 do
  local row=gate.build_rows_row(build_row_cursor)
  if not row then break end
  ticklog.write('BUILD_ROW '..row);build_row_cursor=build_row_cursor+1
 end
end
-- Stale fixture entries and what else the flight module could not do (lockstep_flight.h); at most a few rows a site.
local flight_fault_cursor=0
local function drain_flight_faults()
 while gate.flight_fault_row do
  local row=gate.flight_fault_row(flight_fault_cursor)
  if not row then break end
  log('FLIGHT_FAULT_ROW '..row);flight_fault_cursor=flight_fault_cursor+1
 end
end
-- Every frame, held ticks too: the clears empty the native event buffers, whose overflow stops the
-- gate (bullets: reason 48). One function, one upvalue of frame().
local function drain_traces()
 drain_bullets();drain_deaths();drain_mass();drain_radar();drain_damage();drain_interrupts();drain_contacts();drain_shape_lifetimes();drain_flight_faults();drain_build_rows()
end
local radar_objects={}
local world_actors={}
local world_hash_state
local probe_last_us -- previous frame() wall time, native_gate_frame_timing only
local camera_perturb=false -- this client perturbs its camera (native_gate_camera_perturb)
local camera_offset -- {x,y,z} when this client moves its camera (native_gate_camera_offset); {follow=slot} for native_gate_camera_anchor
local nav_cadence_cursor=0
local row_cursors={coverage=0,step_trace=0,fp_redirect=0,view=0,policy=0,highway=0,command_order=0,actor_rng=0,draw_watch=0,draw_watch_stray=0,warp_rng=0,transition=0,tempzone=0,gravidar_post=0,physics={[0]=0,0,0,0},body_order=0,shape_order=0,nav_octree=0,nav_octree_lookup=0,vplayer_map=0,vplayer_spike='',event_rng=0,ui_cargo=0,supply=0,ui_lazy=0,supply_wants=0,loadout_order=0,order_canon=0,address_order=0,pick_order=0,macro_arena=0} -- one upvalue: frame() is at LuaJIT's limit of 60
local world_trace_rows,world_queue_rows,world_wait_rows,world_wait_rng_rows=0,0,0,0
local world_action_rows,world_values_rows,world_values_unknown_rows,world_unknown_identity_rows=0,0,0,0
local world_values_cursors_loaded=false
local world_safe_position_rows
local world_position_rows
local world_position_rng_rows
local world_find_rows=0
local job_rng_cursor=0
local function drain_job_rng()
 if not cfg.native_gate_job_rng then return end
 local emitted=0
 while emitted<4096 do
  local row=gate.job_rng_row(job_rng_cursor)
  if not row then break end
  log('JOB_RNG '..row);job_rng_cursor=job_rng_cursor+1;emitted=emitted+1
 end
end
-- Event census rows: one bounded C row per log line, absolute cursor.
local event_row_cursor=0
local function drain_event_rows(limit)
 if not cfg.native_gate_event_rows then return end
 local emitted=0
 while not limit or emitted<limit do
  local row=gate.event_order_row(event_row_cursor)
  if not row then break end
  log('EVENT_ROW '..row);event_row_cursor=event_row_cursor+1;emitted=emitted+1
 end
end
-- Per-tick diagnostic rows that share one drain (frame() is at LuaJIT's upvalue limit):
-- Gravidar watch rows (header per keyed apply of a watched unit, then its nonempty sets),
-- speech lines routed to the game-clock deadline (native_gate_speech_game_time),
-- effect deferral rows (native_gate_effect_rows), Jolt step/body/contact rows
-- (native_gate_physics_trace), physics body order rows (native_gate_body_order),
-- moved compound children (native_gate_shape_order), finished nav octree builds
-- (native_gate_nav_octree) and per-ship motion, AI move order and flight model
-- target rows (native_gate_motion_watch and native_gate_motion_watch_orders).
-- final (at stop) also takes body, contact and order rows of the current tick.
local gravidar_watch_cursor,speech_time_cursor,effect_row_cursor,motion_watch_cursor=0,0,0,0
local motion_cursors={order=0,target=0,event=0,refresh=0} -- one upvalue for the four order-row kinds
local physics_row_names={[0]='PHYSICS_STEP ','PHYSICS_BODY ','PHYSICS_CONTACT ','PHYSICS_CHILD '}
local function drain_gravidar_watch(final)
 local f=final and 1 or 0
 if cfg.native_gate_physics_trace~=nil then
  for kind=0,cfg.native_gate_physics_trace_children~=nil and 3 or 2 do
   for _=1,70000 do
    local row=gate.physics_trace_row(kind,row_cursors.physics[kind],f)
    if not row then break end
    log(physics_row_names[kind]..row);row_cursors.physics[kind]=row_cursors.physics[kind]+1
   end
  end
 end
 if cfg.native_gate_body_order then
  for _=1,70000 do
   local row=gate.body_order_row(row_cursors.body_order,f)
   if not row then break end
   log('BODY_ORDER_ROW '..row);row_cursors.body_order=row_cursors.body_order+1
  end
 end
 if cfg.native_gate_shape_order then
  for _=1,4096 do
   local row=gate.shape_order_row(row_cursors.shape_order)
   if not row then break end
   log('SHAPE_ORDER_ROW '..row);row_cursors.shape_order=row_cursors.shape_order+1
  end
 end
 if cfg.native_gate_nav_octree then
  for _=1,8192 do
   local row=gate.nav_octree_row(row_cursors.nav_octree)
   if not row then break end
   log('NAV_OCTREE_ROW '..row);row_cursors.nav_octree=row_cursors.nav_octree+1
  end
  for _=1,16384 do -- 'deadline'/'agreed': per-frame lookups of pending or hidden octrees, invalidations of records
   local row=gate.nav_octree_lookup_row and gate.nav_octree_lookup_row(row_cursors.nav_octree_lookup)
   if not row then break end
   log('NAV_OCTREE_LOOKUP '..row);row_cursors.nav_octree_lookup=row_cursors.nav_octree_lookup+1
  end
 end
 if cfg.native_gate_supply_wants~=nil then -- stations' wanted supply orders (native_gate_supply_wants)
  for _=1,8192 do
   local row=gate.supply_wants_row(row_cursors.supply_wants)
   if not row then break end
   log('SUPPLY_WANTS_ROW '..row);row_cursors.supply_wants=row_cursors.supply_wants+1
  end
 end
 if cfg.native_gate_coverage then -- the listed functions that ran in the window, after it (native_gate_coverage)
  for _=1,8192 do
   local row=gate.coverage_row(row_cursors.coverage)
   if not row then break end
   log('COVERAGE_ROW '..row);row_cursors.coverage=row_cursors.coverage+1
  end
 end
 if cfg.native_gate_step_trace then -- the Linear step's entry and exit for watched ships (native_gate_step_trace)
  for _=1,4096 do
   local row=gate.step_trace_row(row_cursors.step_trace)
   if not row then break end
   log('STEP_TRACE_ROW '..row);row_cursors.step_trace=row_cursors.step_trace+1
  end
 end
 if cfg.native_gate_fp_redirect then -- the refresh recomputed next to native: watched ships' inputs and both results (native_gate_fp_redirect)
  for _=1,4096 do
   local row=gate.fp_redirect_row(row_cursors.fp_redirect)
   if not row then break end
   log('FP_REDIRECT_ROW '..row);row_cursors.fp_redirect=row_cursors.fp_redirect+1
  end
 end
 if cfg.native_gate_macro_arena then -- macros in id hash order (native_gate_macro_arena); the drain also audits them
  for _=1,1024 do
   local row=gate.macro_arena_row(row_cursors.macro_arena)
   if not row then break end
   log('MACRO_ARENA_ROW '..row);row_cursors.macro_arena=row_cursors.macro_arena+1
  end
 end
 if cfg.native_gate_pick_order then -- weighted picks from the libraries' selection tables (native_gate_pick_order)
  for _=1,8192 do
   local row=gate.pick_order_row(row_cursors.pick_order)
   if not row then break end
   log('PICK_ORDER_ROW '..row);row_cursors.pick_order=row_cursors.pick_order+1
  end
 end
 if cfg.native_gate_order_canon then -- generated loadouts' candidates in arrival order (native_gate_order_canon)
  for _=1,8192 do
   local row=gate.order_canon_row(row_cursors.order_canon)
   if not row then break end
   log('ORDER_CANON_ROW '..row);row_cursors.order_canon=row_cursors.order_canon+1
  end
 end
 if cfg.native_gate_address_order then -- macro and ware address order (native_gate_address_order)
  for _=1,8192 do
   local row=gate.address_order_row(row_cursors.address_order)
   if not row then break end
   log('ADDRESS_ORDER_ROW '..row);row_cursors.address_order=row_cursors.address_order+1
  end
 end
 if cfg.native_gate_loadout_order then -- stations' loadout targets and macro order (native_gate_loadout_order)
  for _=1,8192 do
   local row=gate.loadout_order_row(row_cursors.loadout_order)
   if not row then break end
   log('LOADOUT_ROW '..row);row_cursors.loadout_order=row_cursors.loadout_order+1
  end
 end
 -- These two go out at once: they name the container a client's UI brought up to date, or the delivery
 -- whose draws differ, and the status stride would drop them on the ticks it holds (play30-long, 16080).
 if cfg.native_gate_ui_lazy then -- Lua UI lazy-state queries (native_gate_ui_lazy)
  for _=1,16384 do
   local row=gate.ui_lazy_row(row_cursors.ui_lazy)
   if not row then break end
   ticklog.write('UI_LAZY_ROW '..row);row_cursors.ui_lazy=row_cursors.ui_lazy+1
  end
 end
 if cfg.native_gate_supply_rows then -- stations' supply-wares posts (native_gate_supply_rows)
  for _=1,8192 do
   local row=gate.supply_row(row_cursors.supply)
   if not row then break end
   log('SUPPLY_POST_ROW '..row);row_cursors.supply=row_cursors.supply+1
  end
 end
 if cfg.native_gate_ui_cargo then -- UI cargo/production queries in admitted frames (native_gate_ui_cargo)
  for _=1,16384 do
   local row=gate.ui_cargo_row(row_cursors.ui_cargo)
   if not row then break end
   log('UI_CARGO_ROW '..row);row_cursors.ui_cargo=row_cursors.ui_cargo+1
  end
 end
 if cfg.native_gate_event_rng and not cfg.native_gate_play_mode then -- due-event deliveries whose listeners drew (native_gate_event_rng)
  for _=1,16384 do
   local row=gate.event_rng_row(row_cursors.event_rng)
   if not row then break end
   ticklog.write('EVENT_RNG_ROW '..row);row_cursors.event_rng=row_cursors.event_rng+1
  end
 end
 if cfg.native_gate_motion_watch then
  for _=1,4096 do
   local row=gate.motion_watch_row(motion_watch_cursor)
   if not row then break end
   log('MOTION_WATCH '..row);motion_watch_cursor=motion_watch_cursor+1
  end
  if cfg.native_gate_motion_watch_orders then
   for _,kind in ipairs({{'order','MOTION_ORDER ',gate.motion_watch_order_row},{'target','MOTION_TARGET ',gate.motion_watch_target_row},
                         {'event','MOTION_EVENT ',gate.motion_watch_event_row},{'refresh','MOTION_REFRESH ',gate.motion_watch_refresh_row}}) do
    for _=1,4096 do
     local row=kind[3](motion_cursors[kind[1]])
     if not row then break end
     log(kind[2]..row);motion_cursors[kind[1]]=motion_cursors[kind[1]]+1
    end
   end
  end
 end
 if cfg.native_gate_viewpoint_policy_rows then
  for _=1,70000 do
   local row=gate.region_camera_policy_row(row_cursors.policy)
   if not row then break end
   -- Written at once: a status stride holds a tick's lines and drops them unless the run fails (reg-tour6 lost the traffic rows).
   ticklog.write('POLICY_ROW '..row);row_cursors.policy=row_cursors.policy+1
  end
 end
 if cfg.native_gate_highway_rows then
  for _=1,70000 do
   local row=gate.region_camera_highway_row(row_cursors.highway)
   if not row then break end
   log('HIGHWAY_PARAM_ROW '..row);row_cursors.highway=row_cursors.highway+1
  end
 end
 if cfg.native_gate_effect_rows then
  for _=1,8192 do
   local row=gate.region_camera_effect_row(effect_row_cursor)
   if not row then break end
   log('EFFECT_ROW '..row);effect_row_cursor=effect_row_cursor+1
  end
 end
 if cfg.native_gate_speech_game_time then
  local emitted=0
  while true do
   local row=gate.speech_time_row(speech_time_cursor)
   if not row then break end
   log('SPEECH_TIME_ROW '..row);speech_time_cursor=speech_time_cursor+1;emitted=emitted+1
  end
  if emitted>0 then log('SPEECH_TIME '..tostring(gate.speech_time_status())) end
 end
 if not cfg.native_gate_gravidar_watch then return end
 while true do
  local row=gate.gravidar_watch_row(gravidar_watch_cursor)
  if not row then break end
  log('GRAVIDAR_WATCH_ROW '..row)
  local counts=row:match(' counts=([%d/]+)$')
  if counts then
   local set=0
   for count in counts:gmatch('%d+') do
    if tonumber(count)>0 then log('GRAVIDAR_WATCH_SET '..assert(gate.gravidar_watch_set(gravidar_watch_cursor,set),'Gravidar watch set unavailable')) end
    set=set+1
   end
  end
  gravidar_watch_cursor=gravidar_watch_cursor+1
 end
end
local script_rng_cursor=0
local script_rng_rows_over=false
-- native_gate_script_rng_rows_window = {first, last} (ticks, at most 600 apart): rows of those ticks only, written
-- at once in batches, so the status stride does not drop them. Nothing is drained before the window; at its start the
-- cursor moves to the oldest row the native ring (32768) still holds and rows of earlier ticks are skipped.
local script_rng_window={cfg.native_gate_script_rng_rows_window,false}
do
 local w=script_rng_window[1]
 assert(w==nil or (cfg.native_gate_script_rng_rows==true and type(w)=='table' and type(w[1])=='number' and type(w[2])=='number' and
  w[1]%1==0 and w[2]%1==0 and w[1]>=1 and w[2]>=w[1] and w[2]-w[1]<=600),
  'native_gate_script_rng_rows_window must be {first, last} ticks at most 600 apart, with native_gate_script_rng_rows = true')
end
local function drain_script_rng_window(tick)
 local w=script_rng_window[1]
 if script_rng_rows_over or (tick and tick<w[1]) then return end
 if not script_rng_window[2] then
  -- the first index without a row: rows exist below it (evicted ones say so), none at or above it
  local lo,hi=script_rng_cursor,math.max(script_rng_cursor*2,1)
  while gate.script_rng_row(hi) do lo=hi;hi=hi*2 end
  if not gate.script_rng_row(lo) then hi=lo end
  while hi-lo>1 do local mid=math.floor((lo+hi)/2);if gate.script_rng_row(mid) then lo=mid else hi=mid end end
  script_rng_cursor=math.max(script_rng_cursor,hi-32768);script_rng_window[2]=true
 end
 local batch={}
 while #batch<16384 do
  local row=gate.script_rng_row(script_rng_cursor)
  if not row then break end
  script_rng_cursor=script_rng_cursor+1
  local t=tonumber(row:match('^index=%d+ tick=(%d+)'))
  if t and t>=w[1] and t<=w[2] then batch[#batch+1]='SCRIPT_RNG_ROW '..row end
 end
 if #batch>0 then ticklog.emit(batch) end
 if tick and tick>=w[2] then script_rng_rows_over=true end
end
local function drain_script_rng(tick)
 if not cfg.native_gate_script_rng_rows then return end
 if script_rng_window[1] then drain_script_rng_window(tick);return end
 -- native_gate_script_rng_rows_ticks: rows of the first N ticks only; at 2-4k rows a tick the whole run is gigabytes.
 local limit=cfg.native_gate_script_rng_rows_ticks
 if limit and tick and tick>limit then script_rng_rows_over=true end
 if script_rng_rows_over then return end
 -- Boot phase 2 alone records about 12,300 keyed rows (tick 0): drain up to
 -- 16384 per frame so they do not age out of the native ring (32768).
 local emitted=0
 while emitted<16384 do
  local row=gate.script_rng_row(script_rng_cursor)
  if not row then break end
  log('SCRIPT_RNG_ROW '..row);script_rng_cursor=script_rng_cursor+1;emitted=emitted+1
 end
end
local md_resume_cursor=0
local function drain_md_resume()
 if cfg.native_gate_play_mode then return end
 local emitted=0
 while emitted<4096 do
  local row=gate.md_resume_rng_row(md_resume_cursor)
  if not row then break end
  log('MD_RESUME_RNG_ROW '..row);md_resume_cursor=md_resume_cursor+1;emitted=emitted+1
 end
end
local gravidar_digest_cursor=0
local function drain_gravidar_digest()
 local emitted=0
 while emitted<40000 do
  local row=gate.gravidar_digest_row(gravidar_digest_cursor)
  if not row then break end
  log('GRAVIDAR_DIGEST_ROW '..row);gravidar_digest_cursor=gravidar_digest_cursor+1;emitted=emitted+1
 end
end
local region_cursor=0
local function drain_region()
 if cfg.native_gate_play_mode then return end
 local emitted=0
 while emitted<4096 do
  local row=gate.region_rng_row(region_cursor)
  if not row then break end
  log('REGION_RNG_ROW '..row);region_cursor=region_cursor+1;emitted=emitted+1
 end
end
local spawn_cursor=0
local function drain_spawn()
 if cfg.native_gate_play_mode then return end
 local emitted=0
 while emitted<4096 do
  local row=gate.spawn_rng_row(spawn_cursor)
  if not row then break end
  log('SPAWN_RNG_ROW '..row);spawn_cursor=spawn_cursor+1;emitted=emitted+1
 end
end
local ai_wait_cursor=0
local function drain_ai_wait()
 if cfg.native_gate_play_mode then return end
 local emitted=0
 while emitted<8192 do
  local row=gate.ai_wait_rng_row(ai_wait_cursor)
  if not row then break end
  log('AI_WAIT_RNG_ROW '..row);ai_wait_cursor=ai_wait_cursor+1;emitted=emitted+1
 end
end
local find_order_cursor=0
local function drain_find_order()
 if cfg.native_gate_play_mode then return end
 local emitted=0
 while emitted<4096 do
  local row=gate.find_order_row(find_order_cursor)
  if not row then break end
  log('FIND_ORDER_ROW '..row);find_order_cursor=find_order_cursor+1;emitted=emitted+1
 end
end
local created_row_cursor=0
local function drain_created_rows()
 if not cfg.native_gate_created_rows then return end
 local emitted=0
 while emitted<16384 do
  local row=gate.created_row(created_row_cursor)
  if not row then break end
  log('CREATED_ROW '..row);created_row_cursor=created_row_cursor+1;emitted=emitted+1
 end
end
local function drain_world_trace()
 if not cfg.native_gate_world_trace_ticks then return end
 local count=assert(gate.world_trace_row(0),'world trace unavailable')
 for row=world_trace_rows+1,count do log(assert(gate.world_trace_row(row),'world trace row unavailable')) end
 world_trace_rows=count
 if cfg.native_gate_world_action_manifest then
  local count=assert(gate.world_action_trace_row(0),'world action trace unavailable')
  for row=world_action_rows+1,count do log(assert(gate.world_action_trace_row(row),'world action row unavailable')) end
  world_action_rows=count
 end
 if cfg.native_gate_world_values then
  if not world_values_cursors_loaded then
   world_values_rows,world_values_unknown_rows,world_unknown_identity_rows=gate.world_values_cursors()
   for _,cursor in ipairs({world_values_rows,world_values_unknown_rows,world_unknown_identity_rows}) do
    assert(type(cursor)=='number' and cursor>=0 and cursor%1==0,'world retained cursor invalid')
   end
   assert(world_values_rows and world_values_unknown_rows and world_unknown_identity_rows,'world retained cursors unavailable')
   world_values_cursors_loaded=true
  end
  local count=assert(gate.world_values_row(0),'world typed output trace unavailable')
  assert(count>=world_values_rows,'world retained output cursor regressed')
  for row=world_values_rows+1,count do log(assert(gate.world_values_row(row),'world typed output row unavailable')) end
  world_values_rows=count
  local unknown=assert(gate.world_values_unknown_row(0),'world unknown-owner trace unavailable')
  assert(unknown>=world_values_unknown_rows,'world retained unknown cursor regressed')
  for row=world_values_unknown_rows+1,unknown do log(assert(gate.world_values_unknown_row(row),'world unknown-owner row unavailable')) end
  world_values_unknown_rows=unknown
  local details=assert(gate.world_values_unknown_identity_row(0),'world unknown identity trace unavailable')
  assert(details>=world_unknown_identity_rows,'world retained identity cursor regressed')
  for row=world_unknown_identity_rows+1,details do log(assert(gate.world_values_unknown_identity_row(row),'world unknown identity row unavailable')) end
  world_unknown_identity_rows=details
 end
 if cfg.native_gate_world_position then
  if world_position_rows==nil then world_position_rows=gate.world_position_cursor() end
  assert(type(world_position_rows)=='number' and world_position_rows>=0 and world_position_rows%1==0 and world_position_rows<=1344,'world position retained cursor invalid')
  local count=assert(gate.world_position_row(0),'world position trace unavailable')
  assert(type(count)=='number' and count%1==0 and count>=world_position_rows and count<=1344,'world position row count invalid')
  for row=world_position_rows+1,count do log(assert(gate.world_position_row(row),'world position row unavailable')) end
  world_position_rows=count
 end
 if cfg.native_gate_world_safe_position then
  if world_safe_position_rows==nil then world_safe_position_rows=gate.world_safe_position_cursor() end
  assert(type(world_safe_position_rows)=='number' and world_safe_position_rows>=0 and world_safe_position_rows%1==0 and world_safe_position_rows<=100,'world safe position retained cursor invalid')
  local count=assert(gate.world_safe_position_row(0),'world safe position trace unavailable')
  assert(type(count)=='number' and count%1==0 and count>=world_safe_position_rows and count<=100,'world safe position row count invalid')
  for row=world_safe_position_rows+1,count do log(assert(gate.world_safe_position_row(row),'world safe position row unavailable')) end
  world_safe_position_rows=count
 end
 if cfg.native_gate_world_position_rng_manifest then
  if world_position_rng_rows==nil then world_position_rng_rows=gate.world_position_rng_cursor() end
  assert(type(world_position_rng_rows)=='number' and world_position_rng_rows>=0 and world_position_rng_rows%1==0 and world_position_rng_rows<=24,'world position RNG retained cursor invalid')
  local count=assert(gate.world_position_rng_row(0),'world position RNG receipts unavailable')
  assert(type(count)=='number' and count%1==0 and count>=world_position_rng_rows and count<=24,'world position RNG row count invalid')
  for row=world_position_rng_rows+1,count do log(assert(gate.world_position_rng_row(row),'world position RNG row unavailable')) end
  world_position_rng_rows=count
 end
 if cfg.native_gate_world_find_rng_manifest then
  local count=assert(gate.world_find_rng_row(0),'world FindComponent RNG trace unavailable')
  for row=world_find_rows+1,count do log(assert(gate.world_find_rng_row(row),'world FindComponent RNG row unavailable')) end
  world_find_rows=count
 end
 if cfg.native_gate_world_queue_trace then
  local queued=assert(gate.world_queue_trace_row(0),'world queue trace unavailable')
  for row=world_queue_rows+1,queued do log(assert(gate.world_queue_trace_row(row),'world queue row unavailable')) end
  world_queue_rows=queued
 end
 if cfg.native_gate_world_wait_manifest then
  local waited=assert(gate.world_wait_trace_row(0),'world wait trace unavailable')
  for row=world_wait_rows+1,waited do log(assert(gate.world_wait_trace_row(row),'world wait row unavailable')) end
  world_wait_rows=waited
  if cfg.native_gate_world_wait_rng_manifest then
   local count=assert(gate.world_wait_rng_row(0),'world Wait RNG trace unavailable')
   for row=world_wait_rng_rows+1,count do log(assert(gate.world_wait_rng_row(row),'world Wait RNG row unavailable')) end
   world_wait_rng_rows=count
  end
 end
end
local function trace_radar_objects(raw,tick)
 for identity,mapped in raw:gmatch('([0-9a-f]+)/(%d+)') do
  if mapped=='0' and not radar_objects[identity] then
   assert(#identity==16,'invalid radar object identity')
   local object=ffi.new('UniverseID',tonumber(identity:sub(1,8),16))*4294967296+tonumber(identity:sub(9),16)
   local luaid=ConvertStringToLuaID((tostring(object):gsub('ULL$','')))
   local macro,owner=GetComponentData(luaid,'macro','owner')
   local function atom(value) return tostring(value or 'none'):gsub('[^%w_.%-]','_') end
   local nativecode=ffi.C.GetObjectIDCode(object)
   local code=nativecode~=nil and ffi.string(nativecode) or 'none'
   local player=(object==ffi.C.GetPlayerContainerID() and 1 or 0)
     +(object==ffi.C.GetPlayerOccupiedShipID() and 2 or 0)+(object==ffi.C.GetPlayerControlledShipID() and 4 or 0)
   local saved=cfg.native_gate_world_identities and (' saved='..assert(gate.saved_identity((tostring(object):gsub('ULL$',''))))) or ''
   log('RADAR_OBJECT tick='..tick..' id='..identity..' macro='..atom(macro)..' owner='..atom(owner)..' player='..player..' code='..atom(code)..saved)
   radar_objects[identity]=true
  end
 end
end
local function registered_ships(manifest)
 local result={};for i=1,12 do result[#result+1]=i end
 for slot in (type(manifest)=='string' and manifest or ''):gmatch('(%d+)/[0-9a-f]+/[a-z0-9_]+/[a-z0-9_]+') do result[#result+1]=tonumber(slot) end
 return result
end
local function enroll_dynamic_weapons(object,actor,handle)
 local ok,result=pcall(function()
  assert(actor>=15 and actor<=30 and actor%1==0 and not weapons[actor],'invalid dynamic weapon enrollment')
  local count=tonumber(ffi.C.GetNumUpgradeSlots(object,'','weapon'))
  local turrets=tonumber(ffi.C.GetNumUpgradeSlots(object,'','turret'))
  assert(count and count>=0 and count<=32 and count%1==0,'dynamic weapon slot bounds')
  assert(turrets and turrets>=0 and turrets<=128 and turrets%1==0,'dynamic turret slot bounds')
  local occupied=0
  for slot=1,turrets do if ffi.C.GetUpgradeSlotCurrentComponent(object,'turret',slot)~=0 then occupied=occupied+1 end end
  if occupied>0 then log('SHIP_WEAPON_UNSUPPORTED handle='..handle..' slot='..actor..' turret_slots='..turrets..' occupied_turrets='..occupied) end
  assert(occupied==0,'occupied dynamic surface turret layout unsupported')
  weapons[actor]={}
  for slot=1,count do
   local id=ffi.C.GetUpgradeSlotCurrentComponent(object,'weapon',slot)
   if id~=0 then
    assert(ffi.C.IsComponentOperational(id),'dynamic weapon unavailable')
    assert(gate.register_weapon((tostring(id):gsub('ULL$','')),actor,slot)==1,'dynamic weapon registration refused')
    weapons[actor][#weapons[actor]+1]=slot
    local luaid=ConvertStringToLuaID((tostring(id):gsub('ULL$','')))
    log('WEAPON_REGISTER id='..actor..' slot='..slot..' macro='..tostring(GetComponentData(luaid,'macro')))
   end
  end
  log('SHIP_WEAPON_SCOPE handle='..handle..' slot='..actor..' weapon_slots='..count..' registered='..#weapons[actor]..' turret_slots='..turrets..' occupied_turrets='..occupied)
  return 1
 end)
 if not ok then log('SHIP_ENROLL_REFUSED '..tostring(result));gate.stop();return nil,result end
 return result
end
local function trace_gravidar(tick,manifest)
 if not cfg.native_gate_radar_serial then return end
 for _,i in ipairs(registered_ships(manifest)) do
  local raw=assert(gate.gravidar(i))
  local updated,calls=raw:match('^tick=(%d+) calls=(%d+)')
  assert(updated and calls,'malformed native radar unit trace')
  if tonumber(updated)==tick and tonumber(calls)>0 then
   log('GRAVIDAR_UNIT id='..i..' '..raw)
   for stage=1,8 do
    local contacts=assert(gate.gravidar_contacts(i,stage))
    log('GRAVIDAR_CONTACTS id='..i..' '..contacts);trace_radar_objects(contacts,tick)
   end
  end
 end
end
-- The value after the first key (';name=' plain search, then an anchored class): ten times cheaper than
-- an unanchored pattern scan of a 3 KB actor raw, the same value for the C-formatted raws (each key once).
local function raw_field(raw,key,class)
 local at=raw:find(key,1,true)
 return at and raw:match(class,at+#key)
end
-- Eight-hex words of a value, some skipped, joined by ':' in one gsub (no string per word).
local WORDS_7='^(........)(........)(........)........(........)(........)(........)(........)$' -- skip word 3
local WORDS_6='^(........)(........)(........)........(........)(........)(........)........$' -- skip 3 and 7
local VECTORS='^'..string.rep('(........................)........',6)..'$' -- six xyz vectors, storage w skipped
local function physics_field(raw,name,size)
 local value=raw_field(raw,name..'=','^[0-9a-f]+')
 assert(value,'physics '..name..' missing')
 assert(#value==size,'invalid physics '..name..' size')
 return value
end
local function physics_bits(raw)
 assert(type(raw)=='string','native physics state unavailable')
 if raw=='none' then return raw end
 local pose,motion=physics_field(raw,'pose',64),physics_field(raw,'motion',256)
 local pre,delta=physics_field(raw,'pre',64),physics_field(raw,'delta',64)
 return table.concat({
  (pose:gsub(WORDS_7,'%1:%2:%3:%4:%5:%6:%7')), -- xyz and quaternion; skip position storage w.
  (motion:sub(1,64):gsub(WORDS_6,'%1:%2:%3:%4:%5:%6')), -- velocities, inverse inertia/frame/mass:
  (motion:sub(65,128):gsub(WORDS_7,'%1:%2:%3:%4:%5:%6:%7')), -- words 0-2 4-6 8-10 12-15 22
  motion:sub(177,184),
  motion:sub(243,244), -- allowed DOFs byte at MotionProperties+0x7a.
  (pre:gsub(WORDS_6,'%1:%2:%3:%4:%5:%6')),(delta:gsub(WORDS_6,'%1:%2:%3:%4:%5:%6'))},':')
end
-- Piloting prototype, per admitted tick: stage every slot's admitted record
-- (wire), capture and propose the local one, log status, and return the
-- applied-request receipt of this tick's pathing pass for the state hash.
local pilot_proposal
local pilot_records -- this tick's admitted pilot section of every slot (wire source)
local pilot_camera_follow,pilot_camera_stage=false,nil -- this client follows its own slot ship (presentation only)
-- predict: presentation prediction on this client ('observe' or 'present'), raw: its RAW rows;
-- hits: hit claims armed (blobs are input containers, gate.input_split/input_mux); fire:
-- pilot firing mode; stock: 'capture' | 'replay' (stock piloting), the stock slots (set), its
-- pending proposal and scripted events. One table keeps the probe's arming function below LuaJIT's upvalue limit.
local pilot_view={predict=nil,raw=false,hits=nil,fire=nil,stock=nil,stock_slots=nil,stock_proposal=nil,stock_script=nil}
-- Per-tick state of every slot ship (position in sector, hull, shield): the
-- ships the humans fly enter the declared state every tick, so a desync of a
-- driven ship (the occupied player ship is not a fixture) reaches the relay at once.
local function pilot_ship_rows(tick)
 local rows={}
 for slot=1,#cfg.native_gate_pilot_slots do
  local runtime,saved=gate.pilot_ship(slot)
  local row=slot..'/'..tostring(saved or cfg.native_gate_pilot_slots[slot])
  if not runtime then row=row..'/unresolved'
  else
   local object=ffi.new('UniverseID',runtime)
   if ffi.C.IsComponentOperational(object) then
    local p=ffi.C.GetObjectPositionInSector(object)
    if pilot_view.death_last then pilot_view.death_last[slot]={x=p.x,y=p.y,z=p.z,yaw=math.deg(p.yaw),pitch=math.deg(p.pitch),roll=math.deg(p.roll)} end
    local hull,shield=GetComponentData(ConvertStringToLuaID(tostring(runtime)),'hullpercent','shieldpercent')
    assert(type(hull)=='number' and type(shield)=='number','pilot ship health sample missing')
    row=row..string.format('/%.9g/%.9g/%.9g/%.9g/%.9g/%.9g/%.17g/%.17g',p.x,p.y,p.z,p.yaw,p.pitch,p.roll,hull,shield)
   else row=row..'/dead' end
  end
  rows[#rows+1]=row
 end
 local text=table.concat(rows,',')
 log('PILOT_SHIPS tick='..tick..' '..text)
 return 'PILOT_SHIPS_1:'..text
end
-- Sandbox order block (native_gate_pilot_block_orders): orders, travel/SETA
-- and autopilot started from the interact menu or the map would run on this
-- client only. The stock menus call these through their own tables, so the
-- entries are replaced by refusals once the menus exist (logged, no wire).
local ORDER_BLOCK={
 InteractMenu={'buttonTravelMode','buttonRepeatLastOrder','buttonProceedWithOrders','buttonRemoveOrder','buttonRemoveAllOrders',
  'buttonChangeOverrideOrder','buttonAssignCommander','buttonAssignFleetUnitCommander','buttonChangeAssignment',
  'buttonChangeFleetUnitAssignment','buttonFormationShape','buttonRecallSubordinates','buttonRemoveAssignment',
  'buttonTriggerSyncPoint','setOrderImmediate'},
 MapMenu={'buttonConfirmTrade','buttonConfirmTradeLoop','buttonDefaultOrderConfirm','buttonDeleteAllOrders','buttonDockToTrade',
  'buttonNewOrder','buttonOrderDown','buttonOrderUp','buttonReleaseSyncPoint','buttonRemoveAssignment','buttonSetOrderParam',
  'buttonStartOrders','checkboxOrderPlayerOverride','checkboxOrderPlayerOverrideValue','dropdownBehaviourFormation',
  'dropdownChangeOverrideOrder','dropdownNewSyncPoint','removeOrder','setOrderParamFromMode','slidercellSetOrderParam'},
}
-- Map hotkeys that start autopilot, travel mode or SETA on the player ship.
local HOTKEY_BLOCK={INPUT_ACTION_ADDON_DETAILMONITOR_A_SHIFT=true,INPUT_ACTION_ADDON_DETAILMONITOR_TRAVELMODE=true,INPUT_ACTION_ADDON_DETAILMONITOR_SETA=true}
local orders_blocked={}
local function block_orders()
 if orders_blocked.InteractMenu and orders_blocked.MapMenu then return end -- both wrapped: nothing left per tick
 for _,menu in ipairs(type(Menus)=='table' and Menus or {}) do
  local names=ORDER_BLOCK[menu.name]
  if names and not orders_blocked[menu.name] then
   orders_blocked[menu.name]=true
   local menu_name,count=menu.name,0
   local function refuse(name) return function() log('PILOT_ORDER_BLOCKED menu='..menu_name..' entry='..name);return 0 end end
   -- UI replay: what it captures stays open, everything else refuses as before.
   local open=pilot_view.ui_replay and pilot_view.ui_replay.unblocked or {}
   local seen={}
   for name,value in pairs(menu) do if type(value)=='function' and name:match('^order%u') then seen[name]=true end end
   for _,name in ipairs(names) do if type(menu[name])=='function' then seen[name]=true end end
   local sorted={};for name in pairs(seen) do sorted[#sorted+1]=name end
   table.sort(sorted)
   for _,name in ipairs(sorted) do
    if not open[menu_name..'.'..name] then menu[name]=refuse(name);count=count+1 end
   end
   if menu_name=='MapMenu' and type(menu.hotkey)=='function' then
    local hotkey=menu.hotkey
    menu.hotkey=function(action,...)
     if HOTKEY_BLOCK[action] and not open['hotkey.'..tostring(action)] then log('PILOT_ORDER_BLOCKED menu=MapMenu entry=hotkey:'..tostring(action));return end
     return hotkey(action,...)
    end
    count=count+1
   end
   log('PILOT_ORDERS_BLOCKED menu='..menu_name..' entries='..count)
  end
 end
end
-- The pilot section of the input multiplexer (wire source): apply keeps every slot's admitted
-- record for pilot_frame, the proposal is this tick's local capture. Outside frame(), whose
-- upvalue count is at LuaJIT's limit.
local function arm_pilot_section()
 input_section('pilot','pilot',{apply=function(_,records) pilot_records=records end,
  proposal=function() local r=pilot_proposal;pilot_proposal=nil;return r end})
 -- Native hit claims: every slot's admitted hits payload is staged in slot order (a refusal is
 -- the same on every client: logged, not fatal); the proposal is this client's queued claims that
 -- fit the budget (the rest wait). Claims due are resolved in pilot_frame.
 local hits=cfg.native_gate_hit_claims
 if hits~=nil and hits~=false and hits~='carry' then
  input_section('hits','hits_native',{
   apply=function(tick,inputs)
    local admitted,refused=0,0
    for slot=1,#inputs do
     local payload=inputs[slot]
     if payload and payload~='' then
      if gate.hit_claims_admit(slot,payload)==1 then admitted=admitted+1
      else refused=refused+1;ticklog.write('HIT_CLAIMS_REFUSED tick='..tick..' slot='..slot) end
     end
    end
    return 'staged='..admitted..'/refused='..refused
   end,
   proposal=function(tick,budget) return gate.hit_claims_proposal(tick,budget) end})
 end
end
-- Stock piloting (lockstep_pilot_stock.h): every stock slot's record rides section 'stock' (kind
-- 0x53), authored by that slot's own client; every client stages every one. The proposal is this
-- tick's capture of this client's own slot.
local function arm_stock_section()
 input_section('stock','stock',{
  apply=function(tick,inputs)
   local staged=0
   for slot=1,#inputs do
    local payload=inputs[slot]
    if payload and payload~='' then
     assert(pilot_view.stock_slots[slot],'stock record from slot '..slot)
     assert(gate.pilot_stock_apply(slot,payload)==1,'stock record refused slot='..slot)
     staged=staged+1
    end
   end
   return 'stock='..staged
  end,
  proposal=function() local r=pilot_view.stock_proposal;pilot_view.stock_proposal=nil;return r end})
end
-- Scripted stock input for smokes without a human ('demo', 'demo2'..'demo4' or {{tick,kind,id,value},...},
-- absolute admitted ticks; the named ones are relative to the pilot window start): ids of the
-- replay set. 'demo2'..'demo4' are for slots 2..4 (no travel, scan or SETA: MD activities stay slot 1's).
local STOCK_DEMO={{10,'state',0xd,1},{70,'state',0xd,0},{100,'range',1,0.5},{160,'range',1,0},{180,'range',2,-0.4},
 {220,'range',2,0},{240,'state',0x11,1},{300,'state',0x11,0},{330,'action',0x12f,0},{480,'action',0x12f,0},{500,'action',0x7b,0},
 {520,'action',0xdd,0},{560,'action',0xdd,0}}
local STOCK_DEMO2={{15,'state',0xd,1},{75,'state',0xd,0},{110,'range',2,0.4},{160,'range',2,0},{190,'range',1,-0.5},
 {240,'range',1,0},{260,'state',0x11,1},{320,'state',0x11,0},{350,'state',0xe,1},{400,'state',0xe,0},{450,'action',0x7b,0},
 {470,'state',0xd,1},{530,'state',0xd,0},{545,'action',0xdd,0},{575,'action',0xdd,0}}
local STOCK_DEMO3={{20,'state',0xd,1},{90,'state',0xd,0},{120,'range',1,0.4},{170,'range',1,0},{200,'range',2,0.5},
 {250,'range',2,0},{280,'state',0x11,1},{340,'state',0x11,0},{370,'state',0xe,1},{410,'state',0xe,0},{460,'action',0x7b,0},
 {480,'state',0xd,1},{540,'state',0xd,0},{550,'action',0xdd,0},{580,'action',0xdd,0}}
local STOCK_DEMO4={{25,'state',0xd,1},{95,'state',0xd,0},{125,'range',2,-0.3},{175,'range',2,0},{205,'range',1,0.6},
 {255,'range',1,0},{290,'state',0x11,1},{350,'state',0x11,0},{380,'state',0xe,1},{420,'state',0xe,0},{470,'action',0x7b,0},
 {490,'state',0xd,1},{550,'state',0xd,0},{555,'action',0xdd,0},{585,'action',0xdd,0}}
local STOCK_DEMOS={demo=STOCK_DEMO,demo2=STOCK_DEMO2,demo3=STOCK_DEMO3,demo4=STOCK_DEMO4}
-- 'aim': a hit test without people. Each slot stops, then steers at the next slot's ship through its own
-- recorded stock input (axes 1 yaw, 2 pitch, negative is nose up). Both poses are the sector poses of
-- PILOT_SHIPS: forward = (sin yaw cos pitch, sin pitch, cos yaw cos pitch).
function X4LAN_STOCK_AIM_AXES(me,other)
 local dx,dy,dz=other.x-me.x,other.y-me.y,other.z-me.z
 local yaw=math.atan2(dx,dz)-me.yaw
 while yaw>math.pi do yaw=yaw-2*math.pi end
 while yaw<-math.pi do yaw=yaw+2*math.pi end
 local pitch=math.atan2(dy,math.sqrt(dx*dx+dz*dz))-me.pitch
 local function axis(e)
  local v=math.max(-0.5,math.min(0.5,e*2))
  return math.abs(v)<0.004 and 0 or v
 end
 return axis(yaw),axis(-pitch)
end
local stock_aim_state={stopped=false,axes={0,0}}
local function stock_aim()
 if not gate.pilot_ship then return end
 local count=#cfg.native_gate_pilot_slots
 local me=tonumber(tostring(cfg.slot):match('^p(%d)$'))
 local function pose(s)
  local runtime=gate.pilot_ship(s)
  if not runtime then return nil end
  local object=ffi.new('UniverseID',runtime)
  return ffi.C.IsComponentOperational(object) and ffi.C.GetObjectPositionInSector(object) or nil
 end
 local a,b=pose(me),pose(me%count+1)
 if not a or not b then return end
 if not stock_aim_state.stopped then
  assert(gate.pilot_stock_inject('action',0x7b,0)==1,'pilot stock aim stop refused')
  stock_aim_state.stopped=true
 end
 local wanted={X4LAN_STOCK_AIM_AXES(a,b)}
 for i=1,2 do
  if wanted[i]~=stock_aim_state.axes[i] and (wanted[i]==0 or math.abs(wanted[i]-stock_aim_state.axes[i])>=0.01) then
   assert(gate.pilot_stock_inject('range',i,wanted[i])==1,'pilot stock aim axis refused')
   stock_aim_state.axes[i]=wanted[i]
  end
 end
end
local function stock_script(plan,start)
 if not plan then return nil end
 if plan=='aim' then pilot_view.stock_aim_from=start+2;pilot_view.stock_aim=stock_aim;return nil end
 local list,base=plan,0
 if type(plan)=='string' then list,base=STOCK_DEMOS[plan],start end
 assert(type(list)=='table','pilot stock script must be demo, demo2, demo3, demo4 or a list')
 local by={}
 for _,e in ipairs(list) do
  local tick,kind,id,value=e[1],e[2],e[3],e[4]
  assert(type(tick)=='number' and tick%1==0 and tick>=0 and (kind=='action' or kind=='state' or kind=='range') and
   type(id)=='number' and id%1==0 and id>=0 and id<512 and type(value)=='number','invalid pilot stock script event')
  local at=base+tick
  by[at]=by[at] or {};by[at][#by[at]+1]={kind,id,value}
 end
 return by
end
pilot_view.arm_stock,pilot_view.stock_plan=arm_stock_section,stock_script -- reached through pilot_view: frame() is at the upvalue limit
-- A slot ship's death (lockstep_pilot_death.h, stop reason 163; native_gate_pilot_death, default off). The gate
-- says what changed in this tick's pass; with native_gate_pilot_respawn = delay the probe raises the MD events
-- (native_gate_pilot_respawn.xml) at those ticks, on every client, and hands the ship MD built to the gate.
local death={delay=nil,start=0,poses={},built={},suits={}}
-- MD answers with an object: the gate takes its runtime ID as a decimal string.
local function death_answer(name,slot,hand)
 RegisterEvent(name..slot,function(_,object)
  local id=object
  if type(id)=='number' then id=string.format('%.0f',id)
  else
   if type(id)~='cdata' then id=ConvertIDTo64Bit(id) end
   id=(tostring(id):gsub('ULL$',''))
  end
  log(name:gsub('^X4LAN_PilotRespawn.','PILOT_RESPAWN_'):upper()..' slot='..slot..' accepted='..tostring(hand(slot,id)))
 end)
end
local function death_arm(delay,start,suit)
 death.delay,death.start,death.suit=delay,start,suit
 if suit then pilot_view.death_last={} end
 for slot=1,#cfg.native_gate_pilot_slots do
  if delay>0 then death_answer('X4LAN_PilotRespawn.Ship',slot,gate.pilot_death_ship) end
  if suit then death_answer('X4LAN_PilotRespawn.Suit',slot,gate.pilot_death_suit) end
 end
end
-- A slot's spacesuit, as X4's eject builds its own player's: up from where the ship died, its pose kept.
local DEATH_SUIT_UP=100
local function death_suit(slot)
 local last=assert(pilot_view.death_last[slot],'pilot death: no last pose for slot '..slot)
 death.suits[slot]=(death.suits[slot] or 0)+1
 AddUITriggeredEvent('X4LAN_PilotRespawn','Suit',{slotname=tostring(slot),seed=8000+100*slot+death.suits[slot],
  x=last.x,y=last.y+DEATH_SUIT_UP,z=last.z,yaw=last.yaw,pitch=last.pitch,roll=last.roll})
end
local function death_note(slot)
 local runtime=gate.pilot_ship(slot)
 assert(runtime,'pilot respawn: slot '..slot..' has no ship at the window start')
 local p=ffi.C.GetObjectPositionInSector(ffi.new('UniverseID',runtime))
 death.poses[slot]={x=p.x,y=p.y,z=p.z,yaw=math.deg(p.yaw),pitch=math.deg(p.pitch),roll=math.deg(p.roll)}
 AddUITriggeredEvent('X4LAN_PilotRespawn','Note',{slotname=tostring(slot),object=ConvertStringToLuaID(tostring(runtime))})
end
local function death_frame(tick)
 if (death.delay>0 or death.suit) and tick==death.start then for slot=1,#cfg.native_gate_pilot_slots do death_note(slot) end end
 local events=gate.pilot_death_frame()
 if events then
  ticklog.write('PILOT_DEATH tick='..tick..' '..events) -- rare, and the report's first question: not held by the status stride
  for kind,slot in events:gmatch('(%a+):(%d+)') do
   slot=tonumber(slot)
   if kind=='suit' then death_suit(slot)
   elseif kind=='create' then
    local pose=assert(death.poses[slot],'pilot respawn: no start pose for slot '..slot)
    death.built[slot]=(death.built[slot] or 0)+1
    AddUITriggeredEvent('X4LAN_PilotRespawn','Respawn',{slotname=tostring(slot),seed=7000+100*slot+death.built[slot],
     x=pose.x,y=pose.y,z=pose.z,yaw=pose.yaw,pitch=pose.pitch,roll=pose.roll})
   elseif kind=='live' and tostring(cfg.slot)=='p'..slot then
    -- The new ship starts from nothing held: the aim script sends its axes and its stop again.
    stock_aim_state.stopped=false;stock_aim_state.axes[1]=0;stock_aim_state.axes[2]=0
   end
  end
 end
 if tick%600==0 then ticklog.status('PILOT_DEATH_STATUS',tick,gate.pilot_death_status) end
end
pilot_view.death_arm,pilot_view.death_frame=death_arm,death_frame
-- Scripted fire of some slots only (a test's shooter): the list as a bit mask.
function X4LAN_PILOT_FIRE_MASK(list,fire,count)
 if list==nil then return 0 end
 assert(fire=='script' and type(list)=='table','pilot fire refused: native_gate_pilot_fire_slots is a list, for script fire')
 local mask,seen=0,{}
 for _,s in ipairs(list) do
  assert(type(s)=='number' and s%1==0 and s>=1 and s<=count and not seen[s],'pilot fire refused: native_gate_pilot_fire_slots invalid')
  seen[s]=true;mask=mask+2^(s-1)
 end
 return mask
end
-- UI replay (lockstep_ui_replay.h, native_gate_ui_replay_core.lua; stop reason 164): stock UI actions as events of this
-- client's slot, section 'ui' (kind 0x55) of the input blob; native events apply in path_release, the
-- rest in this frame. Armed after gate.pilot_present.
function pilot_view.arm_ui_replay(slot,window)
 local core=assert(X4LAN_UI_REPLAY_CORE,'ui replay core missing (ui/native_gate_ui_replay_core.lua)')
 local mask,why=core.families(cfg.native_gate_ui_replay)
 assert(mask,'ui replay refused: '..tostring(why))
 assert(cfg.native_gate_pilot_present,'ui replay refused: needs native_gate_pilot_present')
 assert(cfg.native_gate_runtime_ids~=nil and cfg.native_gate_runtime_ids~='observe','ui replay refused: needs native_gate_runtime_ids')
 assert(gate.ui_replay,'ui replay refused: gate without ui_replay')
 if cfg.native_gate_ui_replay_fleet then
  assert(type(cfg.native_gate_ui_replay_fleet)=='table' and #cfg.native_gate_ui_replay_fleet>=1,'ui replay refused: invalid test fleet')
  if not pcall(ffi.typeof,'Order3') then
   ffi.cdef[[typedef struct {
    size_t queueidx; const char *state,*statename,*orderdef; size_t actualparams;
    bool enabled,isinfinite,issyncpointreached,istemporder,isoverride,ispriority;
   } Order3;]]
  end
  for _,line in ipairs({'uint32_t GetNumOrders(UniverseID);','uint32_t GetOrders3(Order3*,uint32_t,UniverseID);','size_t GetOrderQueueCurrentIdx(UniverseID);'}) do
   pcall(ffi.cdef,line)
  end
 end
 -- What the scripted runs and the apply call; the stock menus declare the same.
 for _,line in ipairs({'void StartPlayerActivity(const char*);','void StopPlayerActivity(const char*);','bool SetSofttarget(UniverseID,const char*const);',
   'bool RemoveAllOrders(UniverseID);','bool RemoveAllOrders2(UniverseID,bool,bool);'}) do pcall(ffi.cdef,line) end
 local armed,refused=gate.ui_replay(mask)
 assert(armed==1,'ui replay refused: '..tostring(refused))
 local function slot_ships()
  local out={}
  for s=1,#cfg.native_gate_pilot_slots do
   local runtime=gate.pilot_ship(s)
   if runtime then out[string.format('%.0f',runtime)]=s end
  end
  return out
 end
 local tools={gate=gate,C=ffi.C,env=_G,
  slot_id=function(s) return ffi.new('UniverseID',assert(gate.pilot_ship(s),'ui replay script: slot ship unresolved')) end,
  saved_id=function(saved) return ConvertStringTo64Bit(assert(gate.ui_replay_saved(saved),'ui replay script: saved ID unresolved')) end,
  saved_lua=function(saved) return ConvertStringToLuaID(assert(gate.ui_replay_saved(saved),'ui replay script: saved ID unresolved')) end}
 local plan=cfg.native_gate_ui_replay_script   -- one plan per client, {} or false for none
 assert(plan==nil or plan==false or type(plan)=='table','ui replay refused: invalid script')
 plan=plan and plan[slot] or nil
 -- The test fleet's queue, read only: how many orders and the current one's definition.
 local function queue_of(saved)
  local runtime=gate.ui_replay_saved(saved)
  if not runtime then return 'gone' end
  local id=ConvertStringTo64Bit(runtime)
  if not ffi.C.IsComponentOperational(id) then return 'dead' end
  local count=tonumber(ffi.C.GetNumOrders(id))
  if count==0 then return '0' end
  local orders=ffi.new('Order3[?]',count)
  count=tonumber(ffi.C.GetOrders3(orders,count,id))
  local parts={tostring(count),tostring(tonumber(ffi.C.GetOrderQueueCurrentIdx(id)))}
  for i=0,count-1 do parts[#parts+1]=ffi.string(orders[i].orderdef)..'/'..ffi.string(orders[i].state) end
  return table.concat(parts,'/')
 end
 -- Its rows are events, written at once: log holds frame rows for the ticks the status stride writes.
 local S=core.session{gate=gate,C=ffi.C,env=_G,mask=mask,log=ticklog.write,slot_ships=slot_ships,script=core.script(plan,window[1],tools),
  fleet=cfg.native_gate_ui_replay_fleet,fleet_tick=window[1]-4,saved_lua=tools.saved_lua,queue_of=queue_of,
  local_mark=cfg.native_gate_runtime_ids_local and gate.runtime_ids_probe or nil}
 pilot_view.ui_replay=S
 input_section('ui','ui',S.section())
 log('UI_REPLAY_ARMED slot='..slot..' families='..mask..' script='..tostring(plan and #plan or 0))
end
local function pilot_frame(s,tick)
 if cfg.native_gate_pilot_block_orders then block_orders() end
 if pilot_view.death then pilot_view.death_frame(tick) end
 local wire=cfg.native_gate_pilot_source=='wire'
 if wire and pilot_records then
  for slot=1,#cfg.native_gate_pilot_slots do
   local record=pilot_records[slot]
   if record and record~='' then assert(gate.pilot_apply(slot,record)==1,'pilot record refused slot='..slot) end
  end
 end
 pilot_records=nil
 -- Claims due this tick, every client, before this tick's capture.
 local hits_receipt
 if pilot_view.hits then
  hits_receipt=assert(gate.hit_claims_resolve(tick),'hit claims resolution failed')
  if not hits_receipt:match('^HITS_1:%d+:0:') then ticklog.write('HIT_CLAIMS tick='..tick..' '..hits_receipt) end
  if tick%600==0 then ticklog.status('HIT_CLAIMS_STATUS',tick,gate.hit_claims_status) end
 end
 if tick==1 then
  local ok,ship=pcall(function() return ffi.C.GetPlayerOccupiedShipID() end)
  local saved=ok and gate.saved_identity((tostring(ship):gsub('ULL$','')))
  log('PILOT_PLAYER tick=1 occupied='..tostring(saved or 'unknown'))
 end
 -- Stock capture (slot 1's client): scripted events first, then this tick's record; nil only
 -- before the window opens. Its slot has no pilot record.
 local stock_capture=pilot_view.stock=='capture'
 if stock_capture then
  for _,e in ipairs(pilot_view.stock_script and pilot_view.stock_script[tick] or {}) do
   assert(gate.pilot_stock_inject(e[1],e[2],e[3])==1,'pilot stock script event refused tick='..tick)
  end
  -- The gate refuses injects near the window's end; only a ship that is still alive there reaches them.
  if pilot_view.stock_aim_from and tick>=pilot_view.stock_aim_from and tick+8<cfg.native_gate_pilot_window[2] then pilot_view.stock_aim() end
  local record,why=gate.pilot_stock_capture()
  assert(record or why=='not_live','pilot stock capture failed '..tostring(why))
  pilot_view.stock_proposal=record
  if cfg.native_gate_pilot_capture and record then log('PILOT_STOCK_CAPTURE tick='..tick..' record='..record) end
 end
 if (wire or cfg.native_gate_pilot_capture) and not stock_capture then
  -- Capture never changes local input. Only the wire needs it to succeed.
  local record,why=gate.pilot_capture()
  assert(record or not wire,'pilot capture failed '..tostring(why))
  if cfg.native_gate_pilot_capture then
   local row='PILOT_CAPTURE tick='..tick..(record and ' record='..record or ' refused='..tostring(why))
   -- Observation: what the native processed channels hold now (zero while parked).
   if cfg.native_gate_pilot_observe_native then
    local native,refused=gate.pilot_native_capture()
    row=row..(native and ' native='..native or ' native_refused='..tostring(refused))
   end
   log(row)
  end
  if wire then pilot_proposal=record end
 end
 -- Native controls: flight actions refused on this client since the last tick.
 if cfg.native_gate_pilot_capture_source~=nil then
  local refusals=gate.pilot_native_refusals()
  for item in (refusals or ''):gmatch('[^,]+') do
   local at,kind,id,name,value=item:match('^(%d+):(%a+):(0x%x+):([%w_?]+):(%-?%d+)$')
   ticklog.write('PILOT_NATIVE_REFUSED tick='..tostring(at)..' kind='..tostring(kind)..' id='..tostring(id)..' input='..tostring(name)..' value='..tostring(value))
  end
 end
 -- Presentation prediction: after this tick's capture (the pending ring is
 -- complete). This client only; logged, never part of the declared state, and
 -- a refusal never stops the session.
 if pilot_view.predict then
  local row,why=gate.pilot_predict_frame(tick)
  log('PILOT_PREDICT '..(row or ('tick='..tick..' refused='..tostring(why))))
  if pilot_view.raw then
   for slot=1,#cfg.native_gate_pilot_slots do
    local raw=gate.pilot_predict_raw(slot)
    if raw then log('PILOT_PREDICT_RAW '..raw) end
   end
  end
  if tick%600==0 then ticklog.status('PILOT_PREDICT_STATUS',tick,gate.pilot_predict_status) end
 end
 ticklog.status('PILOT_INPUTS',tick,gate.pilot_inputs_status,'pilot status unavailable')
 if pilot_view.stock and tick%600==0 and gate.pilot_stock_callers then
  local callers=gate.pilot_stock_callers()
  if callers then log('PILOT_STOCK_CALLERS tick='..tick..' '..callers) end
 end
 if pilot_view.stock and gate.pilot_present_diff then
  local diff=gate.pilot_present_diff()
  if diff then log('PILOT_PRESENT_DIFF tick='..tick..' '..diff) end
 end
 -- Camera follow: presentation only, never part of the declared state. Logged
 -- when its stage changes and every 600 ticks.
 local window=cfg.native_gate_pilot_window or {30,600}
 if pilot_camera_follow and tick>=window[1] and tick<=window[2] then
  local stage=gate.pilot_camera(tick)
  if stage~=pilot_camera_stage or tick%600==0 then log('PILOT_CAMERA tick='..tick..' '..tostring(gate.pilot_camera_status())) end
  pilot_camera_stage=stage
 end
 local receipt=assert(gate.pilot_inputs_frame(),'pilot receipt unavailable')
 -- Stock piloting: this tick's replayed record, flight state and settings fingerprint (every client).
 if pilot_view.stock then receipt=receipt..'|'..assert(gate.pilot_stock_frame(),'pilot stock receipt unavailable') end
 if pilot_view.ui_replay then receipt=receipt..'|'..pilot_view.ui_replay.frame(tick) end
 if cfg.native_gate_pilot_ship_state then receipt=receipt..'|'..pilot_ship_rows(tick) end
 if hits_receipt then receipt=receipt..'|'..hits_receipt end
 -- Pilot firing: the slot ships' guns after this tick's walk, every client.
 if pilot_view.fire then
  receipt=receipt..'|'..assert(gate.pilot_fire_frame(),'pilot fire receipt unavailable')
  if tick%600==0 then ticklog.status('PILOT_FIRE_STATUS',tick,gate.pilot_fire_status) end
  -- Sampled: the declared receipt and the guns it read, to name a difference without a rerun.
  if tick%60==0 then
   log('PILOT_RECEIPT tick='..tick..' '..receipt)
   local guns=gate.pilot_fire_guns and gate.pilot_fire_guns()
   if guns then log('PILOT_FIRE_GUNS tick='..tick..' '..guns) end
  end
 end
 return receipt
end
local function sample(tick)
 if cfg.native_gate_hash=='clock' then return 'CLOCK_ONLY_1' end
 if not ready then
  assert(tick<=4,'battle fixture did not spawn');return 'BATTLE_FIXTURE_PENDING_1'
 end
 local rows={}
 local dockships,dockanchors
 if cfg.native_gate_docking_state then dockships,dockanchors=assert(X4LAN_NATIVE_DOCKING,'native docking reader missing')(actors,tick,gate) end
 local window=cfg.native_gate_trace_window
 local trace=tick%(cfg.native_gate_trace_stride or 1)==0 or (window and window[1]<=tick and tick<=window[2])
 if trace and cfg.native_gate_docking_shape_rng then
  for actor=13,14 do for which=0,1 do log('DOCK_SHAPE id='..actor..' which='..which..' '..assert(gate.shape(actor,which))) end end
  for actor=13,14 do for which=2,16 do
   local extra=gate.shape(actor,which)
   if extra then log('DOCK_EXTRA_SHAPE id='..actor..' which='..which..' '..extra) end
  end end
 end
 for i=1,12 do
  local object=assert(actors[i],'missing battle actor')
  if cfg.native_gate_world_identities and not world_actors[i] then
   local saved=assert(gate.saved_identity((tostring(object):gsub('ULL$',''))))
   assert(saved~='0000000000000000','fixture missing saved identity')
   log('WORLD_ACTOR id='..i..' saved='..saved);world_actors[i]=true
  end
  local bodies,row={} -- row: this actor's declared state parts
  local function body(which)
   if not bodies[which] then bodies[which]=assert(gate.body(i,which),'physics body state unavailable') end
   return bodies[which]
  end
  if ffi.C.IsComponentOperational(object) then
   local native
   if cfg.native_gate_actor_diag then
    local since=profile and gate.qpc_us()
    local raw,why=gate.actor(tonumber(object),i,cfg.native_gate_shield_refresh and 1 or 0)
    if trace then log('ACTOR tick='..tick..' id='..i..' raw='..tostring(raw)) end
    if cfg.native_gate_strict_state then
     assert(type(raw)=='string','native actor state unavailable id='..i..' '..tostring(why))
     local hull=assert(raw_field(raw,';hull_bits=','^[0-9a-f]+'),'raw hull missing')
     local shield=assert(raw_field(raw,';shield=','^[^;]+'),'stored shield missing')
     assert(#hull==16 and (shield=='none' or shield:match('^[0-9a-f]+$')),'invalid native health state')
     local dock=raw_field(raw,';controller=','^[^;]+')
     local parts={hull,shield} -- joined by ':' once
     if dock then
      assert(dock=='dock' and cfg.native_gate_docking_state,'native dock controller requires docking state capture')
      local values=assert(raw_field(raw,';dock_state=','^[^;]+'),'native dock controller state missing')
      local kind,time,animation,flags,vectors=values:match('^(%u+CTL_1):([0-9a-f]+):([0-9a-f]+):([0-9a-f]+):([0-9a-f]+)$')
      assert(time and #time==16 and #animation==8 and
        ((kind=='DOCKCTL_1' and flags:match('^0[01]0[01]$') and #vectors==336) or
         (kind=='UNDOCKCTL_1' and flags:match('^0[01]$') and #vectors==192)),'invalid native dock controller state')
      parts[3]='dockctl='..values
     else
      for _,field in ipairs({';current=',';previous='}) do
       local value=assert(raw_field(raw,field,'^[0-9a-f]+'),'navigation state missing')
       assert(#value==192,'navigation state size')
       -- Six xyz vectors: position, three basis vectors, linear/angular velocity.
       -- Skip each vector's fourth storage word, which is not a coordinate.
       parts[#parts+1]=(value:gsub(VECTORS,'%1:%2:%3:%4:%5:%6'))
      end
     end
     if cfg.native_gate_strict_physics then
      parts[#parts+1]='attention='..assert(raw:match('^attention=(%d+);'),'native attention missing')
      if not dock and cfg.native_gate_steering_schema==2 then
       local at=raw:find(';steering=',1,true)
       -- slot: a stock slot's ship (lockstep_pilot_slots.h), generation and the Agent's model class.
       local steering=at and (raw:match('^%d+:[0-9a-f]+:[0-9a-f]+',at+10) or raw:match('^none:%d+',at+10) or raw:match('^spline:%d+',at+10) or
        raw:match('^slot:%d+:[0-9a-f]+',at+10))
       parts[#parts+1]='steering='..assert(steering,'native steering lifetime missing or unsupported')
      end
      if not dock and cfg.native_gate_spline_state then
       local values=assert(raw_field(raw,';spline_state=','^[^;]+'),'native spline state missing')
       assert(values=='none' or (#values==32 and values:match('^[0-9a-f]+$')),'invalid native spline state')
       parts[#parts+1]='spline='..values
      end
     end
     native=table.concat(parts,':')
    end
    if profile then profile.part('actor',since) end
    if trace and cfg.native_gate_flight_diag then
     log('FLIGHT id='..i..' '..tostring(gate.flight(i)))
     for phase=0,1 do log('AVOIDANCE id='..i..' '..assert(gate.avoidance(i,phase))) end
    end
    if trace and cfg.native_gate_ai_diag then log('AI id='..i..' '..tostring(gate.ai(i))) end
    if trace and cfg.native_gate_agent_diag then log('AGENT id='..i..' '..tostring(gate.agent(i))) end
    if trace and cfg.native_gate_spline_diag then log('SPLINE id='..i..' '..assert(gate.spline(i))) end
    if trace and cfg.native_gate_ship_diag then log('SHIP id='..i..' '..tostring(gate.ship(i))) end
    if trace and cfg.native_gate_attention_diag then log('ATTENTION id='..i..' '..assert(gate.attention(i))) end
    if trace and cfg.native_gate_nav_diag then log('NAV id='..i..' '..tostring(gate.nav(i))) end
    if trace and cfg.native_gate_params_diag then
     for ordinal=1,assert(gate.params(i,0)) do log('PARAMS id='..i..' ordinal='..ordinal..' '..tostring(gate.params(i,ordinal))) end
    end
    if trace and cfg.native_gate_velocity_diag then
     for ordinal=1,assert(gate.velocity(i,0)) do log('VELOCITY id='..i..' ordinal='..ordinal..' '..tostring(gate.velocity(i,ordinal))) end
    end
    if trace and cfg.native_gate_body_diag then
     for which=0,1 do log('BODY id='..i..' which='..which..' tick='..tick..' '..body(which)) end
    end
    if trace and cfg.native_gate_shape_diag then
     for which=0,1 do log('SHAPE id='..i..' which='..which..' '..assert(gate.shape(i,which),'physics shape trace unavailable')) end
    end
    if cfg.native_gate_weapon_diag then
     if not weapons[i] then
      weapons[i]={}
      local n=tonumber(ffi.C.GetNumUpgradeSlots(object,'','weapon'))
      assert(n>=1 and n<=32,'weapon slot bounds')
      for slot=1,n do
       local id=ffi.C.GetUpgradeSlotCurrentComponent(object,'weapon',slot)
       if id~=0 then
        assert(ffi.C.IsComponentOperational(id),'fixture weapon unavailable')
        assert(gate.register_weapon(tonumber(id),i,slot)==1,'fixture weapon registration refused')
        weapons[i][#weapons[i]+1]=slot
        local luaid=ConvertStringToLuaID((tostring(id):gsub('ULL$','')))
        log('WEAPON_REGISTER id='..i..' slot='..slot..' macro='..tostring(GetComponentData(luaid,'macro')))
       end
      end
      assert(#weapons[i]>0,'fixture has no registered guns')
     end
     if trace then
      for _,slot in ipairs(weapons[i]) do log('WEAPON id='..i..' slot='..slot..' '..tostring(gate.weapon(i,slot))) end
     end
    end
   end
   local since=profile and gate.qpc_us()
   local p=ffi.C.GetObjectPositionInSector(object)
   local hull,shield=GetComponentData(object,'hullpercent','shieldpercent')
   assert(type(hull)=='number' and type(shield)=='number','native health sample missing')
   if profile then profile.part('health',since);since=gate.qpc_us() end
   -- The row's parts are concatenated once (a 1.3 KB row is not copied per part).
   row={string.format('%d/%.9g/%.9g/%.9g/%.9g/%.9g/%.9g/%.17g/%.17g',i,p.x,p.y,p.z,p.yaw,p.pitch,p.roll,hull,shield)}
   if native then row[2]='/raw:';row[3]=native end
   if cfg.native_gate_order_state then
    row[#row+1]='/orders:';row[#row+1]=assert(X4LAN_NATIVE_ORDERS,'native order reader missing')(object)
    if profile then profile.part('orders',since);since=gate.qpc_us() end
   end
   if cfg.native_gate_fleet_state then
    row[#row+1]='/fleet:';row[#row+1]=assert(X4LAN_NATIVE_FLEET,'native fleet reader missing')(object,actors)
    if profile then profile.part('fleet',since) end
   end
  else
   if trace and cfg.native_gate_body_diag then
    for which=0,1 do log('DEADBODY id='..i..' which='..which..' tick='..tick..' '..body(which)) end
   end
   row={i,'/dead'}
  end
  if cfg.native_gate_strict_physics then
   local since=profile and gate.qpc_us()
   for which=0,1 do row[#row+1]='/body'..which..':';row[#row+1]=physics_bits(body(which)) end
   if profile then profile.part('bodies',since) end
  end
  if dockships then
   row[#row+1]='/dock:';row[#row+1]=dockships[i]
   if i<=2 then row[#row+1]='/anchor:';row[#row+1]=dockanchors[i] end
  end
  rows[#rows+1]=table.concat(row)
 end
 local texts={cfg.native_gate_docking_state and 'BATTLE_SUBSET_8' or cfg.native_gate_fleet_state and 'BATTLE_SUBSET_7' or cfg.native_gate_spline_state and 'BATTLE_SUBSET_6' or cfg.native_gate_order_state and 'BATTLE_SUBSET_5' or cfg.native_gate_strict_physics and (cfg.native_gate_steering_schema==2 and 'BATTLE_SUBSET_4' or 'BATTLE_SUBSET_3') or cfg.native_gate_strict_state and 'BATTLE_SUBSET_2' or 'BATTLE_SUBSET_1'};for _,r in ipairs(rows) do texts[#texts+1]=r end
 local data=table.concat(texts,'|')
 if cfg.native_gate_strict_physics then
  -- X4 crops a DebugError line at 8 KiB. Keep every canonical byte available
  -- to the independent validator instead of logging a truncated whole frame.
  ticklog.lines(function()
   local lines={}
   for i,row in ipairs(rows) do lines[i]='STATE_ROW tick='..tick..' id='..i..' data='..row end
   lines[#lines+1]='STATE_END tick='..tick..' entities='..#rows..' scope='..texts[1]
   return lines
  end)
 else
  log('STATE tick='..tick..' entities='..#rows..' data='..data)
 end
 return data
end
local function frame()
 if done then return end
 assert(not command_error,'native tactical command refused')
 if not snapshot and not owner then
  local ok,result,why=pcall(fixed.start,'fixed60')
  if not ok or not result then log('REFUSED fixed='..tostring(why or result));done=true;return end
  owner=result;log('FIXED60 armed')
 end
 if not armed then
  if not snapshot and ffi.C.GetCurrentGameTime()<(cfg.native_gate_start_time or 20) then return end
  local data=snapshot and ('SNAPSHOT_BYTES_1|'..snapshot) or sample(0)
  if cfg.native_gate_snapshot_fixture then data=data..'|SNAPSHOT_FIXTURE_1|'..table.concat(cfg.native_gate_snapshot_fixture,',') end
  if cfg.native_gate_player_probe=='agreed-getup' or diagnostic then
   assert(cfg.native_gate_world_hash_stride==1 and cfg.native_gate_player_transition_observer and
    cfg.native_gate_player_origin_token=='1|0000000000011e12|00000000000ae79c|1|120' and
    not cfg.native_gate_command_test and not cfg.native_gate_management_test and not cfg.native_gate_construction_test,'GetUp exclusive strict fixture configuration')
   data=data..(diagnostic and '|PLAYER_GETUP_LOCAL_PLAN_1:diagnostic-getup-1:1:' or '|PLAYER_GETUP_PLAN_1:')..X4LAN_SHA256.digest(cfg.token)..':1:30:120:0000000000011e12:00000000000ae79c'
  end
  if cfg.native_gate_player_transition_queue_evidence~=nil then
   assert(cfg.native_gate_player_transition_queue_evidence==true and diagnostic and cfg.native_gate_player_transition_observer==true and
    cfg.native_gate_player_transition_manifest=='2|00000000000ae79c/p/ship_arg_s_scout_01_a_macro/player' and
    cfg.native_gate_player_transition_last_tick==120,'queue evidence requires exact local GetUp plan')
   data=data..'|PLAYER_TRANSITION_QUEUE_DIAG_1'
  end
  if cfg.native_gate_player_transition_prebind_receipt~=nil then
   assert(cfg.native_gate_player_transition_prebind_receipt==true and cfg.native_gate_player_transition_queue_evidence==true and diagnostic,'prebind receipt requires exact local queue evidence')
   data=data..'|PLAYER_TRANSITION_PREBIND_1'
  end
  if cfg.native_gate_creation_rng_schema or cfg.native_gate_creation_rng_manifest then
   data=assert(X4LAN_NATIVE_CREATION_RNG,'construction RNG module missing').preimage(cfg,data)
  end
  local digest=X4LAN_SHA256.digest(data)
  -- Per-tick digest: gate.sha256 (BCrypt, ~7 us) when the gate has it, else X4LAN_SHA256 (pure Lua,
  -- ~4 ms per play config tick without the JIT). Both are SHA-256: the native one must reproduce this digest.
  if gate.sha256 then assert(gate.sha256(data)==digest,'native SHA-256 differs from X4LAN_SHA256') end
  local options=(cfg.native_gate_serial_pathing and 1 or 0)+(cfg.native_gate_seed_rng and 2 or 0)
    +(cfg.native_gate_flight_diag and 4 or 0)+(cfg.native_gate_flight_rng and 8 or 0)+(cfg.native_gate_flight_full and 16 or 0)
    +(cfg.native_gate_ai_diag and 32 or 0)+(cfg.native_gate_ai_rng and 64 or 0)
    +(cfg.native_gate_agent_diag and 128 or 0)+(cfg.native_gate_agent_rng and 256 or 0)+(cfg.native_gate_agent_full and 512 or 0)
    +(cfg.native_gate_ship_diag and 1024 or 0)+(cfg.native_gate_ship_rng and 2048 or 0)
    +(cfg.native_gate_params_init and 4096 or 0)
    +(cfg.native_gate_weapon_diag and 8192 or 0)+(cfg.native_gate_weapon_rng and 16384 or 0)
    +(cfg.native_gate_params_diag and 32768 or 0)
    +(cfg.native_gate_nav_diag and 65536 or 0)+(cfg.native_gate_nav_rng and 131072 or 0)+(cfg.native_gate_nav_initial_refresh and 262144 or 0)
    +(cfg.native_gate_velocity_diag and 524288 or 0)
    +(cfg.native_gate_shape_diag and 1048576 or 0)+(cfg.native_gate_shape_rng and 2097152 or 0)+(cfg.native_gate_shape_initial_refresh and 4194304 or 0)
    +(cfg.native_gate_bullet_diag and 8388608 or 0)+(cfg.native_gate_bullet_rng and 16777216 or 0)
    +(cfg.native_gate_death_diag and 33554432 or 0)+(cfg.native_gate_death_rng and 67108864 or 0)
    +(cfg.native_gate_mass_diag and 134217728 or 0)
    +(cfg.native_gate_attention_diag and 268435456 or 0)+(cfg.native_gate_attention_rng and 536870912 or 0)+(cfg.native_gate_attention_initial_refresh and 1073741824 or 0)
    +(cfg.native_gate_avoidance_refresh and 2147483648 or 0)
    +(cfg.native_gate_steering_init and 4294967296 or 0)
    +(cfg.native_gate_spline_diag and 8589934592 or 0)
    +(cfg.native_gate_spline_refresh and 17179869184 or 0)
    +(cfg.native_gate_spline_join and 34359738368 or 0)
    +(cfg.native_gate_radar_diag and 68719476736 or 0)
    +(cfg.native_gate_radar_serial and 137438953472 or 0)
    +(cfg.native_gate_radar_rng and 274877906944 or 0)
    +(cfg.native_gate_radar_initial_refresh and 549755813888 or 0)
    +(cfg.native_gate_damage_diag and 1099511627776 or 0)
    +(cfg.native_gate_interrupt_diag and 2199023255552 or 0)
    +(cfg.native_gate_interrupt_rng and 4398046511104 or 0)
    +(cfg.native_gate_contact_diag and 8796093022208 or 0)
    +(cfg.native_gate_docking_shape_rng and 17592186044416 or 0)
    +(cfg.native_gate_docking_shape_refresh and 35184372088832 or 0)
    +(cfg.native_gate_world_identities and 70368744177664 or 0)
    +(cfg.native_gate_player_solver_observer and 1125899906842624 or 0)
    +(cfg.native_gate_npc_rng and 140737488355328 or 0)
    +(cfg.native_gate_npc_diag and 281474976710656 or 0)
    +(cfg.native_gate_npc_deadline and 562949953421312 or 0)
    +(cfg.native_gate_registry_md_trace and 2251799813685248 or 0)
    +(cfg.native_gate_player_request_observer and 4503599627370496 or 0)
  if diagnostic then assert(options==70377334178981 and snapshot and cfg.native_gate_world_trace_early and cfg.native_gate_world_queue_trace,'diagnostic passive observer contract') end
  -- Before start/boot; a reloaded UI repeats the identical configuration.
  if wire3 and not (gate.wire and gate.wire(3,clients)==1) then log('REFUSED wire=3 clients='..clients);done=true;return end
  -- Protocol 4 window; a DLL without gate.lookahead refuses instead of running protocol 3.
  local lookahead=cfg.native_gate_lookahead or 0
  if lookahead>0 and not (gate.lookahead and gate.lookahead(lookahead)==1) then log('REFUSED lookahead='..lookahead);done=true;return end
  -- Opt-in remote relay (dotted IPv4); default loopback. Same token/HMAC rules.
  if cfg.native_gate_relay_host and not (gate.relay_host and gate.relay_host(cfg.native_gate_relay_host)==1) then
   log('REFUSED relay_host='..tostring(cfg.native_gate_relay_host));done=true;return
  end
  -- native_gate_tick_hz (lockstep_tick.h, default 60): dt = 1/hz on every client, held to real
  -- time unless native_gate_tick_pace = false. The relay needs the same --tick-hz.
  -- native_gate_play_mode (lockstep_gate.c): no observer sample tables, no row drains, no world pages.
  -- The keys, the per-tick digest and the world capture it folds in stay as they are.
  if cfg.native_gate_play_mode then
   if not (gate.play_mode and gate.play_mode()==1) then log('REFUSED play_mode');done=true;return end
   log('PLAY_MODE_ARMED')
  end
  if cfg.native_gate_npc_lifecycle and not (gate.npc_action_rows and gate.npc_action_rows()==1) then log('REFUSED npc_action_rows');done=true;return end
  -- native_gate_fast_reads (lockstep_gate.c read_local): small reads by a guarded copy.
  if cfg.native_gate_fast_reads then
   if not (gate.fast_reads and gate.fast_reads()==1) then log('REFUSED fast_reads');done=true;return end
   log('FAST_READS_ARMED')
  end
  if cfg.native_gate_tick_hz~=nil then
   local pace=cfg.native_gate_tick_pace~=false
   if not (gate.tick_rate and gate.tick_rate(cfg.native_gate_tick_hz,pace and 1 or 0)==1) then
    log('REFUSED tick_hz='..tostring(cfg.native_gate_tick_hz));done=true;return
   end
   log('TICK_RATE_ARMED hz='..tostring(cfg.native_gate_tick_hz)..' pace='..tostring(pace))
  end
  local start=snapshot and gate.boot or gate.start
  local code=start(tonumber(cfg.slot:sub(2)),cfg.port,cfg.token,60000,digest,options)
  if code~=0 then log('REFUSED code='..code..(gate.verify_stage and ' stage='..tostring(gate.verify_stage()) or ''));done=true;return end
  native_owned=true
  if cfg.native_gate_registry_md_trace then
   local capability=gate.poll();assert(capability.registry_md_installed==1 and capability.registry_md_schema==2,'native MD observer schema2 not installed')
   assert(cfg.native_gate_registry_origin_schema==1 and capability.registry_origin_schema==1 and capability.registry_origin_enabled==1,'native registry origin observer not enabled')
   assert(cfg.native_gate_registry_origin_identity_schema==1 and capability.registry_origin_identity_schema==1,'native registry origin identity schema unsupported')
  end
  if cfg.native_gate_construction_test then
   assert(type(gate.ship_raw_motion)=='function','construction requires passive raw motion')
   assert(assert(X4LAN_NATIVE_CONSTRUCTION,'native construction adapter missing').bind_gate,'native construction enrollment adapter missing')(gate,enroll_dynamic_weapons)
  end
  if cfg.native_gate_creation_trace_schema then
   creation_trace=assert(X4LAN_NATIVE_CREATION_TRACE,'creation trace module missing').new(gate,cfg,log)
  end
  local registry_activate,rng_activate
  if cfg.native_gate_registry_creation_schema then
   registry_creation,registry_activate=assert(X4LAN_NATIVE_REGISTRY_CREATION,'provisional registry module missing').new(gate,cfg,data,log)
  end
  if cfg.native_gate_creation_rng_schema then
   creation_rng,rng_activate=assert(X4LAN_NATIVE_CREATION_RNG).new(gate,cfg,data,log)
  end
  -- Both protected drains exist before either configuration can refuse. Native
  -- installation/attachment still occurs in registry then RNG order.
  if registry_activate then registry_activate() end
  if rng_activate then rng_activate() end
  if cfg.native_gate_timer_resolution then
   local ms=cfg.native_gate_timer_resolution
   assert(type(ms)=='number' and ms>=1 and ms<=15 and ms==math.floor(ms) and gate.timer_resolution and gate.timer_resolution(ms)==1,'timer resolution refused')
   log('TIMER_RESOLUTION_ARMED ms='..ms)
  end
  -- The 26 transcendental CRT math entries and the 3 compiler sin/cos helpers to
  -- the portable library (src/native/portable_math): the same bits on every CPU,
  -- no FMA3/SSE2 dispatch. Installed before the snapshot load. Default off.
  if cfg.native_gate_portable_math then
   assert(cfg.native_gate_portable_math==true and gate.portable_math and gate.portable_math_status and gate.portable_math()==1,
    'portable math refused '..tostring(gate.portable_math_status and gate.portable_math_status()))
   log('PORTABLE_MATH_ARMED '..tostring(gate.portable_math_status()))
  end
  -- RCPSS/RSQRTSS in X4.exe to exactly rounded division and square root (1,262
  -- instructions, 819 windows): vendor-independent results. Before the load. Default off.
  if cfg.native_gate_exact_reciprocal then
   assert(cfg.native_gate_exact_reciprocal==true and gate.exact_reciprocal and gate.exact_reciprocal_status and gate.exact_reciprocal()==1,
    'exact reciprocal refused '..tostring(gate.exact_reciprocal_status and gate.exact_reciprocal_status()))
   log('EXACT_RECIPROCAL_ARMED '..tostring(gate.exact_reciprocal_status()))
  end
  -- MXCSR: 'default' runs every X4 thread with 0x1f80 (no flush-to-zero on the main
  -- thread, as on Linux; repaired every tick), 'observe' only counts. Default off.
  if cfg.native_gate_fp_env then
   local mode=cfg.native_gate_fp_env
   assert((mode=='default' or mode=='observe') and gate.fp_env and gate.fp_env_status and gate.fp_env(mode)==1,
    'fp env refused '..tostring(gate.fp_env_status and gate.fp_env_status()))
   log('FP_ENV_ARMED '..tostring(gate.fp_env_status()))
  end
  -- The saved-value readers (0x14c3090/0x14c2bd0) to the portable decimal reader:
  -- same syntax, correctly rounded, the bits the Linux preload gives. Before the load.
  if cfg.native_gate_decimal then
   assert(cfg.native_gate_decimal==true and gate.decimal and gate.decimal_status and gate.decimal()==1,
    'decimal refused '..tostring(gate.decimal_status and gate.decimal_status()))
   log('DECIMAL_ARMED '..tostring(gate.decimal_status()))
  end
  if cfg.native_gate_boot_rng_anchor then
   assert(snapshot and type(cfg.native_gate_boot_rng_anchor)=='string' and cfg.native_gate_boot_rng_anchor:match('^[0-9a-f]+$') and
    #cfg.native_gate_boot_rng_anchor==16 and gate.boot_rng_anchor and gate.boot_rng_anchor(cfg.native_gate_boot_rng_anchor,cfg.native_gate_boot_rng_anchor_mask or 7)==1,'boot RNG anchor refused')
   log('BOOT_RNG_ANCHOR_ARMED seed='..cfg.native_gate_boot_rng_anchor)
  end
  -- Bit 1 stamps every batched NavigationController once per frame after the
  -- serial pathing join; bit 2 lifts the pathing QPC budget. Default off.
  if cfg.native_gate_path_determinism then
   local mask=cfg.native_gate_path_determinism
   assert(snapshot and type(mask)=='number' and mask>=1 and mask<=3 and mask==math.floor(mask) and
    (mask%2==0 or cfg.native_gate_serial_pathing) and gate.path_determinism and gate.path_determinism(mask)==1,'path determinism refused')
   log('PATH_DETERMINISM_ARMED mask='..mask)
  end
  -- native_gate_path_budget = N (lockstep_path_budget.h): stock's round robin by count: N full
  -- agent updates per batch and pass, the rest cheap, the clock out of the decision.
  if cfg.native_gate_path_budget then
   local n=cfg.native_gate_path_budget
   assert(snapshot and type(n)=='number' and n%1==0 and n>=1 and cfg.native_gate_serial_pathing and
    gate.path_budget and gate.path_budget(n)==1,'path budget refused '..tostring(gate.path_budget_status and gate.path_budget_status()))
   log('PATH_BUDGET_ARMED n='..n)
  end
  -- Runs the pathing batch jobs inline, in batch order, on the pathing thread so
  -- cross-agent reads (formation travel flags) see one fixed order. Default off.
  if cfg.native_gate_travel_determinism then
   local mask=cfg.native_gate_path_determinism
   assert(snapshot and cfg.native_gate_travel_determinism==true and cfg.native_gate_serial_pathing and
    type(mask)=='number' and (mask==2 or mask==3) and gate.travel_determinism and gate.travel_determinism()==1,'travel determinism refused')
   log('TRAVEL_DETERMINISM_ARMED')
  end
  -- Parallel alternative: wingmen read their leader's pass-start travel flag
  -- from a per-pass snapshot; batch jobs stay on JobThreads. Default off.
  if cfg.native_gate_travel_snapshot then
   local mask=cfg.native_gate_path_determinism
   assert(snapshot and cfg.native_gate_travel_snapshot==true and not cfg.native_gate_travel_determinism and cfg.native_gate_serial_pathing and
    type(mask)=='number' and (mask==2 or mask==3) and gate.travel_snapshot and gate.travel_snapshot()==1,'travel snapshot refused')
   log('TRAVEL_SNAPSHOT_ARMED')
  end
  -- Virtual players, step 1 (reports/LOCKSTEP-VIRTUAL-PLAYERS-RE-20261029.txt):
  -- 'observe' reads the canonical player's block of X4's own control path (input
  -- bank, controller environment, coefficients, triggers, assist/activity/aim
  -- fields) once per admitted frame before the serial pathing pass, counts its
  -- changes per class, checks the read-only tables and takes one census of the
  -- input handlers. No game write. Default off. 'construct' (step 3 spike, stop
  -- reason 160) also builds one second U::Player with X4's own factory at the
  -- admitted tick native_gate_virtual_players_construct_tick (default 20),
  -- unparented and unseated, and puts the canonical player back.
  if cfg.native_gate_virtual_players then
   local mode,due=cfg.native_gate_virtual_players,cfg.native_gate_virtual_players_construct_tick or 20
   assert(snapshot and (mode=='observe' or (mode=='construct' and type(due)=='number' and due%1==0 and due>=2 and due<=18000)) and
    (mode=='construct' or cfg.native_gate_virtual_players_construct_tick==nil) and cfg.native_gate_serial_pathing and gate.virtual_players and
    gate.virtual_players_status and gate.virtual_players(mode,due)==1,'virtual players refused')
   log('VPLAYER_ARMED mode='..mode..(mode=='construct' and ' tick='..due or ''))
  end
  -- Stock slots 2..N (lockstep_pilot_slots.h) arm much further down, after modules that patch X4's
  -- code at arming (boot hold inside UniverseFrameUpdate, stock-J1). Their pins are checked here,
  -- where 'observe' and 'construct' check theirs, and are cached for that later arming.
  if cfg.native_gate_pilot_stock_slots~=nil then
   assert(gate.pilot_stock_pins,'pilot stock refused: gate without pilot_stock_pins')
   local ok,why=gate.pilot_stock_pins()
   assert(ok==1,'pilot stock refused: '..tostring(why))
   log('PILOT_STOCK_PINS ok=1')
  end
  if cfg.native_gate_ui_replay~=nil and cfg.native_gate_ui_replay~=false then
   assert(gate.ui_replay_pins,'ui replay refused: gate without ui_replay_pins')
   local ok,why=gate.ui_replay_pins()
   assert(ok==1,'ui replay refused: '..tostring(why))
   log('UI_REPLAY_PINS ok=1')
  end
  -- Render RNG split: VBI slot 10 (GameRender::Move, the camera render) runs on
  -- a private thread-RNG word, so the view no longer reaches the simulation
  -- stream (slot 11 JobEngine onwards). 'observe' keeps the stream and measures
  -- slot 10's draws on it. UniverseIDs are not split. Default off.
  if cfg.native_gate_render_rng_split then
   local mode=cfg.native_gate_render_rng_split==true and 'split' or cfg.native_gate_render_rng_split
   assert((mode=='split' or mode=='observe') and gate.render_rng_split and gate.render_rng_split_status and
    gate.render_rng_split(mode)==1,'render RNG split refused')
   log('RENDER_RNG_SPLIT_ARMED mode='..mode)
  end
  -- Bullet hit-test cadence: 'every' (or true) tests every bullet every frame,
  -- independent of the camera distance, the view cache and the thread RNG (the
  -- native 0.1-0.2 s cadence beyond 5 km from the camera); 'observe' only counts
  -- per-frame tests. Counts come from the bullet Move wrapper, so the bullet
  -- module (native_gate_bullet_rng or native_gate_bullet_diag) is required. Default off.
  if cfg.native_gate_bullet_cadence then
   local mode=cfg.native_gate_bullet_cadence==true and 'every' or cfg.native_gate_bullet_cadence
   assert((mode=='every' or mode=='observe') and snapshot and (cfg.native_gate_bullet_rng or cfg.native_gate_bullet_diag) and
    gate.bullet_cadence and gate.bullet_cadence_status and gate.bullet_cadence(mode)==1,'bullet cadence refused')
   log('BULLET_CADENCE_ARMED mode='..mode)
  end
  -- Navigation refresh cadence (NavigationController::vfunc33): the parameter
  -- and obstacle-probe refresh intervals no longer depend on the camera distance.
  -- 'far' (or true) always takes the 1-2 s interval (one draw, keyed for fixtures
  -- by native_gate_nav_rng), 'every' refreshes every frame without a draw,
  -- 'observe' only counts. Counts come from the nav Move wrapper, so the nav
  -- module (native_gate_nav_rng, _nav_diag or _nav_initial_refresh) is required. Default off.
  if cfg.native_gate_nav_cadence then
   local mode=cfg.native_gate_nav_cadence==true and 'far' or cfg.native_gate_nav_cadence
   assert((mode=='far' or mode=='every' or mode=='observe') and snapshot and
    (gate.platform and gate.platform()=='linux' or cfg.native_gate_nav_rng or cfg.native_gate_nav_diag or cfg.native_gate_nav_initial_refresh) and
    gate.nav_cadence and gate.nav_cadence_status and gate.nav_cadence(mode)==1,'nav cadence refused')
   -- Watched ships get NAV_CADENCE_ROW lines too: the world-hash watch list plus native_gate_nav_cadence_watch.
   local watched=0
   for _,list in ipairs({cfg.native_gate_world_hash_watch or {},cfg.native_gate_nav_cadence_watch or {}}) do
    for _,id in ipairs(list) do
     assert(type(id)=='string' and #id==16 and id:match('^[0-9a-f]+$') and gate.nav_cadence_watch and gate.nav_cadence_watch(id)==1,'nav cadence watch refused '..tostring(id))
     watched=watched+1
    end
   end
   log('NAV_CADENCE_ARMED mode='..mode..' watched='..watched)
   -- Obstacle-probe deadlines: pinned to "due now" while avoidance is off and
   -- reset once per portable ID when it is on at the first call, so loading-time
   -- deadlines no longer decide when a ship's first probe runs. Default off.
   if cfg.native_gate_nav_probe_init then
    assert(cfg.native_gate_nav_probe_init==true and gate.nav_probe_init and gate.nav_probe_init()==1,'nav probe init refused')
    log('NAV_PROBE_INIT_ARMED')
   end
   -- Parameter refresh deadlines (ctrl+0xd0), the same way: pinned to "due now"
   -- while the parameter block is off and reset once per portable ID when it is
   -- on at the first call, for every controller the cadence brackets (with
   -- native_gate_nav_player the player's too). Default off.
   if cfg.native_gate_nav_params_init then
    assert(cfg.native_gate_nav_params_init==true and gate.nav_params_init and gate.nav_params_init()==1,'nav params init refused')
    log('NAV_PARAMS_INIT_ARMED')
   end
   -- The Linux gate: the refresh's own draws keyed per ship (tick, portable ID, ordinal, time), inside the
   -- cadence hook; Windows keys them in actor_rng_all's ship scope instead. Default off.
   if cfg.native_gate_nav_key then
    assert(cfg.native_gate_nav_key==true and cfg.native_gate_world_identities and gate.platform and gate.platform()=='linux' and
     gate.nav_key and gate.nav_key()==1,'nav key refused')
    log('NAV_KEY_ARMED')
   end
   -- The player's own controller: PlayerController::vfunc33 runs the nav upkeep
   -- by a direct call, outside the nav wrapper, so the probe init, the counts and
   -- the watch rows never saw it. This brackets it the same way and logs its own
   -- NAV_CADENCE_ROW lines (id=0, saved = the player ship's key). Default off.
   if cfg.native_gate_nav_player then
    assert(cfg.native_gate_nav_player==true and not cfg.native_gate_player_observer and gate.nav_player and gate.nav_player_status and
     gate.nav_player()==1,'nav player refused')
    log('NAV_PLAYER_ARMED')
   end
  end
  assert(not cfg.native_gate_nav_probe_init or cfg.native_gate_nav_cadence,'nav probe init needs native_gate_nav_cadence')
  assert(not cfg.native_gate_nav_player or cfg.native_gate_nav_cadence,'nav player needs native_gate_nav_cadence')
  assert(not cfg.native_gate_nav_params_init or cfg.native_gate_nav_cadence,'nav params init needs native_gate_nav_cadence')
  -- Event commands the pathing batch jobs append in one pass (completion order):
  -- 'observe' reads them per source after the serial join and logs counts,
  -- hashes and one COMMAND_ORDER_ROW per source with several commands; 'sort'
  -- also puts each such source's inserts in (time, event type) order. Default off.
  if cfg.native_gate_command_order then
   local mode=cfg.native_gate_command_order
   assert((mode=='observe' or mode=='sort') and snapshot and cfg.native_gate_serial_pathing and
    (mode=='observe' or cfg.native_gate_travel_snapshot) and gate.command_order and gate.command_order_status and
    gate.command_order(mode)==1,'command order refused')
   log('COMMAND_ORDER_ARMED mode='..mode)
   -- A row for every source of the segment with each command's kind, queue
   -- position and the portable keys among its event's first qwords. Default off.
   if cfg.native_gate_command_order_rows then
    assert(cfg.native_gate_command_order_rows==true and gate.command_order_rows and gate.command_order_rows()==1,'command order rows refused')
    log('COMMAND_ORDER_ROWS_ARMED')
   end
  end
  assert(not cfg.native_gate_command_order_rows or cfg.native_gate_command_order,'command order rows need native_gate_command_order')
  -- Ship sound starts (ship vtable slot 1143) on a private RNG word: their
  -- callers decide per client (local input, camera context). 'split' (or true)
  -- consumes no simulation draw, 'observe' only counts the draws. Default off.
  if cfg.native_gate_ship_sound_rng then
   local mode=cfg.native_gate_ship_sound_rng==true and 'split' or cfg.native_gate_ship_sound_rng
   assert((mode=='split' or mode=='observe') and snapshot and gate.ship_sound_rng and gate.ship_sound_rng_status and
    gate.ship_sound_rng(mode)==1,'ship sound RNG refused')
   log('SHIP_SOUND_RNG_ARMED mode='..mode)
  end
  -- The player's fleet rebuild timer (U::Player+0x8a538): its first deadline is
  -- an unkeyed loading-time draw, so the first check and its re-arm draw (in
  -- the player's ship scope) came on another tick per client. At the first
  -- admitted call per Player it is set to the frame's time + 15 s. Default off.
  if cfg.native_gate_player_timer then
   assert(cfg.native_gate_player_timer==true and snapshot and gate.player_timer and gate.player_timer_status and
    gate.player_timer()==1,'player timer refused')
   log('PLAYER_TIMER_ARMED')
  end
  -- Warps (WarpEvent::Dispatch) on a word keyed by (tick, object key, its warp
  -- ordinal in the tick): the entry point's push direction is drawn from the
  -- thread word, which other consumers of the same event pass leave per
  -- client. 'split' (or true) keys it, 'observe' keeps the native word; both log
  -- one WARP_RNG_ROW per warp. Needs the world identities. Default off.
  if cfg.native_gate_warp_rng then
   local mode=cfg.native_gate_warp_rng==true and 'split' or cfg.native_gate_warp_rng
   assert((mode=='split' or mode=='observe') and snapshot and cfg.native_gate_world_identities and gate.warp_rng and
    gate.warp_rng_status and gate.warp_rng_row and gate.warp_rng(mode)==1,'warp RNG refused')
   log('WARP_RNG_ARMED mode='..mode)
  end
  -- Read-only: one TRANSITION_ROW per TransitionEvent delivered to a gate
  -- (highway entries): gate and ship keys, the event's post time, the delivery
  -- time. Needs the world identities. Default off.
  if cfg.native_gate_transition_rows then
   assert(cfg.native_gate_transition_rows==true and snapshot and cfg.native_gate_world_identities and gate.transition_rows and
    gate.transition_rows_status and gate.transition_row and gate.transition_rows()==1,'transition rows refused')
   log('TRANSITION_ROWS_ARMED')
  end
  -- Tempzones of the parallel pathing pass: one new zone may be pending, and
  -- natively the first job thread to reach the slot gets it, at its position.
  -- 'order' (or true) parks the slot for each joined pass and grants the
  -- smallest request by (sector key, position) after the join; 'observe' only
  -- logs the native create requests. Rows: TEMPZONE_ROW. Needs the world
  -- identities and serial pathing; 'order' also the travel snapshot (the late
  -- splices drained before the grant posts). Default off.
  if cfg.native_gate_tempzone_order then
   local mode=cfg.native_gate_tempzone_order==true and 'order' or cfg.native_gate_tempzone_order
   assert((mode=='order' or mode=='observe') and snapshot and cfg.native_gate_world_identities and cfg.native_gate_serial_pathing and
    (mode=='observe' or cfg.native_gate_travel_snapshot) and gate.tempzone_order and gate.tempzone_order_status and gate.tempzone_order_row and
    gate.tempzone_order(mode)==1,'tempzone order refused')
   log('TEMPZONE_ORDER_ARMED mode='..mode)
  end
  -- Object attention timer (0x6d1ff0) for every portable object, not only the
  -- fixture actors: the loading-time due is replaced by "due now" at the first
  -- admitted call per portable ID, and every call draws its next due from a
  -- (tick, ID) seed, so the periodic re-evaluation no longer lands on a
  -- client-dependent tick. Needs the attention hook (native_gate_attention_rng
  -- or native_gate_attention_initial_refresh). Default off.
  if cfg.native_gate_attention_all then
   assert(cfg.native_gate_attention_all==true and (cfg.native_gate_attention_rng or cfg.native_gate_attention_initial_refresh) and
    gate.attention_all and gate.attention_all_status and gate.attention_all()==1,'attention all refused')
   log('ATTENTION_ALL_ARMED')
  end
  -- Simulation viewpoints: attention (vtable slot 703 from SetAttention) is
  -- evaluated per viewpoint with an empty camera list and the viewpoint as the
  -- player object; viewpoints are the resolved pilot slot ships, else the local
  -- player object. 'players' (or true) applies it, 'observe' returns the camera
  -- result and counts differences. Rendering keeps the camera. Default off.
  if cfg.native_gate_view_points then
   local mode=cfg.native_gate_view_points==true and 'players' or cfg.native_gate_view_points
   assert((mode=='players' or mode=='observe') and snapshot and gate.view_points and gate.view_points_frame and
    gate.view_points_status and gate.view_points(mode)==1,'view points refused')
   log('VIEW_POINTS_ARMED mode='..mode)
   -- One VIEW_POINTS_ROW whenever an object's attention level changes (diagnostic);
   -- also counts every evaluation by its caller (VIEW_POINTS callers=).
   if cfg.native_gate_view_rows then
    assert(cfg.native_gate_view_rows==true and gate.view_points_rows and gate.view_points_rows()==1,'view rows refused')
    log('VIEW_ROWS_ARMED')
   end
   -- Attention re-evaluations asked for by the camera enter/leave handlers and
   -- by cutscene shots (cockpit monitors, conversations): 'block' keeps the
   -- current level (they are per client), 'observe' counts.
   if cfg.native_gate_view_triggers then
    local trig=cfg.native_gate_view_triggers
    assert((trig=='block' or trig=='observe') and gate.view_points_triggers and gate.view_points_triggers(trig)==1,'view triggers refused')
    log('VIEW_TRIGGERS_ARMED mode='..trig)
   end
   -- Viewpoint without pilot slots: 'ship' takes the container the local
   -- player occupies instead of the player object, whose position follows the
   -- camera; 'player' is the default.
   if cfg.native_gate_view_points_source then
    local src=cfg.native_gate_view_points_source
    assert((src=='player' or src=='ship') and gate.view_points_source and gate.view_points_source(src)==1,'view points source refused')
    log('VIEW_POINTS_SOURCE_ARMED source='..src)
   end
  end
  assert(not cfg.native_gate_view_triggers or cfg.native_gate_view_points,'view triggers need native_gate_view_points')
  assert(not cfg.native_gate_view_points_source or cfg.native_gate_view_points,'view points source needs native_gate_view_points')
  assert(not cfg.native_gate_view_rows or cfg.native_gate_view_points,'view rows need native_gate_view_points')
  -- Mass traffic scheduling (U::MT::MassTrafficUpdateTask): 'sync' (or true)
  -- waits at the gate tick for the pass kicked at the end of the frame (before
  -- the next FrameTick) and seeds the thread's RNG word for each pass, so
  -- Zone::Move never skips its traffic update; 'observe' counts skipped updates
  -- and passes still running, without waiting. Mass traffic stays on. Default off.
  if cfg.native_gate_mass_traffic then
   local mode=cfg.native_gate_mass_traffic==true and 'sync' or cfg.native_gate_mass_traffic
   assert((mode=='sync' or mode=='observe') and snapshot and gate.mass_traffic and gate.mass_traffic_status and
    gate.mass_traffic(mode)==1,'mass traffic refused '..tostring(gate.mass_traffic_status and gate.mass_traffic_status()))
   log('MASS_TRAFFIC_ARMED mode='..mode)
  end
  -- Region manager pass (asteroids and other region objects around the camera,
  -- U::Regions::RegionManager): the same join as mass traffic, 'sync' (or true)
  -- or 'observe'. Default off.
  if cfg.native_gate_region_pass then
   local mode=cfg.native_gate_region_pass==true and 'sync' or cfg.native_gate_region_pass
   assert((mode=='sync' or mode=='observe') and snapshot and gate.region_pass and gate.region_pass_status and
    gate.region_pass(mode)==1,'region pass refused '..tostring(gate.region_pass_status and gate.region_pass_status()))
   log('REGION_PASS_ARMED mode='..mode)
  end
  -- native_gate_pass_stride = N: the synced traffic and region passes only at the end of every
  -- Nth frame. They overlap, so the join costs the longer of the two.
  if cfg.native_gate_pass_stride then
   assert(snapshot and gate.pass_stride and gate.pass_stride(cfg.native_gate_pass_stride)==1,'pass stride refused')
   log('PASS_STRIDE_ARMED n='..cfg.native_gate_pass_stride)
  end
  -- Region pass centre: 'view' gives the region manager a stand-in camera at
  -- the first latched viewpoint (pilot slot 1, else the occupied ship or the
  -- player object), so region content and its temp zones are generated
  -- around the same point on every client. Needs view points and the synced
  -- region pass. Default off.
  if cfg.native_gate_region_camera then
   assert(cfg.native_gate_region_camera=='view' and cfg.native_gate_view_points and
    (cfg.native_gate_region_pass==true or cfg.native_gate_region_pass=='sync') and gate.region_camera and
    gate.region_camera_frame and gate.region_camera_status and gate.region_camera('view')==1,
    'region camera refused '..tostring(gate.region_camera_status and gate.region_camera_status()))
   log('REGION_CAMERA_ARMED mode=view')
  end
  -- The same stand-in camera for region fields (Region::Move slot 10: the
  -- despawn of field objects far from the camera, effect fields near the
  -- player object) and for mass traffic (near-camera tests, the MassTraffic
  -- thread's camera placement, Zone::Move's temp-component cleanup). Fields
  -- need view points and a pass join (mass traffic or region pass, whose
  -- gate-tick join installs the sites); traffic the synced mass traffic pass.
  -- Default off.
  if cfg.native_gate_region_fields then
   assert(cfg.native_gate_region_fields=='view' and cfg.native_gate_view_points and
    (cfg.native_gate_mass_traffic or cfg.native_gate_region_pass) and gate.region_camera and
    gate.region_camera_frame and gate.region_camera_status and gate.region_camera('fields')==1,
    'region fields refused '..tostring(gate.region_camera_status and gate.region_camera_status()))
   log('REGION_FIELDS_ARMED mode=view')
  end
  if cfg.native_gate_mass_traffic_camera then
   assert(cfg.native_gate_mass_traffic_camera=='view' and cfg.native_gate_view_points and
    (cfg.native_gate_mass_traffic==true or cfg.native_gate_mass_traffic=='sync') and gate.region_camera and
    gate.region_camera_frame and gate.region_camera_status and gate.region_camera('traffic')==1,
    'mass traffic camera refused '..tostring(gate.region_camera_status and gate.region_camera_status()))
   log('MASS_TRAFFIC_CAMERA_ARMED mode=view')
  end
  -- The same stand-in for the camera-distance cull of short-lived effects at
  -- EffectObject creation (weapon fire, impacts, effect events). Needs view
  -- points and a pass join. Default off.
  if cfg.native_gate_effect_camera then
   assert(cfg.native_gate_effect_camera=='view' and cfg.native_gate_view_points and
    (cfg.native_gate_mass_traffic or cfg.native_gate_region_pass) and gate.region_camera and
    gate.region_camera_frame and gate.region_camera_status and gate.region_camera('effects')==1,
    'effect camera refused '..tostring(gate.region_camera_status and gate.region_camera_status()))
   log('EFFECT_CAMERA_ARMED mode=view')
  end
  -- The same stand-in for the highway's spline parameter (Highway::vfunc674: the
  -- closest spline point to the player camera; UniverseFrameUpdate: the camera
  -- anchor's ship inside a highway; the frame that pathing targets inside a
  -- highway are expressed in). Needs view points and a pass join. Default off.
  if cfg.native_gate_highway_camera then
   assert(cfg.native_gate_highway_camera=='view' and cfg.native_gate_view_points and
    (cfg.native_gate_mass_traffic or cfg.native_gate_region_pass) and gate.region_camera and
    gate.region_camera_frame and gate.region_camera_status and gate.region_camera('highway')==1,
    'highway camera refused '..tostring(gate.region_camera_status and gate.region_camera_status()))
   log('HIGHWAY_CAMERA_ARMED mode=view')
  end
  -- Physics scenes for two viewpoints: scene A follows slot 1 (the player object), scene B
  -- the second view point (slot 2's ship) through a second stand-in camera, on every client
  -- (Zone/Highway::vfunc943). Needs view points and a pass join. Default off.
  if cfg.native_gate_physics_scenes then
   assert(cfg.native_gate_physics_scenes=='viewpoints' and cfg.native_gate_view_points and
    (cfg.native_gate_mass_traffic or cfg.native_gate_region_pass) and gate.region_camera and
    gate.region_camera_frame and gate.region_camera_status and gate.region_camera('scenes')==1,
    'physics scenes refused '..tostring(gate.region_camera_status and gate.region_camera_status()))
   log('PHYSICS_SCENES_ARMED mode=viewpoints')
  end
  -- Camera context events follow the simulation camera: Object::SetParent posts CameraLeft/
  -- CameraEntered (attention, ship sounds) when the re-parented object holds the player camera's
  -- anchor; the test walks up from view point 1 (slot 1's ship) on every client instead of each
  -- client's own camera. Needs view points and a pass join. Default off.
  if cfg.native_gate_camera_context then
   assert(cfg.native_gate_camera_context=='view' and cfg.native_gate_view_points and
    (cfg.native_gate_mass_traffic or cfg.native_gate_region_pass) and gate.region_camera and
    gate.region_camera_frame and gate.region_camera_status and gate.region_camera('context')==1,
    'camera context refused '..tostring(gate.region_camera_status and gate.region_camera_status()))
   log('CAMERA_CONTEXT_ARMED mode=view')
  end
  -- Physics shape refresh follows the simulation: Body_UpdateShape moves a compound's children to
  -- their parts' poses on a due time the engine takes from the render-side view caches (the player's
  -- distance); 'view' takes it from the simulation and the nearest view point in the body's sector,
  -- and refreshes each object once at the install so due times set while loading do not count.
  -- Needs view points and a pass join. Default off.
  if cfg.native_gate_shape_refresh then
   assert(cfg.native_gate_shape_refresh=='view' and cfg.native_gate_view_points and
    (cfg.native_gate_mass_traffic or cfg.native_gate_region_pass) and gate.region_camera and
    gate.region_camera_frame and gate.region_camera_status and gate.region_camera('shape_refresh')==1,
    'shape refresh refused '..tostring(gate.region_camera_status and gate.region_camera_status()))
   log('SHAPE_REFRESH_ARMED mode=view')
  end
  -- AI mining's region object search (0x7884a0) answers "too near" from the view points latched in the
  -- region's cluster instead of each client's camera, and the Crystal pass respawns a crystal only more
  -- than 3 km from every view point in its sector (mining); XS ships (ShipExtraSmall::vfunc876) keep their
  -- physics bodies within 2 km of any view point in their sector (external view taken as on; in a stock
  -- session the slots' soft targets and external views); the camera probe (diagnostic) counts by thread
  -- the camera reads of the highway scene, HighwayBooster and Zone::vfunc921. Need view points and a pass join.
  -- Default off; stop reasons 166, 167, 168.
  -- highway_speed: an AI ship's tube speed jitter (HighwayFlightControlModel +0x80) from its portable key instead of its
  -- per-process runtime ID; needs the world identities too. Default off; stop reason 130.
  assert(not cfg.native_gate_highway_speed or cfg.native_gate_world_identities,'highway speed needs native_gate_world_identities')
  -- view_scenes: a physics scene for every sector with a view point, anchored like scene A; needs the scenes group. Default off.
  assert(not cfg.native_gate_view_scenes or cfg.native_gate_physics_scenes,'view scenes need native_gate_physics_scenes')
  for _,g in ipairs({{'native_gate_mining_camera','mining','MINING_CAMERA','view'},{'native_gate_xs_camera','xs_detail','XS_CAMERA','view'},
                     {'native_gate_camera_probe','camera_probe','CAMERA_PROBE',true},{'native_gate_highway_speed','highway_speed','HIGHWAY_SPEED','key'},
                     {'native_gate_sound_rng','sound_rng','SOUND_RNG','split'},{'native_gate_view_scenes','view_scenes','VIEW_SCENES',true},
                     {'native_gate_traffic_rows','traffic_rows','TRAFFIC_ROWS',true}}) do
   if cfg[g[1]] then
    assert(cfg[g[1]]==g[4] and cfg.native_gate_view_points and (cfg.native_gate_mass_traffic or cfg.native_gate_region_pass) and gate.region_camera and
     gate.region_camera_frame and gate.region_camera_status and gate.region_camera(g[2])==1,
     g[3]:lower():gsub('_',' ')..' refused '..tostring(gate.region_camera_status and gate.region_camera_status()))
    log(g[3]..'_ARMED mode='..tostring(g[4]))
   end
  end
  -- traffic_views: 'block' no view makes a mass traffic ship real; 'observe' the simulation's traffic pass per view point
  -- counts and writes nothing; 'view' the pass queues and places them, the render pass only draws (lockstep_traffic_rows.h).
  -- The pass needs the traffic rows group, armed with it. Default off; stop reason 130.
  if cfg.native_gate_traffic_views then
   local mode=cfg.native_gate_traffic_views
   local groups=({block={'traffic_views'},observe={'traffic_rows','traffic_pass_observe'},
                  view={'traffic_views','traffic_rows','traffic_pass_view'}})[mode]
   assert(groups and cfg.native_gate_view_points and (cfg.native_gate_mass_traffic or cfg.native_gate_region_pass) and gate.region_camera and
    gate.region_camera_frame and gate.region_camera_status,'traffic views refused')
   for _,g in ipairs(groups) do
    if not (g=='traffic_rows' and cfg.native_gate_traffic_rows) then
     assert(gate.region_camera(g)==1,'traffic views refused '..tostring(gate.region_camera_status()))
    end
   end
   log('TRAFFIC_VIEWS_ARMED mode='..mode)
  end
  -- field_fill: field objects (asteroids) from view point 1 at a fixed point of the frame, not from each client's render
  -- pass and real camera ('view'); 'block' only closes the render pass's insert. Needs the fields group. Default off; stop reason 130.
  if cfg.native_gate_field_fill then
   local mode=cfg.native_gate_field_fill
   assert((mode=='view' or mode=='block') and cfg.native_gate_region_fields and cfg.native_gate_view_points and
    (cfg.native_gate_mass_traffic or cfg.native_gate_region_pass) and gate.region_camera and gate.region_camera_frame and gate.region_camera_status and
    gate.region_camera(mode=='view' and 'field_fill' or 'field_fill_block')==1,
    'field fill refused '..tostring(gate.region_camera_status and gate.region_camera_status()))
   log('FIELD_FILL_ARMED mode='..mode)
  end
  -- Per-viewpoint policy for the traffic and highway groups: each camera read that has its
  -- object at hand (mass traffic tests and lanes, temp-component cleanup, Highway::vfunc674)
  -- answers for the view point in the object's sector (view point 2 where view point 1 is not),
  -- UniverseFrameUpdate's highway parameter alternates view points by tick; asteroid fields stay
  -- on view point 1 (one RegionManager holder). Needs the traffic or highway group. Default off.
  if cfg.native_gate_viewpoint_policy then
   assert(cfg.native_gate_viewpoint_policy=='sector' and cfg.native_gate_view_points and
    (cfg.native_gate_mass_traffic_camera or cfg.native_gate_highway_camera) and gate.region_camera and
    gate.region_camera_frame and gate.region_camera_status and gate.region_camera('policy')==1,
    'viewpoint policy refused '..tostring(gate.region_camera_status and gate.region_camera_status()))
   log('VIEWPOINT_POLICY_ARMED mode=sector')
  end
  -- native_gate_viewpoint_standins = 2..4: how many view points the policy answers for, one per sector in slot order
  -- (2: view points 1 and 2, as without it). Needs the policy. Default 2.
  if cfg.native_gate_viewpoint_standins~=nil then
   local n=cfg.native_gate_viewpoint_standins
   assert(cfg.native_gate_viewpoint_policy and type(n)=='number' and n%1==0 and n>=2 and n<=4 and gate.region_camera_standins and
    gate.region_camera_standins(n)==1,'viewpoint standins refused')
   log('VIEWPOINT_STANDINS_ARMED n='..n)
  end
  -- Policy rows (diagnostic): one POLICY_ROW per policy decision for the first N gated ticks.
  if cfg.native_gate_viewpoint_policy_rows~=nil then
   local ticks=cfg.native_gate_viewpoint_policy_rows
   assert((cfg.native_gate_viewpoint_policy or cfg.native_gate_mining_camera or cfg.native_gate_xs_camera) and type(ticks)=='number' and
    ticks>=1 and ticks<=600 and ticks==math.floor(ticks) and
    gate.region_camera_policy_rows and gate.region_camera_policy_row and gate.region_camera_policy_rows(ticks)==1,'viewpoint policy rows refused')
   log('VIEWPOINT_POLICY_ROWS_ARMED ticks='..ticks)
  end
  -- Highway parameter rows (diagnostic): Highway::vfunc674's camera offset and its spline parameter,
  -- UniverseFrameUpdate's parameter; one HIGHWAY_PARAM_ROW per call for ticks from..from+N-1. The calls
  -- pass through unchanged. Needs view points and a pass join. Default off; stop reason 169.
  if cfg.native_gate_highway_rows~=nil then
   local ticks,from=cfg.native_gate_highway_rows,cfg.native_gate_highway_rows_from or 1
   assert(type(ticks)=='number' and ticks>=1 and ticks<=3600 and ticks==math.floor(ticks) and type(from)=='number' and from>=1 and
    from<=1000000 and from==math.floor(from) and cfg.native_gate_view_points and (cfg.native_gate_mass_traffic or cfg.native_gate_region_pass) and
    gate.region_camera and gate.region_camera_frame and gate.region_camera_status and gate.region_camera_highway_rows and
    gate.region_camera_highway_row and gate.region_camera('highway_rows')==1 and gate.region_camera_highway_rows(ticks,from)==1,
    'highway rows refused '..tostring(gate.region_camera_status and gate.region_camera_status()))
   log('HIGHWAY_ROWS_ARMED ticks='..ticks..' from='..from)
  end
  assert(cfg.native_gate_highway_rows_from==nil or cfg.native_gate_highway_rows~=nil,'highway rows from needs native_gate_highway_rows')
  -- Weapon shots on keyed streams: every shot (U::Weapon / U::Turret slot 998)
  -- draws its time jitter, spread and effects from (tick, weapon portable ID,
  -- shot index, ordinal) instead of whatever the thread's word holds. Needs the
  -- world identities. Default off.
  -- Pathing state of watched ships every admitted frame (diagnostic, read-only):
  -- attention, NavigationController slots and cache, flight control model target.
  if cfg.native_gate_motion_watch then
   assert(type(cfg.native_gate_motion_watch)=='table' and #cfg.native_gate_motion_watch>=1 and gate.motion_watch and
    gate.motion_watch_frame and gate.motion_watch_row and gate.motion_watch_status,'motion watch refused')
   for _,id in ipairs(cfg.native_gate_motion_watch) do
    assert(type(id)=='string' and #id==16 and id:match('^[0-9a-f]+$') and gate.motion_watch(id)==1,'motion watch refused '..tostring(id))
   end
   log('MOTION_WATCH_ARMED watched='..#cfg.native_gate_motion_watch)
   -- The AI move points queued for watched ships and the flight model targets set for them
   -- (diagnostic; patches four calls and four SetTarget vtable entries).
   if cfg.native_gate_motion_watch_orders then
    assert(cfg.native_gate_motion_watch_orders==true and gate.motion_watch_orders and gate.motion_watch_order_row and gate.motion_watch_target_row and gate.motion_watch_event_row and
     gate.motion_watch_refresh_row and
     gate.motion_watch_orders()==1,'motion watch orders refused')
    log('MOTION_ORDERS_ARMED')
   end
  end
  assert(not cfg.native_gate_motion_watch_orders or cfg.native_gate_motion_watch,'motion watch orders need native_gate_motion_watch')
  -- Function coverage (diagnostic): the functions listed in coverage.txt next to the gate binary that run in ticks
  -- 1..N, one int3 per entry until it first runs (Option B evidence). Run it with few other modules. Default off.
  if cfg.native_gate_coverage then
   local ticks=cfg.native_gate_coverage
   assert(type(ticks)=='number' and ticks>=1 and ticks<=6000 and ticks%1==0 and gate.coverage and gate.coverage_frame and gate.coverage_status and
    gate.coverage_row and gate.coverage(ticks)==1,'coverage refused '..tostring(gate.coverage_status and gate.coverage_status()))
   log('COVERAGE_ARMED ticks='..ticks..' '..tostring(gate.coverage_status()))
  end
  -- The Linear flight model's step, entry and exit, for watched ships in ticks 1..N (diagnostic, read-only:
  -- swaps one vtable entry; Option B evidence). native_gate_step_trace_ticks defaults to 3. Default off.
  if cfg.native_gate_step_trace then
   assert(type(cfg.native_gate_step_trace)=='table' and #cfg.native_gate_step_trace>=1 and #cfg.native_gate_step_trace<=4 and
    cfg.native_gate_world_identities and gate.step_trace and gate.step_trace_arm and gate.step_trace_status and gate.step_trace_row,'step trace refused')
   for _,id in ipairs(cfg.native_gate_step_trace) do
    assert(type(id)=='string' and #id==16 and id:match('^[0-9a-f]+$') and gate.step_trace(id)==1,'step trace refused '..tostring(id))
   end
   -- FR8 (native_gate_step_chain = true): the step's pitch chain (asinf, approach, rotation builder calls) recorded
   -- inside the watched steps, one more row a record (part=chain). Default off.
   if cfg.native_gate_step_chain then
    assert(cfg.native_gate_step_chain==true and gate.step_chain and gate.step_chain()==1,'step chain refused')
   end
   local ticks=cfg.native_gate_step_trace_ticks or 3
   assert(gate.step_trace_arm(ticks)==1,'step trace refused '..tostring(gate.step_trace_status()))
   log('STEP_TRACE_ARMED watched='..#cfg.native_gate_step_trace..' ticks='..ticks..(cfg.native_gate_step_chain and ' chain=1' or ''))
  end
  -- The flight parameter refresh recomputed by our strict code next to the native one; per output field the
  -- ulp distance and FP_REDIRECT_ROWs for up to four watched saved IDs in ticks 1..N (Option B slice 1;
  -- 'shadow' writes nothing to the game; a lost hook site stops with 179). Default off.
  if cfg.native_gate_fp_redirect then
   local watch=cfg.native_gate_fp_redirect_watch or {}
   assert((cfg.native_gate_fp_redirect=='shadow' or cfg.native_gate_fp_redirect=='writer' or cfg.native_gate_fp_redirect=='redirect') and type(watch)=='table' and #watch<=4 and (#watch==0 or cfg.native_gate_world_identities) and
    gate.fp_redirect and gate.fp_redirect_watch and gate.fp_redirect_status and gate.fp_redirect_row,'fp redirect refused')
   for _,id in ipairs(watch) do
    assert(type(id)=='string' and #id==16 and id:match('^[0-9a-f]+$') and gate.fp_redirect_watch(id)==1,'fp redirect refused '..tostring(id))
   end
   local ticks=cfg.native_gate_fp_redirect_ticks or 3
   assert(gate.fp_redirect(cfg.native_gate_fp_redirect,ticks)==1,'fp redirect refused '..tostring(gate.fp_redirect_status()))
   log('FP_REDIRECT_ARMED mode='..cfg.native_gate_fp_redirect..' watched='..#watch..' ticks='..ticks)
  end
  if cfg.native_gate_weapon_fire_rng then
   assert(cfg.native_gate_weapon_fire_rng==true and cfg.native_gate_world_identities and gate.weapon_fire_rng and
    gate.weapon_fire_rng_status and gate.weapon_fire_rng()==1,
    'weapon fire rng refused '..tostring(gate.weapon_fire_rng_status and gate.weapon_fire_rng_status()))
   log('WEAPON_FIRE_RNG_ARMED')
  end
  -- Engine-parameter refreshes rescheduled from (tick, ship portable key) instead of
  -- the thread's word (OnUpdateEngineParameters: next refresh at now + 8 + [0, 2) s).
  -- Needs the world identities. Default off.
  if cfg.native_gate_engine_refresh_rng then
   assert(cfg.native_gate_engine_refresh_rng==true and cfg.native_gate_world_identities and gate.engine_refresh_rng and
    gate.engine_refresh_rng_status and gate.engine_refresh_rng()==1,
    'engine refresh rng refused '..tostring(gate.engine_refresh_rng_status and gate.engine_refresh_rng_status()))
   log('ENGINE_REFRESH_RNG_ARMED')
  end
  -- Build storage updates rescheduled from (storage portable key, game time) instead of the
  -- thread's word: the snapshot load reschedules nearly every build storage at now + 60 +
  -- [0, 10) s per client. Armed before the load. Needs the world identities. Default off.
  if cfg.native_gate_build_rng then
   assert(cfg.native_gate_build_rng==true and cfg.native_gate_world_identities and gate.build_rng and
    gate.build_rng_status and gate.build_rng()==1,
    'build rng refused '..tostring(gate.build_rng_status and gate.build_rng_status()))
   log('BUILD_RNG_ARMED')
  end
  -- The UI's cargo/production queries (GetCargo, GetStorageData, ...) no longer bring the
  -- container up to date in admitted frames ('block'), or are only counted ('observe').
  -- Needs the world identities (row keys). Default off.
  if cfg.native_gate_ui_cargo then
   local mode=cfg.native_gate_ui_cargo
   assert((mode=='block' or mode=='observe') and cfg.native_gate_world_identities and gate.ui_cargo and gate.ui_cargo_status and
    gate.ui_cargo_row and gate.ui_cargo(mode)==1,'ui cargo refused '..tostring(gate.ui_cargo_status and gate.ui_cargo_status()))
   log('UI_CARGO_ARMED mode='..mode)
  end
  -- Diagnostic: one SUPPLY_POST_ROW per SupplyWaresAddedEvent a station posts in an admitted
  -- frame (key, due, thread, eight return addresses). Needs the world identities. Default off.
  if cfg.native_gate_supply_rows then
   assert(cfg.native_gate_supply_rows==true and cfg.native_gate_world_identities and gate.supply_rows and gate.supply_rows_status and
    gate.supply_row and gate.supply_rows()==1,'supply rows refused '..tostring(gate.supply_rows_status and gate.supply_rows_status()))
   log('SUPPLY_ROWS_ARMED')
  end
  -- Lua UI queries (cargo/production 0x41a910, trade offers 0x4079e0, units 0x4d8110 with the Lua
  -- runtime on the stack) in admitted frames: 'observe' counts them, 'block' also skips the lazy
  -- update. Armed before the load. Needs the world identities. Default off.
  if cfg.native_gate_ui_lazy then
   local mode,window=cfg.native_gate_ui_lazy,cfg.native_gate_ui_lazy_rows -- window: {first tick, last tick}, every call a row
   assert(window==nil or (type(window)=='table' and type(window[1])=='number' and type(window[2])=='number'),'ui lazy refused (rows)')
   assert((mode=='block' or mode=='observe') and cfg.native_gate_world_identities and gate.ui_lazy and gate.ui_lazy_status and
    gate.ui_lazy_row and (window and gate.ui_lazy(mode,window[1],window[2]) or not window and gate.ui_lazy(mode))==1,
    'ui lazy refused '..tostring(gate.ui_lazy_status and gate.ui_lazy_status()))
   log('UI_LAZY_ARMED mode='..mode..(window and ' rows='..window[1]..'-'..window[2] or ''))
  end
  -- Rows of who gets built; only watches, and what it cannot watch loses a row.
  if cfg.native_gate_build_rows then
   assert(cfg.native_gate_build_rows==true and gate.build_rows and gate.build_rows_status and gate.build_rows_row and gate.build_rows()==1,
    'build rows refused '..tostring(gate.build_rows_status and gate.build_rows_status()))
   log('BUILD_ROWS_ARMED')
  end
  -- Macros sit in id hash order on every client, so every map keyed by macro pointer agrees. Armed
  -- before the load, which rebuilds them. Default off. Its free route stays until the process exits.
  if cfg.native_gate_macro_arena then
   assert(cfg.native_gate_macro_arena==true and gate.macro_arena and gate.macro_arena_status and gate.macro_arena_row and gate.macro_arena()==1,
    'macro arena refused '..tostring(gate.macro_arena_status and gate.macro_arena_status()))
   log('MACRO_ARENA_ARMED')
  end
  -- Weighted picks from the libraries' selection tables walk in file order, not heap order: 'observe'
  -- counts (PICK_ORDER_ROWs in native_gate_pick_order_rows {first, last}), 'canonical' also uses it.
  -- Armed before the load. Default off.
  if cfg.native_gate_pick_order then
   local mode,window=cfg.native_gate_pick_order,cfg.native_gate_pick_order_rows
   assert(window==nil or (type(window)=='table' and type(window[1])=='number' and type(window[2])=='number'),'pick order refused (rows)')
   assert((mode=='observe' or mode=='canonical') and gate.pick_order and gate.pick_order_status and gate.pick_order_row and
    (window and gate.pick_order(mode,window[1],window[2]) or not window and gate.pick_order(mode))==1,
    'pick order refused '..tostring(gate.pick_order_status and gate.pick_order_status()))
   log('PICK_ORDER_ARMED mode='..mode..(window and ' rows='..window[1]..'-'..window[2] or ''))
  end
  -- Generated loadouts' picks and script tables keyed by wares or macros do not follow heap addresses:
  -- 'observe' counts (ORDER_CANON_ROWs in native_gate_order_canon_rows {first, last}), 'canonical'
  -- orders them by id. Armed before the load, so every table is built that way. Default off.
  if cfg.native_gate_order_canon then
   local mode,window=cfg.native_gate_order_canon,cfg.native_gate_order_canon_rows
   assert(window==nil or (type(window)=='table' and type(window[1])=='number' and type(window[2])=='number'),'order canon refused (rows)')
   assert((mode=='observe' or mode=='canonical') and gate.order_canon and gate.order_canon_status and gate.order_canon_row and
    (window and gate.order_canon(mode,window[1],window[2]) or not window and gate.order_canon(mode))==1,
    'order canon refused '..tostring(gate.order_canon_status and gate.order_canon_status()))
   log('ORDER_CANON_ARMED mode='..mode..(window and ' rows='..window[1]..'-'..window[2] or ''))
  end
  -- Diagnostic: the macros and wares in address order, now and at the given tick (ADDRESS_ORDER_ROW;
  -- compare clients with tools/dev/census_address_order.py). Read only. Default off.
  if cfg.native_gate_address_order then
   local tick=cfg.native_gate_address_order
   assert(type(tick)=='number' and gate.address_order and gate.address_order_status and gate.address_order_row and gate.address_order(tick)==1,
    'address order refused '..tostring(gate.address_order_status and gate.address_order_status()))
   log('ADDRESS_ORDER_ARMED tick='..tick)
  end
  -- What a station wants to restock does not depend on the macros' heap addresses: 'observe' keeps
  -- LOADOUT_ROWs in the window {first, last} (native_gate_loadout_order_rows, needs the world
  -- identities), 'canonical' also fixes the order to the macro id. Armed before the load. Default off.
  if cfg.native_gate_loadout_order then
   local mode,window=cfg.native_gate_loadout_order,cfg.native_gate_loadout_order_rows
   assert(window==nil or (type(window)=='table' and type(window[1])=='number' and type(window[2])=='number' and cfg.native_gate_world_identities),'loadout order refused (rows)')
   assert((mode=='observe' or mode=='canonical') and gate.loadout_order and gate.loadout_order_status and gate.loadout_order_row and
    (window and gate.loadout_order(mode,window[1],window[2]) or not window and gate.loadout_order(mode))==1,
    'loadout order refused '..tostring(gate.loadout_order_status and gate.loadout_order_status()))
   log('LOADOUT_ORDER_ARMED mode='..mode..(window and ' rows='..window[1]..'-'..window[2] or ''))
  end
  -- Diagnostic: one SUPPLY_WANTS_ROW per GetWantedSupplyOrders call in the window {first, last}
  -- (site, station, arguments, wanted wares and amounts). Needs the world identities. Default off.
  if cfg.native_gate_supply_wants~=nil then
   local window=cfg.native_gate_supply_wants
   assert(type(window)=='table' and type(window[1])=='number' and type(window[2])=='number' and cfg.native_gate_world_identities and
    gate.supply_wants and gate.supply_wants_status and gate.supply_wants_row and gate.supply_wants(window[1],window[2])==1,
    'supply wants refused '..tostring(gate.supply_wants_status and gate.supply_wants_status()))
   log('SUPPLY_WANTS_ARMED rows='..window[1]..'-'..window[2])
  end
  -- Diagnostic: one EFFECT_ROW per effect element SpawnEffect hands to the
  -- creator 0xd73060 (owner key, delay, shot time, game time, deferred to an
  -- ActivateEffectEvent). Needs view points and a pass join. Default off.
  if cfg.native_gate_effect_rows then
   assert(cfg.native_gate_effect_rows==true and cfg.native_gate_view_points and
    (cfg.native_gate_mass_traffic or cfg.native_gate_region_pass) and gate.region_camera and gate.region_camera_effect_row and
    gate.region_camera_frame and gate.region_camera_status and gate.region_camera('effect_rows')==1,
    'effect rows refused '..tostring(gate.region_camera_status and gate.region_camera_status()))
   log('EFFECT_ROWS_ARMED')
  end
  -- Parity-leak harness: on the named client only (1/2 or 'p1'/'p2'), change
  -- the player camera's field of view every admitted tick. Presentation only:
  -- no command, event, ID, RNG draw or option write. Default off.
  if cfg.native_gate_camera_perturb then
   local target=cfg.native_gate_camera_perturb
   if type(target)=='string' then target=tonumber(target:match('^p([12])$')) end
   assert((target==1 or target==2) and gate.camera_perturb and gate.camera_perturb_status,'camera perturbation refused')
   camera_perturb=target==tonumber(cfg.slot:sub(2))
   log('CAMERA_PERTURB_ARMED slot=p'..target..' local='..tostring(camera_perturb))
  end
  -- Camera A/B harness: on the named client only, move the player camera by
  -- (x, y, z) metres in its anchor's frame through the camera's local position
  -- (no view API, no event or component); every admitted tick it pauses the
  -- camera's control update (look input and smoothing; the camera still follows
  -- its anchor) so the value is held, and fails closed if anything else
  -- rewrites it. Default off.
  if cfg.native_gate_camera_offset then
   local o=cfg.native_gate_camera_offset
   local target=type(o)=='table' and o.client
   if type(target)=='string' then target=tonumber(target:match('^p([12])$')) end
   local finite=function(v) return type(v)=='number' and v==v and v>-1e7 and v<1e7 end
   assert(type(o)=='table' and (target==1 or target==2) and finite(o.x or 0) and finite(o.y or 0) and finite(o.z or 0) and
    ((o.x or 0)~=0 or (o.y or 0)~=0 or (o.z or 0)~=0) and gate.camera_offset and gate.camera_offset_status,'camera offset refused')
   if target==tonumber(cfg.slot:sub(2)) then camera_offset={o.x or 0,o.y or 0,o.z or 0} end
   log('CAMERA_OFFSET_ARMED slot=p'..target..' local='..tostring(camera_offset~=nil)..' offset='..(o.x or 0)..','..(o.y or 0)..','..(o.z or 0))
  end
  -- Presentation harness: on one client the player camera follows another pilot slot's
  -- latched ship (its anchor pair), the control update paused as for the offset, from a chase
  -- view (back/up metres in the ship's frame, default 40/10; 0/0 keeps the camera's own offset).
  -- 'own' (or {own=true, back=, up=}) is the play binding: every client whose pilot slot is not 1
  -- follows its own slot's ship (slot 1's ship is the player object; its client keeps the native
  -- camera). Needs view points; excludes native_gate_camera_offset and native_gate_pilot_camera_follow
  -- (the stock target view, which also writes the anchor). Default off.
  if cfg.native_gate_camera_anchor then
   local o=cfg.native_gate_camera_anchor
   local own=o=='own' or (type(o)=='table' and o.own==true)
   local mine=tonumber(tostring(cfg.slot or ''):match('^p(%d)$'))
   local target,slot=mine,mine
   if not own then
    target=type(o)=='table' and o.client;slot=type(o)=='table' and o.slot
    if type(target)=='string' then target=tonumber(target:match('^p([12])$')) end
   end
   local back,up=type(o)=='table' and o.back or 40,type(o)=='table' and o.up or 10
   assert((own and mine and mine>=1 and mine<=4 or type(o)=='table' and (target==1 or target==2) and (slot==1 or slot==2)) and
    not cfg.native_gate_camera_offset and
    not cfg.native_gate_pilot_camera_follow and type(back)=='number' and type(up)=='number' and back==back and up==up and
    math.abs(back)<=5000 and math.abs(up)<=5000 and
    cfg.native_gate_view_points and gate.camera_anchor and gate.camera_anchor_status,'camera anchor refused')
   -- Orbit key map (free look): true = Ctrl+J/L yaw, Ctrl+I/K pitch, Ctrl+U/O zoom in/out, Ctrl+P reset
   -- (unbound in X4's default maps); 'numpad' = 4/6/8/2, +/-, 5 (shared with X4's camera keys);
   -- a table names keys: {modifier='CONTROL'|'SHIFT'|'NONE', yaw_left='J', ..., reset='P'}.
   local spec=type(o)=='table' and o.orbit or nil
   local orbit=(spec==true or spec=='numpad' or type(spec)=='table') and 1 or 0
   local vk={CONTROL=0x11,SHIFT=0x10,NONE=0,UP=0x26,DOWN=0x28,LEFT=0x25,RIGHT=0x27,PRIOR=0x21,NEXT=0x22,HOME=0x24,END=0x23,
             INSERT=0x2d,DELETE=0x2e,ADD=0x6b,SUBTRACT=0x6d,MULTIPLY=0x6a,DIVIDE=0x6f,DECIMAL=0x6e}
   for i=0,9 do vk['NUMPAD'..i]=0x60+i;vk[tostring(i)]=0x30+i end
   for i=1,12 do vk['F'..i]=0x6f+i end
   for c=65,90 do vk[string.char(c)]=c end
   local names={'modifier','yaw_left','yaw_right','pitch_up','pitch_down','zoom_in','zoom_out','reset'}
   local map={modifier='CONTROL',yaw_left='J',yaw_right='L',pitch_up='I',pitch_down='K',zoom_in='U',zoom_out='O',reset='P'}
   if spec=='numpad' then map={modifier='NONE',yaw_left='NUMPAD4',yaw_right='NUMPAD6',pitch_up='NUMPAD8',pitch_down='NUMPAD2',
                               zoom_in='ADD',zoom_out='SUBTRACT',reset='NUMPAD5'} end
   if type(spec)=='table' then for _,k in ipairs(names) do if spec[k]~=nil then map[k]=spec[k] end end end
   local keys,known={},spec==nil or spec==false or orbit==1
   for i,k in ipairs(names) do keys[i]=type(map[k])=='string' and vk[map[k]:upper()] or -1;known=known and (orbit==0 or keys[i]>=0) end
   assert(known and (orbit==0 or keys[1]==0 or keys[1]==0x10 or keys[1]==0x11),'camera anchor refused')
   if target==mine and (not own or slot>=2) then
    camera_offset={follow=slot,back=back,up=up,orbit=orbit}
    if orbit==1 then assert(gate.camera_anchor_keys and gate.camera_anchor_keys(unpack(keys))==1,'camera anchor keys refused') end
   end
   local named={};if orbit==1 then for i,k in ipairs(names) do named[i]=k..'='..map[k] end end
   log('CAMERA_ANCHOR_ARMED client='..(own and 'own' or 'p'..target)..' local='..tostring(camera_offset~=nil)..' slot='..slot..' chase='..back..','..up..' orbit='..orbit..
       (orbit==1 and ' keys='..table.concat(named,',') or ''))
  end
  if cfg.native_gate_gravidar_key_all then
   assert(snapshot and cfg.native_gate_radar_serial and cfg.native_gate_world_identities and gate.gravidar_key and gate.gravidar_key()==1,'Gravidar unit keying refused')
   log('GRAVIDAR_KEY_ARMED')
  end
  -- Read-only: every admitted tick, order-independent digests of every Gravidar
  -- unit's radar factor, scan times and class, of the active-source lists and
  -- of the field sectors' factor-pass deadlines. native_gate_gravidar_digest_rows=N
  -- adds a row per changed unit (every change in the first N ticks, factor
  -- changes after) and per changed field sector.
  local digest_rows=cfg.native_gate_gravidar_digest_rows
  if cfg.native_gate_gravidar_digest then
   assert(cfg.native_gate_gravidar_digest==true and gate.gravidar_digest and gate.gravidar_digest_frame,'Gravidar digest refused')
   assert(digest_rows==nil or (type(digest_rows)=='number' and digest_rows>=1 and digest_rows<=1000000 and digest_rows%1==0 and
    gate.gravidar_digest_row),'Gravidar digest rows refused')
   local accepted
   if digest_rows then accepted=gate.gravidar_digest(digest_rows) else accepted=gate.gravidar_digest() end
   assert(accepted==1,'Gravidar digest refused')
   log('GRAVIDAR_DIGEST_ARMED'..(digest_rows and ' rows='..digest_rows or ''))
  else
   assert(digest_rows==nil,'Gravidar digest rows need native_gate_gravidar_digest')
  end
  if cfg.native_gate_gravidar_watch then
   -- Slot-indexed: the loading-UI reload repeats this identical list and re-arms each slot.
   for slot,id in ipairs(cfg.native_gate_gravidar_watch) do assert(gate.gravidar_watch and gate.gravidar_watch(id,slot)==1,'Gravidar watch refused '..id) end
   log('GRAVIDAR_WATCH_ARMED ids='..table.concat(cfg.native_gate_gravidar_watch,','))
  end
  -- Read-only: one GRAVIDAR_POST_ROW per has_scanned post that changed a
  -- unit's contacts (added/removed as portable keys, unmapped ones with their
  -- class), GRAVIDAR_POSTS per tick with a cumulative digest. Needs
  -- native_gate_gravidar_key_all (it rides the keyed unit apply). Default off.
  if cfg.native_gate_gravidar_posts then
   assert(cfg.native_gate_gravidar_posts==true and cfg.native_gate_gravidar_key_all and gate.gravidar_posts and gate.gravidar_posts_status and
    gate.gravidar_post_row and gate.gravidar_posts()==1,'Gravidar posts refused')
   log('GRAVIDAR_POSTS_ARMED')
  end
  -- Determinism fix for stations' Gravidar contacts (lockstep_gravidar_wide.h,
  -- stop reason 150). Natively a wide-range unit (stations) is applied before
  -- the zone groups at a higher address are checked against it. true/'wide':
  -- it takes its result after every pair check of its sector (changes play:
  -- stations see the ships in range in every zone). 'sort': native semantics,
  -- the zone groups in portable order for every gated pass (needs the gated
  -- Gravidar frame and the world identities). Default off.
  if cfg.native_gate_gravidar_wide then
   local mode=cfg.native_gate_gravidar_wide==true and 'wide' or cfg.native_gate_gravidar_wide
   assert((mode=='wide' or (mode=='sort' and snapshot and cfg.native_gate_radar_serial and cfg.native_gate_world_identities)) and
    gate.gravidar_wide and gate.gravidar_wide_status and gate.gravidar_wide(mode)==1,'Gravidar wide refused')
   log('GRAVIDAR_WIDE_ARMED mode='..mode)
  end
  -- Speech lines complete on the game clock (lockstep_npc_deadline.h), stop reason 120.
  if cfg.native_gate_speech_game_time then
   assert(snapshot and gate.speech_time and gate.speech_time()==1,'speech game time refused')
   log('SPEECH_TIME_ARMED')
  end
  -- A cutscene (a comm monitor's talking head) is ready 2.0 s of game time after it starts, whatever the local
  -- streaming does (lockstep_cutscene_ready.h, stop reason 178). Default off; needs serial pathing.
  if cfg.native_gate_cutscene_ready then
   assert(cfg.native_gate_cutscene_ready==true and snapshot and cfg.native_gate_serial_pathing and gate.cutscene_ready and
    gate.cutscene_ready_status and gate.cutscene_ready()==1,'cutscene ready refused '..tostring(gate.cutscene_ready_status and gate.cutscene_ready_status()))
   log('CUTSCENE_READY_ARMED')
  end
  -- Every speech line ends on its text timer, whatever the local sound does (lockstep_speak_timer.h), stop reason 130.
  if cfg.native_gate_speak_timer then
   assert(cfg.native_gate_speak_timer==true and snapshot and gate.speak_timer and gate.speak_timer_status and gate.speak_timer()==1,
    'speak timer refused '..tostring(gate.speak_timer_status and gate.speak_timer_status()))
   log('SPEAK_TIMER_ARMED')
  end
  -- Shields as the lazy getter leaves them (lockstep_state.h): a client's UI that reads a full shield
  -- rewrites it; the world hash hashes value=max, time=0 for a full shield. Default off.
  if cfg.native_gate_world_hash_shields then
   assert(cfg.native_gate_world_hash_shields=='canonical' and cfg.native_gate_world_hash_stride and gate.world_hash_shields and
    gate.world_hash_shields_status and gate.world_hash_shields('canonical')==1,'world hash shields refused')
   log('WORLD_HASH_SHIELDS_ARMED mode=canonical')
  end
  if cfg.native_gate_gravidar_time_sync then
   assert(snapshot and cfg.native_gate_radar_serial and gate.gravidar_time and gate.gravidar_time()==1,'Gravidar time sync refused')
   log('GRAVIDAR_TIME_ARMED')
  end
  -- Gravidar factor-pass and scan deadlines (lockstep_gravidar_deadlines.h, stop
  -- reason 121): each field sector's first admitted Move re-bases its factor
  -- pass deadline, due passes draw from a word keyed by tick and sector, and
  -- active units' pre-gate scan times go back to the constructor's at
  -- admission. Needs the Gravidar time sync and the world identities. Default off.
  if cfg.native_gate_gravidar_deadlines then
   assert(snapshot and cfg.native_gate_gravidar_deadlines==true and cfg.native_gate_gravidar_time_sync and cfg.native_gate_world_identities and
    gate.gravidar_deadlines and gate.gravidar_deadlines_status and gate.gravidar_deadlines()==1,'Gravidar deadlines refused')
   log('GRAVIDAR_DEADLINES_ARMED')
  end
  -- Gravidar sector round-robin: zero the idle cursor before the load pass,
  -- prove it at admission and check its native advance every frame (mask 1).
  -- native_gate_gravidar_load_pass holds Init's load pass so the first pass
  -- runs in gated frame 1 (mask 2); native_gate_gravidar_sector_order runs
  -- every gated pass on the sector list in saved-ID order instead of address
  -- order (mask 4). Needs the Gravidar time sync (publishes held in boot).
  -- Default off.
  if cfg.native_gate_ai_schedule then
   local mask=1+(cfg.native_gate_gravidar_load_pass and 2 or 0)+(cfg.native_gate_gravidar_sector_order and 4 or 0)
   assert(snapshot and cfg.native_gate_ai_schedule==true and cfg.native_gate_radar_serial and cfg.native_gate_serial_pathing and
    cfg.native_gate_gravidar_time_sync and (cfg.native_gate_gravidar_load_pass==nil or cfg.native_gate_gravidar_load_pass==true) and
    (cfg.native_gate_gravidar_sector_order==nil or (cfg.native_gate_gravidar_sector_order==true and cfg.native_gate_world_identities)) and
    gate.ai_schedule and gate.ai_schedule(mask)==1,'AI schedule refused')
   log('AI_SCHEDULE_ARMED mask='..mask)
  else
   assert(cfg.native_gate_gravidar_load_pass==nil and cfg.native_gate_gravidar_sector_order==nil,'AI schedule refused: Gravidar load pass/sector order need native_gate_ai_schedule')
  end
  if cfg.native_gate_job_rng then
   assert(snapshot and (cfg.native_gate_job_rng=='observe' or cfg.native_gate_job_rng=='key') and
    gate.job_rng and gate.job_rng(cfg.native_gate_job_rng)==1,'job RNG hook refused')
   log('JOB_RNG_ARMED mode='..cfg.native_gate_job_rng)
  end
  -- Delivers each due-event batch in (time, saved ID) source order instead of
  -- heap-address order. Needs the saved identities; default off.
  if cfg.native_gate_event_order then
   assert(snapshot and cfg.native_gate_event_order==true and cfg.native_gate_world_identities and
    gate.event_order and gate.event_order()==1,'event order refused')
   log('EVENT_ORDER_ARMED')
  end
  -- Every due event delivered inside its own RNG scope: 'key' seeds the thread word from
  -- (tick, source portable key, event type, due time) and puts the caller's word back;
  -- 'observe' only counts each listener's draws. Rides the event order; default off.
  if cfg.native_gate_event_rng then
   local mode,window=cfg.native_gate_event_rng,cfg.native_gate_event_rng_rows -- window: {first tick, last tick}, every delivery a row
   assert(window==nil or (type(window)=='table' and type(window[1])=='number' and type(window[2])=='number'),'event rng refused (rows)')
   assert((mode=='observe' or mode=='key') and cfg.native_gate_event_order==true and cfg.native_gate_world_identities and
    gate.event_rng and gate.event_rng_status and gate.event_rng_row and
    (window and gate.event_rng(mode,window[1],window[2]) or not window and gate.event_rng(mode))==1,
    'event rng refused '..tostring(gate.event_rng_status and gate.event_rng_status()))
   log('EVENT_RNG_ARMED mode='..mode..(window and ' rows='..window[1]..'-'..window[2] or ''))
  end
  -- Census rows for boot passes and the first N gated ticks (true means 3).
  if cfg.native_gate_event_rows~=nil then
   local ticks=cfg.native_gate_event_rows==true and 3 or cfg.native_gate_event_rows
   assert(cfg.native_gate_event_order and type(ticks)=='number' and ticks>=0 and ticks<=(gate.platform and gate.platform()=='linux' and 20000 or 600) and ticks==math.floor(ticks) and
    gate.event_rows and gate.event_rows(ticks)==1,'event rows refused')
   log('EVENT_ROWS_ARMED ticks='..ticks)
  end
  -- Portable IDs for components created after the snapshot load: creation
  -- tick, parent's saved/created ID, class, macro and ordinal, high bit set.
  -- World captures then include them; native_gate_created_rows also logs one
  -- CREATED_ROW per assignment/retirement. Needs saved identities; default off.
  if cfg.native_gate_created_identities then
   assert(snapshot and cfg.native_gate_created_identities==true and cfg.native_gate_world_identities and
    (cfg.native_gate_created_rows==nil or cfg.native_gate_created_rows==true) and
    gate.created_identities and gate.created_identities_status and gate.created_row and
    gate.created_identities(cfg.native_gate_created_rows and 1 or 0)==1,'created identities refused')
   log('CREATED_IDENTITIES_ARMED rows='..(cfg.native_gate_created_rows and 1 or 0))
   -- Created IDs also as order/RNG keys: path determinism, travel snapshot,
   -- Gravidar keys, event order, find order and script RNG then treat a
   -- created object (from the frame after its creation) like an imported one,
   -- behind every saved object. Each module still needs its own switch.
   if cfg.native_gate_portable_created then
    assert(cfg.native_gate_portable_created==true and gate.portable_created and gate.portable_created()==1,'portable created keys refused')
    log('PORTABLE_CREATED_ARMED')
   end
   -- Objects the load creates after the snapshot import (no saved ID) get
   -- created IDs with tick 0 at the seal, from the load's creation order.
   if cfg.native_gate_created_load then
    assert(cfg.native_gate_created_load==true and gate.created_load and gate.created_load()==1,'created load identities refused')
    log('CREATED_LOAD_ARMED')
   end
  else
   assert(cfg.native_gate_created_rows==nil,'created identities refused: native_gate_created_rows needs native_gate_created_identities')
   assert(cfg.native_gate_portable_created==nil,'portable created keys refused: native_gate_portable_created needs native_gate_created_identities')
   assert(cfg.native_gate_created_load==nil,'created load identities refused: native_gate_created_load needs native_gate_created_identities')
  end
  -- Read-only Jolt step trace: per JPH::PhysicsSystem::Update order-independent
  -- digests of every portable body's BodyID, active index, inputs and outputs
  -- and of the contact callbacks (set and order; callbacks off the stepping
  -- thread counted). native_gate_physics_trace=N also gives PHYSICS_BODY and
  -- PHYSICS_CONTACT rows of created bodies for ticks 1..N (0: step rows only).
  -- Needs world identities. Default off.
  if cfg.native_gate_physics_trace~=nil then
   local rows=cfg.native_gate_physics_trace
   assert(snapshot and cfg.native_gate_world_identities and type(rows)=='number' and rows>=0 and rows<=1000000 and rows%1==0 and
    gate.physics_trace and gate.physics_trace_status and gate.physics_trace_row and gate.physics_trace(rows)==1,
    'physics trace refused '..tostring(gate.physics_trace_status and gate.physics_trace_status()))
   log('PHYSICS_TRACE_ARMED rows='..rows)
  end
  -- PHYSICS_CHILD rows (diagnostic): every compound child of each keyed body for ticks 1..N (index,
  -- class, the part it stands for, pose). Needs native_gate_physics_trace. Default off.
  -- native_gate_physics_trace_children_from = A starts them at tick A.
  if cfg.native_gate_physics_trace_children~=nil then
   local ticks,from=cfg.native_gate_physics_trace_children,cfg.native_gate_physics_trace_children_from or 1
   assert(cfg.native_gate_physics_trace~=nil and type(ticks)=='number' and ticks>=1 and ticks<=3600 and ticks%1==0 and
    type(from)=='number' and from>=1 and from<=ticks and from%1==0 and
    gate.physics_trace_children and gate.physics_trace_children(ticks,from)==1,
    'physics trace children refused '..tostring(gate.physics_trace_status and gate.physics_trace_status()))
   log('PHYSICS_TRACE_CHILDREN_ARMED ticks='..ticks..' from='..from)
  end
  assert(cfg.native_gate_physics_trace_children_from==nil or cfg.native_gate_physics_trace_children~=nil,'physics trace children refused')
  -- PHYSICS_BODY rows for saved bodies too (diagnostic): every keyed body's shape, mass and state per
  -- step for ticks 1..native_gate_physics_trace (>= 1). Default off.
  if cfg.native_gate_physics_trace_saved then
   assert(cfg.native_gate_physics_trace_saved==true and type(cfg.native_gate_physics_trace)=='number' and cfg.native_gate_physics_trace>=1 and
    gate.physics_trace_saved and gate.physics_trace_saved()==1,
    'physics trace saved rows refused '..tostring(gate.physics_trace_status and gate.physics_trace_status()))
   log('PHYSICS_TRACE_SAVED_ARMED rows='..cfg.native_gate_physics_trace)
  end
  -- Physics bodies of the frame's queued objects (attention/parent changes)
  -- created in portable order: 'sort' runs each object's drop and creation by
  -- saved ID, created ID, then this frame's creation order instead of heap
  -- address order (so BodyIDs agree); 'observe' records the rows only. Needs
  -- world, created and portable created identities. Default off.
  if cfg.native_gate_body_order then
   local mode=cfg.native_gate_body_order
   assert(snapshot and (mode=='sort' or mode=='observe') and cfg.native_gate_world_identities and cfg.native_gate_created_identities and
    cfg.native_gate_portable_created and gate.body_order and gate.body_order_status and gate.body_order_row and gate.body_order(mode)==1,
    'body order refused '..tostring(gate.body_order_status and gate.body_order_status()))
   log('BODY_ORDER_ARMED mode='..mode)
  end
  -- A ship part whose collision file finishes loading after the ship was built
  -- gets its real child shape appended at the end of the compound; true keeps
  -- it at its stand-in's index, so SubShapeIDs (Jolt's contact order) do not
  -- depend on the loader thread's timing; 'wait' also makes each physics step
  -- wait until every collision file requested before it has been applied;
  -- 'sync' also loads a missing collision file before the part's shape is
  -- built, so every child is built in place. Default off.
  if cfg.native_gate_shape_order then
   local mode=cfg.native_gate_shape_order==true and 'place' or cfg.native_gate_shape_order
   assert(snapshot and (mode=='place' or mode=='wait' or mode=='sync') and gate.shape_order and gate.shape_order_status and gate.shape_order_row and
    gate.shape_order(mode)==1,'shape order refused '..tostring(gate.shape_order_status and gate.shape_order_status()))
   log('SHAPE_ORDER_ARMED mode='..mode)
  end
  -- Object navigation octrees are built on their own thread and published
  -- whenever a build ends (docking approach, pathing); 'sync' builds every
  -- octree requested in a frame at the gate's join after it; 'deadline' lets
  -- them build in the background and shows each at the join of its request's
  -- tick + native_gate_nav_octree_delay (default 30); 'agreed' shows each once
  -- every client reported it built through the input wire (needs
  -- native_gate_clients); 'observe' only logs NAV_OCTREE_ROW per finished
  -- build. native_gate_nav_octree_snapshot = true builds every octree from
  -- the object's state at its request's join (any mode). Default off.
  if cfg.native_gate_nav_octree then
   local mode=cfg.native_gate_nav_octree
   local delay=mode=='deadline' and (cfg.native_gate_nav_octree_delay or 30) or 0
   local snap=cfg.native_gate_nav_octree_snapshot and 1 or 0
   assert(snapshot and (mode=='observe' or mode=='sync' or mode=='deadline' or (mode=='agreed' and wire3 and gate.nav_octree_report and
    gate.nav_octree_agree)) and gate.nav_octree and gate.nav_octree_status and gate.nav_octree_row and gate.nav_octree(mode,delay,snap)==1,
    'nav octree refused '..tostring(gate.nav_octree_status and gate.nav_octree_status()))
   log('NAV_OCTREE_ARMED mode='..mode..(mode=='deadline' and ' delay='..delay or '')..(snap==1 and ' snapshot=1' or ''))
  end
  -- Read-only: every nav octree invalidation through the module's stubs
  -- counted by (phase menu/load/run, site, return address, object class,
  -- thread; menu rows may differ between clients, load and run rows not);
  -- NAV_OCTREE_SITE rows at the first admitted tick and at the stop,
  -- NAV_OCTREE_SITES per tick. Needs native_gate_nav_octree 'deadline' or
  -- 'agreed'. Default off.
  if cfg.native_gate_nav_octree_sites then
   assert(cfg.native_gate_nav_octree_sites==true and (cfg.native_gate_nav_octree=='deadline' or cfg.native_gate_nav_octree=='agreed') and
    gate.nav_octree_sites and gate.nav_octree_sites_status and gate.nav_octree_site_row and gate.nav_octree_sites()==1,'nav octree sites refused')
   log('NAV_OCTREE_SITES_ARMED')
  end
  -- Every ship with a portable identity (saved; created with portable created
  -- keys) gets its own RNG stream in Ship Move (sim thread) and the pathing
  -- Agent update (JobThreads), like the fixture actors; fixtures are unchanged.
  -- Needs the seeded agent and ship modules. Default off.
  if cfg.native_gate_actor_rng_all then
   assert(snapshot and cfg.native_gate_actor_rng_all==true and cfg.native_gate_world_identities and cfg.native_gate_agent_rng and
    cfg.native_gate_ship_rng and gate.actor_rng_all and gate.actor_rng_all_status and gate.actor_rng_all()==1,'all-ship actor RNG refused')
   log('ACTOR_RNG_ALL_ARMED')
   -- One ACTOR_RNG_ROW per keyed scope that drew: path, portable ID, draws. Default off.
   if cfg.native_gate_actor_rng_rows then
    assert(cfg.native_gate_actor_rng_rows==true and gate.actor_rng_rows and gate.actor_rng_rows()==1,'actor RNG rows refused')
    log('ACTOR_RNG_ROWS_ARMED')
   end
   -- Who writes a watched ship's word inside its ship scope: a hardware write
   -- breakpoint while the scope is open; one DRAW_WATCH_ROW per store (rip,
   -- six callers, value). Up to four saved IDs. Default off.
   if cfg.native_gate_draw_watch then
    local list=cfg.native_gate_draw_watch
    assert(type(list)=='table' and #list>=1 and #list<=4 and gate.draw_watch_add and gate.draw_watch and gate.draw_watch_status and
     gate.draw_watch_row and gate.draw_watch_stray,'draw watch refused')
    for _,id in ipairs(list) do
     assert(type(id)=='string' and #id==16 and id:match('^[0-9a-f]+$') and gate.draw_watch_add(id)==1,'draw watch refused '..tostring(id))
    end
    assert(gate.draw_watch()==1,'draw watch refused '..tostring(gate.draw_watch_status()))
    log('DRAW_WATCH_ARMED ids='..#list)
   end
  end
  assert(not cfg.native_gate_actor_rng_rows or cfg.native_gate_actor_rng_all,'actor RNG rows need native_gate_actor_rng_all')
  assert(not cfg.native_gate_draw_watch or cfg.native_gate_actor_rng_all,'draw watch needs native_gate_actor_rng_all')
  -- Who draws from the frame's own word: a hardware write breakpoint from the frame's
  -- seed to this probe's update, a DRAW_WATCH_ROW per frame-level draw. Diagnostic, default off.
  if cfg.native_gate_draw_watch_frame then
   assert(cfg.native_gate_draw_watch_frame==true and cfg.native_gate_seed_rng and not cfg.native_gate_draw_watch and gate.draw_watch_frame and gate.draw_watch_frame_end and
    gate.draw_watch_status and gate.draw_watch_row and gate.draw_watch_stray and gate.draw_watch_frame()==1,'draw watch frame refused')
   log('DRAW_WATCH_FRAME_ARMED')
  end
  -- Per-script RNG streams on the sim thread: mask 1 AI scripts, 2 MD cues,
  -- 4 also boot phase 2. Rides on the interrupt/nested hooks and saved IDs.
  -- Options, default off: native_gate_script_rng_natural=true also exports a
  -- row for every outermost unkeyed entry (the natural word it ran on; needs
  -- rows); native_gate_script_rng_unmapped='key' keys AI owners without a
  -- portable ID (saved, or created with native_gate_portable_created: their
  -- creation frame) by a marker plus per-tick rank (needs the AI bit).
  if cfg.native_gate_script_rng then
   local mode,mask=cfg.native_gate_script_rng,cfg.native_gate_script_rng_mask or 3
   local natural,unmapped=cfg.native_gate_script_rng_natural,cfg.native_gate_script_rng_unmapped
   local options=(natural and 1 or 0)+(unmapped and 2 or 0)
   -- The Linux gate's scopes have their own entries (gate.script_rng_own_entries), so they need neither trace.
   assert(snapshot and (mode=='observe' or mode=='key') and (gate.script_rng_own_entries or (cfg.native_gate_interrupt_diag and cfg.native_gate_nested_actions)) and
    cfg.native_gate_world_identities and type(mask)=='number' and mask>=1 and mask<=7 and mask==math.floor(mask) and mask%4~=0 and
    (cfg.native_gate_script_rng_rows==nil or cfg.native_gate_script_rng_rows==true) and
    (natural==nil or (natural==true and cfg.native_gate_script_rng_rows==true)) and
    (unmapped==nil or (unmapped=='key' and mask%2==1)) and
    not (mask>=4 and cfg.native_gate_boot_rng_anchor) and
    not (mode=='key' and (cfg.native_gate_ai_rng or cfg.native_gate_interrupt_rng or cfg.native_gate_npc_rng)) and
    gate.script_rng and gate.script_rng(mode,mask,cfg.native_gate_script_rng_rows and 1 or 0,options)==1,'script RNG hook refused')
   log('SCRIPT_RNG_ARMED mode='..mode..' mask='..mask..' options='..options)
  end
  -- Direct MD cue checks (MDScheduler_Update timed checks, Cue_EnterWaiting,
  -- Cue_RaiseCueEvent listeners) on the per-cue script RNG streams; needs the
  -- script RNG with its MD bit. Default off.
  if cfg.native_gate_md_timed_rng then
   local mask=cfg.native_gate_script_rng_mask or 3
   assert(snapshot and cfg.native_gate_md_timed_rng==true and cfg.native_gate_script_rng and type(mask)=='number' and mask%4>=2 and
    gate.md_timed_rng and gate.md_timed_rng()==1,'MD timed RNG refused')
   log('MD_TIMED_RNG_ARMED')
  end
  -- Boot hold: the first AIDirector::Move after the load runs in tick 1, after
  -- the Gravidar boot drain and the serial pathing join, instead of in the
  -- post-load universe update of boot phase 2. Default off.
  if cfg.native_gate_boot_hold then
   assert(snapshot and cfg.native_gate_boot_hold==true and gate.boot_hold and gate.boot_hold()==1,'boot hold refused')
   log('BOOT_HOLD_ARMED')
  end
  -- Load-time MD delays: the snapshot load re-draws the <delay> of every cue
  -- saved with a pending delay; key that draw by cue name, delay slot and
  -- saved activation time instead of the natural load-time word. Default off.
  if cfg.native_gate_md_resume_rng then
   assert(snapshot and cfg.native_gate_md_resume_rng==true and gate.md_resume_rng and gate.md_resume_rng_row and
    gate.md_resume_rng()==1,'MD resume RNG refused')
   log('MD_RESUME_RNG_ARMED')
  end
  if cfg.native_gate_snapshot_fixture then
   assert(snapshot and #cfg.native_gate_snapshot_fixture==12,'invalid snapshot fixture manifest')
   assert(gate.snapshot_fixture(table.concat(cfg.native_gate_snapshot_fixture,','))==1,'snapshot fixture manifest refused')
  end
  if cfg.native_gate_world_trace_early then
   assert(snapshot and gate.world_trace_config(table.concat(cfg.native_gate_world_hash_watch,','),cfg.native_gate_world_trace_ticks)==1,'early world trace manifest refused')
   if cfg.native_gate_world_queue_trace then assert(gate.world_queue_trace_config()==1,'world queue trace configuration refused') end
   if cfg.native_gate_player_transition_observer then assert(X4LAN_NATIVE_TRANSITION,'transition adapter missing').configure(gate) end
   if cfg.native_gate_player_origin_token then assert(X4LAN_NATIVE_ORIGIN,'origin adapter missing').configure(gate) end
   if cfg.native_gate_world_wait_manifest then assert(gate.world_wait_trace_config(cfg.native_gate_world_wait_manifest)==1,'world wait trace configuration refused') end
   if cfg.native_gate_world_wait_rng_manifest then assert(gate.world_wait_rng_config(cfg.native_gate_world_wait_rng_manifest)==1,'world Wait RNG experiment configuration refused') end
   if cfg.native_gate_world_action_manifest then assert(gate.world_action_trace_config(cfg.native_gate_world_action_manifest)==1,'world action trace configuration refused') end
   if cfg.native_gate_world_values then assert(cfg.native_gate_world_action_manifest and gate.world_values_config()==1,'world typed output configuration refused') end
   if cfg.native_gate_world_position then assert(cfg.native_gate_world_position==true and cfg.native_gate_world_values and cfg.native_gate_world_action_manifest and gate.world_position_config()==1,'world position configuration refused') end
   if cfg.native_gate_world_safe_position then assert(cfg.native_gate_world_safe_position==true and cfg.native_gate_world_position and gate.world_safe_position_config()==1,'world safe position configuration refused') end
   if cfg.native_gate_world_find_rng_manifest then assert(cfg.native_gate_world_values and cfg.native_gate_world_wait_rng_manifest and gate.world_find_rng_config(cfg.native_gate_world_find_rng_manifest)==1,'world FindComponent RNG configuration refused') end
   if cfg.native_gate_world_position_rng_manifest then assert(cfg.native_gate_world_position and cfg.native_gate_world_find_rng_manifest and gate.world_position_rng_config(cfg.native_gate_world_position_rng_manifest)==1,'world position RNG configuration refused') end
  end
  -- Load-time AI Wait durations: the snapshot load re-draws the duration of
  -- every saved blocking <wait>; key that draw by saved owner, script position
  -- and saved start time instead of the natural load-time word. Shares the
  -- Wait observer's entry hook, so it arms after the observer. Default off.
  if cfg.native_gate_ai_wait_rng then
   assert(snapshot and cfg.native_gate_ai_wait_rng==true and cfg.native_gate_world_identities and
    not cfg.native_gate_world_wait_rng_manifest and gate.ai_wait_rng and gate.ai_wait_rng_row and
    gate.ai_wait_rng()==1,'AI wait RNG refused')
   log('AI_WAIT_RNG_ARMED')
  end
  -- The economy schedule every container gets from the save: the load re-draws each station's next
  -- economy run from the loading thread's word; key that draw by saved ID and saved last run instead
  -- (lockstep_economy_load.h). Before the load. Default off.
  if cfg.native_gate_economy_load_rng then
   assert(snapshot and cfg.native_gate_economy_load_rng==true and cfg.native_gate_world_identities and
    gate.economy_load_rng and gate.economy_load_rng_status and gate.economy_load_rng()==1,
    'economy load RNG refused '..tostring(gate.economy_load_rng_status and gate.economy_load_rng_status()))
   log('ECONOMY_LOAD_RNG_ARMED')
  end
  -- find_* candidate lists (FindComponentAction): 'observe' records saved-ID
  -- set/order rows; 'sort' also drops repeated entries and orders the list by
  -- saved ID before the native store. Needs the saved-ID map. Default off.
  if cfg.native_gate_find_order then
   local modes={observe=1,sort=2}
   assert(snapshot and modes[cfg.native_gate_find_order] and cfg.native_gate_world_identities and gate.find_order and
    gate.find_order_row and gate.find_order(modes[cfg.native_gate_find_order])==1,'find order refused')
   log('FIND_ORDER_ARMED mode='..cfg.native_gate_find_order)
  end
  -- Lockstep piloting prototype: one saved ship per slot is driven on every
  -- client from that slot's per-tick record through its own native Steering
  -- velocity requests. 'script' records come from (slot, tick); 'wire' records
  -- are captured locally, proposed and staged from s.inputs (needs the input
  -- wire). Optional local suppression parks native player controls. Default off.
  if cfg.native_gate_pilot_inputs then
   local slots,source,window=cfg.native_gate_pilot_slots,cfg.native_gate_pilot_source,cfg.native_gate_pilot_window or {30,600}
   local delay=source=='wire' and (cfg.native_gate_pilot_delay or 3) or 0
   local suppress=cfg.native_gate_pilot_suppress_local
   assert(snapshot and cfg.native_gate_pilot_inputs==true and cfg.native_gate_serial_pathing and cfg.native_gate_world_identities and
    not cfg.native_gate_player_request_observer and type(slots)=='table' and #slots>=1 and #slots<=4 and
    (source=='script' or (source=='wire' and type(gate.propose)=='function')) and type(window)=='table' and #window==2 and
    (suppress==nil or suppress==true) and (cfg.native_gate_pilot_capture==nil or cfg.native_gate_pilot_capture==true) and
    gate.pilot_inputs and gate.pilot_inputs(table.concat(slots,','),source,delay,window[1],window[2],suppress and 1 or 0)==1,'pilot inputs refused')
   if source=='wire' then
    -- Records ride the pilot section: apply keeps every slot's admitted record
    -- for pilot_frame to stage, the proposal is this tick's local capture.
    assert(wire3,'pilot wire needs protocol 3')
    arm_pilot_section()
   end
   log('PILOT_INPUTS_ARMED source='..source..' delay='..delay..' window='..window[1]..'-'..window[2]..' slots='..table.concat(slots,',')..' suppress='..(suppress and 1 or 0))
   -- Per-client options: a value applies to every client, a table is indexed by
   -- client slot (1..clients); a missing entry means off for that client.
   local slot=tonumber(cfg.slot:sub(2))
   local function own(value) if type(value)=='table' then return value[slot] end return value end
   -- Device sampler (parked controls): 'kbm' | 'xinput' | 'both', XInput pad
   -- 0..3, invert = list of axis names. The record stays v1.
   local device=own(cfg.native_gate_pilot_device)
   if cfg.native_gate_pilot_device~=nil then
    local kinds={kbm=true,xinput=true,both=true}
    local pad=own(cfg.native_gate_pilot_pad) or 0
    local bits={pitch=1,yaw=2,roll=4,strafe_x=8,strafe_y=16}
    local invert,names=0,{}
    local list=own(cfg.native_gate_pilot_invert) or {}
    assert(type(list)=='table','pilot invert must be a list of axis names')
    for _,name in ipairs(list) do
     assert(bits[name] and invert%(bits[name]*2)<bits[name],'pilot invert axis invalid or repeated')
     invert=invert+bits[name];names[#names+1]=name
    end
    assert((device==nil or device==false or kinds[device]) and type(pad)=='number' and pad%1==0 and pad>=0 and pad<=3,'pilot device refused')
    if device then
     local ok,xinput=gate.pilot_device(device,pad,invert)
     assert(ok==1,'pilot device refused')
     log('PILOT_DEVICE_ARMED kind='..device..' pad='..pad..' invert='..(#names>0 and table.concat(names,'+') or 'none')..' xinput='..tostring(xinput))
    end
   end
   -- Capture source (per client): 'device' = the device sampler above while
   -- controls stay parked; 'native' = X4's own controls (lockstep_pilot_native.h,
   -- stop reason 113): from start-2 the stock input processing feeds the capture,
   -- flight control stays off, flight actions that change ship state are refused
   -- (PILOT_NATIVE_REFUSED). native_gate_pilot_native_requests=false keeps the
   -- channels for the occupied ship too. Absent = the earlier behaviour.
   if cfg.native_gate_pilot_capture_source~=nil then
    local capture_source,requests=own(cfg.native_gate_pilot_capture_source),cfg.native_gate_pilot_native_requests
    assert(capture_source==nil or capture_source==false or capture_source=='device' or capture_source=='native' or
     (capture_source=='stock' and cfg.native_gate_pilot_stock==true),'pilot capture source refused')
    assert(capture_source~='device' or device,'pilot capture source device needs native_gate_pilot_device for this client')
    if capture_source=='native' then
     assert(not device and suppress==true and (cfg.native_gate_pilot_capture==true or source=='wire') and gate.pilot_native and
      (requests==nil or requests==true or requests==false) and gate.pilot_native(requests==false and 0 or 1)==1,'pilot native controls refused')
     log('PILOT_NATIVE_ARMED slot='..slot..' requests='..(requests==false and 0 or 1))
    end
   end
   -- Stock piloting (lockstep_pilot_stock.h, stop reason 155; every client): the stock slots fly
   -- with X4's own player control path on every client from their recorded dispatch streams.
   -- Slot 1, the ship every client sits in, always; native_gate_pilot_stock_slots = 'all' or a
   -- list adds slots 2..N, each with its own player and PlayerController in a slot scope
   -- (lockstep_pilot_slots.h, stop reason 161). A stock slot's client captures (capture source
   -- 'stock', X4's own input, no device); every other client samples its own slot with the device.
   if cfg.native_gate_pilot_stock~=nil then
    local capture_source=own(cfg.native_gate_pilot_capture_source)
    local capture=capture_source=='stock'
    local list,slots,mask=cfg.native_gate_pilot_stock_slots,{},0
    if list==nil then slots[1]=true
    elseif list=='all' then for s=1,#cfg.native_gate_pilot_slots do slots[s]=true end
    else
     assert(type(list)=='table','pilot stock slots must be all or a list of slots')
     for _,s in ipairs(list) do
      assert(type(s)=='number' and s%1==0 and s>=1 and s<=#cfg.native_gate_pilot_slots and not slots[s],'pilot stock slots invalid')
      slots[s]=true
     end
    end
    assert(slots[1],'pilot stock slots must hold slot 1')
    for s in pairs(slots) do mask=mask+2^(s-1) end
    -- A stock slot has no pilot record: its client's prediction stays off, pilot fire reads the stock
    -- record's trigger (hit claims work on the slot ships, not on records; the stock section is packed first).
    assert(cfg.native_gate_pilot_stock==true,'pilot stock refused: native_gate_pilot_stock must be true')
    assert(source=='wire','pilot stock refused: pilot source must be wire')
    assert(suppress==true,'pilot stock refused: needs native_gate_pilot_suppress_local')
    assert(capture==(slots[slot]==true),'pilot stock refused: capture source stock exactly on the stock slots, client slot '..tostring(slot))
    assert(not (capture and own(cfg.native_gate_pilot_predict)),'pilot stock refused: excludes prediction on a capturing client')
    assert(capture and not device or (not capture and device and capture_source=='device'),
     'pilot stock refused: no device on a capturing client, a device on the others')
    assert(gate.pilot_stock,'pilot stock refused: gate without pilot_stock')
    local armed,why=gate.pilot_stock(capture and 1 or 0,mask)
    assert(armed==1,'pilot stock refused: '..tostring(why))
    pilot_view.stock=capture and 'capture' or 'replay';pilot_view.stock_slots=slots
    -- The script: one for every capturing client ('demo', 'demo2'..'demo4', 'aim' or an event list) or one
    -- per client ({'demo','demo2'}, false for none).
    local script=cfg.native_gate_pilot_stock_script
    if type(script)=='table' and type(script[1])~='table' then script=script[slot] end
    if capture then pilot_view.stock_script=pilot_view.stock_plan(script,window[1]) end
    pilot_view.arm_stock()
    log('PILOT_STOCK_ARMED slot='..slot..' capture='..(capture and 1 or 0)..' slots='..mask..' script='..tostring(capture and script and
     (type(script)=='string' and script or 'list') or 'none'))
   end
   -- Presentation (lockstep_pilot_present.h, stop reason 162; every client of a stock slots session):
   -- client k's stock camera, HUD and UI present slot k's ship as the player, X4's own code in a
   -- presentation scope that refuses simulation writes. Slot 1's client presents as X4 always does.
   -- true: camera, UI and the HUD's anchor; 'camera': the anchor stays on slot 1's cockpit.
   if cfg.native_gate_pilot_present~=nil then
    assert(cfg.native_gate_pilot_present==true or cfg.native_gate_pilot_present=='camera','pilot present refused: native_gate_pilot_present must be true or camera')
    assert(cfg.native_gate_pilot_stock==true and cfg.native_gate_pilot_stock_slots~=nil,'pilot present refused: needs stock slots')
    assert(not own(cfg.native_gate_pilot_camera_follow) and not cfg.native_gate_camera_anchor,'pilot present refused: excludes camera follow and anchor')
    assert(gate.pilot_present,'pilot present refused: gate without pilot_present')
    local armed,why=gate.pilot_present(cfg.native_gate_pilot_present==true and 1 or 0)
    assert(armed==1,'pilot present refused: '..tostring(why))
    log('PILOT_PRESENT_ARMED slot='..slot..' mode='..tostring(cfg.native_gate_pilot_present))
   end
   -- A slot ship's death (lockstep_pilot_death.h, stop reason 163; every client of a stock session): the slot
   -- is dead on the same tick everywhere, the session goes on. native_gate_pilot_respawn: the delay in ticks
   -- until a new ship of the slot's macro at its start (absent: the slot stays dead).
   if cfg.native_gate_pilot_death~=nil then
    assert(cfg.native_gate_pilot_death==true,'pilot death refused: native_gate_pilot_death must be true')
    assert(cfg.native_gate_pilot_stock==true,'pilot death refused: needs native_gate_pilot_stock')
    local respawn=cfg.native_gate_pilot_respawn
    assert(respawn==nil or (type(respawn)=='number' and respawn%1==0 and respawn>=1),'pilot respawn refused: native_gate_pilot_respawn must be a whole number of ticks')
    assert(gate.pilot_death,'pilot death refused: gate without pilot_death')
    -- native_gate_pilot_death_suit: slots 2..N wait in a spacesuit of their own, as X4's own player does (presented on their client).
    local suit=cfg.native_gate_pilot_death_suit
    assert(suit==nil or (suit==true and cfg.native_gate_pilot_present~=nil and cfg.native_gate_pilot_ship_state==true),
     'pilot death suit refused: needs native_gate_pilot_present and native_gate_pilot_ship_state')
    local armed,why=gate.pilot_death(respawn or 0,suit and 1 or 0)
    assert(armed==1,'pilot death refused: '..tostring(why))
    pilot_view.death=true;pilot_view.death_arm(respawn or 0,window[1],suit)
    log('PILOT_DEATH_ARMED slot='..slot..' respawn='..tostring(respawn or 'off')..' suit='..tostring(suit==true))
   else
    assert(cfg.native_gate_pilot_respawn==nil,'pilot respawn refused: needs native_gate_pilot_death')
    assert(cfg.native_gate_pilot_death_suit==nil,'pilot death suit refused: needs native_gate_pilot_death')
   end
   if cfg.native_gate_ui_replay~=nil and cfg.native_gate_ui_replay~=false then pilot_view.arm_ui_replay(slot,window) end
   -- Camera follow: this client watches its own slot ship through the stock
   -- target view unless that ship is the occupied one. Differing cameras need
   -- the render RNG split (slot 10 draws stay off the simulation word).
   if own(cfg.native_gate_pilot_camera_follow) then
    assert(own(cfg.native_gate_pilot_camera_follow)==true and cfg.native_gate_render_rng_split==true and
     gate.pilot_camera_follow and gate.pilot_camera_follow()==1,'pilot camera anchor refused')
    pilot_camera_follow=true;log('PILOT_CAMERA_ARMED slot='..slot)
   end
   assert(cfg.native_gate_pilot_ship_state==nil or (cfg.native_gate_pilot_ship_state==true and gate.pilot_ship),'pilot ship state refused')
   assert(cfg.native_gate_pilot_block_orders==nil or cfg.native_gate_pilot_block_orders==true,'pilot order block refused')
   -- The orders UI capture wraps the same menu entries: one owner for the order menus.
   assert(not (cfg.native_gate_pilot_block_orders and cfg.native_gate_player_orders_capture),'pilot order block excludes the player orders UI capture')
   assert(cfg.native_gate_pilot_observe_native==nil or (cfg.native_gate_pilot_observe_native==true and cfg.native_gate_pilot_capture==true and
    gate.pilot_native_capture),'pilot native observation needs native_gate_pilot_capture')
   -- Presentation prediction (reports/LOCKSTEP-PREDICTION-20261029.txt), per
   -- client like the device: 'observe' (or true) logs the predicted against the
   -- later simulated pose of this client's slot ship (wire) or of every slot
   -- (script, native_gate_pilot_predict_horizon ticks ahead); 'present' also
   -- draws this client's slot ship at the predicted pose in slot 10 (needs the
   -- render RNG split). native_gate_pilot_predict_tau: display smoothing in ms.
   local predict=own(cfg.native_gate_pilot_predict)
   if predict~=nil and predict~=false then
    local mode=predict==true and 'observe' or predict
    local horizon=source=='wire' and 0 or cfg.native_gate_pilot_predict_horizon or 3
    local tau=cfg.native_gate_pilot_predict_tau or 100
    assert((mode=='observe' or mode=='present') and (mode=='observe' or cfg.native_gate_render_rng_split==true) and
     (source=='script' or cfg.native_gate_pilot_predict_horizon==nil) and type(horizon)=='number' and horizon%1==0 and
     horizon>=0 and horizon<=30 and (source=='wire' or horizon>=1) and type(tau)=='number' and tau%1==0 and tau>=0 and tau<=2000 and
     (cfg.native_gate_pilot_predict_raw==nil or type(cfg.native_gate_pilot_predict_raw)=='boolean') and
     gate.pilot_predict and gate.pilot_predict(mode,horizon,tau)==1,'pilot predict refused')
    pilot_view.predict,pilot_view.raw=mode,cfg.native_gate_pilot_predict_raw==true
    log('PILOT_PREDICT_ARMED slot='..slot..' mode='..mode..' horizon='..(horizon>0 and horizon or 'delay')..' tau_ms='..tau)
   end
   -- Native hit claims (reports/LOCKSTEP-HIT-CLAIMS-NATIVE-20261029.txt), every
   -- client: true = 'shooter' (player versus player decided by the shooter),
   -- 'victim' or 'first'; 'carry' is the Lua test carrier (registered at load).
   -- Needs the wire source (the 'hits' section rides with the pilot section)
   -- and camera-independent bullet tests.
   if cfg.native_gate_hit_claims~=nil and cfg.native_gate_hit_claims~=false and cfg.native_gate_hit_claims~='carry' then
    local policy=cfg.native_gate_hit_claims==true and 'shooter' or cfg.native_gate_hit_claims
    assert((policy=='shooter' or policy=='victim' or policy=='first') and source=='wire' and
     (cfg.native_gate_bullet_cadence==true or cfg.native_gate_bullet_cadence=='every') and gate.hit_claims and
     gate.hit_claims_admit and gate.hit_claims_resolve and gate.hit_claims_proposal and gate.hit_claims(policy)==1,'hit claims refused')
    pilot_view.hits=policy
    log('HIT_CLAIMS_ARMED policy='..policy)
   end
   -- Pilot firing (every client): 'record' (or true) fires the slot ships' primary
   -- guns from the record's trigger bit, 'script' in fixed bursts. The launch draws
   -- need the all-ship RNG keys and the creation-frame hit test the 'every' cadence.
   if cfg.native_gate_pilot_fire~=nil and cfg.native_gate_pilot_fire~=false then
    local fire=cfg.native_gate_pilot_fire==true and 'record' or cfg.native_gate_pilot_fire
    assert((fire=='record' or fire=='script') and cfg.native_gate_actor_rng_all==true and
     (cfg.native_gate_bullet_cadence==true or cfg.native_gate_bullet_cadence=='every') and
     gate.pilot_fire and gate.pilot_fire_frame and gate.pilot_fire(fire,X4LAN_PILOT_FIRE_MASK(cfg.native_gate_pilot_fire_slots,fire,#cfg.native_gate_pilot_slots))==1,'pilot fire refused')
    pilot_view.fire=fire
    log('PILOT_FIRE_ARMED mode='..fire)
   end
  end
  -- Native hit claims belong to pilot slots (owners, records): refused without them.
  assert(cfg.native_gate_pilot_inputs or cfg.native_gate_hit_claims==nil or cfg.native_gate_hit_claims==false or
   cfg.native_gate_hit_claims=='carry','hit claims refused: native claims need native_gate_pilot_inputs')
  -- Region updates: a hazard DamageField draws its next low-attention pass time
  -- from the shared sim-thread word mid-walk. 'key' runs every Region::Move on a
  -- stream keyed by tick and saved region ID; 'observe' derives the same keys
  -- and pass rows without writing. Default off.
  if cfg.native_gate_region_rng~=nil then
   local mode=({key=1,observe=0})[cfg.native_gate_region_rng]
   assert(snapshot and mode and cfg.native_gate_world_identities and gate.region_rng and gate.region_rng_row and
    gate.region_rng(mode)==1,'region RNG refused')
   log('REGION_RNG_ARMED mode='..cfg.native_gate_region_rng)
  end
  -- Ship loadouts: ShipGenerator::Generate gets the caller's explicit generator
  -- state (job, god entry, create_ship seed) but the loadout draws the calling
  -- thread's TLS word, which differs per client during the load. 'key' runs
  -- each Generate on a word keyed by that state; 'observe' records the same
  -- keys without writing. Armed before loading so load spawns are covered.
  if cfg.native_gate_spawn_rng~=nil then
   local mode=({key=1,observe=0})[cfg.native_gate_spawn_rng]
   assert(snapshot and mode and gate.spawn_rng and gate.spawn_rng_status and gate.spawn_rng_row and
    gate.spawn_rng(mode)==1,'spawn RNG refused')
   log('SPAWN_RNG_ARMED mode='..cfg.native_gate_spawn_rng)
  end
  if cfg.native_gate_npc_pilot then assert(gate.npc_pilot_watch(cfg.native_gate_npc_pilot)==1,'NPC pilot watch refused') end
  armed=true;log('ARMED slot='..cfg.slot..' hash_scope='..cfg.native_gate_hash..(wire3 and ' wire=3 clients='..clients or '')..
   ((cfg.native_gate_lookahead or 0)>0 and ' lookahead='..cfg.native_gate_lookahead or ''))
  local jit_ok,jit_on=pcall(function() return jit.status() end)
  log('LUA_RUNTIME jit='..(jit_ok and (jit_on and 'on' or 'off') or 'unknown')..' digest='..(gate.sha256 and 'native' or 'lua'))
  if not snapshot then log('RNG tick=0 state='..tostring(assert(gate.poll()).rng));return end
 end
 local s=gate.poll()
 if not s and snapshot then return end -- Loading UI is not the simulation owner.
 assert(s,'gate poll unavailable')
 if profile then profile.begin() end
 -- Status stride: this frame's output belongs to tick s.issued; a stop first writes the held ticks.
 if s.active==0 then ticklog.flush() else ticklog.open((not snapshot or (s.boot_phase>=3 and s.issued~=s.report_tick)) and tonumber(s.issued)) end
 if diagnostic and s.active==0 and not assert(X4LAN_NATIVE_DIAGNOSTIC,'diagnostic adapter missing').drain(gate,false) then return end
 if cfg.native_gate_player_transition_observer then assert(X4LAN_NATIVE_TRANSITION,'transition adapter missing').drain(gate,s.active==0) end
 if cfg.native_gate_player_origin_token then assert(X4LAN_NATIVE_ORIGIN,'origin adapter missing').drain(gate,s.active==0) end
 drain_registry_md(s)
 if creation_trace then creation_trace(s.active==0) end
 if creation_rng then creation_rng(s.active==0) end
 if registry_creation then registry_creation(s.active==0) end
 if cfg.native_gate_rng_schema then assert(s.rng_schema==cfg.native_gate_rng_schema,'native RNG schema mismatch') end
 drain_traces()
 if cfg.native_gate_snapshot_fixture and s.active==0 then
  log('SNAPSHOT_BIND_STOP bound='..s.snapshot_bound..' prepared='..s.snapshot_prepared..' enrolled='..s.snapshot_enrolled..' failures='..s.snapshot_failures)
  log('SNAPSHOT_BOUNDARY '..s.snapshot_boundary)
 end
 if s.active==0 then
  done=true
  if cfg.native_gate_registry_md_trace then
   for i=1,s.registry_pending_count do log('REGISTRY_UPDATE_FIRST '..assert(gate.registry_update(i),'native pending update row missing')) end
   for i=1,s.registry_pending_count do log('REGISTRY_UPDATE_ORIGIN '..assert(gate.registry_origin(i),'native pending origin row missing')) end
   for i=1,s.registry_pending_count do log('REGISTRY_ORIGIN_IDENTITY '..assert(gate.registry_origin_identity(i),'native pending origin identity row missing')) end
   log('REGISTRY_ORIGIN_STOP schema=1 count='..s.registry_pending_count..' enabled='..s.registry_origin_enabled..' closed='..s.registry_origin_closed..' failures='..s.registry_origin_failures..' open='..s.registry_origin_open..' restore_open='..s.registry_origin_restore_open..' peak='..s.registry_origin_peak)
   log('REGISTRY_MD_STOP schema=2 count='..s.registry_md_count..' order='..s.registry_md_order..' updates='..s.registry_pending_count..' failures='..s.registry_md_failures..' restored='..s.registry_md_restored..' open='..s.registry_md_open..' restore_open='..s.registry_md_restore_open..' peak='..s.registry_md_peak)
  end
  log(string.format('STOP slot=%s completed=%d reason=%d restored=%d wait_game_delta=%.9g max_dt_error=%.9g',
   cfg.slot,tonumber(s.completed),s.reason,s.restored,s.wait_game_delta,s.max_dt_error))
  log('PATH joins='..s.path_joins..' restored='..s.path_restored..' rng_seeds='..s.rng_seeds)
  if (cfg.native_gate_lookahead or 0)>0 then log('LOOKAHEAD_STOP '..tostring(gate.lookahead_status())) end
  if cfg.native_gate_player_observer then assert(X4LAN_NATIVE_PLAYER_TRACE,'player observer adapter missing')(gate,true) end
  if cfg.native_gate_player_probe=='agreed-fire' then assert(X4LAN_NATIVE_PLAYER_FIRE_TRACE,'player firing adapter missing')(gate,true) end
  if (tonumber(s.registry_count) or 0)>0 then
   log('SHIP_REGISTRY_STOP count='..s.registry_count..' failures='..s.registry_failures..' data='..s.registry_evidence)
  end
  if cfg.native_gate_rng_schema then log('RNG_SCHEMA version='..s.rng_schema) end
  if snapshot then log('BOOT phase='..s.boot_phase..' dt_restored='..s.dt_restored) end
  if cfg.native_gate_flight_diag then log('FLIGHT_STOP calls='..s.flight_calls..' restored='..s.flight_restored..' failures='..s.flight_failures..' refreshes='..s.flight_refreshes) end
  if cfg.native_gate_steering_init then
   log('STEERING_INIT count='..s.flight_inits)
   log('STEERING_INIT_TOTALS data='..s.flight_init_totals)
   log('STEERING_CONSTRUCT calls='..s.flight_ctor_calls..' restored='..s.flight_ctor_restored..' registrations='..s.flight_registration_inits)
  end
  if cfg.native_gate_shield_refresh then log('SHIELD_REFRESH calls='..s.shield_refresh_calls..' failures='..s.shield_refresh_failures) end
  if cfg.native_gate_spline_diag then
   log('SPLINE_STOP calls='..s.spline_calls..' restored='..s.spline_restored..' failures='..s.spline_failures..' refreshes='..s.spline_refreshes)
   if cfg.native_gate_spline_join then log('SPLINE_JOIN requests='..s.spline_joins..' waits='..s.spline_waits..' timeouts='..s.spline_timeouts) end
   for _,i in ipairs(registered_ships(s.registry_manifest)) do log('SPLINE id='..i..' final=1 '..assert(gate.spline(i),'final spline trace missing')) end
  end
  if cfg.native_gate_ai_diag then log('AI_STOP calls='..s.ai_calls..' restored='..s.ai_restored..' failures='..s.ai_failures) end
  if cfg.native_gate_radar_diag then log('RADAR_STOP calls='..s.radar_calls..' restored='..s.radar_restored..' failures='..s.radar_failures) end
  if cfg.native_gate_interrupt_rng then log('INTERRUPT_SEEDS count='..s.interrupt_seeds) end
  if cfg.native_gate_interrupt_diag then log('INTERRUPT_STOP calls='..s.interrupt_calls..' restored='..s.interrupt_restored..' failures='..s.interrupt_failures) end
  if cfg.native_gate_interrupt_diag then log('INTERRUPT_STEP_STOP calls='..s.interrupt_step_calls..' restored='..s.interrupt_step_restored..' failures='..s.interrupt_failures) end
  if cfg.native_gate_interrupt_diag and gate.interrupt_faults then log('INTERRUPT_FAULTS_STOP '..tostring(gate.interrupt_faults())) end
  if gate.observer_status then log('OBSERVERS_STOP '..tostring(gate.observer_status())) end
  if gate.flight_faults then log('FLIGHT_FAULTS_STOP '..tostring(gate.flight_faults())) end
  if cfg.native_gate_build_rows then log('BUILD_ROWS_STOP '..tostring(gate.build_rows_status())) end
  if cfg.native_gate_nested_actions then log('NESTED_ACTION_STOP calls='..s.nested_action_calls..' restored='..s.nested_action_restored..' failures='..s.interrupt_failures) end
  if cfg.native_gate_npc_lifecycle then log('NPC_ACTION_STOP calls='..s.npc_action_calls..' restored='..s.nested_action_restored..' failures='..s.interrupt_failures) end
  if cfg.native_gate_npc_trigger then
   log('NPC_TRIGGER_STOP calls='..s.npc_trigger_calls..' causes='..s.npc_cause_calls..' restored='..s.npc_exec_restored..' failures='..s.interrupt_failures..' identity=fixture_trigger_v1')
   log('NPC_TRIGGER_COVERAGE seen='..s.npc_trigger_seen..' foreign_thread='..s.npc_trigger_foreign_thread..' unsupported='..s.npc_trigger_unsupported)
   if cfg.native_gate_npc_provenance then
    log('NPC_PROVENANCE_STOP calls='..s.npc_provenance_calls..' restored='..s.npc_exec_restored..' failures='..s.npc_provenance_failures..' overflows='..s.npc_provenance_overflows..' scope='..(cfg.native_gate_npc_provenance_origin and 'windows_local_observation_v2' or 'windows_local_observation_v1'))
    log('NPC_PROVENANCE_COVERAGE unresolved='..s.npc_provenance_unresolved..' unowned='..s.npc_provenance_unowned)
   end
  end
  if cfg.native_gate_npc_pilot then
   log('NPC_PILOT_STOP calls='..s.npc_pilot_calls..' restored='..s.interrupt_restored..' failures='..s.npc_pilot_failures..' overflows='..s.npc_pilot_overflows..' scope='..(cfg.native_gate_npc_pilot_origin and 'selected_saved_pilot_v2' or 'selected_saved_pilot_v1'))
   log('NPC_PILOT_COVERAGE foreign='..s.npc_pilot_foreign..' unmapped='..s.npc_pilot_unmapped..' unreadable='..s.npc_pilot_unreadable..' retired='..s.npc_pilot_retired..' after_unresolved='..s.npc_pilot_after_unresolved)
  end
  if cfg.native_gate_npc_rng then log('NPC_EXEC_STOP calls='..s.npc_exec_calls..' restored='..s.npc_exec_restored..' failures='..s.interrupt_failures..' identity=ordered_invocation_v1') end
  if cfg.native_gate_npc_deadline then
   log('NPC_READY_STOP calls='..s.npc_ready_calls..' restored='..s.npc_ready_restored..' failures='..s.interrupt_failures)
   log('NPC_READY_COVERAGE seen='..s.npc_ready_seen..' foreign='..s.npc_ready_foreign..' foreign_thread='..s.npc_ready_foreign_thread)
   log('NPC_DEADLINE_STOP calls='..s.npc_deadline_calls..' restored='..s.npc_deadline_restored..' failures='..s.interrupt_failures)
   log('NPC_DEADLINE_COVERAGE seen='..s.npc_deadline_seen..' foreign='..s.npc_deadline_foreign..' foreign_thread='..s.npc_deadline_foreign_thread)
  end
  if cfg.native_gate_npc_diag then
   log('NPC_ENQUEUE_STOP calls='..s.npc_enqueue_calls..' restored='..s.npc_enqueue_restored..' failures='..s.interrupt_failures)
   log('NPC_SPEAK_STOP calls='..s.npc_speak_calls..' restored='..s.npc_speak_restored..' failures='..s.interrupt_failures)
   log('NPC_SPEAK_COVERAGE seen='..s.npc_speak_seen..' foreign_thread='..s.npc_speak_foreign_thread..' unowned='..s.npc_speak_unowned)
   if cfg.native_gate_npc_finish then
    log('NPC_FINISH_STOP calls='..s.npc_finish_calls..' restored='..s.npc_finish_restored..' failures='..s.interrupt_failures)
    log('NPC_FINISH_COVERAGE seen='..s.npc_finish_seen..' foreign_thread='..s.npc_finish_foreign_thread..' unowned='..s.npc_finish_unowned)
   end
  end
  if cfg.native_gate_world_identities then log('WORLD_IDENTITY_STOP calls='..s.world_identity_calls..' count='..s.world_identity_count..' late='..s.world_identity_late..' restored='..s.world_identity_restored..' failures='..s.world_identity_failures) end
  if cfg.native_gate_boot_rng_anchor then log('BOOT_RNG_ANCHOR_STOP '..tostring(gate.boot_rng_anchor_status())) end
  if cfg.native_gate_path_determinism then log('PATH_DETERMINISM_STOP '..tostring(gate.path_determinism_status())) end
  if cfg.native_gate_virtual_players then
   log('VPLAYER_STOP '..tostring(gate.virtual_players_status()))
   log('VPLAYER_REGIONS '..tostring(gate.virtual_players_regions()))
   log('VPLAYER_UNDECLARED '..tostring(gate.virtual_players_undeclared()))
   if gate.virtual_players_neighbours then log('VPLAYER_NEIGHBOURS '..tostring(gate.virtual_players_neighbours())) end
   if gate.virtual_players_hashes then log('VPLAYER_HASHES '..tostring(gate.virtual_players_hashes())) end
   if gate.virtual_players_raw then log('VPLAYER_RAW class=config '..tostring(gate.virtual_players_raw('config'))..' option='..tostring(gate.virtual_players_raw('option'))) end
  end
  if cfg.native_gate_travel_determinism then log('TRAVEL_DETERMINISM_STOP '..tostring(gate.travel_determinism_status())) end
  if cfg.native_gate_travel_snapshot then log('TRAVEL_SNAPSHOT_STOP '..tostring(gate.travel_snapshot_status())) end
  if cfg.native_gate_path_budget then log('PATH_BUDGET_STOP '..tostring(gate.path_budget_status())) end
  if cfg.native_gate_render_rng_split then log('RENDER_RNG_SPLIT_STOP '..tostring(gate.render_rng_split_status())) end
  if cfg.native_gate_bullet_cadence then log('BULLET_CADENCE_STOP '..tostring(gate.bullet_cadence_status())) end
  if cfg.native_gate_speak_timer then log('SPEAK_TIMER_STOP '..tostring(gate.speak_timer_status())) end
  if cfg.native_gate_cutscene_ready then log('CUTSCENE_READY_STOP '..tostring(gate.cutscene_ready_status())) end
  if cfg.native_gate_nav_cadence then log('NAV_CADENCE_STOP '..tostring(gate.nav_cadence_status())) end
  if cfg.native_gate_nav_player then log('NAV_PLAYER_STOP '..tostring(gate.nav_player_status())) end
  if cfg.native_gate_command_order then log('COMMAND_ORDER_STOP '..tostring(gate.command_order_status())) end
  if cfg.native_gate_ship_sound_rng then log('SHIP_SOUND_RNG_STOP '..tostring(gate.ship_sound_rng_status())) end
  if cfg.native_gate_player_timer then log('PLAYER_TIMER_STOP '..tostring(gate.player_timer_status())) end
  if cfg.native_gate_warp_rng then log('WARP_RNG_STOP '..tostring(gate.warp_rng_status())) end
  if cfg.native_gate_transition_rows then log('TRANSITION_ROWS_STOP '..tostring(gate.transition_rows_status())) end
  if cfg.native_gate_tempzone_order then log('TEMPZONE_ORDER_STOP '..tostring(gate.tempzone_order_status())) end
  if cfg.native_gate_draw_watch or cfg.native_gate_draw_watch_frame then log('DRAW_WATCH_STOP '..tostring(gate.draw_watch_status())) end
  if cfg.native_gate_attention_all then log('ATTENTION_ALL_STOP '..tostring(gate.attention_all_status())) end
  if cfg.native_gate_view_points then log('VIEW_POINTS_STOP '..tostring(gate.view_points_status())) end
  if cfg.native_gate_mass_traffic then log('MASS_TRAFFIC_STOP '..tostring(gate.mass_traffic_status())) end
  if cfg.native_gate_region_pass then log('REGION_PASS_STOP '..tostring(gate.region_pass_status())) end
  if cfg.native_gate_weapon_fire_rng then log('WEAPON_FIRE_RNG_STOP '..tostring(gate.weapon_fire_rng_status())) end
  if cfg.native_gate_engine_refresh_rng then log('ENGINE_REFRESH_RNG_STOP '..tostring(gate.engine_refresh_rng_status())) end
  if cfg.native_gate_portable_math then log('PORTABLE_MATH_STOP '..tostring(gate.portable_math_status())) end
  if cfg.native_gate_exact_reciprocal then log('EXACT_RECIPROCAL_STOP '..tostring(gate.exact_reciprocal_status())) end
  if cfg.native_gate_fp_env then log('FP_ENV_STOP '..tostring(gate.fp_env_status())) end
  if cfg.native_gate_decimal then log('DECIMAL_STOP '..tostring(gate.decimal_status())) end
  if cfg.native_gate_motion_watch then log('MOTION_WATCH_STOP '..tostring(gate.motion_watch_status())) end
  if cfg.native_gate_physics_trace~=nil then log('PHYSICS_TRACE_STOP '..tostring(gate.physics_trace_status())) end
  if cfg.native_gate_body_order then log('BODY_ORDER_STOP '..tostring(gate.body_order_status())) end
  if cfg.native_gate_shape_order then log('SHAPE_ORDER_STOP '..tostring(gate.shape_order_status())) end
  if cfg.native_gate_nav_octree then log('NAV_OCTREE_STOP '..tostring(gate.nav_octree_status())) end
  if cfg.native_gate_nav_octree_sites then
   log('NAV_OCTREE_SITES_STOP '..tostring(gate.nav_octree_sites_status()))
   for i=0,511 do local row=gate.nav_octree_site_row(i);if not row then break end;if not row:find(' free$') then log('NAV_OCTREE_SITE at=stop '..row) end end
  end
  if cfg.native_gate_region_camera or cfg.native_gate_region_fields or cfg.native_gate_mass_traffic_camera or cfg.native_gate_effect_camera or cfg.native_gate_effect_rows or cfg.native_gate_highway_camera or cfg.native_gate_physics_scenes or cfg.native_gate_camera_context or cfg.native_gate_shape_refresh or cfg.native_gate_mining_camera or
   cfg.native_gate_xs_camera or cfg.native_gate_camera_probe or cfg.native_gate_highway_rows or cfg.native_gate_highway_speed or cfg.native_gate_sound_rng or cfg.native_gate_traffic_views or cfg.native_gate_traffic_rows then
   log('REGION_CAMERA_STOP '..tostring(gate.region_camera_status()))
  end
  if camera_perturb then log('CAMERA_PERTURB_STOP '..tostring(gate.camera_perturb_status())) end
  if camera_offset then
   if camera_offset.follow then log('CAMERA_ANCHOR_STOP '..tostring(gate.camera_anchor_status()))
   else log('CAMERA_OFFSET_STOP '..tostring(gate.camera_offset_status())) end
  end
  if cfg.native_gate_pilot_inputs then log('PILOT_INPUTS_STOP '..tostring(gate.pilot_inputs_status())) end
  if pilot_camera_follow then log('PILOT_CAMERA_STOP '..tostring(gate.pilot_camera_status())) end
  if pilot_view.predict then log('PILOT_PREDICT_STOP '..tostring(gate.pilot_predict_status())) end
  if pilot_view.hits then log('HIT_CLAIMS_STOP '..tostring(gate.hit_claims_status())) end
  if pilot_view.fire then log('PILOT_FIRE_STOP '..tostring(gate.pilot_fire_status())) end
  if cfg.native_gate_gravidar_key_all or cfg.native_gate_gravidar_time_sync then log('GRAVIDAR_KEY_STOP '..tostring(gate.gravidar_key_status())) end
  if cfg.native_gate_gravidar_watch or cfg.native_gate_speech_game_time or cfg.native_gate_physics_trace~=nil or cfg.native_gate_body_order or
     cfg.native_gate_shape_order or cfg.native_gate_nav_octree or cfg.native_gate_event_rng or cfg.native_gate_ui_cargo or cfg.native_gate_supply_rows or cfg.native_gate_ui_lazy or cfg.native_gate_supply_wants~=nil or cfg.native_gate_loadout_order or cfg.native_gate_order_canon or cfg.native_gate_address_order or cfg.native_gate_pick_order or cfg.native_gate_macro_arena then
   drain_gravidar_watch(true)
  end
  if cfg.native_gate_gravidar_watch then log('GRAVIDAR_WATCH_STOP '..tostring(gate.gravidar_watch_status())) end
  if cfg.native_gate_gravidar_posts then log('GRAVIDAR_POSTS_STOP '..tostring(gate.gravidar_posts_status())) end
  if cfg.native_gate_gravidar_wide then log('GRAVIDAR_WIDE_STOP '..tostring(gate.gravidar_wide_status())) end
  if cfg.native_gate_speech_game_time then log('SPEECH_TIME_STOP '..tostring(gate.speech_time_status())) end
  if cfg.native_gate_ai_schedule then
   local fallback=gate.ai_schedule_fallbacks and gate.ai_schedule_fallbacks()
   if fallback then log('AI_SCHEDULE_FALLBACK stop=1 '..fallback) end
   log('AI_SCHEDULE_STOP '..tostring(gate.ai_schedule_status()))
  end
  if cfg.native_gate_job_rng then drain_job_rng();log('JOB_RNG_STOP '..tostring(gate.job_rng_status())) end
  if cfg.native_gate_event_order then drain_event_rows();log('EVENT_ORDER_STOP '..tostring(gate.event_order_status())) end
  if cfg.native_gate_event_rng then log('EVENT_RNG_STOP '..tostring(gate.event_rng_status())) end
  if cfg.native_gate_build_rng then log('BUILD_RNG_STOP '..tostring(gate.build_rng_status())) end
  if cfg.native_gate_ui_cargo then log('UI_CARGO_STOP '..tostring(gate.ui_cargo_status())) end
  if cfg.native_gate_supply_rows then log('SUPPLY_ROWS_STOP '..tostring(gate.supply_rows_status())) end
  if cfg.native_gate_ui_lazy then log('UI_LAZY_STOP '..tostring(gate.ui_lazy_status())) end
  if cfg.native_gate_supply_wants~=nil then log('SUPPLY_WANTS_STOP '..tostring(gate.supply_wants_status())) end
  if cfg.native_gate_loadout_order then log('LOADOUT_ORDER_STOP '..tostring(gate.loadout_order_status())) end
  if cfg.native_gate_step_trace then log('STEP_TRACE_STOP '..tostring(gate.step_trace_status())) end
  if cfg.native_gate_fp_redirect then log('FP_REDIRECT_STOP '..tostring(gate.fp_redirect_status())) end
  if cfg.native_gate_coverage then log('COVERAGE_STOP '..tostring(gate.coverage_status())) end
  if cfg.native_gate_order_canon then log('ORDER_CANON_STOP '..tostring(gate.order_canon_status())) end
  if cfg.native_gate_pick_order then log('PICK_ORDER_STOP '..tostring(gate.pick_order_status())) end
  if cfg.native_gate_macro_arena then log('MACRO_ARENA_STOP '..tostring(gate.macro_arena_status())) end
  if cfg.native_gate_address_order then log('ADDRESS_ORDER_STOP '..tostring(gate.address_order_status())) end
  if cfg.native_gate_script_rng then drain_script_rng();log('SCRIPT_RNG_STOP '..tostring(gate.script_rng_status())) end
  if cfg.native_gate_md_timed_rng then log('MD_TIMED_RNG_STOP '..tostring(gate.md_timed_rng_status())) end
  if cfg.native_gate_boot_hold then log('BOOT_HOLD_STOP '..tostring(gate.boot_hold_status())) end
  if cfg.native_gate_md_resume_rng then drain_md_resume();log('MD_RESUME_RNG_STOP '..tostring(gate.md_resume_rng_status())) end
  if cfg.native_gate_ai_wait_rng then drain_ai_wait();log('AI_WAIT_RNG_STOP '..tostring(gate.ai_wait_rng_status())) end
  if cfg.native_gate_economy_load_rng then log('ECONOMY_LOAD_RNG_STOP '..tostring(gate.economy_load_rng_status())) end
  if gate.read_cost then log('READ_COST '..tostring(gate.read_cost())) end
  if cfg.native_gate_find_order then drain_find_order();log('FIND_ORDER_STOP '..tostring(gate.find_order_status())) end
  if cfg.native_gate_region_rng~=nil then drain_region();log('REGION_RNG_STOP '..tostring(gate.region_rng_status())) end
  if cfg.native_gate_gravidar_deadlines then log('GRAVIDAR_DEADLINES_STOP '..tostring(gate.gravidar_deadlines_status())) end
  if cfg.native_gate_spawn_rng~=nil then drain_spawn();log('SPAWN_RNG_STOP '..tostring(gate.spawn_rng_status())) end
  if cfg.native_gate_created_identities then drain_created_rows();log('CREATED_IDENTITIES_STOP '..tostring(gate.created_identities_status())) end
  if cfg.native_gate_actor_rng_all then log('ACTOR_RNG_ALL_STOP '..tostring(gate.actor_rng_all_status())) end
  if cfg.native_gate_world_hash_stride then log('WORLD_HASH_STOP scans='..s.world_hash_scans..' failures='..s.world_hash_failures) end
  if cfg.native_gate_world_trace_ticks then
   drain_world_trace();log('WORLD_TRACE_STOP '..assert(gate.world_trace_status(),'world trace status unavailable'))
   if cfg.native_gate_world_queue_trace then log('WORLD_QUEUE_STOP '..assert(gate.world_queue_trace_status(),'world queue status unavailable')) end
   if cfg.native_gate_world_wait_manifest then log('WORLD_WAIT_STOP '..assert(gate.world_wait_trace_status(),'world wait status unavailable')) end
   if cfg.native_gate_world_wait_rng_manifest then log('WORLD_WAIT_RNG_STOP '..assert(gate.world_wait_rng_status(),'world Wait RNG status unavailable')) end
   if cfg.native_gate_world_action_manifest then log('WORLD_ACTION_STOP '..assert(gate.world_action_trace_status(),'world action status unavailable')) end
   if cfg.native_gate_world_values then log('WORLD_UNKNOWN_IDENTITY_STOP '..assert(gate.world_values_unknown_identity_status()));log('WORLD_VALUES_STOP '..assert(gate.world_values_status(),'world typed output status unavailable')) end
   if cfg.native_gate_world_position then log('WORLD_POSITION_STOP '..assert(gate.world_position_status(),'world position status unavailable')) end
   if cfg.native_gate_world_safe_position then log('WORLD_SAFE_POSITION_STOP '..assert(gate.world_safe_position_status(),'world safe position status unavailable')) end
   if cfg.native_gate_world_position_rng_manifest then log('WORLD_POSITION_RNG_STOP '..assert(gate.world_position_rng_status(),'world position RNG status unavailable')) end
   if cfg.native_gate_world_find_rng_manifest then log('WORLD_FIND_STOP '..assert(gate.world_find_rng_status(),'world FindComponent RNG status unavailable')) end
  end
  if cfg.native_gate_damage_diag then log('DAMAGE_STOP calls='..s.damage_calls..' restored='..s.damage_restored..' failures='..s.damage_failures) end
  if cfg.native_gate_contact_diag then log('CONTACT_STOP calls='..s.contact_calls..' restored='..s.contact_restored..' failures='..s.contact_failures) end
  if cfg.native_gate_contact_properties then log('CONTACT_PROPERTIES_STOP calls='..s.contact_properties_calls..' restored='..s.contact_properties_restored..' failures='..s.contact_failures) end
  if cfg.native_gate_radar_serial then
   log('GRAVIDAR_STOP frames='..s.gravidar_frames..' waits='..s.gravidar_waits..' timeouts='..s.gravidar_timeouts..' calls='..s.gravidar_calls..' restored='..s.gravidar_restored..' failures='..s.gravidar_failures)
   if cfg.native_gate_radar_initial_refresh then log('GRAVIDAR_INIT count='..s.gravidar_inits) end
   for _,i in ipairs(registered_ships(s.registry_manifest)) do log('GRAVIDAR_UNIT id='..i..' final=1 '..assert(gate.gravidar(i))) end
  end
  if cfg.native_gate_agent_diag then log('AGENT_STOP calls='..s.agent_calls..' restored='..s.agent_restored..' failures='..s.agent_failures) end
  if cfg.native_gate_ship_diag then log('SHIP_STOP calls='..s.ship_calls..' restored='..s.ship_restored..' failures='..s.ship_failures) end
  if cfg.native_gate_params_init then log('PARAMS_STOP calls='..s.params_calls) end
  if cfg.native_gate_params_diag then log('PARAMS_TRACE_STOP calls='..s.params_trace_calls..' restored='..s.params_restored..' failures='..s.params_trace_failures) end
  if cfg.native_gate_nav_diag then log('NAV_STOP calls='..s.nav_calls..' restored='..s.nav_restored..' failures='..s.nav_failures..' initial_refreshes='..s.nav_inits) end
  if cfg.native_gate_velocity_diag then log('VELOCITY_STOP calls='..s.velocity_calls..' restored='..s.velocity_restored..' failures='..s.velocity_failures) end
  if cfg.native_gate_shape_diag then
   log('SHAPE_STOP calls='..s.shape_calls..' restored='..s.shape_restored..' failures='..s.shape_failures..' initial_refreshes='..s.shape_inits)
   if cfg.native_gate_shape_lifetimes then log('SHAPE_LIFETIME_STOP calls='..s.shape_lifetime_calls..' failures='..s.shape_failures) end
   log('SHAPE_INIT_TOTALS data='..s.shape_init_totals)
   if cfg.native_gate_docking_shape_rng then log('DOCK_SHAPE_STOP registered='..s.shape_dock_registered..' extras='..s.shape_extra_registered..' extra_calls='..s.shape_extra_calls..' replaced='..tostring(s.shape_extra_replaced)) end
   if cfg.native_gate_docking_shape_refresh then log('DOCK_REFRESH_STOP calls='..s.shape_dock_calls..' forced='..s.shape_forced_refreshes) end
  end
  if cfg.native_gate_attention_diag then log('ATTENTION_STOP calls='..s.attention_calls..' restored='..s.attention_restored..' failures='..s.attention_failures..' initial_refreshes='..s.attention_inits) end
  if cfg.native_gate_mass_diag then log('MASS_STOP calls='..s.mass_calls..' restored='..s.mass_restored..' failures='..s.mass_failures) end
  if cfg.native_gate_death_diag then log('DEATH_STOP calls='..s.death_calls..' restored='..s.death_restored..' failures='..s.death_failures) end
  if cfg.native_gate_bullet_diag then log('BULLET_STOP calls='..s.bullet_calls..' created='..s.bullet_created..' restored='..s.bullet_restored..' failures='..s.bullet_failures..' unknown='..s.bullet_unknown..' outside='..s.bullet_outside..' unsupported='..s.bullet_unsupported) end
  if cfg.native_gate_weapon_diag then log('WEAPON_STOP calls='..s.weapon_calls..' restored='..s.weapon_restored..' failures='..s.weapon_failures..' registered='..s.weapon_registered) end
  return
 end
 if snapshot and s.boot_phase==0 then
  if not ffi.C.IsStartmenu() then return end
  if not refreshed then ffi.C.ReloadSaveList();refreshed=true;return end
  if not ffi.C.IsSaveListLoadingComplete() then return end
  local found=false
  for _,entry in pairs(GetSaveList({save_009=true}) or {}) do if entry.filename=='save_009' then found=true end end
  assert(found,'staged snapshot slot missing')
  -- Runtime IDs: the same slots on every client from here on, set right before the load so everything the load
  -- creates takes them in the same order; 'observe' only logs. Needs the world identities. Default off; stop reason 130.
  if cfg.native_gate_runtime_ids~=nil then
   local value=cfg.native_gate_runtime_ids
   local base=value==true and 65536 or value=='observe' and 0 or value
   assert(cfg.native_gate_world_identities and type(base)=='number' and base==math.floor(base) and base>=0 and base<=524288 and (value=='observe' or base>0) and
    gate.runtime_ids and gate.runtime_ids_status and gate.runtime_ids_word and gate.runtime_ids_map_row and gate.runtime_ids_caller_row and gate.runtime_ids(base)==1,
    'runtime ids refused '..tostring(gate.runtime_ids_status and gate.runtime_ids_status()))
   log('RUNTIME_IDS_ARMED '..tostring(gate.runtime_ids_status()))
  end
  assert(cfg.native_gate_runtime_ids_barrier==nil or (cfg.native_gate_runtime_ids_barrier==true and cfg.native_gate_runtime_ids~=nil),'runtime ids barrier refused')
  -- The Linux gate's table of who allocates; Windows has it with native_gate_created_identities. Rows at tick 1 and every 300 ticks.
  if cfg.native_gate_runtime_ids_callers then
   assert(cfg.native_gate_runtime_ids_callers==true and cfg.native_gate_runtime_ids~=nil and cfg.native_gate_runtime_ids~='observe' and
    gate.runtime_ids_callers and gate.runtime_ids_callers()==1,'runtime ids callers refused '..tostring(gate.runtime_ids_status and gate.runtime_ids_status()))
   log('RUNTIME_IDS_CALLERS_ARMED')
  end
  -- What only the view asks for (UI elements, ICom, effects, slots 6 and 10, a presentation scope) takes its IDs from its own lists.
  if cfg.native_gate_runtime_ids_local then
   assert(cfg.native_gate_runtime_ids_local==true and cfg.native_gate_runtime_ids~=nil and cfg.native_gate_runtime_ids~='observe' and cfg.native_gate_created_identities and
    gate.runtime_ids_local and gate.runtime_ids_probe and gate.runtime_ids_local()==1,
    'runtime ids local refused '..tostring(gate.runtime_ids_status and gate.runtime_ids_status()))
   log('RUNTIME_IDS_LOCAL_ARMED '..tostring(gate.runtime_ids_status()))
  end
  assert(gate.load_requested()==1,'load request refused')
  log('LOAD_REQUEST snapshot='..snapshot);LoadGame('save_009');return
 end
 if snapshot and (s.boot_phase<3 or s.issued==s.report_tick) then return end
 local tick=tonumber(s.issued)
 if (tonumber(s.registry_count) or 0)>0 then
  local count=0
  for slot,handle,macro,owner in s.registry_manifest:gmatch('(%d+)/([0-9a-f]+)/([a-z0-9_]+)/([a-z0-9_]+)') do
   local status,why=gate.ship_status(handle)
   assert(status,'dynamic native ship unavailable '..tostring(why))
   log('SHIP_REGISTRY tick='..tick..' macro='..macro..' owner='..owner..' status='..status)
   count=count+1
  end
  assert(count==s.registry_count,'native registry manifest incomplete')
  for row in s.registry_navigation:gmatch('[^,]+') do
   local id=assert(row:match('^(%d+)/'),'invalid dynamic navigation trace')
   if not registry_navigation_seen[id] then
    log('SHIP_NAV_FIRST data='..row);registry_navigation_seen[id]=true
   end
  end
 end
 if cfg.native_gate_snapshot_fixture and tick==1 then
  for i=1,12 do log('SNAPSHOT_ACTOR '..assert(gate.snapshot_actor(i),'snapshot actor unavailable '..i)) end
 end
 trace_gravidar(tick,s.registry_manifest)
 if profile then profile.mark('drains') end
 local input_state
 if cfg.native_gate_command_test then
  assert(s.wire_version==2,'native command wire version mismatch')
  input_state=assert(X4LAN_NATIVE_COMMANDS,'native command adapter missing').apply(tick,s.input1,s.input2)
 elseif X4LAN_NATIVE_PLAYER_COMMANDS then
  assert(s.wire_version==2,'native player wire version mismatch')
  input_state=assert(X4LAN_NATIVE_PLAYER_COMMANDS,'native player command adapter missing').apply(tick,s.input1,s.input2)
 else
  assert((not s.input1 or s.input1=='00000000') and (not s.input2 or s.input2=='00000000'),'unexpected native commands')
 end
 if wire3 then input_state=apply_inputs(tick,s) end
 if profile then profile.mark('inputs') end
 log('RNG tick='..tick..' state='..tostring(s.rng))
 if cfg.native_gate_frame_timing then ticklog.write('FRAME_TIMING tick='..tick..' '..tostring(gate.frame_timing())..' probe_us='..string.format('%.0f',probe_last_us or -1)) end
 local pilot_receipt=cfg.native_gate_pilot_inputs and pilot_frame(s,tick)
 if profile then profile.mark('pilot') end
 if cfg.native_gate_attention_all then
  ticklog.status('ATTENTION_ALL',tick,gate.attention_all_status,'attention all status unavailable')
 end
 -- After the pilot slots resolved: latch this frame's viewpoints for the next frames.
 if cfg.native_gate_view_points then
  if gate.view_points_frame(tick)~=1 then error('view points frame failed '..tostring(gate.view_points_status()),0) end
  ticklog.status('VIEW_POINTS',tick,gate.view_points_status,'view points status unavailable')
  if cfg.native_gate_view_rows then
   for _=1,8192 do
    local row=gate.view_points_row(row_cursors.view)
    if not row then break end
    log('VIEW_POINTS_ROW '..row);row_cursors.view=row_cursors.view+1
   end
  end
 end
 -- After the latch: the stand-in camera the next region pass is centred on.
 if cfg.native_gate_region_camera or cfg.native_gate_region_fields or cfg.native_gate_mass_traffic_camera or cfg.native_gate_effect_camera or cfg.native_gate_effect_rows or cfg.native_gate_highway_camera or cfg.native_gate_physics_scenes or cfg.native_gate_camera_context or cfg.native_gate_shape_refresh or cfg.native_gate_mining_camera or
   cfg.native_gate_xs_camera or cfg.native_gate_camera_probe or cfg.native_gate_highway_rows or cfg.native_gate_highway_speed or cfg.native_gate_sound_rng or cfg.native_gate_traffic_views or cfg.native_gate_traffic_rows then
  if gate.region_camera_frame(tick)~=1 then error('region camera frame failed '..tostring(gate.region_camera_status()),0) end
  -- A view point neither stand-in answers for: no physics scene, traffic or highway parameter near it. Say so when it changes.
  if gate.region_camera_uncovered then
   local uncovered=gate.region_camera_uncovered()
   if uncovered~=(row_cursors.uncovered or 0) then
    row_cursors.uncovered=uncovered
    log('VIEW_POINTS_UNCOVERED tick='..tick..' count='..uncovered..(uncovered>0 and
     (cfg.native_gate_view_scenes and ' no camera-centred traffic or highway parameter near them' or ' no physics scene, camera-centred traffic or highway parameter near them') or ''))
   end
  end
  ticklog.status('REGION_CAMERA',tick,gate.region_camera_status,'region camera status unavailable')
 end
 if cfg.native_gate_runtime_ids~=nil then
  ticklog.status('RUNTIME_IDS',tick,gate.runtime_ids_status,'runtime ids status unavailable')
  if tick==1 then log('RUNTIME_IDS_MAP '..assert(gate.runtime_ids_map_row(),'runtime ids map unavailable')) end
  local every=cfg.native_gate_runtime_ids_caller_stride
  if tick%(every or 300)==0 or (tick==1 and cfg.native_gate_runtime_ids_callers) then
   for i=0,2047 do
    local row=gate.runtime_ids_caller_row(i)
    if not row then break end
    -- A finer stride asks for ticks the status stride does not write, so these go out at once.
    if every then ticklog.write('RUNTIME_IDS_CALLER tick='..tick..' '..row) else log('RUNTIME_IDS_CALLER tick='..tick..' '..row) end
   end
  end
 end
 if cfg.native_gate_weapon_fire_rng then
  ticklog.status('WEAPON_FIRE_RNG',tick,gate.weapon_fire_rng_status,'weapon fire rng status unavailable')
 end
 if cfg.native_gate_motion_watch then assert(gate.motion_watch_frame(tick)==1,'motion watch frame failed') end
 if cfg.native_gate_coverage then gate.coverage_frame(tick) end
 if cfg.native_gate_physics_trace~=nil then
  ticklog.status('PHYSICS_TRACE',tick,gate.physics_trace_status,'physics trace status unavailable')
 end
 if cfg.native_gate_body_order then
  ticklog.status('BODY_ORDER',tick,gate.body_order_status,'body order status unavailable')
 end
 if cfg.native_gate_shape_order then
  ticklog.status('SHAPE_ORDER',tick,gate.shape_order_status,'shape order status unavailable')
 end
 if cfg.native_gate_nav_octree then
  ticklog.status('NAV_OCTREE',tick,gate.nav_octree_status,'nav octree status unavailable')
 end
 if cfg.native_gate_nav_octree_sites then
  log('NAV_OCTREE_SITES tick='..tick..' '..assert(gate.nav_octree_sites_status(),'nav octree sites status unavailable'))
  if not row_cursors.nav_octree_sites then -- the load phase is complete at the first admitted tick
   row_cursors.nav_octree_sites=true
   for i=0,511 do local row=gate.nav_octree_site_row(i);if not row then break end;if not row:find(' free$') then log('NAV_OCTREE_SITE at=first '..row) end end
  end
 end
 if cfg.native_gate_mass_traffic then
  ticklog.status('MASS_TRAFFIC',tick,gate.mass_traffic_status,'mass traffic status unavailable')
 end
 if cfg.native_gate_ship_sound_rng then
  ticklog.status('SHIP_SOUND_RNG',tick,gate.ship_sound_rng_status,'ship sound RNG status unavailable')
 end
 if cfg.native_gate_player_timer then
  ticklog.status('PLAYER_TIMER',tick,gate.player_timer_status,'player timer status unavailable')
 end
 if cfg.native_gate_warp_rng then
  ticklog.status('WARP_RNG',tick,gate.warp_rng_status,'warp RNG status unavailable')
  for _=1,4096 do
   local row=gate.warp_rng_row(row_cursors.warp_rng)
   if not row then break end
   log('WARP_RNG_ROW '..row);row_cursors.warp_rng=row_cursors.warp_rng+1
  end
 end
 if cfg.native_gate_transition_rows then
  for _=1,8192 do
   local row=gate.transition_row(row_cursors.transition)
   if not row then break end
   log('TRANSITION_ROW '..row);row_cursors.transition=row_cursors.transition+1
  end
 end
 if cfg.native_gate_tempzone_order then
  ticklog.status('TEMPZONE_ORDER',tick,gate.tempzone_order_status,'tempzone order status unavailable')
  for _=1,4096 do
   local row=gate.tempzone_order_row(row_cursors.tempzone)
   if not row then break end
   log('TEMPZONE_ROW '..row);row_cursors.tempzone=row_cursors.tempzone+1
  end
 end
 if cfg.native_gate_command_order then
  ticklog.status('COMMAND_ORDER',tick,gate.command_order_status,'command order status unavailable')
  for _=1,4096 do
   local row=gate.command_order_row(row_cursors.command_order)
   if not row then break end
   log('COMMAND_ORDER_ROW '..row);row_cursors.command_order=row_cursors.command_order+1
  end
 end
 if cfg.native_gate_region_pass then
  ticklog.status('REGION_PASS',tick,gate.region_pass_status,'region pass status unavailable')
 end
 if cfg.native_gate_gravidar_digest then
  log('GRAVIDAR_DIGEST tick='..tick..' '..assert(gate.gravidar_digest_frame(),'Gravidar digest unavailable'))
  if cfg.native_gate_gravidar_digest_rows then drain_gravidar_digest() end
 end
 if cfg.native_gate_gravidar_deadlines then ticklog.status('GRAVIDAR_DEADLINES',tick,gate.gravidar_deadlines_status,'Gravidar deadlines status unavailable') end
 if cfg.native_gate_gravidar_posts then
  ticklog.status('GRAVIDAR_POSTS',tick,gate.gravidar_posts_status,'Gravidar posts status unavailable')
  for _=1,4096 do
   local row=gate.gravidar_post_row(row_cursors.gravidar_post)
   if not row then break end
   log('GRAVIDAR_POST_ROW '..row);row_cursors.gravidar_post=row_cursors.gravidar_post+1
  end
 end
 if cfg.native_gate_gravidar_wide then ticklog.status('GRAVIDAR_WIDE',tick,gate.gravidar_wide_status,'Gravidar wide status unavailable') end
 if cfg.native_gate_thread_rng_census then
  -- Native log lines are cut near 8 KiB: header plus rows in chunks of 40 threads.
  local census=assert(gate.thread_rng(),'thread RNG census unavailable')
  local head,rows=assert(census:match('^(.-) rows=(.*)$'))
  local chunk,part,parts={},0,{}
  for row in rows:gmatch('[^,]+') do chunk[#chunk+1]=row;if #chunk==40 then parts[#parts+1]=table.concat(chunk,',');chunk={} end end
  if #chunk>0 then parts[#parts+1]=table.concat(chunk,',') end
  log('THREAD_RNG tick='..tick..' '..head..' parts='..#parts)
  for i,rows in ipairs(parts) do log('THREAD_RNG_PART tick='..tick..' part='..i..' rows='..rows) end
 end
 if cfg.native_gate_path_determinism then ticklog.status('PATH_DETERMINISM',tick,gate.path_determinism_status,'path determinism status unavailable') end
 if cfg.native_gate_virtual_players then
  ticklog.status('VPLAYER',tick,gate.virtual_players_status,'virtual players status unavailable')
  if row_cursors.vplayer_map==0 then
   local map=gate.virtual_players_map()
   if map then log('VPLAYER_MAP tick='..tick..' '..map);row_cursors.vplayer_map=1 end
  end
  -- Step 3 spike: one VPLAYER_SPIKE row whenever its stage changes (waiting, constructed, failed).
  if cfg.native_gate_virtual_players=='construct' then
   local status=gate.virtual_players_status() or ''
   local stage=status:match('spike_stage=(%a+)') or 'unknown'
   if stage~=row_cursors.vplayer_spike then
    row_cursors.vplayer_spike=stage;log('VPLAYER_SPIKE tick='..tick..' '..tostring(status:match('spike=.*$')))
   end
  end
  -- Per-region hashes to locate a lane difference (tick 1, every 300th), raw config/option bytes once.
  if gate.virtual_players_hashes and (tick==1 or tick%300==0) then
   log('VPLAYER_HASHES tick='..tick..' '..tostring(gate.virtual_players_hashes()))
   if tick==1 and gate.virtual_players_raw then
    log('VPLAYER_RAW tick=1 class=config '..tostring(gate.virtual_players_raw('config'))..' option='..tostring(gate.virtual_players_raw('option')))
   end
  end
 end
 if cfg.native_gate_travel_determinism then ticklog.status('TRAVEL_DETERMINISM',tick,gate.travel_determinism_status,'travel determinism status unavailable') end
 if cfg.native_gate_travel_snapshot then ticklog.status('TRAVEL_SNAPSHOT',tick,gate.travel_snapshot_status,'travel snapshot status unavailable') end
 if cfg.native_gate_path_budget then ticklog.status('PATH_BUDGET',tick,gate.path_budget_status,'path budget status unavailable') end
 -- Slot 6 runs before slot 10: frame= in the status is the previous admitted frame's render.
 if cfg.native_gate_render_rng_split then ticklog.status('RENDER_RNG_SPLIT',tick,gate.render_rng_split_status,'render RNG split status unavailable') end
 -- Slot 6 runs after slot 3's bullet Moves: frame= is this admitted frame.
 if cfg.native_gate_bullet_cadence then ticklog.status('BULLET_CADENCE',tick,gate.bullet_cadence_status,'bullet cadence status unavailable') end
 if cfg.native_gate_cutscene_ready then
  -- Rare and the first question after a split: written at once.
  while true do local row=gate.cutscene_ready_row();if not row then break end;ticklog.write('CUTSCENE_READY_ROW '..row) end
  if tick%600==0 then ticklog.status('CUTSCENE_READY',tick,gate.cutscene_ready_status,'cutscene ready status unavailable') end
 end
 if cfg.native_gate_speak_timer then ticklog.status('SPEAK_TIMER',tick,gate.speak_timer_status,'speak timer status unavailable') end
 if cfg.native_gate_nav_cadence then
  ticklog.status('NAV_CADENCE',tick,gate.nav_cadence_status,'nav cadence status unavailable')
  -- Fixture controller state changes (enables, deadlines, attention, body, zone scene).
  for _=1,4096 do
   local row=gate.nav_cadence_row and gate.nav_cadence_row(nav_cadence_cursor)
   if not row then break end
   log('NAV_CADENCE_ROW '..row);nav_cadence_cursor=nav_cadence_cursor+1
  end
  if cfg.native_gate_nav_player then ticklog.status('NAV_PLAYER',tick,gate.nav_player_status,'nav player status unavailable') end
 end
 -- Applied here, in slot 6, so this tick's render (slot 10) already uses it.
 if camera_offset and camera_offset.follow then
  assert(gate.camera_anchor(tick,camera_offset.follow,camera_offset.back,camera_offset.up,camera_offset.orbit)==1,'camera anchor failed '..tostring(gate.camera_anchor_status()))
  ticklog.status('CAMERA_ANCHOR',tick,gate.camera_anchor_status,'camera anchor status unavailable')
 elseif camera_offset then
  assert(gate.camera_offset(tick,camera_offset[1],camera_offset[2],camera_offset[3])==1,'camera offset failed '..tostring(gate.camera_offset_status()))
  ticklog.status('CAMERA_OFFSET',tick,gate.camera_offset_status,'camera offset status unavailable')
 end
 if camera_perturb then
  assert(gate.camera_perturb(tick)==1,'camera perturbation failed '..tostring(gate.camera_perturb_status()))
  ticklog.status('CAMERA_PERTURB',tick,gate.camera_perturb_status,'camera perturbation status unavailable')
 end
 if cfg.native_gate_gravidar_key_all or cfg.native_gate_gravidar_time_sync then ticklog.status('GRAVIDAR_KEY',tick,gate.gravidar_key_status) end
 drain_gravidar_watch()
 if cfg.native_gate_ai_schedule then
  ticklog.status('AI_SCHEDULE',tick,gate.ai_schedule_status,'AI schedule status unavailable')
  -- Sector-list entries without a saved ID (fallback keys or refusals); logged when they change.
  local fallback=gate.ai_schedule_fallbacks and gate.ai_schedule_fallbacks()
  if fallback and fallback~=ai_schedule_fallback then log('AI_SCHEDULE_FALLBACK tick='..tick..' '..fallback) end
  ai_schedule_fallback=fallback
 end
 if cfg.native_gate_job_rng then ticklog.status('JOB_RNG_STATUS',tick,gate.job_rng_status);drain_job_rng() end
 if cfg.native_gate_event_order then ticklog.status('EVENT_ORDER',tick,gate.event_order_status,'event order status unavailable');drain_event_rows(16384) end
 if cfg.native_gate_event_rng then ticklog.status('EVENT_RNG',tick,gate.event_rng_status,'event RNG status unavailable') end
 if cfg.native_gate_ui_cargo then ticklog.status('UI_CARGO',tick,gate.ui_cargo_status,'UI cargo status unavailable') end
 if cfg.native_gate_ui_lazy then ticklog.status('UI_LAZY',tick,gate.ui_lazy_status,'UI lazy status unavailable') end
 if cfg.native_gate_script_rng then ticklog.status('SCRIPT_RNG',tick,gate.script_rng_status,'script RNG status unavailable');drain_script_rng(tick) end
 if cfg.native_gate_md_timed_rng then ticklog.status('MD_TIMED_RNG',tick,gate.md_timed_rng_status,'MD timed RNG status unavailable') end
 if cfg.native_gate_boot_hold then ticklog.status('BOOT_HOLD',tick,gate.boot_hold_status,'boot hold status unavailable') end
 if cfg.native_gate_md_resume_rng then ticklog.status('MD_RESUME_RNG',tick,gate.md_resume_rng_status,'MD resume RNG status unavailable');drain_md_resume() end
 if cfg.native_gate_ai_wait_rng then ticklog.status('AI_WAIT_RNG',tick,gate.ai_wait_rng_status,'AI wait RNG status unavailable');drain_ai_wait() end
 if cfg.native_gate_economy_load_rng then ticklog.status('ECONOMY_LOAD_RNG',tick,gate.economy_load_rng_status,'economy load RNG status unavailable') end
 if cfg.native_gate_find_order then ticklog.status('FIND_ORDER',tick,gate.find_order_status,'find order status unavailable');drain_find_order() end
 if cfg.native_gate_region_rng~=nil then ticklog.status('REGION_RNG',tick,gate.region_rng_status,'region RNG status unavailable');drain_region() end
 if cfg.native_gate_spawn_rng~=nil then ticklog.status('SPAWN_RNG',tick,gate.spawn_rng_status,'spawn RNG status unavailable');drain_spawn() end
 if cfg.native_gate_created_identities then
  ticklog.status('CREATED_IDENTITIES',tick,gate.created_identities_status,'created identities status unavailable');drain_created_rows()
 end
 if cfg.native_gate_actor_rng_all then ticklog.status('ACTOR_RNG_ALL',tick,gate.actor_rng_all_status,'all-ship actor RNG status unavailable') end
 if cfg.native_gate_actor_rng_rows then
  for _=1,4096 do
   local row=gate.actor_rng_row(row_cursors.actor_rng)
   if not row then break end
   log('ACTOR_RNG_ROW '..row);row_cursors.actor_rng=row_cursors.actor_rng+1
  end
 end
 if cfg.native_gate_draw_watch or cfg.native_gate_draw_watch_frame then
  for _=1,4096 do
   local row=gate.draw_watch_row(row_cursors.draw_watch)
   if not row then break end
   log('DRAW_WATCH_ROW '..row);row_cursors.draw_watch=row_cursors.draw_watch+1
  end
  -- Sampled stray single steps (classification in the status).
  for _=1,64 do
   local row=gate.draw_watch_stray(row_cursors.draw_watch_stray)
   if not row then break end
   log('DRAW_WATCH_STRAY '..row);row_cursors.draw_watch_stray=row_cursors.draw_watch_stray+1
  end
 end
 if cfg.native_gate_boot_rng_anchor and tick==1 then
  local anchor=assert(gate.boot_rng_anchor_status(),'boot RNG anchor status unavailable')
  log('BOOT_RNG_ANCHOR '..anchor)
  assert(anchor:match(' applied=1 ') and anchor:match(' failures=0 ') and anchor:match(' late=0$'),'boot RNG anchor not applied before load completed')
 end
 if profile then profile.mark('modules') end
 if cfg.native_gate_hash=='battle' then
  if tick==1 then AddUITriggeredEvent('X4LAN_NativeGate',snapshot and 'RestoreActors' or 'Spawn',{}) end
  if tick==5 and cfg.native_gate_docking_state then AddUITriggeredEvent('X4LAN_NativeCommands','DockSetup',{mode=cfg.native_gate_command_test}) end
  -- native_gate_battle_engage=false keeps the restored fixture idle (default
  -- order Wait) while its rows stay in the declared state: a calm sandbox.
  if tick==10 and cfg.native_gate_battle_engage~=false then AddUITriggeredEvent('X4LAN_NativeGate','Engage',{}) end
  -- Opt-in stress fixture (native_gate_bigbattle.xml): faction war and/or 40v40 spawn.
  if cfg.native_gate_big_war and tick==12 then AddUITriggeredEvent('X4LAN_NativeGate','BigWar',{}) end
  if cfg.native_gate_big_spawn and tick==14 then AddUITriggeredEvent('X4LAN_NativeGate','BigSpawn',{}) end
  if cfg.native_gate_big_spawn and tick==20 then AddUITriggeredEvent('X4LAN_NativeGate','BigEngage',{}) end
  -- Opt-in player tour (native_gate_tour.xml): warp the player ship to another sector every 60 ticks.
  if cfg.native_gate_player_tour and tick==25 then AddUITriggeredEvent('X4LAN_NativeGate','TourStart',{}) end
  if cfg.native_gate_player_tour and tick>=30 and (tick-30)%60==0 then AddUITriggeredEvent('X4LAN_NativeGate','TourStep',{}) end
  -- Opt-in scenario fixtures (native_gate_missions.xml, native_gate_highway.xml).
  local scenarios=X4LAN_NATIVE_SCENARIOS
  if scenarios.text and tick==1 then log('SCENARIO '..scenarios.text) end
  for _,event in ipairs(scenarios.events(tick)) do AddUITriggeredEvent('X4LAN_NativeGate',event[1],event[2]) end
  if scenarios.trace then scenarios.trace(tick) end
  if cfg.native_gate_boost_diag and tick%60==30 then AddUITriggeredEvent('X4LAN_NativeCommands','BoostSample',{tick=tick}) end
  if cfg.native_gate_travel_diag and tick%60==30 then AddUITriggeredEvent('X4LAN_NativeCommands','TravelSample',{tick=tick}) end
 end
 -- After this tick's fixture events, so their MD order is fixed relative to the identity steps.
 local identity_state=identity and identity.frame(tick)
 local data=sample(tick)
 if profile then profile.mark('sample') end
 if pilot_receipt then data=data..'|'..pilot_receipt end
 if identity_state then data=data..'|'..identity_state end
 if cfg.native_gate_runtime_ids_barrier then data=data..'|RUNTIME_IDS_1|'..assert(gate.runtime_ids_word(),'runtime ids word unavailable') end
 if input_state then data=data..'|'..input_state end
 if cfg.native_gate_player_probe then data=data..'|'..assert(X4LAN_NATIVE_PLAYER,'native player probe missing')(tick,gate) end
 if cfg.native_gate_world_hash_stride then
  if tick==1 then
   for _,saved in ipairs(cfg.native_gate_world_hash_watch or {}) do assert(gate.world_watch(saved)==1,'world watch refused '..saved) end
  end
  if tick==1 or tick%cfg.native_gate_world_hash_stride==0 then
   local summary,why=world_capture(tick)
   assert(summary,'native world hash capture failed '..tostring(why))
   log('WORLD_BANKS '..assert(gate.world_banks(),'native world bank phase unavailable'))
   world_hash_state=assert(summary:match(' state=('..world_hash_schema..':[^ ]+)$'),'invalid world hash summary')
   if not cfg.native_gate_play_mode then
    for page=1,assert(gate.world_page(0)) do log_world_page(assert(gate.world_page(page))) end
    for row=1,assert(gate.world_detail(0)) do log('WORLD_DETAIL '..assert(gate.world_detail(row))) end
   end
   log('WORLD_HASH '..summary)
   if cfg.native_gate_world_hash_shields then log('WORLD_HASH_SHIELDS tick='..tick..' '..gate.world_hash_shields_status()) end
   if tick==1 and cfg.native_gate_world_trace_ticks then assert(gate.world_trace(cfg.native_gate_world_trace_ticks)==1,'world trace arm refused') end
  end
  drain_world_trace()
  data=data..'|'..assert(world_hash_state,'world hash baseline missing')
 end
 if profile then profile.mark('capture') end
 local hash=(gate.sha256 or X4LAN_SHA256.digest)(data)
 local command=cfg.native_gate_command_test and X4LAN_NATIVE_COMMANDS.proposal(tick) or '00000000'
 if X4LAN_NATIVE_PLAYER_COMMANDS then command=X4LAN_NATIVE_PLAYER_COMMANDS.proposal(tick) end
 if wire3 then
  local blob=X4LAN_NATIVE_INPUTS and X4LAN_NATIVE_INPUTS.proposal and X4LAN_NATIVE_INPUTS.proposal(tick)
  if blob~=nil then assert(type(blob)=='string' and gate.propose(s.issued,blob)==1,'native input proposal refused') end
  command=nil -- the report seals the blob staged through gate.propose, or the empty blob
 end
 assert(gate.report(s.issued,hash,command)~=0,'native report rejected')
 if profile then profile.mark('digest') end
 ticklog.close() -- a written tick is emitted; lines between frames (UI events) are written at once
 if profile then profile.mark('log');profile.finish(tick) end
end
SetScript('onUpdate',function()
 if done and diagnostic and native_owned then
  pcall(function() assert(X4LAN_NATIVE_DIAGNOSTIC,'diagnostic adapter missing').drain(gate,true) end)
  return
 end
 local probe_started=cfg.native_gate_frame_timing and gate and gate.qpc_us and gate.qpc_us()
 -- A presenting client's UI slot runs as its slot's player; the probe reads and reports as the canonical one.
 if cfg.native_gate_draw_watch_frame and gate and gate.draw_watch_frame_end then gate.draw_watch_frame_end() end
 local paused=gate and gate.pilot_present_pause and gate.pilot_present_pause()
 local probe_mark=cfg.native_gate_runtime_ids_local and gate and gate.runtime_ids_probe
 if probe_mark then probe_mark(1) end
 local ok,error=pcall(frame)
 if probe_mark then probe_mark(0) end
 if paused==1 then gate.pilot_present_resume() end
 if probe_started then probe_last_us=gate.qpc_us()-probe_started end
 if not ok then
  ticklog.flush() -- held ticks, the failing one up to the error, precede the ERROR line
  log('ERROR '..tostring(error))
  if native_owned then
   local stopped=gate.stop()
   if registry_creation then
    local drained,drain_error=pcall(registry_creation,true)
    if not drained then log('REGISTRY_CREATION_DRAIN_ERROR '..tostring(drain_error)) end
   end
   if creation_rng then
    local drained,drain_error=pcall(creation_rng,true)
    if not drained then log('CREATION_RNG_DRAIN_ERROR '..tostring(drain_error)) end
   end
   if creation_trace then
    local drained,drain_error=pcall(creation_trace,true)
    if not drained then log('CREATION_TRACE_DRAIN_ERROR '..tostring(drain_error)) end
   end
   local status=gate.poll() or {}
   log('ABORT stopped='..tostring(stopped)..' active='..tostring(status.active)..' restored='..tostring(status.restored)..' tick='..tostring(status.issued))
   if cfg.native_gate_player_transition_observer and X4LAN_NATIVE_TRANSITION then
    local drained,why=pcall(X4LAN_NATIVE_TRANSITION.drain,gate,true)
    if not drained then log('PLAYER_TRANSITION_DRAIN_ERROR '..tostring(why)) end
   end
   if cfg.native_gate_player_origin_token and X4LAN_NATIVE_ORIGIN then pcall(X4LAN_NATIVE_ORIGIN.drain,gate,true) end
   if cfg.native_gate_render_rng_split then pcall(function() log('RENDER_RNG_SPLIT_STOP '..tostring(gate.render_rng_split_status())) end) end
   if cfg.native_gate_created_identities then pcall(function() drain_created_rows();log('CREATED_IDENTITIES_STOP '..tostring(gate.created_identities_status())) end) end
   if cfg.native_gate_actor_rng_all then pcall(function() log('ACTOR_RNG_ALL_STOP '..tostring(gate.actor_rng_all_status())) end) end
   if cfg.native_gate_bullet_cadence then pcall(function() log('BULLET_CADENCE_STOP '..tostring(gate.bullet_cadence_status())) end) end
   if cfg.native_gate_speak_timer then pcall(function() log('SPEAK_TIMER_STOP '..tostring(gate.speak_timer_status())) end) end
   if cfg.native_gate_nav_cadence then pcall(function() log('NAV_CADENCE_STOP '..tostring(gate.nav_cadence_status())) end) end
   if cfg.native_gate_nav_player then pcall(function() log('NAV_PLAYER_STOP '..tostring(gate.nav_player_status())) end) end
   if cfg.native_gate_command_order then pcall(function() log('COMMAND_ORDER_STOP '..tostring(gate.command_order_status())) end) end
   if cfg.native_gate_ship_sound_rng then pcall(function() log('SHIP_SOUND_RNG_STOP '..tostring(gate.ship_sound_rng_status())) end) end
   if cfg.native_gate_player_timer then pcall(function() log('PLAYER_TIMER_STOP '..tostring(gate.player_timer_status())) end) end
   if cfg.native_gate_warp_rng then pcall(function() log('WARP_RNG_STOP '..tostring(gate.warp_rng_status())) end) end
   if cfg.native_gate_transition_rows then pcall(function() log('TRANSITION_ROWS_STOP '..tostring(gate.transition_rows_status())) end) end
   if cfg.native_gate_tempzone_order then pcall(function() log('TEMPZONE_ORDER_STOP '..tostring(gate.tempzone_order_status())) end) end
   if cfg.native_gate_gravidar_wide then pcall(function() log('GRAVIDAR_WIDE_STOP '..tostring(gate.gravidar_wide_status())) end) end
   if cfg.native_gate_draw_watch or cfg.native_gate_draw_watch_frame then pcall(function() log('DRAW_WATCH_STOP '..tostring(gate.draw_watch_status())) end) end
   if cfg.native_gate_attention_all then pcall(function() log('ATTENTION_ALL_STOP '..tostring(gate.attention_all_status())) end) end
   if cfg.native_gate_view_points then pcall(function() log('VIEW_POINTS_STOP '..tostring(gate.view_points_status())) end) end
   if cfg.native_gate_mass_traffic then pcall(function() log('MASS_TRAFFIC_STOP '..tostring(gate.mass_traffic_status())) end) end
   if cfg.native_gate_region_pass then pcall(function() log('REGION_PASS_STOP '..tostring(gate.region_pass_status())) end) end
   if cfg.native_gate_weapon_fire_rng then pcall(function() log('WEAPON_FIRE_RNG_STOP '..tostring(gate.weapon_fire_rng_status())) end) end
   if cfg.native_gate_engine_refresh_rng then pcall(function() log('ENGINE_REFRESH_RNG_STOP '..tostring(gate.engine_refresh_rng_status())) end) end
   if cfg.native_gate_portable_math then pcall(function() log('PORTABLE_MATH_STOP '..tostring(gate.portable_math_status())) end) end
   if cfg.native_gate_exact_reciprocal then pcall(function() log('EXACT_RECIPROCAL_STOP '..tostring(gate.exact_reciprocal_status())) end) end
   if cfg.native_gate_fp_env then pcall(function() log('FP_ENV_STOP '..tostring(gate.fp_env_status())) end) end
   if cfg.native_gate_decimal then pcall(function() log('DECIMAL_STOP '..tostring(gate.decimal_status())) end) end
   if cfg.native_gate_event_rng then pcall(function() log('EVENT_RNG_STOP '..tostring(gate.event_rng_status())) end) end
   if cfg.native_gate_build_rng then pcall(function() log('BUILD_RNG_STOP '..tostring(gate.build_rng_status())) end) end
   if cfg.native_gate_ui_cargo then pcall(function() log('UI_CARGO_STOP '..tostring(gate.ui_cargo_status())) end) end
   if cfg.native_gate_supply_rows then pcall(function() log('SUPPLY_ROWS_STOP '..tostring(gate.supply_rows_status())) end) end
   if cfg.native_gate_ui_lazy then pcall(function() log('UI_LAZY_STOP '..tostring(gate.ui_lazy_status())) end) end
   if cfg.native_gate_supply_wants~=nil then pcall(function() log('SUPPLY_WANTS_STOP '..tostring(gate.supply_wants_status())) end) end
   if cfg.native_gate_loadout_order then pcall(function() log('LOADOUT_ORDER_STOP '..tostring(gate.loadout_order_status())) end) end
   if cfg.native_gate_step_trace then pcall(function() log('STEP_TRACE_STOP '..tostring(gate.step_trace_status())) end) end
   if cfg.native_gate_fp_redirect then pcall(function() log('FP_REDIRECT_STOP '..tostring(gate.fp_redirect_status())) end) end
   if cfg.native_gate_coverage then pcall(function() log('COVERAGE_STOP '..tostring(gate.coverage_status())) end) end
   if cfg.native_gate_order_canon then pcall(function() log('ORDER_CANON_STOP '..tostring(gate.order_canon_status())) end) end
   if cfg.native_gate_pick_order then pcall(function() log('PICK_ORDER_STOP '..tostring(gate.pick_order_status())) end) end
   if cfg.native_gate_macro_arena then pcall(function() log('MACRO_ARENA_STOP '..tostring(gate.macro_arena_status())) end) end
   if cfg.native_gate_address_order then pcall(function() log('ADDRESS_ORDER_STOP '..tostring(gate.address_order_status())) end) end
   if cfg.native_gate_physics_trace~=nil or cfg.native_gate_body_order or cfg.native_gate_shape_order or cfg.native_gate_nav_octree then pcall(drain_gravidar_watch,true) end
   if cfg.native_gate_physics_trace~=nil then pcall(function() log('PHYSICS_TRACE_STOP '..tostring(gate.physics_trace_status())) end) end
   if cfg.native_gate_body_order then pcall(function() log('BODY_ORDER_STOP '..tostring(gate.body_order_status())) end) end
   if cfg.native_gate_shape_order then pcall(function() log('SHAPE_ORDER_STOP '..tostring(gate.shape_order_status())) end) end
   if cfg.native_gate_nav_octree then pcall(function() log('NAV_OCTREE_STOP '..tostring(gate.nav_octree_status())) end) end
   if cfg.native_gate_nav_octree_sites then pcall(function()
    log('NAV_OCTREE_SITES_STOP '..tostring(gate.nav_octree_sites_status()))
    for i=0,511 do local row=gate.nav_octree_site_row(i);if not row then break end;if not row:find(' free$') then log('NAV_OCTREE_SITE at=stop '..row) end end
   end) end
   if cfg.native_gate_region_camera or cfg.native_gate_region_fields or cfg.native_gate_mass_traffic_camera or cfg.native_gate_effect_camera or cfg.native_gate_effect_rows or cfg.native_gate_highway_camera or cfg.native_gate_physics_scenes or cfg.native_gate_camera_context or cfg.native_gate_shape_refresh or cfg.native_gate_mining_camera or
   cfg.native_gate_xs_camera or cfg.native_gate_camera_probe or cfg.native_gate_highway_rows or cfg.native_gate_highway_speed or cfg.native_gate_sound_rng or cfg.native_gate_traffic_views or cfg.native_gate_traffic_rows then
    pcall(function() log('REGION_CAMERA_STOP '..tostring(gate.region_camera_status())) end)
   end
   if camera_perturb then pcall(function() log('CAMERA_PERTURB_STOP '..tostring(gate.camera_perturb_status())) end) end
   if cfg.native_gate_pilot_inputs then pcall(function() log('PILOT_INPUTS_STOP '..tostring(gate.pilot_inputs_status())) end) end
   if (cfg.native_gate_lookahead or 0)>0 then pcall(function() log('LOOKAHEAD_STOP '..tostring(gate.lookahead_status())) end) end
   if pilot_camera_follow then pcall(function() log('PILOT_CAMERA_STOP '..tostring(gate.pilot_camera_status())) end) end
   if pilot_view.predict then pcall(function() log('PILOT_PREDICT_STOP '..tostring(gate.pilot_predict_status())) end) end
   if pilot_view.hits then pcall(function() log('HIT_CLAIMS_STOP '..tostring(gate.hit_claims_status())) end) end
   if pilot_view.fire then pcall(function() log('PILOT_FIRE_STOP '..tostring(gate.pilot_fire_status())) end) end
   if cfg.native_gate_player_observer and X4LAN_NATIVE_PLAYER_TRACE then pcall(X4LAN_NATIVE_PLAYER_TRACE,gate,true) end
   if cfg.native_gate_player_probe=='agreed-fire' and X4LAN_NATIVE_PLAYER_FIRE_TRACE then pcall(X4LAN_NATIVE_PLAYER_FIRE_TRACE,gate,true) end
   if cfg.native_gate_world_trace_ticks then
    pcall(function()
     drain_world_trace();log('WORLD_TRACE_STOP '..assert(gate.world_trace_status()))
     if cfg.native_gate_world_queue_trace then log('WORLD_QUEUE_STOP '..assert(gate.world_queue_trace_status())) end
     if cfg.native_gate_world_wait_manifest then log('WORLD_WAIT_STOP '..assert(gate.world_wait_trace_status())) end
     if cfg.native_gate_world_wait_rng_manifest then log('WORLD_WAIT_RNG_STOP '..assert(gate.world_wait_rng_status())) end
     if cfg.native_gate_world_action_manifest then log('WORLD_ACTION_STOP '..assert(gate.world_action_trace_status())) end
     if cfg.native_gate_world_values then log('WORLD_UNKNOWN_IDENTITY_STOP '..assert(gate.world_values_unknown_identity_status()));log('WORLD_VALUES_STOP '..assert(gate.world_values_status())) end
     if cfg.native_gate_world_position then log('WORLD_POSITION_STOP '..assert(gate.world_position_status())) end
     if cfg.native_gate_world_safe_position then log('WORLD_SAFE_POSITION_STOP '..assert(gate.world_safe_position_status())) end
     if cfg.native_gate_world_position_rng_manifest then log('WORLD_POSITION_RNG_STOP '..assert(gate.world_position_rng_status())) end
     if cfg.native_gate_world_find_rng_manifest then log('WORLD_FIND_STOP '..assert(gate.world_find_rng_status())) end
    end)
   end
  end
  done=true
 end
end)
