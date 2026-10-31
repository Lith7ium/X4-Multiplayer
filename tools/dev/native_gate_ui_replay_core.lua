-- UI replay, Lua half (config native_gate_ui_replay, default off; src/native/engine/lockstep_ui_replay.h,
-- reports/LOCKSTEP-UI-REPLAY-20261030.txt). Stock menus call a few world-changing functions as Lua C globals, the
-- native hooks cannot see those: here they are wrapped by name (the menus look them up at each call), encoded as
-- events of this client's slot and handed to the gate's queue, so they keep their order with the native events.
-- At the apply tick every client's probe frame runs them through the stock function, canonical, with the same
-- arguments. Nothing runs locally before that. Pure Lua apart from the double's bytes; the engine is injected.
X4LAN_UI_REPLAY_CORE={}
local K=X4LAN_UI_REPLAY_CORE
local ffi=require('ffi')
K.FAMILY={travel=1,target=2,orders=4}
K.OP_ORDERS_CLEAR,K.OP_ORDERS_CLEAR2,K.OP_LUA_CALL=0x41,0x42,0x43
K.EVENT_MAX=200
K.REF_HEX=24
local TAG={['nil']=0,['false']=1,['true']=2,number=3,string=4,luaid=5,id=6,table=7}
local DEPTH_MAX,ITEMS_MAX,STRING_MAX=4,32,64

-- true = every family; a list names them. Returns the mask, or nil and the reason.
function K.families(value)
 if value==true then return 7 end
 if type(value)~='table' or #value==0 then return nil,'native_gate_ui_replay must be true or a list of travel, target, orders' end
 local mask=0
 for _,name in ipairs(value) do
  local bit=K.FAMILY[name]
  if not bit then return nil,'unknown family '..tostring(name) end
  if math.floor(mask/bit)%2==1 then return nil,'family listed twice '..name end
  mask=mask+bit
 end
 return mask
end

