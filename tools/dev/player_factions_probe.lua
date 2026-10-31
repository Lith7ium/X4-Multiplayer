-- Unshipped UI half of tools/dev/player_factions_probe.xml (install as ui/x4lan_pf_probe.lua).
-- Logs what the vanilla UI sees of pool faction x4lan_player2: the diplomacy list source
-- (GetLibrary("factions"), menu_diplomacy.lua), faction data, the UI relation and a ship's owner.
local ship
local function log(v) DebugError('[X4LAN] PFPROBE UI '..v) end
local function try(f) local ok,a,b,c,d,e,g=pcall(f);if not ok then return 'error:'..tostring(a) end;return a,b,c,d,e,g end
RegisterEvent('X4LAN_PFProbe.Ship',function(_,object) ship=ConvertIDTo64Bit(object) end)
RegisterEvent('X4LAN_PFProbe.Check',function(_,phase)
 local listed,pool,total='no',0,0
 local lib=try(function() return GetLibrary('factions') end)
 if type(lib)=='table' then
  for _,entry in ipairs(lib) do
   total=total+1
   if tostring(entry.id):match('^x4lan_player') then pool=pool+1 end
   if entry.id=='x4lan_player2' then listed='yes name='..tostring(entry.name) end
  end
 end
 log(string.format('phase=%s library=%s pool_listed=%d listed=%s',tostring(phase),tostring(total),pool,listed))
 local name,short,active,locked,diplomacy=try(function() return GetFactionData('x4lan_player2','name','shortname','isactive','isrelationlocked','isdiplomacyactive') end)
 log(string.format('phase=%s faction name=%s short=%s active=%s locked=%s diplomacy=%s uirelation=%s',tostring(phase),tostring(name),tostring(short),
  tostring(active),tostring(locked),tostring(diplomacy),tostring(try(function() return GetUIRelation('x4lan_player2') end))))
 if ship then
  local owner,ownername,playerowned=try(function() return GetComponentData(ship,'owner','ownername','isplayerowned') end)
  log(string.format('phase=%s ship owner=%s ownername=%s playerowned=%s',tostring(phase),tostring(owner),tostring(ownername),tostring(playerowned)))
 end
end)
log('armed')
