-- Actual build-task/deployment diagnostic; pending requests are not completion.
local cfg=X4LAN_CONFIG
if not cfg or not cfg.native_gate_construction_test then return end
local ffi=require('ffi')
if not pcall(ffi.typeof,'UniverseID') then ffi.cdef('typedef uint64_t UniverseID;') end
ffi.cdef[[
 float GetCurrentBuildProgress(UniverseID);
 const char* GetContainerBuildMethod(UniverseID);
 void SetContainerBuildMethod(UniverseID,const char*);
 size_t GetNumUpgradeSlots(UniverseID,const char*,const char*);
 UniverseID GetUpgradeSlotCurrentComponent(UniverseID,const char*,size_t);
]]
local own=assert(tonumber(cfg.slot:sub(2)))
local native={};local inbox={};local sample_inbox={};local failure
local transaction=assert(X4LAN_NATIVE_CONSTRUCTION_TRANSACTIONS).new()
local raw_reader=assert(X4LAN_NATIVE_CONSTRUCTION_RAW)
local semantic='CONSTRUCTION_PLAN_1|struct_arg_base_02_macro|0,0,0|0,0,0|no_connections|no_equipment|default|unpaid_plot_2000,2000,2000'
local planhash=assert(cfg.native_gate_construction_plan_sha)
local checked_plan=false
local gate,registry_slot,enroll_weapons
local builder_handle='0000000500000003'
local last_admitted=0
local function log(text) DebugError('[X4LAN] NATIVE_GATE '..text) end
local function checked_native(operation,fn,...)
 local value,why=fn(...)
 if value==nil or value==false then
  failure='construction '..operation..' refused: '..tostring(why)
  log('CONSTRUCTION_NATIVE_REFUSED operation='..operation..' reason='..tostring(why))
  error(failure)
 end
 if operation=='ship_bind' and why~=nil then log('CONSTRUCTION_NATIVE_IDENTITY '..tostring(why)) end
 return value,why
end
local function exact_number(value)
 assert(value==value and value~=math.huge and value~=-math.huge,'non-finite native construction number')
 local encoded=string.format('%.17g',value)
 assert(tonumber(encoded)==value,'native construction number did not round-trip')
 return encoded
end
local function exact_id(object)
 local value=tostring(object):gsub('ULL$','')
 assert(value:match('^%d+$'),'invalid exact native construction ID')
 return value
end
local function builder_raw(tick,phase)
 if not native.Builder then return 'builder_pending' end
 local raw,private=checked_native('ship_raw_motion',gate.ship_raw_motion,builder_handle,exact_id(native.Builder))
 local decoded=raw_reader.decode(raw,builder_handle,registry_slot,tick)
 raw_reader.local_detail(private)
 if phase then
  raw_reader.emit(log,'RAW',tick,phase,raw)
  log('CONSTRUCTION_RAW_LOCAL tick='..tick..' phase='..phase..' data='..private)
 end
 return raw,decoded,private
end
local function target_state()
 return 'CONSTRUCTION_TARGETS_2;station_ready='..(native.Station and 1 or 0)..';storage_ready='..(native.Storage and 1 or 0)..';sector_pose_supported=0'
end
local function armament_state()
 if not native.Builder then return 'armament_pending' end
 local count={}
 for _,kind in ipairs({'weapon','turret'}) do
  local n=tonumber(ffi.C.GetNumUpgradeSlots(native.Builder,'',kind))
  assert(n and n>=0 and n<= (kind=='weapon' and 32 or 128) and n==math.floor(n),'construction armament slot census invalid')
  for slot=1,n do assert(ffi.C.GetUpgradeSlotCurrentComponent(native.Builder,kind,slot)==0,'explicitly unarmed builder has installed '..kind) end
  count[kind]=n
 end
 return 'UNARMED_1;weapon_slots='..count.weapon..';turret_slots='..count.turret..';weapons=0;turrets=0'
end
local function registry_state(tick)
 if not registry_slot then assert(tick<5,'construction builder reservation missing');return 'registry_pending' end
 local raw=checked_native('ship_status',gate.ship_status,builder_handle)
 local handle,slot,reserved,bound,prepared,first,kind,late,identity=raw:match('^SHIP_REGISTRY_1;handle=([0-9a-f]+);slot=(%d+);reserved_tick=(%d+);bound_tick=(%d+);prepared_tick=(%d+);first_update_tick=(%d+);first_update_kind=(%d+);late_updates=(%d+);identity_ok=([01])$')
 assert(handle==builder_handle and tonumber(slot)==registry_slot and tonumber(reserved)==5 and tonumber(late)==0,'invalid construction builder registry')
 bound,prepared,first,kind,identity=tonumber(bound),tonumber(prepared),tonumber(first),tonumber(kind),tonumber(identity)
 if native.Builder then
  assert(identity==1 and bound>=5 and bound<=6 and prepared==bound and prepared<=tick,'construction builder not prepared at creation boundary')
  assert((first==0 and kind==0) or (first>=prepared and first<=tick and kind>0),'construction builder updated outside enrollment')
  assert(tick<20 or first>0,'construction builder native updates unobserved')
 else
  assert(tick<=6 and bound==0 and prepared==0 and first==0 and kind==0 and identity==0,'construction builder callback missing or registry bound unexpectedly')
 end
 return raw
