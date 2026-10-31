-- Player factions: engine glue for player_factions_core.lua (config flag player_factions).
-- Off in the inert source config, on in internal packages (distribution/session_launcher.py).
-- Data/MD/Lua only: the engine still treats only `player` as the player (see docs/PLAYER-FACTIONS.txt).
local cfg=X4LAN_CONFIG
if not cfg or not cfg.enabled or cfg.player_factions~=true then return end
local function log(v) DebugError('[X4LAN] '..v) end
local function disabled(reason) log('FEATURE player_factions DISABLED reason='..reason) end
local A,K=X4LAN_BUBBLE_API,X4LAN_PLAYERS_CORE
if not K then disabled('core_missing');return end
if not A or not A.state or not A.state.active then disabled('bubble_adapter_inactive');return end
if not cfg.coop then disabled('requires_coop');return end
if not K.index(cfg.slot) then disabled('invalid_slot');return end
local ffi=require('ffi');local C=ffi.C;local s=A.state
local function cdef(name,def) if not pcall(function() return C[name] end) then pcall(ffi.cdef,def) end end
cdef('GetPlayerName','const char* GetPlayerName(void);')
cdef('GetNumAllFactionShips','uint32_t GetNumAllFactionShips(const char*);')
cdef('GetAllFactionShips','uint32_t GetAllFactionShips(UniverseID*,uint32_t,const char*);')
local SETUP_TIMEOUT,RELAY_TIMEOUT=30,30
local pf={}
A.players=pf
local core
local function lease() return s.client and s.client.lease end
local function send(packet) if s.transport and lease() then s.transport:send(packet) end end
local io={send=send,md=function(action,args) AddUITriggeredEvent('X4LAN_PlayerFactions',action,args) end,log=log}
-- Shared co-op ships carry the faction of the human flying them (see coopFrame); a stopped feature
-- hands both back to `player` so the 7.x co-op paths see their original owners.
local function stop(reason)
 if pf.failed then return end
 pf.failed=reason;disabled(reason)
 if pf.coopSent then for i in pairs(pf.coopSent) do pcall(io.md,'CoopOwner',{ship=i,faction='player'}) end end
