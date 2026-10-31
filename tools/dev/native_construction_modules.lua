-- Inactive passive census for the single-module construction fixture.
-- The reviewed count/list path requires BOTH inclusion flags. No planned-module
-- query or transform getter is used. See the construction report's read audit.
-- Repeated observations are not an atomic snapshot or a native lifetime lease.
local ffi=require('ffi')
if not pcall(ffi.typeof,'UniverseID') then ffi.cdef('typedef uint64_t UniverseID;') end
ffi.cdef[[
 uint32_t GetNumStationModules(UniverseID,bool,bool);
 uint32_t GetStationModules(UniverseID*,uint32_t,UniverseID,bool,bool);
 bool IsComponentOperational(UniverseID);
]]
local M={}
local station_macro='station_gen_factory_base_01_macro'
local module_macro='struct_arg_base_02_macro'
local function exact(value)
 assert(ffi.istype('uint64_t',value) and value~=0,'module census requires exact native uint64 identity')
 local s=tostring(value):gsub('ULL$','')
 assert(s:match('^[1-9]%d*$'),'invalid exact module census identity')
 return s
end
local function flag(value)
 assert(type(value)=='boolean','module census requires native boolean')
 return value and 1 or 0
end
local function count(value)
 local n=tonumber(value)
 assert(n and n==math.floor(n) and n>=0 and n<=1,'module count outside single-module fixture')
 return n
end
local function metadata(id,expected,is_station)
 local key=exact(id)
 local object=ConvertStringTo64Bit(key)
 assert(exact(ConvertIDTo64Bit(object))==key,'module identity conversion changed native ID')
 local macro=GetComponentData(object,'macro')
 assert(type(macro)=='string' and macro==expected,'module census macro missing or unexpected')
 local operational=flag(ffi.C.IsComponentOperational(id))
 local construction=flag(IsComponentConstruction(object))
 assert(operational+construction<=1,'inconsistent module lifecycle flags')
 assert(not is_station or operational+construction==1,'construction station unavailable')
 return macro..','..operational..','..construction,operational,construction
end
function M.new(station,handle)
 local station_id=exact(station)
 assert(type(handle)=='string' and handle:match('^[0-9a-f]+$') and #handle==16,'module census requires stable station handle')
 local last,first_seen=0,0
 local retained_id,poisoned
 local function ids()
  local n=count(ffi.C.GetNumStationModules(station,true,true))
  local out=ffi.new('UniverseID[1]')
  -- Always offer capacity one, including when count was zero: a birth between
  -- calls must not be silently truncated into a matching empty list.
  assert(count(ffi.C.GetStationModules(out,1,station,true,true))==n,'module count/list race')
  assert(count(ffi.C.GetNumStationModules(station,true,true))==n,'module count changed after list')
  if n==0 then return nil,'-' end
  local id=ffi.new('uint64_t',out[0])
  return id,exact(id)
 end
 local function read()
  local s0,sop,scon=metadata(station,station_macro,true)
  local id,a=ids()
  local m0,mop,mcon='-',0,0
  if id then m0,mop,mcon=metadata(id,module_macro,false) end
  local _,b=ids()
  assert(a==b,'module identities unstable across metadata read')
  if id then
   local m1=metadata(id,module_macro,false)
   assert(m0==m1,'module metadata unstable across reread')
  end
  local _,c=ids()
  assert(a==c,'module identities unstable after metadata reread')
  assert(metadata(station,station_macro,true)==s0,'station metadata unstable across census')
  return a,sop,scon,mop,mcon
 end
 return {sample=function(tick)
  assert(not poisoned and type(tick)=='number' and tick==math.floor(tick) and tick>last and tick<=4294967295,'module sample stale or poisoned')
  local ok,raw,private=pcall(function()
   local id,sop,scon,mop,mcon=read()
   if last==0 then assert(id=='-','module census requires an observed empty baseline') end
   if retained_id then assert(id==retained_id,'observed module disappeared, was replaced or reused') end
   if id~='-' and not retained_id then retained_id=id;first_seen=tick end
   local present=id~='-' and 1 or 0
   local unknown=present==1 and mop+mcon==0 and 1 or 0
   local state=table.concat({'CONSTRUCTION_MODULES_1',tick,handle,
    'native_plan_link_supported=0','sector_pose_supported=0','atomic_snapshot=0','generation_lease=0',
    sop,scon,present,first_seen,present==1 and module_macro or '-',mop,mcon,unknown},':')
   local detail=table.concat({'CONSTRUCTION_MODULES_LOCAL_1',tick,handle,station_id,id},':')
   return state,detail
  end)
  if not ok then poisoned=true;error(raw) end
  last=tick
  return raw,private
 end}
end
return M