end
for _,key in ipairs({'Station','Storage','Builder'}) do
 RegisterEvent('X4LAN_NativeConstruction.'..key,function(_,object)
  assert(native[key]==nil,'construction fixture registered twice')
  native[key]=ConvertIDTo64Bit(object);assert(native[key]~=0,'null construction fixture')
  if key=='Storage' then ffi.C.SetContainerBuildMethod(native[key],'default') end
  if key=='Station' or key=='Storage' then
   local at=tonumber(assert(gate.poll(),'construction target callback outside native gate').issued)
   log('CONSTRUCTION_TARGET_BOUND tick='..at..' kind='..key:lower()..' native_id='..exact_id(native[key])..' sector_pose_supported=0')
  end
  if key=='Builder' then
   assert(gate and registry_slot,'builder created before deterministic reservation')
   local at=tonumber(assert(gate.poll(),'builder callback outside native gate').issued)
   local before,before_fields,private_before=builder_raw(at,'before')
   local unarmed=armament_state()
   log('CONSTRUCTION_ARMAMENT tick='..at..' state='..unarmed)
   local ok,macro,owner=pcall(GetComponentData,native[key],'macro','owner')
   log('CONSTRUCTION_BUILDER_UI tick='..at..' available='..(ok and '1' or '0')..' macro='..(ok and tostring(macro) or 'unavailable')..' owner='..(ok and tostring(owner) or 'unavailable'))
   local native_id=exact_id(native[key])
   -- Runtime identity is private diagnostic evidence, excluded from state hashes.
   log('CONSTRUCTION_BIND_BEGIN tick='..at..' previous_admitted='..last_admitted..' handle='..builder_handle..' native_id='..native_id..' sector_pose_supported=0 raw_phase=before')
   assert(checked_native('ship_bind',gate.ship_bind,builder_handle,native_id)==1,'construction builder enrollment returned invalid success')
   assert(checked_native('enroll_weapons',enroll_weapons,native[key],registry_slot,builder_handle)==1,'construction builder weapon enrollment returned invalid success')
   local after,after_fields,private_after=builder_raw(at,'after')
   raw_reader.same_frame(before_fields,after_fields,private_before,private_after)
   log('CONSTRUCTION_BIND_END tick='..at..' registry='..registry_state(at)..' sector_pose_supported=0 raw_phase=after')
  end
 end)
