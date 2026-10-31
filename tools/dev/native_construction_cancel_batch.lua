-- Inactive, cancellation-only diagnostic batch; not a cross-family dispatcher.
-- A future common dispatcher must decode every family before calling any writer.
local M={}
function M.decode(command,slot)
 assert(type(command)=='string' and #command==8 and command:match('^[0-9a-f]+$'),'invalid cancellation command')
 assert(slot==1 or slot==2,'invalid cancellation slot')
 if command=='00000000' then return end
 assert(command:sub(1,6)=='4b0200','unsupported cancellation command')
 return {slot=slot,revision=tonumber(command:sub(7,8),16)}
end
function M.new(guard,start_tick)
 assert(type(guard)=='table' and type(guard.request)=='function' and type(guard.observe)=='function' and type(guard.sample)=='function','missing native cancellation guard')
 local last=start_tick or 0;local terminal=false;local latest='pending';local state
 local function fields()
  local sample=guard.sample()
  local ticket,revision,status=sample:match('^CANCEL_GUARD_1;deploy=%d+%-2%-2%-1;ticket=([%w%-]+);tick=%d+;revision=(%d+);status=(%w+)$')
  assert(ticket and (status=='IDLE' or status=='PENDING' or status=='ACK' or status=='PARTIAL'),'invalid native cancellation guard state')
  return sample,ticket,tonumber(revision),status
 end
 return {
  sample=function() assert(state,'cancellation sample before admitted batch');return state end,
  batch=function(tick,a,b,md,onpartial)
   assert(not terminal and tick==last+1,'cancellation batch duplicate, gap or poisoned')
   local actions={M.decode(a,1),M.decode(b,2)} -- No native reads/writes before both decode.
   if md then
    assert(type(md.tick)=='number' and md.tick==math.floor(md.tick) and md.tick>=0 and md.tick<tick and type(md.assigned)=='boolean' and type(md.deployed)=='boolean','invalid cancellation MD sample')
   end
   last=tick
   local results={};local ack='none'
   local ok,why=pcall(function()
    local _,before_ticket,_,before_status=fields()
    if md then
     guard.observe(tick,md.tick,md.assigned,md.deployed)
     latest=table.concat({md.tick,md.assigned and 1 or 0,md.deployed and 1 or 0},',')
     local _,after_ticket,_,after_status=fields()
     if before_status=='PENDING' and after_status=='ACK' then
      assert(before_ticket==after_ticket,'cancellation acknowledged a different request');ack=after_ticket
     end
    end
    for slot=1,2 do
     local action=actions[slot]
     if action then
      local accepted,status,ticket=pcall(guard.request,tick,slot,action.revision)
      local _,_,revision=fields()
      if not accepted then
       results[#results+1]=table.concat({slot,75,2,action.revision,'NATIVE_FAILURE',revision,'none'},',')
       error(status)
      end
      assert(status=='PENDING' or status=='DENIED_OWNER' or status=='DENIED_BUSY' or status=='DENIED_COMPLETE' or status=='STALE_REVISION' or status=='DENIED_ASSET' or status=='DENIED_STALE_ORDER' or status=='DENIED_NOT_IMMEDIATE','unsupported native cancellation receipt')
      if status=='PENDING' then assert(ticket==table.concat({tick,slot,2,action.revision+1},'-'),'native cancellation ticket differs')
      else assert(ticket==nil,'denied cancellation created a ticket') end
      results[#results+1]=table.concat({slot,75,2,action.revision,status,revision,ticket or 'none'},',')
     end
    end
   end)
   local native=guard.sample()
   state=table.concat({'CANCEL_BATCH_1',tick,table.concat(results,';'),ack,latest,native},':')
   if not ok then
    terminal=true
    if onpartial then onpartial(state,tostring(why)) end
    error('cancellation batch failed; no replay: '..tostring(why))
   end
   return table.concat(results,';'),ack,state
  end,
 }
end
return M
