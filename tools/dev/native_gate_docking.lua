-- Read-only docking observations for two disposable native station anchors.
local cfg=X4LAN_CONFIG
if not cfg or not cfg.native_gate_docking_state then return end
local ffi=require('ffi')
if not pcall(ffi.typeof,'UniverseID') then ffi.cdef('typedef uint64_t UniverseID;') end
if not pcall(ffi.typeof,'UIPosRot') then ffi.cdef('typedef struct {float x,y,z,yaw,pitch,roll;} UIPosRot;') end
ffi.cdef[[
 UniverseID GetContextByClass(UniverseID,const char*,bool);
 uint32_t GetNumDockedShips(UniverseID,const char*);
 uint32_t GetDockedShips(UniverseID*,uint32_t,UniverseID,const char*);
 UIPosRot GetObjectPositionInSector(UniverseID);
 bool IsComponentOperational(UniverseID);
]]
local anchors={}
local colliders,registered={},{}
for i=1,2 do
 RegisterEvent('X4LAN_NativeGate.DockAnchor'..i,function(_,object) anchors[i]=ConvertIDTo64Bit(object) end)
 RegisterEvent('X4LAN_NativeGate.DockCollider'..i,function(_,object) colliders[i]=ConvertIDTo64Bit(object) end)
end
local function flag(value)
 assert(type(value)=='boolean','missing native docking flag')
 return value and '1' or '0'
end
local function number(value)
 assert(type(value)=='number' and value==value and math.abs(value)<math.huge,'invalid native docking number')
 return string.format('%.17g',value)
end
local function text(value)
 assert(type(value)=='string' and #value>0 and #value<=96 and not value:find('[^%w_.%-]'),'invalid native docking text')
 return value
end
X4LAN_NATIVE_DOCKING=function(actors,tick,gate)
 if cfg.native_gate_docking_shape_rng then
  for i=1,2 do
   if not registered[i] then
    if anchors[i] and colliders[i] then
     assert(gate and gate.register_dock_collider(i,tonumber(anchors[i]),tonumber(colliders[i]))==1,'native dock collider registration refused')
     registered[i]=true
    else assert(tick<=7,'native dock collider missing') end
   end
  end
 end
 local ships,stations={},{}
 for i=1,12 do
  local object=assert(actors[i],'missing docking actor')
  if ffi.C.IsComponentOperational(object) then
   local docked,docking,stored=GetComponentData(object,'isdocked','isdocking','isinternallystored')
   local parent=ffi.C.GetContextByClass(object,'container',false)
   local target=0
   if parent~=0 then
    for j=1,2 do if parent==anchors[j] then target=j end end
    assert(target~=0,'ship docked outside the disposable anchors')
   end
   ships[i]=table.concat({'DOCK_1',flag(docked),flag(docking),flag(stored),target},';')
  else ships[i]='dead' end
 end
 for i=1,2 do
  local object=anchors[i]
  if not object then
   assert(tick<=7,'native docking anchor missing');stations[i]='pending'
  else
   assert(ffi.C.IsComponentOperational(object),'native docking anchor destroyed')
   local macro,owner,hull,shield=GetComponentData(object,'macro','owner','hullpercent','shieldpercent')
   local p=ffi.C.GetObjectPositionInSector(object)
   local count=tonumber(ffi.C.GetNumDockedShips(object,nil))
   assert(count>=0 and count<=12,'unexpected anchor docking population')
   local docked=ffi.new('UniverseID[?]',math.max(1,count))
   assert(tonumber(ffi.C.GetDockedShips(docked,count,object,nil))==count,'anchor population changed during sample')
   local members,seen={},{}
   for index=0,count-1 do
    local identity
    for actor=1,12 do if docked[index]==actors[actor] then identity=actor end end
    assert(identity and not seen[identity],'unknown or duplicate ship at native docking anchor')
    seen[identity]=true;members[#members+1]=identity
   end
   table.sort(members)
   stations[i]=table.concat({'ANCHOR_1',i,text(macro),text(owner),number(hull),number(shield),
    table.concat({number(p.x),number(p.y),number(p.z),number(p.yaw),number(p.pitch),number(p.roll)},','),count,
    count>0 and table.concat(members,',') or 'none'},';')
  end
 end
 return ships,stations
end
