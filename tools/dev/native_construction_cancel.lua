-- Inactive construction-cancellation adapter. Not installed in the build fixture.
-- The caller must preflight the whole admitted batch before calling request().
-- Native order identities remain local; only deterministic tickets are portable.
local ffi=require('ffi')
if not pcall(ffi.typeof,'UniverseID') then ffi.cdef('typedef uint64_t UniverseID;') end
if not pcall(ffi.typeof,'AIOrderID') then ffi.cdef('typedef uint64_t AIOrderID;') end
ffi.cdef[[
 uint32_t GetNumOrders(UniverseID);
 AIOrderID GetOrderID(UniverseID,size_t);
 bool RemoveOrder2(UniverseID,size_t,bool,bool,bool);
 bool IsComponentOperational(UniverseID);
]]
local M={}
local function integer(value,maximum)
 assert(type(value)=='number' and value==math.floor(value) and value>=0 and value<=maximum,'invalid cancellation integer')
 return value
end
local function queue_ids(builder)
 local count=integer(tonumber(ffi.C.GetNumOrders(builder)),16)
 local result={}
 for i=1,count do
  result[i]=ffi.C.GetOrderID(builder,i)
  assert(result[i]~=0,'native cancellation queue lacks identity')
  for j=1,i-1 do assert(result[j]~=result[i],'duplicate native order identity') end
 end
 return result
end
local function local_identity(value)
 assert(type(value)=='cdata' and ffi.istype('AIOrderID',value) and value~=0,'local order identity must be exact uint64')
 return (tostring(value):gsub('ULL$',''))
end
local function local_ids(values)
 local result={};for i,id in ipairs(values) do result[i]=local_identity(id) end
 return #result>0 and table.concat(result,',') or 'none'
end
local function same_ids(a,b)
 if #a~=#b then return false end
 for i=1,#a do if a[i]~=b[i] then return false end end
 return true