end
RegisterEvent('X4LAN_NativeConstruction.Error',function(_,why) failure=tostring(why) end)
RegisterEvent('X4LAN_NativeConstruction.Boundary',function(_,phase)
 -- This callback can be deferred. Native dispatch ordinals establish MD ordering.
 local ok,s=pcall(function() return gate and gate.poll() end)
 local at=ok and s and tonumber(s.issued) or 0
 log('CONSTRUCTION_BOUNDARY_UI phase='..tostring(phase)..' tick='..at..' previous_admitted='..last_admitted)
end)
RegisterEvent('X4LAN_NativeConstruction.Ack',function(_,raw) inbox[#inbox+1]=tostring(raw) end)
RegisterEvent('X4LAN_NativeConstruction.State',function(_,raw) sample_inbox[#sample_inbox+1]=tostring(raw) end)
local function canonical_md(raw)
 local tick,exists,processing,assigned,deployed,total,complete,operational,resources=raw:match('^(%d+)|([01])|([01])|([01])|([01])|(%d+)|(%d+)|([01])|(.*)$')
 assert(tick,'malformed native construction state')
 local entries={};local seen={}
 for name,seeded,remaining in resources:gmatch('([%w_]+),(%d+),(%d+);') do
  assert(not seen[name] and tonumber(remaining)<=tonumber(seeded),'invalid construction resource ledger')
  seen[name]=true;entries[#entries+1]=name..','..seeded..','..remaining
 end
 assert(resources:gsub('[%w_]+,%d+,%d+;','')=='','malformed construction resource suffix')
 table.sort(entries)
 assert(#entries<=8 and (resources=='' or #entries>0),'unsupported construction resource list')
 return tonumber(tick),table.concat({tick,exists,processing,assigned,deployed,total,complete,operational,table.concat(entries,';')},'|')
end
local latest_md,latest_tick='pending',0
local state
local plans={[1]={[30]='49010000'},[2]={[90]='4a020000'}}
X4LAN_NATIVE_CONSTRUCTION={
 bind_gate=function(value,weapon_helper)
  assert(not gate or gate==value,'construction gate rebound')
  assert(type(value)=='table' and type(value.ship_reserve)=='function' and type(value.ship_bind)=='function' and type(value.ship_status)=='function' and type(value.ship_raw_motion)=='function' and type(value.poll)=='function','construction requires dynamic enrollment and passive raw motion')
  assert(type(weapon_helper)=='function','construction requires native weapon occupancy verification')
  assert(not enroll_weapons or enroll_weapons==weapon_helper,'construction weapon helper rebound')
  gate=value;enroll_weapons=weapon_helper
 end,
 proposal=function(tick) return plans[own][tick] or '00000000' end,
 apply=function(tick,a,b)
  assert(tick==last_admitted+1,'construction adapter duplicate or skipped frame')
  X4LAN_NATIVE_CONSTRUCTION_TRANSACTIONS.decode(a,1)
  X4LAN_NATIVE_CONSTRUCTION_TRANSACTIONS.decode(b,2)
  if not checked_plan then
   assert(X4LAN_SHA256 and X4LAN_SHA256.digest(semantic)==planhash,'construction semantic plan hash differs')
   checked_plan=true
  end
  assert(gate,'construction gate interface not bound')
  last_admitted=tick
  assert(not failure,'native construction error: '..tostring(failure))
  local acknowledgements={}
  for _,raw in ipairs(inbox) do
   local ticket,kind,result=raw:match('^(%d+%-%d+%-%d+%-%d+)|(%w+)|(.*)$')
   assert(ticket,'malformed construction acknowledgement')
   transaction.ack(ticket,kind,result)
   acknowledgements[#acknowledgements+1]=raw
   log('CONSTRUCTION_ACK tick='..tick..' ticket='..ticket..' kind='..kind..' result='..result)
  end
  inbox={}
  for _,raw in ipairs(sample_inbox) do
   local at,canonical=canonical_md(raw)
   assert(at>latest_tick and at<tick,'native construction sample duplicate or future')
   latest_md=canonical;latest_tick=at
   log('CONSTRUCTION_MD tick='..at..' observed='..tick..' state='..canonical)
  end
  sample_inbox={}
  if tick==5 then
   local readiness=gate.poll()
   assert(cfg.native_gate_registry_md_trace==true and readiness and readiness.registry_md_installed==1,'marked construction requires ready native MD boundary observer')
   registry_slot=checked_native('ship_reserve',gate.ship_reserve,builder_handle,'ship_arg_xl_builder_01_a_macro','player',5)
   assert(type(registry_slot)=='number' and registry_slot>=15 and registry_slot<=30 and registry_slot==math.floor(registry_slot),'construction registry slot outside dynamic ship range')
   AddUITriggeredEvent('X4LAN_NativeConstruction','Setup',{})
  end
  local registry=registry_state(tick)
  local receipt=transaction.batch(tick,a,b,function(action)
   assert(native.Station and native.Storage and native.Builder,'construction fixture not ready')
   AddUITriggeredEvent('X4LAN_NativeConstruction',action.asset==1 and 'Plan' or 'Deploy',action)
  end)
  if tick<20 then
   state='CONSTRUCTION_6:'..tick..':pending:'..planhash..':'..builder_raw(tick)..':'..registry..':'..armament_state()..':'..target_state()
  else
   for _,key in ipairs({'Station','Storage','Builder'}) do assert(native[key],'construction fixture missing '..key) end
   assert(ffi.string(ffi.C.GetContainerBuildMethod(native.Storage))=='default','construction method changed outside admission')
   if tick%30==20 then AddUITriggeredEvent('X4LAN_NativeConstruction','Sample',{tick=tick}) end
   assert(tick<=22 or (latest_tick>0 and tick-latest_tick<=60),'native construction sample missing/stale')
   local progress=ffi.C.GetCurrentBuildProgress(native.Station)
   assert(progress==progress and progress>=-1 and progress<=100,'invalid native build progress')
   -- Numeric arrays produced empty payloads through the game's ffi.string path.
   -- Preserve the exported numeric value with exact double round-trip text.
   local encoded_progress=exact_number(progress)
   local pending={};local complete={}
   for asset=1,2 do pending[asset]=transaction.pending[asset] and transaction.pending[asset].ticket or 'none';complete[asset]=transaction.completed[asset] or 'none' end
   local orders=assert(X4LAN_NATIVE_ORDERS)(native.Builder)
   state=table.concat({'CONSTRUCTION_6',tick,planhash,table.concat(transaction.revisions,','),table.concat(pending,','),table.concat(complete,','),latest_md,encoded_progress,orders,receipt,table.concat(acknowledgements,';'),builder_raw(tick),registry,armament_state(),target_state()},':')
  end
  log('INPUTS tick='..tick..' mode=construction p1='..a..' p2='..b)
  if receipt~='' then log('CONSTRUCTION_RECEIPTS tick='..tick..' data='..receipt) end
  raw_reader.emit(log,'STATE',tick,'sample',state)
  return 'INPUTS_1:construction:'..a..':'..b
 end,
 sample=function() assert(state,'construction sample before admission');return state end,
}
