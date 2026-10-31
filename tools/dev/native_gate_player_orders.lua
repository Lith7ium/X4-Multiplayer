-- Lockstep player orders: engine glue (config native_gate_player_orders = true; default off).
-- A local order is never executed locally. It is captured as portable saved IDs, proposed on the
-- variable-length gate input wire and applied by every client through MD at the same gated tick.
-- Pure codec/scheduling: native_gate_player_orders_core.lua. MD: native_gate_player_orders.xml.
local cfg=X4LAN_CONFIG
if not cfg or cfg.native_gate_player_orders~=true then return end
local K=assert(X4LAN_PLAYER_ORDERS_CORE,'player orders core missing')
assert(cfg.native_gate_world_identities==true,'player orders resolve saved IDs: enable native_gate_world_identities')
local slot=type(cfg.slot)=='string' and tonumber(cfg.slot:match('^p(%d+)$'))
assert(slot and K.integer(slot,1,K.MAX_SLOTS) and 'p'..slot==cfg.slot,'player orders need slot p1..p16')
local delay=cfg.native_gate_player_orders_delay or 3
assert(K.integer(delay,1,K.MAX_DELAY),'player order delay must be 1..64 ticks')
local S=K.session({owners=cfg.native_gate_player_orders_owners,ownership=cfg.native_gate_player_orders_ownership,players=cfg.native_gate_player_orders_players,
 factions=cfg.native_gate_player_orders_factions})
local O=K.outbox(slot)
-- Scripted proposals for live tests: {[slot]={[tick]={command,...}}}; validated before arming.
local script=cfg.native_gate_player_orders_script or {}
assert(type(script)=='table','invalid player order script')
for owner,plan in pairs(script) do
 assert(K.integer(owner,1,K.MAX_SLOTS) and type(plan)=='table','invalid player order script slot')
 for tick,commands in pairs(plan) do
  assert(K.integer(tick,1,K.MAX_TICK) and type(commands)=='table','invalid player order script tick')
  for _,c in ipairs(commands) do assert(K.command_hex(c) and c.target~=c.subject,'invalid scripted player order at tick '..tick) end
 end
end
local ffi=require('ffi');local C=ffi.C
if not pcall(ffi.typeof,'UniverseID') then ffi.cdef('typedef uint64_t UniverseID;') end
local function cdef(name,def) if not pcall(function() return C[name] end) then pcall(ffi.cdef,def) end end
cdef('GetNumAllFactions','uint32_t GetNumAllFactions(bool includehidden);')
cdef('GetAllFactions','uint32_t GetAllFactions(const char** result, uint32_t resultlen, bool includehidden);')
cdef('GetNumAllFactionShips','uint32_t GetNumAllFactionShips(const char* factionid);')
cdef('GetAllFactionShips','uint32_t GetAllFactionShips(UniverseID* result, uint32_t resultlen, const char* factionid);')
cdef('GetNumAllFactionStations','uint32_t GetNumAllFactionStations(const char* factionid);')
cdef('GetAllFactionStations','uint32_t GetAllFactionStations(UniverseID* result, uint32_t resultlen, const char* factionid);')
cdef('IsComponentClass','bool IsComponentClass(UniverseID componentid, const char* classname);')
cdef('GetPlayerOccupiedShipID','UniverseID GetPlayerOccupiedShipID(void);')
local function log(text) DebugError('[X4LAN] '..text) end
local gate,index,ordinal,refused_capture=nil,nil,0,0

-- Runtime IDs stay in this process: decimal strings only feed gate.saved_identity and MD events.
local function decimal(id)
 local kind=type(id)
 if kind=='number' then return string.format('%.0f',id) end
 if kind~='cdata' then id=ConvertIDTo64Bit(id) end
 local text=(tostring(id):gsub('ULL$',''))
 assert(text:match('^%d+$'),'invalid runtime component ID')
 return text
end
local function is_class(component,class)
 return component~=nil and component~=0 and C.IsComponentClass(ConvertStringTo64Bit(decimal(component)),class)
end
local function saved_of(component)
 if component==nil or component==0 then return nil end
 local saved=gate and gate.saved_identity(decimal(component))
 if not K.saved(saved) then return nil end
 return saved
