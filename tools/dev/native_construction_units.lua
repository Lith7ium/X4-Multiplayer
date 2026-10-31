-- Inactive read-only logical inventory census. This is not physical drone identity.
-- Layouts/signatures match the pinned stock UI exports; no native fields are written.
local ffi=require('ffi')
if not pcall(ffi.typeof,'UniverseID') then ffi.cdef('typedef uint64_t UniverseID;') end
if not pcall(ffi.typeof,'UnitData') then
 ffi.cdef('typedef struct {const char* macro;const char* category;uint32_t amount;} UnitData;')
end
assert(ffi.sizeof('UnitData')==24 and ffi.offsetof('UnitData','macro')==0 and ffi.offsetof('UnitData','category')==8 and ffi.offsetof('UnitData','amount')==16,'unsupported native UnitData layout')
ffi.cdef[[
 uint32_t GetNumAllUnits(UniverseID,bool);
 uint32_t GetAllUnits(UnitData*,uint32_t,UniverseID,bool);
 uint32_t GetNumStoredUnits(UniverseID,const char*,bool);
 uint32_t GetNumUnavailableUnits(UniverseID,const char*);
 bool IsComponentOperational(UniverseID);
]]
local M={}
local expected_macro='ship_gen_xs_buildingdrone_01_a_macro'
local function count(value,maximum)
 local n=tonumber(value)
 assert(n and n==math.floor(n) and n>=0 and n<=maximum,'native unit count outside fixture bounds')
 return n
end
local function text(value,maximum)
 assert(value~=nil,'missing native unit text')
 -- The export owns this borrowed pointer. Bound the terminator search as well as
 -- the copied text; stable rereads detect observed changes, not arbitrary memory.
 local length=0
 while length<=maximum and value[length]~=0 do length=length+1 end
 assert(length<=maximum,'native unit text exceeds fixture bound')
 local s=ffi.string(value,length)
 assert(#s>0 and #s<=maximum and s:match('^[a-z0-9_]+$'),'malformed native unit text')
 return s
end
function M.new(object,handle,expected)
 assert(ffi.istype('uint64_t',object) and object~=0,'logical inventory requires an exact native uint64 identity')
 assert(type(handle)=='string' and handle:match('^[0-9a-f]+$') and #handle==16,'logical inventory requires a stable creation handle')
 expected=count(expected,100)
 local last=0;local poisoned=false
 local function read()
  assert(ffi.C.IsComponentOperational(object),'logical inventory builder unavailable')
  -- The boolean is onlydrones, not an include-unavailable switch. Non-drone
  -- inventory (for example missiles) is outside this census.
  local n=count(ffi.C.GetNumAllUnits(object,true),16)
  local units=ffi.new('UnitData[?]',math.max(n,1))
  assert(count(ffi.C.GetAllUnits(units,n,object,true),16)==n,'native unit inventory changed during read')
  local rows,seen={},{};local total,build,unknown=0,0,0
  for i=0,n-1 do
   local macro,category=text(units[i].macro,96),text(units[i].category,24)
   local amount=count(units[i].amount,100)
   local key=macro..','..category
   assert(not seen[key],'duplicate logical unit macro/category')
   seen[key]=true;rows[#rows+1]=key..','..amount;total=total+amount
   if category=='build' then build=build+amount end
   if macro~=expected_macro or category~='build' then unknown=unknown+1 end
  end
  assert(total<=100,'logical inventory exceeds pinned builder capacity')
  table.sort(rows)
  local stored=count(ffi.C.GetNumStoredUnits(object,'build',false),100)
  local unavailable=count(ffi.C.GetNumUnavailableUnits(object,'build'),100)
  assert(ffi.C.IsComponentOperational(object),'logical inventory builder lost during read')
  -- These flags are observed count relationships, not physical-child coverage.
  -- Never add unavailable to stored: its native accounting meaning is not assumed.
  local row_match=build==expected and 1 or 0
  local stored_match=stored==build and 1 or 0
  local unavailable_bound=unavailable<=stored and 1 or 0
  local accounting_unknown=(row_match==0 or stored_match==0 or unavailable_bound==0 or unknown>0) and 1 or 0
  return table.concat({expected,total,build,stored,unavailable,row_match,stored_match,unavailable_bound,unknown,accounting_unknown,table.concat(rows,';')},':')
 end
 return {
  sample=function(tick)
   assert(not poisoned and type(tick)=='number' and tick==math.floor(tick) and tick>last and tick<=4294967295,'logical inventory sample stale or poisoned')
   local ok,first=pcall(function()
    local a,b=read(),read()
    assert(a==b,'logical inventory unstable across bounded reread')
    return a
   end)
   if not ok then poisoned=true;error(first) end
   last=tick
   return table.concat({'CONSTRUCTION_UNITS_1',tick,handle,'logical_only=1','physical_ids=0','only_drones=1',first},':')
  end,
 }
end
return M
