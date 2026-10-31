-- Read-only exported UI order queries, bounded to the disposable fixture.
local cfg=X4LAN_CONFIG
if not cfg or not cfg.native_gate_order_state then return end
local ffi=require('ffi')
if not pcall(ffi.typeof,'UniverseID') then ffi.cdef('typedef uint64_t UniverseID;') end
if not pcall(ffi.typeof,'Order') then
 ffi.cdef[[typedef struct {
  size_t queueidx; const char *state,*statename,*orderdef; size_t actualparams;
  bool enabled,isinfinite,issyncpointreached,istemporder;
 } Order;]]
end
if not pcall(ffi.typeof,'Order3') then
 ffi.cdef[[typedef struct {
  size_t queueidx; const char *state,*statename,*orderdef; size_t actualparams;
  bool enabled,isinfinite,issyncpointreached,istemporder,isoverride,ispriority;
 } Order3;]]
end
ffi.cdef[[
 uint32_t GetNumOrders(UniverseID);
 uint32_t GetOrders3(Order3*,uint32_t,UniverseID);
 size_t GetOrderQueueCurrentIdx(UniverseID);
 bool GetDefaultOrder(Order*,UniverseID);
]]
local function integer(value)
 local n=tonumber(value)
 assert(n and n>=0 and n<=4294967295 and n==math.floor(n),'order index/count out of range')
 return string.format('%.0f',n)
end
local function text(value)
 assert(value~=nil,'missing native order text')
 local s=ffi.string(value)
 assert(#s<=48 and not s:find('[^%w_.%-]'),'unsupported native order text')
 return s
end
local function row(value,extended)
 local parts={integer(value.queueidx),text(value.orderdef),text(value.state),integer(value.actualparams)}
 for _,key in ipairs({'enabled','isinfinite','issyncpointreached','istemporder'}) do
  parts[#parts+1]=value[key] and '1' or '0'
 end
 if extended then
  parts[#parts+1]=value.isoverride and '1' or '0'
  parts[#parts+1]=value.ispriority and '1' or '0'
 end
 return table.concat(parts,',')
end
-- One queue and one default buffer for every sample (twelve actors a tick), zeroed before each
-- read as a fresh ffi.new would be.
local queue,default=ffi.new('Order3[16]'),ffi.new('Order')
X4LAN_NATIVE_ORDERS=function(object)
 local count=tonumber(ffi.C.GetNumOrders(object))
 assert(count and count>=0 and count<=16 and count==math.floor(count),'native order queue exceeds fixture bound')
 ffi.fill(queue,ffi.sizeof('Order3')*math.max(count,1))
 assert(tonumber(ffi.C.GetOrders3(queue,count,object))==count,'native order queue changed during sample')
 local parts={'ORDERS_1',integer(ffi.C.GetOrderQueueCurrentIdx(object)),integer(count)}
 for i=0,count-1 do parts[#parts+1]=row(queue[i],true) end
 ffi.fill(default,ffi.sizeof('Order'))
 parts[#parts+1]=ffi.C.GetDefaultOrder(default,object) and ('default,'..row(default,false)) or 'default,none'
 return table.concat(parts,';')
end