end
-- Saved -> runtime index over every imported ship, station and sector, built once at the first
-- gated tick. Objects created after loading carry no saved ID and stay unmapped (refused).
local function build_index(tick)
 local map,duplicate,objects,mapped,duplicates={}, {},0,0,0
 local function add(id)
  objects=objects+1
  local runtime=decimal(id)
  local saved=gate.saved_identity(runtime)
  if K.saved(saved) then
   if map[saved] and map[saved]~=runtime then duplicate[saved]=true else map[saved]=runtime end
  end
 end
 local count=tonumber(C.GetNumAllFactions(true))
 local names=ffi.new('const char*[?]',count+1)
 count=tonumber(C.GetAllFactions(names,count+1,true))
 local factions={};for i=0,count-1 do factions[#factions+1]=ffi.string(names[i]) end
 table.sort(factions)
 for _,faction in ipairs(factions) do
  for _,kind in ipairs({'Ships','Stations'}) do
   local n=tonumber(C['GetNumAllFaction'..kind](faction))
   local buffer=ffi.new('UniverseID[?]',n+1)
   n=tonumber(C['GetAllFaction'..kind](buffer,n+1,faction))
   for i=0,n-1 do add(buffer[i]) end
  end
 end
 for _,cluster in ipairs(GetClusters(true) or {}) do
  for _,sector in ipairs(GetSectors(cluster) or {}) do add(sector) end
 end
 local keys={}
 for saved in pairs(map) do if duplicate[saved] then map[saved]=false;duplicates=duplicates+1 else mapped=mapped+1;keys[#keys+1]=saved end end
 table.sort(keys)
 local digest=X4LAN_SHA256 and X4LAN_SHA256.digest(table.concat(keys,',')) or '-'
 log(string.format('PLAYER_ORDERS INDEX tick=%d objects=%d mapped=%d duplicates=%d digest=%s',tick,objects,mapped,duplicates,digest))
 return map
end
local function resolve(saved)
 local runtime=index[saved]
 if runtime==false then return nil,'ambiguous' end
 if not runtime then return nil,'unmapped' end
 return runtime
end
local function md_args(row)
 local c,e=row.entry.cmd,row.entry
 ordinal=ordinal+1
 local a={tick=S.tick,slot=e.slot,ordinal=ordinal,op=c.op,head=row.head,tail=row.tail,
  ownership=S.ownership,faction=K.faction(S,e.slot),subject=ConvertStringToLuaID(row.refs.subject),
  clear=K.has(c.flags,K.CLEAR) and 1 or 0,immediate=K.has(c.flags,K.IMMEDIATE) and 1 or 0,
  holdfire=K.has(c.flags,K.HOLDFIRE) and 1 or 0,hascommander=0,x=0,y=0,z=0,
  ware='',amount=0,price=0,assignment='',group=0}
 if row.refs.target then a.target=ConvertStringToLuaID(row.refs.target) end
 if row.refs.sector then a.sector=ConvertStringToLuaID(row.refs.sector);a.x,a.y,a.z=c.x,c.y,c.z end
 if c.op=='buy' or c.op=='sell' then a.ware,a.amount,a.price=c.ware,c.amount,c.price end
 if c.op=='assign' then a.hascommander=c.target and 1 or 0;a.assignment=c.assignment or '';a.group=c.group end
 return a
end

-- Capture: the stock map and interaction menus call these entry points with local components.
-- Supported ones become portable commands; every other order entry point is blocked, so no
-- order can be created locally outside the lockstep path (see the report for remaining gaps).
local function capture(op,component,fields,clear,immediate,holdfire)
 local c={op=op,subject=saved_of(component),flags=(clear and K.CLEAR or 0)+(immediate and K.IMMEDIATE or 0)+(holdfire and K.HOLDFIRE or 0)}
 for key,value in pairs(fields or {}) do c[key]=value end
 local ok,why
 if not c.subject then why='unmapped_subject' else ok,why=K.submit(O,c) end
 if ok then log('PLAYER_ORDER_CAPTURED slot='..slot..' op='..op..' subject='..c.subject..' queued='..#O.queue)
 else refused_capture=refused_capture+1;log('PLAYER_ORDER_CAPTURE_REFUSED slot='..slot..' op='..op..' reason='..tostring(why)) end
 return 0
end
local function refuse_capture(menu,name,why)
 refused_capture=refused_capture+1
 log('PLAYER_ORDER_CAPTURE_REFUSED slot='..slot..' menu='..menu..' entry='..name..' reason='..why)
 return 0
end
local function target(field,component) local saved=saved_of(component);return saved and {[field]=saved} end
local function offset_fields(sector,offset)
 local saved=saved_of(sector)
 if not saved or not is_class(sector,'sector') or type(offset)~='table' then return nil end
 return {sector=saved,x=offset.x,y=offset.y,z=offset.z}
end
local function object_order(menu_name,op)
 return function(component,object,clear,immediate)
  local fields=target('target',object)
  if not fields then return refuse_capture(menu_name,op,'unmapped_target') end
  return capture(op,component,fields,clear,immediate==true)
 end
end
local HOOKS={
 InteractMenu=function(menu)
  local function guard(name) return menu.priorityOrderMode and refuse_capture('InteractMenu',name,'priority_order') end
  return {
   orderAttack=function(component,t,clear,immediate)
    return guard('orderAttack') or object_order('InteractMenu','attack')(component,t,clear or menu.clearOtherOrders,immediate)
   end,
   orderFollow=function(component,t,clear)
    return guard('orderFollow') or object_order('InteractMenu','follow')(component,t,clear or menu.clearOtherOrders)
   end,
   orderProtect=function(component,t,clear) return object_order('InteractMenu','protect')(component,t,clear) end,
   orderDock=function(component,t,clear,ventureplatform,showroom)
    if ventureplatform or showroom then return refuse_capture('InteractMenu','orderDock','venture_or_showroom') end
    return guard('orderDock') or object_order('InteractMenu','dock')(component,t,clear or menu.clearOtherOrders)
   end,
   orderMoveWait=function(component,sector,offset,targetobject,_,clear)
    if guard('orderMoveWait') then return 0 end
    if targetobject and not is_class(targetobject,'sector') then return refuse_capture('InteractMenu','orderMoveWait','move_to_object') end
    local fields=offset_fields(sector,offset)
    if not fields then return refuse_capture('InteractMenu','orderMoveWait','unmapped_position') end
    return capture('move',component,fields,clear or menu.clearOtherOrders)
   end,
   orderStopAndHoldFire=function(component,clear,immediate)
    return capture('hold',component,nil,clear,immediate,true)
   end,
   orderAssignCommander=function(component,commander,assignment,group)
    if not K.ASSIGNMENT[assignment] or assignment=='positiondefence' then return refuse_capture('InteractMenu','orderAssignCommander','assignment_unsupported') end
    local fields=target('target',commander)
    if not fields then return refuse_capture('InteractMenu','orderAssignCommander','unmapped_target') end
    fields.assignment,fields.group=assignment,tonumber(group) or 0
    return capture('assign',component,fields)
   end,
   -- The stock handler also resets order loops and intersector defence locally.
   buttonAssignCommander=function(assignment,group)
    if menu.fleetunit then return refuse_capture('InteractMenu','buttonAssignCommander','fleet_unit') end
    local component=menu.componentSlot and menu.componentSlot.component
    if menu.showPlayerInteractions or (#(menu.selectedplayerships or {})==0 and not menu.shown) then
     menu.orderAssignCommander(component,C.GetPlayerOccupiedShipID(),assignment,group)
    elseif is_class(component,'controllable') and menu.data and menu.data.isplayerownedtarget then
     for _,ship in ipairs(menu.selectedplayerships or {}) do
      if decimal(ship)~=decimal(component) then menu.orderAssignCommander(ship,component,assignment,group) end
     end
    end
    pcall(menu.onCloseElement,'close')
   end,
  },{'buttonAssignFleetUnitCommander','buttonChangeAssignment','buttonChangeFleetUnitAssignment','buttonChangeOverrideOrder',
     'buttonFormationShape','buttonRecallSubordinates','buttonRemoveAssignment','buttonRemoveOrder','buttonRemoveAllOrders',
     'buttonTriggerSyncPoint','setOrderImmediate'}
 end,
 MapMenu=function(menu)
  -- The stock map turns these modifier states into priority orders, which v1 does not carry.
  local function priority(name,modified)
   return (modified=='both' or modified==(Helper and Helper.useShiftToQueueOrders and 'ctrl' or 'shift')) and refuse_capture('MapMenu',name,'priority_order')
  end
  return {
   orderMoveWait=function(component,sectororgate,offset,_,clear,modified,isgate)
    if isgate then return refuse_capture('MapMenu','orderMoveWait','gate_destination') end
    if priority('orderMoveWait',modified) then return 0 end
    local fields=offset_fields(sectororgate,offset)
    if not fields then return refuse_capture('MapMenu','orderMoveWait','unmapped_position') end
    return capture('move',component,fields,clear)
   end,
   orderAttack=function(component,t,clear,modified) return priority('orderAttack',modified) or object_order('MapMenu','attack')(component,t,clear) end,
   orderAttackMultiple=function(component,maintarget,secondary,clear,priority,clearother)
    if priority or (secondary and #secondary>0) then return refuse_capture('MapMenu','orderAttackMultiple','multiple_or_priority') end
    return object_order('MapMenu','attack')(component,maintarget,clear or clearother)
   end,
   orderFollow=function(component,t,clear,modified) return priority('orderFollow',modified) or object_order('MapMenu','follow')(component,t,clear) end,
   buttonConfirmTradeLoop=function()
    local d=menu.contextMenuData or {}
    local op=d.loop=='SingleBuy' and 'buy' or d.loop=='SingleSell' and 'sell'
    local station=saved_of(d.component)
    if not op or not station then refuse_capture('MapMenu','buttonConfirmTradeLoop','unsupported_trade')
    else
     local price=d.price or GetWareData(d.ware,'avgprice')
     capture(op,d.currentShip,{target=station,ware=d.ware,amount=math.floor(tonumber(d.amount or (op=='buy' and d.max or 0)) or -1),
      price=math.floor((tonumber(price) or -1)*100+0.5)})
    end
    pcall(menu.closeContextMenu,'back')
   end,
  },{'buttonConfirmTrade','buttonDefaultOrderConfirm','buttonDeleteAllOrders','buttonDockToTrade','buttonNewOrder','buttonOrderDown',
     'buttonOrderUp','buttonReleaseSyncPoint','buttonRemoveAssignment','buttonSetOrderParam','buttonStartOrders',
     'checkboxOrderPlayerOverride','checkboxOrderPlayerOverrideValue','dropdownBehaviourFormation','dropdownChangeOverrideOrder',
     'dropdownNewSyncPoint','removeOrder','setOrderParamFromMode','slidercellSetOrderParam'}
 end,
}
local hooked={}
local function hook_menus()
 if not cfg.native_gate_player_orders_capture or (hooked.InteractMenu and hooked.MapMenu) then return end
 for _,menu in ipairs(Menus or {}) do
  local build=HOOKS[menu.name]
  if build and not hooked[menu.name] then
   hooked[menu.name]=true
   local wrappers,blocked=build(menu)
   local seen={}
   for name,value in pairs(menu) do
    if type(value)=='function' and name:match('^order%u') then seen[name]=true end
   end
   for _,name in ipairs(blocked) do if type(menu[name])=='function' then seen[name]=true end end
   for name in pairs(wrappers) do if type(menu[name])=='function' then seen[name]=true end end
   local names={};for name in pairs(seen) do names[#names+1]=name end
   table.sort(names)
   local captured,refused=0,0
   for _,name in ipairs(names) do
    if wrappers[name] then menu[name]=wrappers[name];captured=captured+1
    elseif menu[name]~=nil then
     local menu_name=menu.name
     menu[name]=function() return refuse_capture(menu_name,name,'not_lockstep_order') end
     refused=refused+1
    end
   end
   log('PLAYER_ORDERS CAPTURE menu='..menu.name..' captured='..captured..' blocked='..refused)
  end
 end
end

X4LAN_NATIVE_PLAYER_ORDERS={
 bind=function(native)
  assert(type(native)=='table' and type(native.saved_identity)=='function','player orders need gate.saved_identity')
  gate=native
  log(string.format('PLAYER_ORDERS ARMED slot=%d delay=%d ownership=%s owners=%s capture=%s',slot,delay,S.ownership,
   X4LAN_SHA256 and X4LAN_SHA256.digest(S.owner_rows) or S.owner_rows,cfg.native_gate_player_orders_capture and 'ui' or 'off'))
 end,
 -- Proposal reported with tick; the relay delivers it with the permit for a later tick. budget: hex
 -- characters left in the shared input blob (native_gate_inputs_core.lua), nil = the full orders limit.
 proposal=function(tick,budget)
  assert(gate,'player orders not bound to the native gate')
  hook_menus()
  local fresh=tick~=O.tick
  if fresh then
   for _,c in ipairs((script[slot] or {})[tick] or {}) do assert(K.submit(O,c)) end
  end
  local hex,blob=K.propose(O,tick,delay,budget)
  if fresh and blob then
   log(string.format('PLAYER_ORDER_PROPOSED tick=%d slot=%d seq=%d apply=%d count=%d hex=%s',tick,slot,blob.seq,blob.tick,#blob.commands,hex))
  end
  return hex
 end,
 -- The identical slot-ordered input list for this gated tick. Returns the canonical decision
 -- summary, which the probe folds into the hashed gate state.
 apply=function(tick,inputs)
  assert(gate,'player orders not bound to the native gate')
  if not index then index=build_index(tick) end
  local rows,summary=K.decide(S,tick,inputs,resolve)
  for _,row in ipairs(rows) do
   if row.result=='queued' then AddUITriggeredEvent('X4LAN_PlayerOrders','Apply',md_args(row))
   else log('PLAYER_ORDER_APPLIED '..row.head..' result='..row.result..' '..row.tail) end
  end
  return summary
 end,
 -- Portable submission (saved IDs) for other adapters and console tests.
 submit=function(c) return K.submit(O,c) end,
 state={session=S,outbox=O,refused_capture=function() return refused_capture end},
}