local function byte_hex(b) return string.format('%02x',b) end
local function double_hex(v)
 local d=ffi.new('double[1]',v);local p=ffi.cast('const uint8_t*',d);local out={}
 for i=0,7 do out[#out+1]=byte_hex(p[i]) end
 return table.concat(out)
end
local function hex_double(h)
 local d=ffi.new('double[1]');local p=ffi.cast('uint8_t*',d)
 for i=0,7 do p[i]=tonumber(h:sub(2*i+1,2*i+2),16) end
 return tonumber(d[0])
end
-- Canonical order: numbers ascending, then strings ascending, so every client gets one encoding of a table.
local function sorted_keys(t)
 local numbers,strings={},{}
 for k in pairs(t) do
  if type(k)=='number' then numbers[#numbers+1]=k
  elseif type(k)=='string' then strings[#strings+1]=k
  else return nil end
 end
 table.sort(numbers);table.sort(strings)
 for _,k in ipairs(strings) do numbers[#numbers+1]=k end
 return numbers
end
local encode_value
function encode_value(v,ctx,depth)
 local kind=type(v)
 if kind=='nil' then return byte_hex(TAG['nil']) end
 if kind=='boolean' then return byte_hex(v and TAG['true'] or TAG['false']) end
 if kind=='number' then
  if v~=v or v==math.huge or v==-math.huge then return nil,'number' end
  return byte_hex(TAG.number)..double_hex(v)
 end
 if kind=='string' then
  if #v>STRING_MAX then return nil,'string' end
  local out={byte_hex(TAG.string),byte_hex(#v)}
  for i=1,#v do out[#out+1]=byte_hex(v:byte(i)) end
  return table.concat(out)
 end
 if kind=='userdata' or kind=='cdata' then
  local ref=ctx.ref(v)
  if not ref then return nil,'unmapped' end
  return byte_hex(kind=='userdata' and TAG.luaid or TAG.id)..ref
 end
 if kind=='table' then
  if depth>=DEPTH_MAX then return nil,'depth' end
  local keys=sorted_keys(v)
  if not keys or #keys>ITEMS_MAX then return nil,'table' end
  local out={byte_hex(TAG.table),byte_hex(#keys)}
  for _,k in ipairs(keys) do
   local a,why=encode_value(k,ctx,depth+1)
   if not a then return nil,why end
   local b,why2=encode_value(v[k],ctx,depth+1)
   if not b then return nil,why2 end
   out[#out+1]=a;out[#out+1]=b
  end
  return table.concat(out)
 end
 return nil,'type'
end
-- (args, n) -> event hex (count, values), or nil and the reason.
function K.encode_call(args,n,ctx)
 if n>ITEMS_MAX then return nil,'count' end
 local out={byte_hex(n)}
 for i=1,n do
  local h,why=encode_value(args[i],ctx,0)
  if not h then return nil,why end
  out[#out+1]=h
 end
 local hex=table.concat(out)
 if #hex/2>K.EVENT_MAX then return nil,'too_long' end
 return hex
end
local decode_value
function decode_value(h,at,ctx,depth)
 if at+1>#h then return nil,nil,'truncated' end
 local tag=tonumber(h:sub(at,at+1),16)
 at=at+2
 if tag==TAG['nil'] then return nil,at end
 if tag==TAG['false'] then return false,at end
 if tag==TAG['true'] then return true,at end
 if tag==TAG.number then
  if at+15>#h then return nil,nil,'truncated' end
  return hex_double(h:sub(at,at+15)),at+16
 end
 if tag==TAG.string then
  if at+1>#h then return nil,nil,'truncated' end
  local n=tonumber(h:sub(at,at+1),16);at=at+2
  if at+2*n-1>#h then return nil,nil,'truncated' end
  local out={}
  for i=0,n-1 do out[#out+1]=string.char(tonumber(h:sub(at+2*i,at+2*i+1),16)) end
  return table.concat(out),at+2*n
 end
 if tag==TAG.luaid or tag==TAG.id then
  if at+K.REF_HEX-1>#h then return nil,nil,'truncated' end
  local value,why=ctx.resolve(h:sub(at,at+K.REF_HEX-1),tag==TAG.luaid and 'luaid' or 'id')
  if value==nil then return nil,nil,why or 'unresolved' end
  return value,at+K.REF_HEX
 end
 if tag==TAG.table then
  if depth>=DEPTH_MAX or at+1>#h then return nil,nil,'truncated' end
  local n=tonumber(h:sub(at,at+1),16);at=at+2
  local t={}
  for _=1,n do
   local key,value,why
   key,at,why=decode_value(h,at,ctx,depth+1)
   if not at then return nil,nil,why end
   if key==nil then return nil,nil,'key' end
   value,at,why=decode_value(h,at,ctx,depth+1)
   if not at then return nil,nil,why end
   t[key]=value
  end
  return t,at
 end
 return nil,nil,'tag'
end
-- Event hex -> args, n; or nil and the reason.
function K.decode_call(hex,ctx)
 if type(hex)~='string' or #hex<2 or #hex%2~=0 or hex:find('[^0-9a-f]') then return nil,'encoding' end
 local n=tonumber(hex:sub(1,2),16)
 if n>ITEMS_MAX then return nil,'count' end
 local args,at={},3
 for i=1,n do
  local value,why
  value,at,why=decode_value(hex,at,ctx,0)
  if not at then return nil,why end
  args[i]=value
 end
 if at~=#hex+1 then return nil,'trailing' end
 return args,n
end

-- The entries that stay unblocked per family (native_gate_pilot_block_orders refuses every other one).
K.UNBLOCKED={
 travel={'InteractMenu.buttonTravelMode','hotkey.INPUT_ACTION_ADDON_DETAILMONITOR_TRAVELMODE'},
 orders={'InteractMenu.orderAttack','InteractMenu.orderMoveWait','InteractMenu.orderDock','InteractMenu.orderFollow',
  'MapMenu.orderMoveWait','MapMenu.orderAttack','MapMenu.orderFollow'},
}
function K.unblocked(mask)
 local out={}
 for name,bit in pairs(K.FAMILY) do
  if math.floor(mask/bit)%2==1 then for _,entry in ipairs(K.UNBLOCKED[name] or {}) do out[entry]=true end end
 end
 return out
end

-- p: gate, C (ffi.C), env (the Lua globals), log, mask, slot_ships() -> {decimal=slot}, script (by absolute tick).
function K.session(p)
 local gate,C,env,log,mask=p.gate,p.C,p.env,p.log,p.mask
 local S={replaying=false,captured=0,refused=0,applied=0,unblocked=K.unblocked(mask)}
 local orders=math.floor(mask/K.FAMILY.orders)%2==1
 local function decimal(id)
  local kind=type(id)
  if kind=='number' then return string.format('%.0f',id) end
  if kind~='cdata' then id=env.ConvertIDTo64Bit(id) end
  local text=(tostring(id):gsub('ULL$',''))
  assert(text:match('^%d+$'),'invalid runtime component ID')
  return text
 end
 S.decimal=decimal
 local ctx={
  ref=function(id) return gate.ui_replay_ref(decimal(id)) end,
  resolve=function(ref,kind)
   local runtime,why=gate.ui_replay_resolve(ref)
   if not runtime then return nil,why end
   return kind=='luaid' and env.ConvertStringToLuaID(runtime) or env.ConvertStringTo64Bit(runtime)
  end}
 S.ctx=ctx
 local function refuse(what,why)
  S.refused=S.refused+1
  log('UI_REPLAY_REFUSED what='..what..' why='..tostring(why))
 end
 -- Stock functions, once per Lua state (a re-arm finds the wrappers in the globals).
 local stock=env.X4LAN_UI_REPLAY_STOCK
 if not stock then
  stock={CreateOrder=env.CreateOrder,SetOrderParam=env.SetOrderParam,RemoveOrderListParam=env.RemoveOrderListParam}
  env.X4LAN_UI_REPLAY_STOCK=stock
 end
 S.stock=stock
 local function is_slot_ship(id)
  local ok,text=pcall(decimal,id)
  return ok and p.slot_ships()[text]~=nil
 end
 if orders then
  env.CreateOrder=function(...)
   if S.replaying then return stock.CreateOrder(...) end
   local args,n={...},select('#',...)
   if n<1 then return refuse('CreateOrder','arguments') or 0 end
   if is_slot_ship(args[1]) then return refuse('CreateOrder','slot_ship') or 0 end
   local hex,why=K.encode_call(args,n,ctx)
   if not hex then return refuse('CreateOrder',why) or 0 end
   if gate.ui_replay_push(K.OP_LUA_CALL,hex)==1 then S.captured=S.captured+1 end   -- a native refusal counts and logs itself
   return 0
  end
  -- Order parameters of queued orders: the queue editor, refused until it can be replayed.
  env.SetOrderParam=function(...)
   if S.replaying then return stock.SetOrderParam(...) end
   return refuse('SetOrderParam','not_replayed_yet')
  end
  env.RemoveOrderListParam=function(...)
   if S.replaying then return stock.RemoveOrderListParam(...) end
   return refuse('RemoveOrderListParam','not_replayed_yet')
  end
 end
 -- One due event; ok, what happened. A reference that is gone is the same on every client, a refusal too.
 local function run(slot,op,hex)
  if op==K.OP_LUA_CALL then
   local args,n=K.decode_call(hex,ctx)
   if not args then
    if n=='mismatch' then gate.ui_replay_fail('ui_ref_mismatch') elseif n~='gone' then gate.ui_replay_fail('ui_lua_decode') end
    return false,n
   end
   if is_slot_ship(args[1]) then return false,'slot_ship' end
   local ok,result=pcall(stock.CreateOrder,unpack(args,1,n))
   return ok,ok and 'order='..tostring(args[2]) or tostring(result)
  end
  if op==K.OP_ORDERS_CLEAR or op==K.OP_ORDERS_CLEAR2 then
   local runtime,why=gate.ui_replay_resolve(hex:sub(1,K.REF_HEX))
   if not runtime then return false,why end
   if p.slot_ships()[runtime] then return false,'slot_ship' end
   local id=env.ConvertStringTo64Bit(runtime)
   if op==K.OP_ORDERS_CLEAR then return pcall(C.RemoveAllOrders,id),'clear' end
   local flags=tonumber(hex:sub(K.REF_HEX+1,K.REF_HEX+2),16)
   return pcall(C.RemoveAllOrders2,id,flags%2==1,flags>=2),'clear2'
  end
  gate.ui_replay_fail('ui_lua_op')
  return false,'op'
 end
 -- Every client, the probe's frame of the tick, the events in slot order and call order.
 function S.apply(tick)
  local list=gate.ui_replay_lua(tick)
  if not list then return end
  S.replaying=true;gate.ui_replay_bypass(1)
  for slot,op,hex in list:gmatch('(%d+):(%d+):(%x*)') do
   local ok,what=run(tonumber(slot),tonumber(op),hex)
   gate.ui_replay_result(tick,ok and 1 or 0)
   S.applied=S.applied+1
   log(string.format('UI_REPLAY_APPLIED tick=%d slot=%s op=%s result=%s what=%s',tick,slot,op,ok and 'applied' or 'refused',tostring(what)))
  end
  gate.ui_replay_bypass(0);S.replaying=false
 end
 -- The input section (every client stages every slot's; the proposal is this client's own).
 function S.section()
  return {
   apply=function(_,inputs)
    local staged=0
    for slot=1,#inputs do
     local payload=inputs[slot]
     if payload and payload~='' then
      local ok,why=gate.ui_replay_stage(slot,payload)
      assert(ok==1,'ui replay section refused slot='..slot..' why='..tostring(why))
      staged=staged+1
     end
    end
    return 'ui='..staged
   end,
   proposal=function(tick,budget) return gate.ui_replay_take(tick,math.floor((budget or 472)/2)) end}
 end
 -- Scripted use of the stock entries, for runs without people: they go through the same capture.
 local script=p.script or {}
 local function run_script(tick)
  for _,e in ipairs(script[tick] or {}) do
   -- As a person's click: the UI half runs on this client alone, so what it allocates is not the probe's.
   if p.local_mark then p.local_mark(0) end
   local ok,result=pcall(e.call,S)
   if p.local_mark then p.local_mark(1) end
   log(string.format('UI_REPLAY_SCRIPT tick=%d op=%s ok=%s result=%s',tick,tostring(e.op),tostring(ok),tostring(result)))
  end
 end
 -- The test fleet: `player` ships, as the stock UI needs them, from the same tick on every client; their queues
 -- join the receipt, so an order that reached one client only shows at once.
 local fleet=p.fleet or {}
 local fleet_owned=false
 local function fleet_rows(tick)
  if #fleet==0 then return '' end
  if not fleet_owned and tick>=(p.fleet_tick or 0) then
   fleet_owned=true
   for _,saved in ipairs(fleet) do
    env.AddUITriggeredEvent('X4LAN_UiReplay','Own',{ship=p.saved_lua(saved)})
   end
  end
  local rows={}
  for _,saved in ipairs(fleet) do rows[#rows+1]=saved..':'..tostring(p.queue_of(saved)) end
  local text=table.concat(rows,',')
  if tick%30==0 then log('UI_REPLAY_FLEET tick='..tick..' '..text) end
  return '|UIFLEET_1:'..text
 end
 -- The module's part of the probe's frame; the receipt joins the pilot receipt.
 function S.frame(tick)
  S.apply(tick)
  run_script(tick)
  local refusals=gate.ui_replay_refusals()
  if refusals then log('UI_REPLAY_REFUSED_NATIVE tick='..tick..' '..refusals) end
  if tick%120==0 then log('UI_REPLAY_STATUS tick='..tick..' '..tostring(gate.ui_replay_status())) end
  local receipt=assert(gate.ui_replay_frame(),'ui replay receipt unavailable')
  -- Every slot's activity and soft target as they change: what the receipt hashes, readable.
  local slots=receipt:match('|(.*)$')
  if slots and slots~=S.slots then S.slots=slots;log('UI_REPLAY_SLOTS tick='..tick..' '..slots) end
  return receipt..fleet_rows(tick)
 end
 return S
end

-- Named script actions for runs without people; arguments are saved IDs or slots, resolved when they run.
-- plan: list of {tick, op, ...}, ticks relative to the pilot window start.
function K.script(plan,start,tools)
 local by={}
 for _,e in ipairs(plan or {}) do
  local at,op=start+e[1],e[2]
  local call
  if op=='travel' then call=function() tools.C.StartPlayerActivity('travel');return 'ffi' end
  elseif op=='travel_stop' then call=function() tools.C.StopPlayerActivity('travel');return 'ffi' end
  elseif op=='travel_key' then call=function() return tools.gate.pilot_stock_inject('action',0x12f,0) end
  elseif op=='target_slot' then call=function() return tools.C.SetSofttarget(tools.slot_id(e[3]),'') end
  elseif op=='target_none' then call=function() return tools.env.RemoveSofttarget() end
  elseif op=='match' then call=function() return tools.gate.pilot_stock_inject('state',0x5b,e[3]) end
  elseif op=='input' then call=function() return tools.gate.pilot_stock_inject(e[3],e[4],e[5]) end
  elseif op=='clear' then call=function() return tools.C.RemoveAllOrders2(tools.saved_id(e[3]),false,false) end
  elseif op=='attack' then call=function() return tools.env.CreateOrder(tools.saved_lua(e[3]),'Attack',{primarytarget=tools.saved_lua(e[4])},false,false,false,nil,nil,nil,true) end
  elseif op=='move' then call=function() return tools.env.CreateOrder(tools.saved_lua(e[3]),'MoveWait',{destination={tools.saved_lua(e[4]),{e[5],e[6],e[7]}}},false,false,false,nil,nil,nil,true) end
  elseif op=='dock' then call=function() return tools.env.CreateOrder(tools.saved_lua(e[3]),'DockAndWait',{destination=tools.saved_lua(e[4])},false,false,false) end
  else error('unknown ui replay script op '..tostring(op)) end
  by[at]=by[at] or {};by[at][#by[at]+1]={op=op,call=call}
 end
 return by
end
