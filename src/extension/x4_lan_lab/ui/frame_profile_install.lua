-- Loaded after every adapter. Installs frame_profile.lua's wrappers on the per-frame and
-- per-packet entry points that actually exist in this configuration. No-op unless enabled.
local P=X4LAN_FRAME_PROFILE
local A=X4LAN_BUBBLE_API
if not P or not A or P.ready then return end
P.ready=true
-- Section names are stable log keys. Chained adapters that live inline in another chain
-- (cargo and identity in universe, coop docks in coop) are part of that chain's self time.
local plan={
 {'coop','coop','frame'},{'coop.prepare','coop','prepare'},{'coop.rx','coop','receive'},
 {'health','health','frame'},{'health.rx','health','receive'},
 {'shot','shot','frame'},{'shot.rx','shot','receive'},
 {'shot_capture','shotCapture','frame'},{'shot_visual','shotVisual','frame'},
 {'projectile','projectiles','contextsFrame'},{'projectile','projectiles','captureFrame'},
 {'projectile','projectiles','renderFrame'},{'projectile.rx','projectiles','receive'},
 {'universe','universe','frame'},{'universe.rx','universe','receive'},
 {'npc_combat','npcCombat','frame'},{'npc_combat.rx','npcCombat','receive'},
 {'life','life','frame'},{'life.rx','life','receive'},
 {'walk','walk','frame'},{'walk.rx','walk','receive'},
 {'trade','trade','frame'},{'trade.rx','trade','receive'},
 {'missions','missions','frame'},{'missions.rx','missions','receive'},
 {'quest','quest','frame'},{'quest.rx','quest','receive'},
}
local installed={}
for _,row in ipairs(plan) do
 if P.install(row[1],A[row[2]],row[3]) then installed[#installed+1]=row[1]..'/'..row[3] end
end
if P.install('seed',rawget(_G,'X4LAN_SEED_GATE'),'frame') then installed[#installed+1]='seed/frame' end
-- Shared motion buffer used by both co-op ships and NPC replicas.
local motion=rawget(_G,'X4LAN_MOTION')
for _,field in ipairs({'push','render','velocity'}) do
 if P.install('motion',motion,field) then installed[#installed+1]='motion/'..field end
end
if type(A.frame)=='function' then
 A.frame=P.root(A.frame)
 X4LAN_MEASURE.log('FRAMECOST installed root=bubble wrappers='..#installed..' '..table.concat(installed,','))
else
 X4LAN_MEASURE.log('FRAMECOST inactive: bubble frame root unavailable')
end