end
local function key(object) return (tostring(object):gsub('ULL$','')) end
-- Host: player-owned ships existing at session start exist in both worlds (shared fixtures,
-- the pristine save's assets); they stay `player` everywhere. Later ones are the host's own.
local function baseline()
 local set={}
 local n=tonumber(C.GetNumAllFactionShips('player'))
 local cap=n+64;local buffer=ffi.new('UniverseID[?]',cap)
 local count=tonumber(C.GetAllFactionShips(buffer,cap,'player'))
 assert(count<cap,'player fleet changed during baseline')
 for i=0,count-1 do set[key(buffer[i])]=true end
 return set,count
end
local function playerName()
 local ok,name=pcall(function() return ffi.string(C.GetPlayerName()) end)
 return ok and name or nil
end
function pf.receive(f,now)
 if not f[1] or f[1]:sub(1,2)~='PF' then return false end
 if pf.failed or not core or not pf.ready then return true end
 local ok,err=pcall(core.receive,lease(),f,now)
 if not ok then stop('error');log('PLAYER_FACTIONS STOPPED '..tostring(err)) end
 return true
end
function pf.localFaction(faction)
 if pf.failed or not core or not pf.ready then return faction end
 return core.localFaction(faction)
end
function pf.personalOwner(object)
 if pf.failed or not core or not pf.ready then return nil end
 return core.personalOwner(key(object))
end
-- Pilot of each shared co-op ship (coop_game.lua AUTH: '0' or the flying slot) -> wanted owner.
local function coopFrame()
 local c=A.coop;if not c or not c.pilots then return end
 pf.coopSent=pf.coopSent or {}
 -- Every shared co-op ship (2 in 7.x sessions, max_players-1 in dedicated 8-player sessions).
 local count=math.max(tonumber(c.count) or 0,#c.pilots,2)
 for i=1,count do
  local want=core.coopOwner(c.pilots[i] or '0')
  if pf.coopSent[i]~=want then
   pf.coopSent[i]=want
   local object=c.ships and c.ships[i]
   io.md('CoopOwner',{ship=i,faction=want,object=object and object~=0 and A.id(object) or nil})
   log('PLAYER_FACTIONS COOP_PILOT ship='..i..' pilot='..tostring(c.pilots[i] or '0')..' want='..want)
  end
 end
end
local function frame(now)
 if pf.failed or not s.ready or not lease() or A.paused() then return end
 if not core then core=K.new(cfg,io);pf.core=core;pf.startedAt=now end
 if not pf.setup then pf.setup=true;pf.setupAt=now;io.md('Setup',{});return end
 if not pf.ready then
  if pf.pool and pf.pool<K.POOL then stop('pool_missing count='..pf.pool);return end
  if pf.pool then
   pf.ready=true;pf.readyAt=now
   if core.host then core.baseline,pf.baselineCount=baseline() end
   for _,r in ipairs(pf.queued or {}) do core.localSector(lease(),r[1],r[2]) end;pf.queued=nil
   log('PLAYER_FACTIONS READY slot='..cfg.slot..' local='..(core.local_ and 'player pool='..K.poolFaction(core.local_) or 'none (server)')..
    ' authority='..tostring(core.host)..' baseline_ships='..tostring(pf.baselineCount or '-'))
  elseif now-pf.setupAt>=SETUP_TIMEOUT then stop('md_unavailable') end
  return
 end
 if now>=(pf.nextId or 0) then
  pf.nextId=now+2
  if core.local_ then core.identity(lease(),playerName(),cfg.player_faction_icon) end
  core.republish(lease())
 end
 coopFrame()
 if core.host and now>=(pf.nextSectors or 0) then pf.nextSectors=now+5;core.republishSectors(lease()) end
 if not core.relayAt and now-pf.readyAt>=RELAY_TIMEOUT then stop('relay_unsupported') end
end
function pf.frame(now)
 if pf.failed then return end
 local ok,err=pcall(frame,now)
 if not ok then stop('error');log('PLAYER_FACTIONS STOPPED '..tostring(err)) end
end
-- Public intent API (console/UI): X4LAN_PLAYERS.setRelation('p2','hostile'|'neutral'|'allied'|number)
X4LAN_PLAYERS={
 setRelation=function(other,value)
  if pf.failed or not core or not pf.ready then return false,'not_ready' end
  return core.setRelation(lease(),other,value)
 end,
 faction=function(slot) local n=K.index(slot);return n and core and core.faction(n) end,
 state=pf,
}
RegisterEvent('X4LAN_PlayerFactions.Ready',function(_,count) pf.pool=tonumber(count) or 0 end)
RegisterEvent('X4LAN_PlayerFactions.Assigned',function(_,text)
 local index,faction=tostring(text):match('^(%d+)|([%w_]+)$')
 if index and core then
  local row=core.roster[tonumber(index)]
  log('PLAYER_FACTIONS IDENTITY_APPLIED slot=p'..index..' faction='..faction..' name='..string.format('%q',row and (row.shown or row.name) or '?')..' announced='..string.format('%q',row and row.name or '?'))
 end
end)
RegisterEvent('X4LAN_PlayerFactions.CoopOwnerApplied',function(_,text) log('PLAYER_FACTIONS COOP_OWNER_APPLIED '..tostring(text)) end)
RegisterEvent('X4LAN_PlayerFactions.Mirror',function(_,text) log('PLAYER_FACTIONS RELATION_MIRROR '..tostring(text)) end)
RegisterEvent('X4LAN_PlayerFactions.RelationApplied',function(_,text) log('PLAYER_FACTIONS RELATION_APPLIED '..tostring(text)) end)
RegisterEvent('X4LAN_PlayerFactions.Refused',function(_,text) log('PLAYER_FACTIONS REFUSED '..tostring(text)) end)
RegisterEvent('X4LAN_PlayerFactions.Sector',function(_,text)
 local macro,faction=tostring(text):match('^([%w_]+)|([%w_]+)$')
 if not macro or pf.failed then return end
 if not core or not pf.ready then
  pf.queued=pf.queued or {};if #pf.queued<1024 then pf.queued[#pf.queued+1]={macro,faction} end
 else
  local ok,err=pcall(core.localSector,lease(),macro,faction)
  if not ok then stop('error');log('PLAYER_FACTIONS STOPPED '..tostring(err)) end
 end
end)
log('PLAYER_FACTIONS ARMED slot='..cfg.slot)
