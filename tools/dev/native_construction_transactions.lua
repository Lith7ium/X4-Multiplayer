-- Test-only pending/acknowledged transactions for one explicit construction plan.
local M={}
function M.decode(command,slot)
 assert(type(command)=='string' and #command==8 and command:match('^[0-9a-f]+$'),'invalid construction command')
 if command=='00000000' then return end
 local op,asset,arg,revision=tonumber(command:sub(1,2),16),tonumber(command:sub(3,4),16),tonumber(command:sub(5,6),16),tonumber(command:sub(7,8),16)
 assert(((op==0x49 and asset==1) or (op==0x4a and asset==2)) and arg==0,'unsupported construction command')
 return {op=op,asset=asset,revision=revision,slot=slot}
end
function M.new()
 local pending,completed,revision={},{},{0,0}
 local last=0;local poisoned=false
 return {
  pending=pending,completed=completed,revisions=revision,
  ack=function(ticket,kind,result)
   assert(not poisoned,'construction transaction poisoned')
   local asset=kind=='plan' and 1 or kind=='deploy' and 2 or 0
   local request=pending[asset]
   assert(request and request.ticket==ticket,'unexpected or duplicate construction acknowledgement')
   assert(result==(asset==1 and '1|struct_arg_base_02_macro' or '1|DeployToStation'),'native construction acknowledgement readback differs')
   completed[asset]=ticket;pending[asset]=nil
  end,
  batch=function(tick,a,b,enqueue)
   assert(not poisoned and tick==last+1,'construction transaction duplicate, gap or poisoned batch')
   local actions={M.decode(a,1),M.decode(b,2)}
   local result={}
   for asset,request in pairs(pending) do
    if tick-request.tick>60 then poisoned=true;error('construction acknowledgement timeout; no retry') end
   end
   for slot=1,2 do
    local action=actions[slot]
    if action then
     local asset=action.asset;local status
     if asset==2 and slot~=2 then status='DENIED_OWNER'
     elseif pending[asset] then status='DENIED_BUSY'
     elseif completed[asset] then status='DENIED_COMPLETE'
     elseif action.revision~=revision[asset] then status='STALE_REVISION'
     elseif asset==2 and not completed[1] then status='DENIED_DEPENDENCY'
     else
      action.ticket=table.concat({tick,slot,asset,revision[asset]+1},'-');action.tick=tick
      local ok,why=pcall(enqueue,action)
      if not ok then poisoned=true;error('construction enqueue failed; no retry: '..tostring(why)) end
      revision[asset]=revision[asset]+1;pending[asset]=action;status='PENDING'
     end
     result[#result+1]=table.concat({slot,action.op,asset,action.revision,status,revision[asset],action.ticket or 'none'},',')
    end
   end
   last=tick;return table.concat(result,';')
  end,
 }
end
X4LAN_NATIVE_CONSTRUCTION_TRANSACTIONS=M
return M
