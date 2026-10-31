-- Native UI API stimulus for a single shared player faction in disposable games.
local cfg=X4LAN_CONFIG
if not cfg or not cfg.native_gate_management_test then return end
local ffi=require('ffi')
if not pcall(ffi.typeof,'UniverseID') then ffi.cdef('typedef uint64_t UniverseID;') end
if not pcall(ffi.typeof,'AIOrderID') then ffi.cdef('typedef uint64_t AIOrderID;') end
ffi.cdef[[
 const char* GetContainerBuildMethod(UniverseID);
 void SetContainerBuildMethod(UniverseID,const char*);
 int32_t GetContainerTradeRuleID(UniverseID,const char*,const char*);
 bool HasContainerOwnTradeRule(UniverseID,const char*,const char*);
 void SetContainerTradeRule(UniverseID,int32_t,const char*,const char*,bool);
 uint32_t CreateOrder3(UniverseID,const char*,bool,bool,bool);
 AIOrderID GetOrderID(UniverseID,size_t);
 bool EnableOrder(UniverseID,size_t);
 bool RemoveOrder2(UniverseID,size_t,bool,bool,bool);
 bool IsComponentOperational(UniverseID);
]]
local own=assert(tonumber(cfg.slot:sub(2)))
local native={};local retained={};local current_tick;local last_admitted=0
local function enroll(key)
 return function(_,object)
  assert(native[key]==nil,'management fixture enrolled twice')
  native[key]=ConvertIDTo64Bit(object)
  assert(native[key]~=0,'management fixture contains null object')
 end
end
for _,key in ipairs({'Station','Account','OperatingAccount','Ship1','Ship2'}) do RegisterEvent('X4LAN_NativeManagement.'..key,enroll(key)) end
local admission=assert(X4LAN_NATIVE_MANAGEMENT_ADMISSION).new()
local methods={'default','closedloop'}
local function luaid(id) return ConvertStringTo64Bit((tostring(id):gsub('ULL$',''))) end
local function number(value)
 local n=assert(tonumber(value),'missing native numeric value')
 assert(n==math.floor(n) and n>=0 and n<=9007199254740991,'native integer outside exact range')
 return n
