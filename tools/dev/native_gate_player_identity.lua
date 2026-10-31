-- Lockstep player identity: engine glue (config native_gate_player_identity = true; default off).
-- At one gated setup tick every client raises the identical ordered steps of the session plan
-- (native_gate_player_identity_plan) as UI events; MD X4LAN_PlayerIdentity applies them in the next
-- gated frame on every client: per-slot pool factions (factions mode), slot ship ownership (saved
-- ships, or ships created from a fixed macro/seed/position), start placements (saved ships warped to a
-- fixed point of a saved sector), relations between the slot factions.
-- Pure plan logic: native_gate_player_identity_core.lua. Report: LOCKSTEP-PLAYER-IDENTITY-20261029.
local cfg=X4LAN_CONFIG
if not cfg or cfg.native_gate_player_identity~=true then return end
local K=assert(X4LAN_PLAYER_IDENTITY_CORE,'player identity core missing')
assert(cfg.native_gate_world_identities==true,'player identity resolves saved IDs: enable native_gate_world_identities')
-- The base mod's asynchronous player factions decide ownership per machine: never in lockstep.
assert(cfg.player_factions~=true,'player identity excludes the asynchronous player_factions feature')
assert(not (cfg.native_gate_player_identity_block_orders and cfg.native_gate_player_orders_capture),
 'the identity order block excludes the player orders UI capture (both wrap the order menus)')
local P,why=K.validate(cfg.native_gate_player_identity_plan)
assert(P,'invalid player identity plan: '..tostring(why))
local canonical=K.canonical(P)
local ffi=require('ffi');local C=ffi.C
if not pcall(ffi.typeof,'UniverseID') then ffi.cdef('typedef uint64_t UniverseID;') end
local function cdef(name,def) if not pcall(function() return C[name] end) then pcall(ffi.cdef,def) end end
cdef('GetNumAllFactions','uint32_t GetNumAllFactions(bool includehidden);')
cdef('GetAllFactions','uint32_t GetAllFactions(const char** result, uint32_t resultlen, bool includehidden);')
cdef('GetNumAllFactionShips','uint32_t GetNumAllFactionShips(const char* factionid);')
cdef('GetAllFactionShips','uint32_t GetAllFactionShips(UniverseID* result, uint32_t resultlen, const char* factionid);')
cdef('IsComponentOperational','bool IsComponentOperational(UniverseID componentid);')
local function log(text) DebugError('[X4LAN] '..text) end
local gate,digest,steps
local start_objects={}   -- start index -> runtime ID (decimal string), checked at the fold tick
local last_tick,setup_done,ready=0,false,false
local inbox,ships,md_text,state_rows,blocked={}, {},nil,nil,0

local function decimal(id)
 local kind=type(id)
 if kind=='number' then return string.format('%.0f',id) end
 if kind~='cdata' then id=ConvertIDTo64Bit(id) end
 local text=(tostring(id):gsub('ULL$',''))
 assert(text:match('^%d+$'),'invalid runtime component ID')
 return text
