-- Player factions: pure session logic (no engine calls; LuaJIT-tested). Config flag player_factions.
-- Every human slot pK owns pool faction x4lan_playerK (libraries/factions.xml). Each client maps
-- its OWN human to the real `player` faction and every other human to that human's pool faction.
-- Slot p1 is the authority for inter-player relations and sector ownership: in 7.x sessions p1 is
-- client 1 (a human that also runs the authority world); on a dedicated server (cfg.dedicated) p1
-- is the server, which has no local human, so there every human maps to its pool faction.
-- Wire (ASCII version-1 envelope; relays without support drop these, the feature then disables):
--   client -> relay  PFID|1|lease|seq|nameHex|icon                 own display identity
--   relay -> client  PFROSTER|1|lease|slot|nameHex|icon|seq         every connected human
--   host   -> relay  PFREL|1|lease|rev|a|b|value                    authoritative relation a<b
--   relay -> client  PFREL|1|lease|rev|a|b|value
--   guest  -> relay  PFREQ|1|lease|seq|target|value                 relation intent (to host)
--   relay -> host    PFREQ|1|hostLease|from|authoritySeq|target|value (relay-stamped, per host lease)
--   client -> relay  PFSEC|1|lease|seq|sectorMacro|owner            owner = S<index> or faction id
--   relay -> client  PFSEC|1|lease|seq|sectorMacro|owner            host rows; guest claims to host
X4LAN_PLAYERS_CORE={}
local K=X4LAN_PLAYERS_CORE
K.POOL=16
K.RELATIONS={allied=0.75,neutral=0,hostile=-0.5}
K.DEFAULT=K.RELATIONS.allied
function K.index(slot)
 local n=type(slot)=='string' and slot:match('^p(%d+)$')
 n=n and tonumber(n)
 if n and n>=1 and n<=K.POOL and n%1==0 then return n end
end
function K.poolFaction(index) return 'x4lan_player'..index end
-- Per-slot pool icon (libraries/icons.xml); the player's own logo faction_player is never used for another human.
function K.poolIcon(index) return 'x4lan_player'..index end
-- The gamestart's default commander name (libraries/gamestarts.xml x4lan_internal): every client that
-- did not rename itself announces it, so it identifies nobody.
K.DEFAULT_NAMES={['Multiplayer Pilot']=true}
-- Name shown for another human `index` on this machine: the announced name, unless it is empty, the
-- default name, or equal to this machine's own human (then two factions would look alike): "Player N".
function K.displayName(name,index,localName)
 if type(name)~='string' or name=='' or K.DEFAULT_NAMES[name] or (localName and name==localName) then
  return 'Player '..index
 end
 return name
end
function K.displayIcon(icon,index)
 if not K.atom(icon) or icon=='faction_player' then return K.poolIcon(index) end
 return icon
end
function K.poolIndex(faction)
 local n=type(faction)=='string' and faction:match('^x4lan_player(%d+)$');n=n and tonumber(n)
 if n and n>=1 and n<=K.POOL and tostring(n)==faction:sub(13) then return n end
end
-- Faction that represents human `index` on the machine whose own human is `localIndex`
-- (localIndex nil: a server without a local human).
function K.factionFor(index,localIndex)
 if localIndex and index==localIndex then return 'player' end
 return K.poolFaction(index)
end
-- Local faction id -> machine-neutral owner token (S<index>) or the NPC faction id itself.
-- On a server `player` is no human (observer), so it has no token.
function K.tokenFor(faction,localIndex)
 if faction=='player' then return localIndex and 'S'..localIndex or nil end
 local n=K.poolIndex(faction);if n then return 'S'..n end
 return faction
end
function K.factionForToken(token,localIndex)
 local n=type(token)=='string' and token:match('^S(%d+)$');n=n and tonumber(n)
 if n then if n>=1 and n<=K.POOL then return K.factionFor(n,localIndex) end;return nil end
 -- Human factions travel only as S<index>: a literal player/pool id would mean another machine's human.
 if token=='player' or K.poolIndex(token) then return nil end
 if K.atom(token) then return token end