end
function M.snapshot(builder)
 local ids=queue_ids(builder)
 local orders=assert(X4LAN_NATIVE_ORDERS,'native order reader missing')(builder)
 local repeated=queue_ids(builder)
 local repeated_orders=X4LAN_NATIVE_ORDERS(builder)
 local final_ids=queue_ids(builder)
 assert(same_ids(ids,repeated) and same_ids(ids,final_ids) and orders==repeated_orders,'native capture queue unstable across bounded reread')
 local count=orders:match('^ORDERS_1;%d+;(%d+);')
 assert(tonumber(count)==#ids,'native capture queue changed during read')
 local definitions={}
 for i=1,#ids do
  definitions[i]=assert(orders:match(';'..i..',([%w_.%-]+),'),'native capture order definition missing')
 end
 return {builder=builder,ids=ids,definitions=definitions}
end
function M.capture(builder,before,deploy_ticket)
 assert(before and before.builder==builder,'deployment capture belongs to a different builder')
 assert(ffi.C.IsComponentOperational(builder),'deployment builder unavailable at capture')
 local after=M.snapshot(builder)
 local selected,previous=0,1
 for i,id in ipairs(after.ids) do
  if id==before.ids[previous] then
   assert(after.definitions[i]==before.definitions[previous],'prior order definition changed during creation')
   previous=previous+1
  else
   assert(selected==0 and after.definitions[i]=='DeployToStation','creation has multiple or unexpected new native orders')
   selected=i
  end
 end
 assert(previous==#before.ids+1 and selected>0 and #after.ids==#before.ids+1,'creation removed or replaced prior native orders')
 local guard=M.new(builder,selected,after.ids[selected],deploy_ticket)
 local receipt=table.concat({'ORDER_CAPTURE_1','ticket='..deploy_ticket,'queue_before='..#before.ids,'queue_after='..#after.ids,'index='..selected,'kind=DeployToStation','new=1'},';')
 -- The third return is private per-client evidence, never a portable hash input.
 local diagnostic=table.concat({'ORDER_CAPTURE_LOCAL_1','ticket='..deploy_ticket,'identity='..local_identity(after.ids[selected]),'before='..local_ids(before.ids),'after='..local_ids(after.ids)},';')
 return guard,receipt,diagnostic
end
function M.new(builder,index,identity,deploy_ticket)
 integer(index,16);assert(index>0,'missing retained deployment index')
 assert(type(identity)=='cdata' and ffi.istype('AIOrderID',identity) and identity~=0,'deployment identity must be exact uint64')
 assert(type(deploy_ticket)=='string' and deploy_ticket:match('^%d+%-2%-2%-1$'),'invalid deployment ticket')
 local status,request_tick,ticket,revision='IDLE',0,'none',1
 local last_observed,last_assigned,last_deployed=0,nil,nil
 local function queue() return queue_ids(builder) end
 local function equal(a,b)
  if #a~=#b then return false end
  for i=1,#a do if a[i]~=b[i] then return false end end
  return true
 end
 local function sample()
  return table.concat({'CANCEL_GUARD_1','deploy='..deploy_ticket,'ticket='..ticket,'tick='..request_tick,'revision='..revision,'status='..status},';')
 end
 return {
  sample=sample,
  local_snapshot=function()
   -- Log only as private evidence. Local native IDs must not enter state hashes.
   local operational=ffi.C.IsComponentOperational(builder)
   return 'CANCEL_NATIVE_LOCAL_1;retained='..local_identity(identity)..';queue='..(operational and local_ids(M.snapshot(builder).ids) or 'unavailable')..';operational='..(operational and '1' or '0')
  end,
  request=function(tick,slot,expected_revision)
   integer(tick,4294967295);integer(slot,2);integer(expected_revision,255)
   assert(tick>0 and slot>0 and status~='PARTIAL','invalid or poisoned cancellation request')
   if slot~=2 then return 'DENIED_OWNER' end
   if status=='PENDING' then return 'DENIED_BUSY' end
   if status=='ACK' then return 'DENIED_COMPLETE' end
   if expected_revision~=revision then return 'STALE_REVISION' end
   if not ffi.C.IsComponentOperational(builder) then return 'DENIED_ASSET' end
   local before=queue()
   if before[index]~=identity then return 'DENIED_STALE_ORDER' end
   local orders=assert(X4LAN_NATIVE_ORDERS,'native order reader missing')(builder)
   assert(orders:find(';'..index..',DeployToStation,',1,true),'retained deployment definition changed')
   -- Match the station UI's immediate-only builder-fire capability query.
   local allowed=ffi.C.RemoveOrder2(builder,index,true,true,true)
   if not equal(before,queue()) or X4LAN_NATIVE_ORDERS(builder)~=orders then
    status='PARTIAL';request_tick=tick;error('cancellation preflight changed native orders; no replay')
   end
   if not allowed then return 'DENIED_NOT_IMMEDIATE' end
   -- Check the exact lifetime again at the mutation boundary.
   if ffi.C.GetOrderID(builder,index)~=identity then return 'DENIED_STALE_ORDER' end
   request_tick=tick;ticket=table.concat({tick,slot,2,revision+1},'-')
   local ok,why=pcall(function()
    assert(ffi.C.RemoveOrder2(builder,index,true,false,true),'native immediate cancellation refused after preflight')
    local remaining={}
    for i,id in ipairs(before) do if i~=index then remaining[#remaining+1]=id end end
    assert(equal(remaining,queue()),'cancellation removed or replaced other native orders')
   end)
   revision=revision+1
   if not ok then status='PARTIAL';error('construction cancellation partial; no replay: '..tostring(why)) end
   status='PENDING'
   return 'PENDING',ticket
  end,
  observe=function(tick,md_tick,assigned,deployed)
   integer(tick,4294967295);integer(md_tick,4294967295)
   assert(type(assigned)=='boolean' and type(deployed)=='boolean','invalid native disengagement state')
   assert(status~='PARTIAL','construction cancellation poisoned')
   if status~='PENDING' then return status end
   assert(tick>=request_tick and md_tick<tick,'invalid cancellation observation timing')
   if md_tick<last_observed or (md_tick==last_observed and last_assigned~=nil and (assigned~=last_assigned or deployed~=last_deployed)) then
    status='PARTIAL';error('native cancellation sample regressed or changed at the same tick')
   end
   last_observed,last_assigned,last_deployed=md_tick,assigned,deployed
   if tick-request_tick>120 then status='PARTIAL';error('construction cancellation disengagement timeout; no replay') end
   if not ffi.C.IsComponentOperational(builder) then status='PARTIAL';error('construction builder lost while cancellation pending') end
   for _,id in ipairs(queue()) do
    if id==identity then status='PARTIAL';error('cancelled native order identity reappeared') end
   end
   -- Pre-request samples cannot acknowledge a later native side effect.
   if md_tick>request_tick and not assigned and not deployed then status='ACK' end
   return status
  end,
 }
end
return M