end
-- MD answers: one result row per step, and the object of every created slot ship. Arrival can
-- lag the MD frame; rows are only read at the fixed fold tick (setup + K.FOLD_LAG).
RegisterEvent('X4LAN_PlayerIdentity.Result',function(_,raw) inbox[#inbox+1]=tostring(raw) end)
for k=1,#P.slots do
 RegisterEvent('X4LAN_PlayerIdentity.Ship'..k,function(_,object)
  assert(P.slots[k].create and not ships[k],'unexpected created slot ship '..k)
  ships[k]=decimal(object)
 end)
end

-- Saved -> runtime for the slot ships, start ships and saved sectors only: one scan over every
-- faction's ships and every sector at the setup tick. A missing or ambiguous ID fails closed.
local function resolve_saved(tick)
 local wanted,found,count={},{},0
 for _,row in ipairs(P.slots) do
  if row.ship then wanted[row.ship]='ship' end
  if row.create and row.create.sector~='player' then wanted[row.create.sector]='sector' end
 end
 for _,r in ipairs(P.starts) do
  wanted[r.object]=wanted[r.object] or 'start ship'
  if K.saved(r.sector) then wanted[r.sector]='sector' end
 end
 for _ in pairs(wanted) do count=count+1 end
 if count==0 then return found end
 local function add(id)
  local runtime=decimal(id)
  local saved=gate.saved_identity(runtime)
  if wanted[saved] then
   assert(not found[saved] or found[saved]==runtime,'player identity: ambiguous saved ID '..saved)
   found[saved]=runtime
  end
 end
 local n=tonumber(C.GetNumAllFactions(true))
 local names=ffi.new('const char*[?]',n+1)
 n=tonumber(C.GetAllFactions(names,n+1,true))
 local factions={};for i=0,n-1 do factions[#factions+1]=ffi.string(names[i]) end
 table.sort(factions)
 for _,faction in ipairs(factions) do
  local m=tonumber(C.GetNumAllFactionShips(faction))
  local buffer=ffi.new('UniverseID[?]',m+1)
  m=tonumber(C.GetAllFactionShips(buffer,m+1,faction))
  for i=0,m-1 do add(buffer[i]) end
 end
 for _,cluster in ipairs(GetClusters(true) or {}) do
  for _,sector in ipairs(GetSectors(cluster) or {}) do add(sector) end
 end
 for saved,kind in pairs(wanted) do assert(found[saved],'player identity: unmapped slot '..kind..' '..saved) end
 return found
end

-- MD event arguments: flat values only; runtime IDs stay in this process (Lua IDs for MD).
local function md_args(step,found)
 local a={ordinal=step.ordinal,ordname=tostring(step.ordinal),step=step.step,head=step.head,mode=P.mode,faction=step.faction or '',
  name=step.name or '',short=step.short or '',mirror=step.mirror or 0,source=step.source or '',
  occupiedpool=step.occupiedpool or 0,fa=step.fa or '',fb=step.fb or '',value=step.value or 0,
  slotname=tostring(step.slot),macro='',seed=0,x=0,y=0,z=0,yaw=0,hassector=0,sectormacro=''}
 if step.source=='saved' then a.object=ConvertStringToLuaID(found[step.ship]) end
 if step.source=='create' then
  local c=step.create
  a.macro,a.seed,a.x,a.y,a.z,a.yaw=c.macro,c.seed,c.x,c.y,c.z,c.yaw
  if c.sector~='player' then a.hassector=1;a.sector=ConvertStringToLuaID(found[c.sector]) end
 end
 if step.step=='start' then
  local r=step.start
  a.object=ConvertStringToLuaID(found[r.object])
  -- Saved sectors resolve here; a macro name is resolved by MD find_sector (the scan above only sees
  -- the clusters the player knows).
  if K.saved(r.sector) then a.sector,a.hassector=ConvertStringToLuaID(found[r.sector]),1 else a.sectormacro=r.sector end
  a.x,a.y,a.z,a.yaw=r.x,r.y,r.z,r.yaw
 end
 return a
end

-- Portable identity of a slot ship: its saved ID, or the created ID the gate assigned after the
-- creating frame (native_gate_created_identities), else the plan label 'created<k>'.
local function portable(k)
 local row=P.slots[k]
 if row.ship then return row.ship end
 local id=gate.created_identity and gate.created_identity(ships[k])
 return (K.saved(id) and id) or 'created'..k
end
local function slot_rows()
 local rows={}
 for k,row in ipairs(P.slots) do
  local runtime=ships[k]
  local text=k..'/'..portable(k)
  if runtime and C.IsComponentOperational(ConvertStringTo64Bit(runtime)) then
   local owner=GetComponentData(ConvertStringToLuaID(runtime),'owner')
   text=text..'/'..tostring(owner)
  else text=text..'/dead' end
  rows[#rows+1]=text..'/'..row.faction
 end
 return table.concat(rows,',')
end

-- Optional for human sessions without the orders adapter: the stock order menus would create
-- orders on one client only. Refuse their order entry points locally (logged, never applied).
local ORDER_MUTATORS={
 InteractMenu={'buttonAssignCommander','buttonAssignFleetUnitCommander','buttonChangeAssignment','buttonChangeFleetUnitAssignment',
  'buttonChangeOverrideOrder','buttonFormationShape','buttonRecallSubordinates','buttonRemoveAssignment','buttonRemoveOrder',
  'buttonRemoveAllOrders','buttonTriggerSyncPoint','setOrderImmediate'},
 MapMenu={'buttonConfirmTrade','buttonConfirmTradeLoop','buttonDefaultOrderConfirm','buttonDeleteAllOrders','buttonDockToTrade',
  'buttonNewOrder','buttonOrderDown','buttonOrderUp','buttonReleaseSyncPoint','buttonRemoveAssignment','buttonSetOrderParam',
  'buttonStartOrders','checkboxOrderPlayerOverride','checkboxOrderPlayerOverrideValue','dropdownBehaviourFormation',
  'dropdownChangeOverrideOrder','dropdownNewSyncPoint','removeOrder','setOrderParamFromMode','slidercellSetOrderParam'},
}
local hooked={}
local function block_orders()
 if cfg.native_gate_player_identity_block_orders~=true or (hooked.InteractMenu and hooked.MapMenu) then return end
 for _,menu in ipairs(Menus or {}) do
  local names=ORDER_MUTATORS[menu.name]
  if names and not hooked[menu.name] then
   hooked[menu.name]=true
   local seen={}
   for name,value in pairs(menu) do if type(value)=='function' and name:match('^order%u') then seen[name]=true end end
   for _,name in ipairs(names) do if type(menu[name])=='function' then seen[name]=true end end
   local list={};for name in pairs(seen) do list[#list+1]=name end
   table.sort(list)
   for _,name in ipairs(list) do
    local menu_name=menu.name
    menu[name]=function()
     blocked=blocked+1
     log('PLAYER_IDENTITY_ORDER_BLOCKED menu='..menu_name..' entry='..name)
     return 0
    end
   end
   log('PLAYER_IDENTITY ORDER_BLOCK menu='..menu.name..' blocked='..#list)
  end
 end
end

X4LAN_NATIVE_PLAYER_IDENTITY={
 -- clients: protocol 3 client count (nil on protocol 2, which has two clients).
 bind=function(native,clients)
  assert(type(native)=='table' and type(native.saved_identity)=='function','player identity needs gate.saved_identity')
  assert(#P.slots==(clients or 2),'player identity plan has '..#P.slots..' slots for '..tostring(clients or 2)..' clients')
  gate=native
  log(string.format('PLAYER_IDENTITY BOUND slot=%s mode=%s slots=%d tick=%d',tostring(cfg.slot),P.mode,#P.slots,P.tick))
 end,
 -- Once per admitted tick, after the fixture events of that tick. Returns the declared state part.
 frame=function(tick)
  assert(gate,'player identity not bound to the native gate')
  assert(type(tick)=='number' and tick>last_tick,'player identity tick must increase')
  if not digest then
   -- The base mod's SHA-256 loads after the overlay adapters (live pf3 logged the raw plan text when
   -- this ran at bind): digest and steps are fixed at the first gated tick, before any step is raised.
   assert(X4LAN_SHA256 and X4LAN_SHA256.digest,'player identity needs X4LAN_SHA256')
   digest=X4LAN_SHA256.digest(canonical)
   steps=K.steps(P,digest)
   log(string.format('PLAYER_IDENTITY ARMED slot=%s mode=%s slots=%d tick=%d steps=%d occupied=%s reputation=%s plan=%s',
    tostring(cfg.slot),P.mode,#P.slots,P.tick,#steps,P.occupied,P.reputation,digest))
  end
  last_tick=tick
  block_orders()
  if tick<P.tick then return 'PIDENT_1:'..digest..':pending' end
  if tick==P.tick then
   local found=resolve_saved(tick)
   for k,row in ipairs(P.slots) do if row.ship then ships[k]=found[row.ship] end end
   for i,r in ipairs(P.starts) do start_objects[i]=found[r.object] end
   for _,step in ipairs(steps) do AddUITriggeredEvent('X4LAN_PlayerIdentity','Step',md_args(step,found)) end
   setup_done=true
   local saved={};for k,row in ipairs(P.slots) do saved[k]=k..'/'..(row.ship or 'create') end
   log(string.format('PLAYER_IDENTITY SETUP tick=%d steps=%d slots=%s',tick,#steps,table.concat(saved,',')))
   return 'PIDENT_1:'..digest..':setup:'..table.concat(saved,',')
  end
  assert(setup_done,'player identity setup tick '..P.tick..' was not admitted')
  if tick<P.tick+K.FOLD_LAG then return 'PIDENT_1:'..digest..':applying' end
  if not ready then
   local text,reason=K.results(steps,inbox)
   assert(text,'player identity MD results: '..tostring(reason))
   for k,row in ipairs(P.slots) do assert(ships[k],'player identity: created slot ship '..k..' not reported') end
   md_text=X4LAN_SHA256.digest(text)
   -- Start placements apply after the MD frame: each start ship must now be in its planned sector.
   for i,r in ipairs(P.starts) do
    local sector=GetComponentData(ConvertStringToLuaID(start_objects[i]),'sectorid')
    local got=sector and (K.saved(r.sector) and gate.saved_identity(decimal(sector)) or GetComponentData(sector,'macro')) or 'none'
    assert(got==r.sector,'player identity: start '..i..' ship '..r.object..' is in '..tostring(got)..', not '..r.sector)
    log(string.format('PLAYER_IDENTITY START tick=%d start=%d object=%s sector=%s',tick,i,r.object,r.sector))
   end
   ready=true;inbox={}
   log(string.format('PLAYER_IDENTITY READY tick=%d steps=%d md=%s',tick,#steps,md_text))
  end
  assert(#inbox==0,'player identity: unexpected MD result after the fold tick')
  local rows=slot_rows()
  if rows~=state_rows or tick%600==0 then log('PLAYER_IDENTITY_STATE tick='..tick..' '..rows) end
  state_rows=rows
  return 'PIDENT_1:'..digest..':'..md_text..':'..rows
 end,
 -- Slot ship runtime ID (decimal string) once known; for other adapters and tests.
 ship=function(k) return ships[k] end,
 plan=P,
 state={canonical=canonical,blocked=function() return blocked end,steps=function() return steps end},
}