end
-- Owner of shared co-op ship on this machine, from its current pilot slot ('0' = nobody at the
-- controls). A ship flown by another human shows as that human's faction; a free ship stays
-- `player` (on every machine), so either human can still take it over (handoff) and dock it.
function K.coopOwner(pilot,localIndex)
 local n=K.index(pilot)
 if not n or n==localIndex then return 'player' end
 return K.poolFaction(n)
end
function K.atom(s) return type(s)=='string' and #s>0 and #s<=64 and not s:find('[^a-zA-Z0-9_]') end
function K.macro(s) return type(s)=='string' and #s<=96 and s:match('^[a-z0-9_]+_macro$')~=nil end
function K.relation(v)
 if type(v)=='string' then v=K.RELATIONS[v] or tonumber(v) end
 if type(v)~='number' or v~=v or v<-1 or v>1 then return nil end
 return math.floor(v*10000+.5)/10000
end
local function utf8(s)
 local i=1
 while i<=#s do
  local c=s:byte(i);local n=c<128 and 1 or c>=194 and c<=223 and 2 or c>=224 and c<=239 and 3 or c>=240 and c<=244 and 4
  if not n or i+n-1>#s then return false end
  for j=1,n-1 do local b=s:byte(i+j);if b<128 or b>191 then return false end end
  i=i+n
 end
 return true
end
-- Plain-text names only (no '{page,line}' references, no markup or control characters).
function K.cleanName(s)
 if type(s)~='string' then return nil end
 s=s:gsub('[%z\1-\31\127]',''):gsub('^%s+',''):gsub('%s+$','')
 if s:find('[{}\\]') then s=s:gsub('[{}\\]','') end
 if #s>48 then
  s=s:sub(1,48);while #s>0 and not utf8(s) do s=s:sub(1,-2) end
 end
 if #s==0 or not utf8(s) then return nil end
 return s
end
function K.encode(s) return (s:gsub('.',function(c) return string.format('%02x',c:byte()) end)) end
function K.decode(h)
 if type(h)~='string' or #h==0 or #h>96 or #h%2~=0 or h:find('[^0-9a-f]') then return nil end
 local s=h:gsub('..',function(c) return string.char(tonumber(c,16)) end)
 local clean=K.cleanName(s);if clean~=s then return nil end
 return s
end
function K.shortName(name)
 local short=name:gsub('%s+','')
 if #short>8 then short=short:sub(1,8);while #short>0 and not utf8(short) do short=short:sub(1,-2) end end
 return #short>0 and short or name
end
local function integer(s,limit)
 if type(s)~='string' or not s:match('^%d+$') or #s>10 then return nil end
 local n=tonumber(s);if n>(limit or 2147483647) then return nil end;return n
end
K.integer=integer

