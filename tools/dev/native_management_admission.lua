-- Bounded diagnostic admission. This is not a general faction authority layer.
local M={}
local specs={
 [0x40]={asset=1,max=5}, -- shared build-storage target, in 10,000 UI-credit units
 [0x42]={asset=1,min=1,max=2}, -- native build method
 [0x43]={minasset=2,maxasset=3,max=0}, -- append and enable Wait
 [0x44]={minasset=2,maxasset=3,max=0}, -- remove our retained Wait
 [0x45]={asset=1,max=1}, -- own/default buy trade-rule override
 [0x46]={asset=1,max=0}, -- bounded diagnostic excessive-budget request
 [0x47]={asset=1,max=5}, -- station operating-account target, in 10,000 UI-credit units
}
function M.decode(command,slot)
 assert(type(command)=='string' and #command==8 and command:match('^[0-9a-f]+$'),'invalid management command encoding')
 if command=='00000000' then return nil end
 local op,asset,value,revision=tonumber(command:sub(1,2),16),tonumber(command:sub(3,4),16),tonumber(command:sub(5,6),16),tonumber(command:sub(7,8),16)
 local spec=assert(specs[op],'unsupported management action')
 assert((spec.asset and asset==spec.asset) or (spec.minasset and asset>=spec.minasset and asset<=spec.maxasset),'invalid management asset')
 assert(value>=(spec.min or 0) and value<=spec.max,'invalid management value')
 assert(slot==1 or slot==2,'invalid management player')
 return {op=op,asset=asset,value=value,revision=revision,slot=slot}
end
function M.new()
 local revision={0,0,0}
 local last=0
 local terminal=false
 return {
  revisions=revision,
  batch=function(tick,a,b,commit,onfailure)
   assert(not terminal,'management batch poisoned after native failure')
   assert(type(tick)=='number' and tick==last+1,'management tick duplicate or gap')
   -- Parse the entire batch before any native side effect.
   local actions={M.decode(a,1),M.decode(b,2)}
   local results={}
   for slot=1,2 do
    local action=actions[slot]
    if action then
     local asset=action.asset
     local status
     if asset>1 and slot~=asset-1 then status='DENIED_OWNER'
     elseif action.revision~=revision[asset] then status='STALE_REVISION'
     elseif revision[asset]>=255 then status='REVISION_EXHAUSTED'
     else
      -- A failed native effect is fatal: never publish a successful receipt.
      local ok,result=pcall(commit,action)
      if not ok or (result~=true and result~='DENIED_FUNDS' and result~='DENIED_STALE_ORDER') then
       terminal=true
       results[#results+1]=table.concat({slot,action.op,asset,action.value,action.revision,'NATIVE_FAILURE',revision[asset]},',')
       if onfailure then onfailure(table.concat(results,';'),action,result) end
       error('native management action failed; batch cannot be retried: '..tostring(result))
      end
      if result==true then revision[asset]=revision[asset]+1;status='APPLIED'
      else status=result end
     end
     results[#results+1]=table.concat({slot,action.op,asset,action.value,action.revision,status,revision[asset]},',')
    end
   end
   last=tick
   return table.concat(results,';')
  end,
 }
end
X4LAN_NATIVE_MANAGEMENT_ADMISSION=M
return M