end
local function text(value)
 assert(value~=nil,'native method missing')
 local s=ffi.string(value)
 assert(#s<=48 and not s:find('[^%w_.%-]'),'unsupported method text')
 return s
end
local function balances()
 return number(GetPlayerMoney()),number(GetComponentData(luaid(assert(native.Account)),'money')),number(GetComponentData(luaid(assert(native.OperatingAccount)),'money'))
end
local function queueship(asset) return assert(native['Ship'..(asset-1)]) end
local function orders(object)
 return assert(X4LAN_NATIVE_ORDERS,'native order reader missing')(object)
end
local function effect_state(action)
 if action.op==0x40 or action.op==0x46 or action.op==0x47 then local wallet,account,station=balances();return wallet..','..account..','..station end
 if action.op==0x42 then return text(ffi.C.GetContainerBuildMethod(native.Account)) end
 if action.op==0x45 then return (ffi.C.HasContainerOwnTradeRule(native.Account,'buy','') and '1' or '0')..','..tonumber(ffi.C.GetContainerTradeRuleID(native.Account,'buy','')) end
 return orders(queueship(action.asset))
end
local function commit(action)
 local op=action.op
 local before=effect_state(action)
 if op==0x40 or op==0x46 or op==0x47 then
  local wallet,account,station=balances();local selected=op==0x47 and station or account
  local target=op==0x46 and 1000000000000 or action.value*10000;local delta=target-selected
  if delta>wallet then
   DebugError('[X4LAN] NATIVE_GATE MANAGEMENT_DENIAL tick='..current_tick..' slot='..action.slot..' reason=DENIED_FUNDS before='..before..' target='..target..' after='..effect_state(action))
   return 'DENIED_FUNDS'
  end
  local object=op==0x47 and native.OperatingAccount or native.Account
  if delta>0 then TransferPlayerMoneyTo(delta,luaid(object))
  elseif delta<0 then TransferMoneyToPlayer(-delta,luaid(object)) end
  local afterwallet,afteraccount,afterstation=balances()
  assert(afterwallet==wallet-delta and afteraccount==(op==0x47 and account or target) and afterstation==(op==0x47 and target or station),'native budget readback/conservation failed')
 elseif op==0x42 then
  local method=assert(methods[action.value])
  ffi.C.SetContainerBuildMethod(native.Account,method)
  assert(text(ffi.C.GetContainerBuildMethod(native.Account))==method,'native build-storage method readback failed')
 elseif op==0x45 then
  local enabled=action.value==1
  ffi.C.SetContainerTradeRule(native.Account,-1,'buy','',enabled)
  assert(ffi.C.HasContainerOwnTradeRule(native.Account,'buy','')==enabled,'native trade rule readback failed')
 elseif op==0x43 then
  assert(not retained[action.asset],'owned order already exists')
  local object=queueship(action.asset)
  assert(ffi.C.IsComponentOperational(object),'managed ship not operational')
  local index=number(ffi.C.CreateOrder3(object,'Wait',false,false,false))
  assert(index>0 and ffi.C.EnableOrder(object,index),'native order creation/enable failed')
  local identity=ffi.C.GetOrderID(object,index)
  assert(identity~=0,'created order has no native identity')
  retained[action.asset]={index=index,identity=identity}
  assert(orders(object):find(';'..index..',Wait,',1,true),'created native order missing')
 elseif op==0x44 then
  local object=queueship(action.asset);local order=assert(retained[action.asset],'no retained order to cancel');local index=order.index
  if ffi.C.GetOrderID(object,index)~=order.identity then
   DebugError('[X4LAN] NATIVE_GATE MANAGEMENT_DENIAL tick='..current_tick..' slot='..action.slot..' reason=DENIED_STALE_ORDER before='..before..' target=retained after='..effect_state(action))
   return 'DENIED_STALE_ORDER'
  end
  assert(orders(object):find(';'..index..',Wait,',1,true),'retained native order changed')
  assert(ffi.C.RemoveOrder2(object,index,true,false,false),'native order cancellation failed')
  retained[action.asset]=nil
  assert(not orders(object):find(';'..index..',Wait,',1,true),'cancelled native order still present')
 else error('unsupported admitted management operation') end
 DebugError('[X4LAN] NATIVE_GATE MANAGEMENT_EFFECT tick='..current_tick..' slot='..action.slot..' op='..op..' asset='..action.asset..' before='..before..' after='..effect_state(action))
 return true
end
local plans={
 [1]={[30]='40010200',[90]='40010002',[120]='42010203',[150]='43020000',[180]='44020001',[240]='45010104',[270]='45010005',[300]='47010206',[330]='47010007'},
 [2]={[30]='40010300',[60]='40010301',[150]='43020000',[210]='43030000',[240]='44030001',[300]='46010007'},
}
local state
X4LAN_NATIVE_MANAGEMENT={
 proposal=function(tick) return plans[own][tick] or '00000000' end,
 apply=function(tick,a,b)
  assert(tick==last_admitted+1,'management adapter duplicate or skipped frame')
  X4LAN_NATIVE_MANAGEMENT_ADMISSION.decode(a,1)
  X4LAN_NATIVE_MANAGEMENT_ADMISSION.decode(b,2)
  if tick<20 then assert(a=='00000000' and b=='00000000','management command before fixture') end
  last_admitted=tick
  current_tick=tick
  if tick==5 then AddUITriggeredEvent('X4LAN_NativeManagement','Setup',{}) end
  if tick<20 then
   admission.batch(tick,a,b,commit)
   state='MANAGEMENT_2:'..tick..':pending'
  else
   for _,key in ipairs({'Station','Account','OperatingAccount','Ship1','Ship2'}) do assert(native[key],'management fixture incomplete: '..key) end
   local receipts=admission.batch(tick,a,b,commit,function(partial,action,why)
    local ok,partialstate=pcall(effect_state,action)
    DebugError('[X4LAN] NATIVE_GATE MANAGEMENT_PARTIAL tick='..tick..' receipts='..partial..' state='..(ok and partialstate or 'unreadable')..' poisoned=1')
   end)
   local wallet,account,station=balances()
   local method=text(ffi.C.GetContainerBuildMethod(native.Account))
   local rule=number(ffi.C.GetContainerTradeRuleID(native.Account,'buy','')+1)-1
   state=table.concat({'MANAGEMENT_2',tick,wallet,account,method,ffi.C.HasContainerOwnTradeRule(native.Account,'buy','') and 1 or 0,rule,
    table.concat(admission.revisions,','),orders(native.Ship1),orders(native.Ship2),receipts,station},':')
   if receipts~='' then DebugError('[X4LAN] NATIVE_GATE MANAGEMENT_RECEIPTS tick='..tick..' data='..receipts) end
  end
  DebugError('[X4LAN] NATIVE_GATE MANAGEMENT_STATE tick='..tick..' state='..state)
  DebugError('[X4LAN] NATIVE_GATE INPUTS tick='..tick..' mode=management p1='..a..' p2='..b)
  return 'INPUTS_1:management:'..a..':'..b
 end,
 sample=function() assert(state,'management sample before command admission');return state end,
}