-- io: send(packet), md(action,args), log(text). cfg: slot, player name/icon.
function K.new(cfg,io)
 local slotIndex=assert(K.index(cfg.slot),'invalid slot')
 local server=cfg.dedicated==true
 assert(not server or slotIndex==1,'a dedicated server must be slot p1')
 -- localIndex: this machine's own human (nil on a server). Authority: slot p1.
 local localIndex=not server and slotIndex or nil
 local p={local_=localIndex,server=server,host=slotIndex==1,roster={},assigned={},relations={},applied={},
  seq=0,relRev=0,secSeq=0,sectors={},hostSectors={},claims={},requestOrders={},io=io}
 p.authority=p.host
 function p.faction(index) return K.factionFor(index,localIndex) end
 function p.coopOwner(pilot) return K.coopOwner(pilot,localIndex) end
 -- Incoming owner id -> local faction (the pool id of this machine's own human is `player`).
 function p.localFaction(faction)
  local n=K.poolIndex(faction)
  if n then return K.factionFor(n,localIndex) end
  return faction
 end
 function p.pair(a,b) if a>b then a,b=b,a end;return a..'-'..b,a,b end
 -- Own identity; resent periodically by the frame. A server has no human to announce.
 function p.identity(lease,name,icon)
  if not localIndex then return false end
  name=K.cleanName(name) or ('LAN Player '..localIndex)
  icon=K.atom(icon) and icon or 'faction_player'
  -- A changed own name changes which announced names collide with it: reassign everybody.
  if p.name~=name then p.roster={} end
  p.name,p.icon=name,icon
  p.seq=p.seq+1
  io.send(table.concat({'PFID','1',lease,p.seq,K.encode(name),icon},'|'))
  return true
 end
 local function applyRelation(a,b,value,rev,source)
  local key=p.pair(a,b)
  local old=p.applied[key]
  if old and old.value==value then old.rev=rev;return false end
  p.applied[key]={value=value,rev=rev}
  io.md('Relation',{a=a,b=b,fa=p.faction(a),fb=p.faction(b),value=value})
  io.log(string.format('PLAYER_FACTIONS RELATION a=p%d b=p%d %s<->%s value=%.4f rev=%d source=%s',
   a,b,p.faction(a),p.faction(b),value,rev,source))
  return true
 end
 -- Host authority: set and publish a relation between humans a and b.
 function p.authorRelation(lease,a,b,value,source)
  if not p.host then return false end
  value=K.relation(value)
  if not value or type(a)~='number' or type(b)~='number' or a==b or a<1 or b<1 or a>K.POOL or b>K.POOL then return false end
  local key,lo,hi=p.pair(a,b)
  local r=p.relations[key]
  if r and r.value==value then return true end
  p.relRev=p.relRev+1
  p.relations[key]={a=lo,b=hi,value=value,rev=p.relRev}
  applyRelation(lo,hi,value,p.relRev,source)
  if lease then io.send(table.concat({'PFREL','1',lease,p.relRev,lo,hi,string.format('%.4f',value)},'|')) end
  return true
 end
 -- Intent: X4LAN_PLAYERS.setRelation(other, value). Host applies; a guest asks the host.
 function p.setRelation(lease,other,value)
  local target=type(other)=='number' and other or K.index(other)
  value=K.relation(value)
  if not localIndex then return false,'no_local_human' end
  if not target or target==localIndex or not value then return false,'invalid' end
  if p.host then return p.authorRelation(lease,localIndex,target,value,'local'),'applied' end
  if not lease then return false,'offline' end
  p.seq=p.seq+1
  io.send(table.concat({'PFREQ','1',lease,p.seq,target,string.format('%.4f',value)},'|'))
  io.log(string.format('PLAYER_FACTIONS RELATION_REQUEST target=p%d value=%.4f',target,value))
  return true,'requested'
 end
 function p.republish(lease)
  if not p.host then return end
  for _,r in pairs(p.relations) do
   io.send(table.concat({'PFREL','1',lease,r.rev,r.a,r.b,string.format('%.4f',r.value)},'|'))
  end
 end
 -- Roster row for human `index`. Returns true when MD must (re)assign the identity.
 local function roster(lease,index,name,icon,now)
  local row=p.roster[index]
  if not row or row.name~=name or row.icon~=icon then
   p.roster[index]={name=name,icon=icon,at=now,shown=K.displayName(name,index,p.name)}
   if index~=localIndex then
    local shown,picture=K.displayName(name,index,p.name),K.displayIcon(icon,index)
    io.md('Assign',{index=index,faction=K.poolFaction(index),name=shown,short=K.shortName(shown),icon=picture})
    io.log(string.format('PLAYER_FACTIONS ASSIGN slot=p%d faction=%s name=%q announced=%q icon=%s',index,K.poolFaction(index),shown,name,picture))
   else
    io.log(string.format('PLAYER_FACTIONS ASSIGN slot=p%d faction=player pool=%s name=%q (local human)',index,K.poolFaction(index),name))
   end
   -- Default: every pair of humans starts allied (authority only; mirrored to peers).
   if p.host then
    for other in pairs(p.roster) do
     if other~=index and not p.relations[p.pair(other,index)] then
      p.authorRelation(lease,other,index,K.DEFAULT,'default')
     end
    end
   end
  else row.at=now end
 end
 -- Local sector owners reported by MD: macro -> local faction id.
 function p.localSector(lease,macro,faction)
  if not K.macro(macro) or not K.atom(faction) then return end
  local token=K.tokenFor(faction,localIndex)
  if not token then return end
  local old=p.sectors[macro];p.sectors[macro]=token
  if old==token then return end
  if p.host then
   p.secSeq=p.secSeq+1
   if lease then io.send(table.concat({'PFSEC','1',lease,p.secSeq,macro,token},'|')) end
   io.log(string.format('PLAYER_FACTIONS SECTOR macro=%s owner=%s token=%s source=authority',macro,faction,token))
  elseif localIndex and token=='S'..localIndex and p.hostSectors[macro]~=token then
   -- A local claim by this human: ask the authority (never applied here first).
   p.secSeq=p.secSeq+1;p.claims[macro]=true
   if lease then io.send(table.concat({'PFSEC','1',lease,p.secSeq,macro,token},'|')) end
   io.log(string.format('PLAYER_FACTIONS SECTOR_CLAIM macro=%s token=%s',macro,token))
  end
 end
 function p.republishSectors(lease)
  if not p.host or not lease then return end
  for macro,token in pairs(p.sectors) do
   if token:match('^S%d+$') then
    p.secSeq=p.secSeq+1
    io.send(table.concat({'PFSEC','1',lease,p.secSeq,macro,token},'|'))
   end
  end
 end
 -- Incoming relay packet (fields split on '|'). Returns true if it was ours.
 function p.receive(lease,f,now)
  local kind=f[1]
  if kind=='PFROSTER' then
   local index=integer(f[4],K.POOL);local name=K.decode(f[5])
   if #f==7 and index and index>=1 and name and K.atom(f[6]) and integer(f[7]) then roster(lease,index,name,f[6],now) end
   p.relayAt=now
   return true
  elseif kind=='PFREL' then
   if p.host or #f~=7 then return true end
   local rev,a,b=integer(f[4]),integer(f[5],K.POOL),integer(f[6],K.POOL);local value=K.relation(tonumber(f[7]))
   if not rev or not a or not b or a<1 or a>=b or not value then return true end
   local key=p.pair(a,b);local old=p.applied[key]
   if old and rev<=old.rev then return true end
   applyRelation(a,b,value,rev,'authority')
   return true
  elseif kind=='PFREQ' then
   if not p.host or #f~=7 then return true end
   local from,target=integer(f[4],K.POOL),integer(f[6],K.POOL);local value=K.relation(tonumber(f[7]))
   local order=integer(f[5])
   -- A human may only change relations it is part of.
   if from and target and from>=1 and target>=1 and from~=target and value and order and order>0 and p.roster[from] then
    -- The outer client has authenticated the destination lease. Reset only for a
    -- new authority lease, never when another human reconnects or refreshes identity.
    if p.requestLease~=lease then p.requestLease=lease;p.requestOrders={} end
    local pair=p.pair(from,target)
    if not p.requestOrders[pair] or order>p.requestOrders[pair] then
     if p.authorRelation(lease,from,target,value,'request:p'..from) then p.requestOrders[pair]=order end
    end
   end
   return true
  elseif kind=='PFSEC' then
   if #f~=6 or not integer(f[4]) or not K.macro(f[5]) then return true end
   local macro,token=f[5],f[6]
   local faction=K.factionForToken(token,localIndex);if not faction then return true end
   if p.host then
    -- Guest claim intent, relay-checked to be the sender's own token. Validate again.
    local n=tonumber(token:match('^S(%d+)$') or '')
    if n and n~=localIndex and p.roster[n] and p.sectors[macro]~=token then
     io.md('Sector',{macro=macro,faction=faction})
     io.log(string.format('PLAYER_FACTIONS SECTOR_APPLY macro=%s token=%s faction=%s source=claim',macro,token,faction))
    end
   else
    p.hostSectors[macro]=token;p.claims[macro]=nil
    if p.sectors[macro]~=token then
     io.md('Sector',{macro=macro,faction=faction})
     io.log(string.format('PLAYER_FACTIONS SECTOR_APPLY macro=%s token=%s faction=%s source=authority',macro,token,faction))
    end
   end
   return true
  end
  return false
 end
 -- Host ship ownership for replication: a player-owned ship that did not exist when the session
 -- baseline was taken belongs to the host's human only (the shared co-op fixtures and the
 -- pristine save's own player assets exist in both worlds and stay `player` everywhere).
 function p.personalOwner(key)
  if not p.host or not localIndex or not p.baseline or p.baseline[key] then return nil end
  return K.poolFaction(localIndex)
 end
 return p
end
