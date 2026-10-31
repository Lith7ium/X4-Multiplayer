-- Native singleton diagnostics. This is deliberately NOT a two-player adapter.
-- The negative mode exercises the same exported actions used by the cockpit UI.
local cfg=X4LAN_CONFIG
if not cfg or not cfg.native_gate_player_probe then return end
local mode=cfg.native_gate_player_probe
assert(mode=='observe' or mode=='native-local-negative' or mode=='agreed-singleton' or mode=='agreed-throttle' or mode=='authority-negative' or mode=='agreed-fire' or mode=='agreed-getup' or mode=='diagnostic-getup','invalid player probe mode')
local ffi=require('ffi')
if not pcall(ffi.typeof,'UniverseID') then ffi.cdef('typedef uint64_t UniverseID;') end
if not pcall(ffi.typeof,'UIPosRot') then ffi.cdef('typedef struct {float x,y,z,yaw,pitch,roll;} UIPosRot;') end
if not pcall(ffi.typeof,'SofttargetDetails2') then ffi.cdef('typedef struct {UniverseID softtargetID; const char* softtargetConnectionName; uint32_t messageID;} SofttargetDetails2;') end
ffi.cdef[[
UniverseID GetPlayerID(void); UniverseID GetPlayerContainerID(void);
UniverseID GetPlayerOccupiedShipID(void); UniverseID GetPlayerControlledShipID(void);
bool IsFlightAssistActive(void); bool IsPlayerOccupiedShipDocked(void);
bool CanStartTravelMode(UniverseID); void StartPlayerActivity(const char*);
void StopPlayerActivity(const char*);
void ToggleFlightAssist(void);
UIPosRot GetObjectPositionInSector(UniverseID);
SofttargetDetails2 GetSofttarget2(void);
UIPosRot GetRelativeAimOffset(UniverseID);
]]
local previous=0
local bound
local applied=0
local native_gate
local observed=0
local solver_observed=0
local request_observed=0
local request_scopes=0
local fire_events=0
local fire_digest=string.rep('0',64)
local launch_count=0
local launch_digest=string.rep('0',64)
local player_targets={}
if mode=='agreed-getup' or mode=='diagnostic-getup' then
 assert(cfg.native_gate_player_transition_observer and cfg.native_gate_player_origin_token and cfg.native_gate_world_hash_stride==1,'GetUp requires strict transition/origin/world coverage')
 X4LAN_NATIVE_PLAYER_COMMANDS={
  proposal=function(tick) return cfg.slot=='p1' and tick==29 and '87010000' or '00000000' end,
  apply=function(tick,a,b)
   -- Entire two-slot batch is structurally/semantically decoded before calling
   -- the DLL, which independently checks the actual admitted input words.
   assert(type(a)=='string' and type(b)=='string' and b=='00000000' and
    a==(tick==30 and '87010000' or '00000000'),'GetUp batch/authority refused')
   local gate=assert(X4LAN_NATIVE_GETUP_GATE,'GetUp adapter not configured')
   assert(gate.player_getup_batch(tick,X4LAN_NATIVE_GETUP_RUN,1,a,b)==1,'native GetUp claim/action refused')
   local state='PLAYER_INPUTS_1:'..a..':'..b
   DebugError('[X4LAN] NATIVE_GATE PLAYER_INPUTS tick='..tick..' data='..state)
   return state
  end,
 }
end
if mode=='agreed-fire' then
 assert(cfg.native_gate_player_observer,'native firing requires context observer')
 for i=1,12 do RegisterEvent('X4LAN_NativeGate.Actor'..i,function(_,object) player_targets[i]=ConvertIDTo64Bit(object) end) end
 X4LAN_NATIVE_PLAYER_FIRE_TRACE=function(gate,stopped)
  if stopped then assert(gate.player_trigger_cleanup()==1,'native owned trigger release failed') end
  local summary=assert(gate.player_fire_event(0),'native player fire trace unavailable')
  local count,failures=summary:match('events=(%d+) failures=(%d+)')
  assert(count and failures,'invalid native player fire summary')
  count,failures=tonumber(count),tonumber(failures)
  while fire_events<count do
   fire_events=fire_events+1
   local row=assert(gate.player_fire_event(fire_events),'native player fire event missing')
   DebugError('[X4LAN] NATIVE_GATE PLAYER_FIRE_EVENT '..row)
   fire_digest=X4LAN_SHA256.digest(fire_digest..'\n'..row)
  end
  if stopped or failures>0 then DebugError('[X4LAN] NATIVE_GATE PLAYER_FIRE_'..(stopped and 'STOP' or 'FAILURE')..' '..summary..' emitted='..fire_events) end
  assert(failures==0,'native player fire observer failed')
  local values={}
  for value in summary:gmatch('=(%d+)') do values[#values+1]=value end
  assert(#values==9,'invalid native player fire summary fields')
  local state='PLAYER_PROJECTILES_1:'..table.concat(values,':')..':'..fire_digest
  if cfg.native_gate_player_weapon_rng then
   local summary=assert(gate.player_launch_event(0),'native player launch summary missing')
   local count,failures,scopes,restored=summary:match('^schema=1 domain=504c5957504e3031 count=(%d+) failures=(%d+) scopes=(%d+) restored=(%d+) bullet_rng=0$')
   assert(count and tonumber(count)<=8192,'invalid native player launch summary')
   while launch_count<tonumber(count) do
    launch_count=launch_count+1
    local row=assert(gate.player_launch_event(launch_count),'native player launch receipt missing')
    DebugError('[X4LAN] NATIVE_GATE PLAYER_LAUNCH_EVENT '..row)
    -- Ambient caller words prove restoration locally, not equality of unrelated streams.
    local canonical=row:gsub(' caller=[0-9a-f]+',''):gsub(' restored=[0-9a-f]+','')
    launch_digest=X4LAN_SHA256.digest(launch_digest..'\n'..canonical)
   end
   if stopped or tonumber(failures)>0 then DebugError('[X4LAN] NATIVE_GATE PLAYER_LAUNCH_'..(stopped and 'STOP' or 'FAILURE')..' '..summary..' emitted='..launch_count) end
   assert(failures=='0' and scopes==restored,'native player launch scope/restore failed')
   state=state..'|PLAYER_LAUNCH_1:1:504c5957504e3031:'..count..':'..failures..':'..scopes..':'..restored..':0:'..launch_digest
  end
  return state
 end
end
if cfg.native_gate_player_observer then
 X4LAN_NATIVE_PLAYER_TRACE=function(gate,stopped)
  local summary=assert(gate.player_observe(0),'native player observer unavailable')
  local count,failures=summary:match('count=(%d+) failures=(%d+)')
  assert(count and failures,'invalid native player observer summary')
  count,failures=tonumber(count),tonumber(failures)
  assert(count<=16384,'native player observer capacity exceeded')
  while observed<count do
   local row=gate.player_observe(observed+1)
   if not row then break end
   observed=observed+1
   DebugError('[X4LAN] NATIVE_GATE PLAYER_OBSERVATION '..row)
  end
  if stopped or failures>0 then DebugError('[X4LAN] NATIVE_GATE PLAYER_OBSERVER_'..(stopped and 'STOP' or 'FAILURE')..' '..summary..' emitted='..observed) end
  assert(failures==0,'native player observer failed')
  if stopped then assert(count==observed and summary:match('restored=1 active=0'),'native player observer restoration/trace incomplete') end
  if cfg.native_gate_player_solver_observer then
   local summary=assert(gate.player_solver_event(0),'native solver observer unavailable')
   local count,failures=summary:match('schema=1 count=(%d+) failures=(%d+)')
   assert(count and tonumber(count)<=8192,'invalid native solver summary')
   while solver_observed<tonumber(count) do
    local row=gate.player_solver_event(solver_observed+1)
    if not row then break end
    solver_observed=solver_observed+1
    DebugError('[X4LAN] NATIVE_GATE PLAYER_SOLVER_EVENT '..row)
   end
   if stopped or failures~='0' then DebugError('[X4LAN] NATIVE_GATE PLAYER_SOLVER_'..(stopped and 'STOP' or 'FAILURE')..' '..summary..' emitted='..solver_observed) end
   assert(failures=='0','native solver observer failed')
   if stopped then assert(tonumber(count)==solver_observed and summary:match('open=0 restored=1 active=0'),'native solver observer restoration/trace incomplete') end
  end
 end
end
local function request_trace(gate,stopped)
 if not cfg.native_gate_player_request_observer then return end
 local summary=assert(gate.player_request_event(0),'native request observer unavailable')
 local count,scopes,failures=summary:match('schema=2 count=(%d+) scopes=(%d+) failures=(%d+)')
 assert(count and tonumber(count)<=16384 and tonumber(scopes)<=2048,'native request capacity/summary invalid')
 while request_observed<tonumber(count) do
  local row=gate.player_request_event(request_observed+1)
  if not row then break end
  request_observed=request_observed+1
  DebugError('[X4LAN] NATIVE_GATE PLAYER_REQUEST_EVENT '..row)
 end
 while request_scopes<tonumber(scopes) do
  local before=gate.player_request_scope(request_scopes+1,0)
  if not before then break end
  local after=assert(gate.player_request_scope(request_scopes+1,1),'native request scope missing after capture')
  request_scopes=request_scopes+1
  -- Native export remains complete. Bound each logger payload rather than
  -- assuming DebugError can retain a worst-case ~12KB scope row.
  for phase,row in ipairs({before,after}) do
   assert(#row>0 and #row<16384,'native request scope export length invalid')
   local parts=math.ceil(#row/768)
   for part=1,parts do
    DebugError('[X4LAN] NATIVE_GATE PLAYER_REQUEST_SCOPE_CHUNK index='..request_scopes..' phase='..(phase-1)..' part='..part..' parts='..parts..' bytes='..#row..' data='..row:sub((part-1)*768+1,part*768))
   end
  end
 end
 if stopped or failures~='0' then DebugError('[X4LAN] NATIVE_GATE PLAYER_REQUEST_'..(stopped and 'STOP' or 'FAILURE')..' '..summary..' emitted='..request_observed..' emitted_scopes='..request_scopes) end
 assert(failures=='0','native request observer failed')
 if stopped then assert(tonumber(count)==request_observed and tonumber(scopes)==request_scopes and summary:match(' open=0 ') and summary:match(' restore_open=0 restored=1 active=0'),'native request restoration/trace incomplete') end
end
if cfg.native_gate_player_request_observer then
 local previous_trace=assert(X4LAN_NATIVE_PLAYER_TRACE,'native request observer requires player trace')
 X4LAN_NATIVE_PLAYER_TRACE=function(gate,stopped)
  request_trace(gate,stopped)
  previous_trace(gate,stopped)
 end
end
local function id(value) return tostring(value):gsub('ULL$','') end
local function atom(value)
 local text=type(value)=='number' and string.format('%.17g',value) or tostring(value)
 assert(text:match('^[%w_%.%+%-]+$'),'invalid native player state')
 return text
end
local function flag(value) return value and '1' or '0' end
local function decode(command,slot)
 assert(type(command)=='string' and command:match('^[0-9a-f]+$') and #command==8,'invalid native player command')
 if command=='00000000' then return end
 local op=tonumber(command:sub(1,2),16)
 assert(slot==1 and command:sub(3,4)=='01' and command:sub(7,8)=='00','native player context ownership refused')
 local value=tonumber(command:sub(5,6),16)
 assert((op==0x84 and mode=='agreed-throttle' and value<=200) or ((op==0x81 or op==0x82 or op==0x83) and value==0) or
        (mode=='agreed-fire' and ((op==0x85 and value<=1) or (op==0x86 and value>=1 and value<=12))),'unsupported native player action')
 return {op=op,value=op==0x84 and (value-100)/100 or value}
end
if mode=='agreed-singleton' or mode=='agreed-throttle' or mode=='authority-negative' or mode=='agreed-fire' then
 X4LAN_NATIVE_PLAYER_COMMANDS={
  proposal=function(tick)
   if mode=='authority-negative' then return tick==29 and '81010000' or '00000000' end
   -- Logical player 2 has no independent native context. It cannot act on p1's.
   if cfg.slot~='p1' then return '00000000' end
   if mode=='agreed-fire' then return ({[29]='86010c00',[89]='85010100',[269]='85010000',[419]='85010100',[599]='85010000'})[tick] or '00000000' end
   if mode=='agreed-throttle' then return ({[29]='8401c800',[149]='84016400',[179]='82010000',[209]='8401c800',[899]='83010000',[929]='84016400'})[tick] or '00000000' end
   return ({[29]='81010000',[59]='81010000',[89]='82010000',[599]='83010000'})[tick] or '00000000'
  end,
  apply=function(tick,a,b)
   assert(tick==applied+1,'native player batch repeated or out of order')
   local ok,ops=pcall(function() return {decode(a,1),decode(b,2)} end) -- Entire batch before writes.
   if not ok then
    DebugError('[X4LAN] NATIVE_GATE PLAYER_BATCH_REJECTED tick='..tick..' p1='..tostring(a)..' p2='..tostring(b)..' effects=0')
    error(ops)
   end
   if ops[1] then
    assert(bound and bound[4]~='0000000000000000','native player context unavailable')
    local current=assert(X4LAN_NATIVE_PLAYER_IDENTITY,'player context resolver missing')()
    assert(current==table.concat(bound,':'),'native player context changed')
    local action
    if ops[1].op==0x81 then action='flight_assist_toggle';ffi.C.ToggleFlightAssist()
    elseif ops[1].op==0x82 then
     assert(ffi.C.CanStartTravelMode(ffi.C.GetPlayerControlledShipID()),'native travel unavailable')
     action='travel_start';ffi.C.StartPlayerActivity('travel')
    elseif ops[1].op==0x83 then action='travel_stop';ffi.C.StopPlayerActivity('travel')
    elseif ops[1].op==0x85 then
     action='trigger_'..ops[1].value
     assert(native_gate.player_trigger(tick,1,bound[4],ops[1].value)==1,'native trigger/context refused')
    elseif ops[1].op==0x86 then
     local target=assert(player_targets[ops[1].value],'native selection fixture unavailable')
     local saved=assert(native_gate.saved_identity(id(target)))
     assert(saved~='0000000000000000','native selection target unmapped')
     action='target_'..a:sub(5,6)
     assert(native_gate.player_target(tick,1,bound[4],ops[1].value)==1,'native target/context refused')
     DebugError('[X4LAN] NATIVE_GATE PLAYER_TARGET_REQUEST tick='..tick..' fixture='..ops[1].value..' saved='..saved)
    else
     assert(native_gate.player_throttle(tick,1,bound[4],ops[1].value)==1,'native throttle/context refused')
     action='throttle_'..a:sub(5,6)
    end
    DebugError('[X4LAN] NATIVE_GATE PLAYER_ACTION tick='..tick..' slot=p1 actor='..bound[4]..' action='..action..' source=admitted_native_export local_only=0')
   end
   applied=tick
   local data='PLAYER_INPUTS_1:'..a..':'..b
   DebugError('[X4LAN] NATIVE_GATE PLAYER_INPUTS tick='..tick..' data='..data)
   return data
  end,
 }
end
X4LAN_NATIVE_PLAYER=function(tick,gate)
 native_gate=gate
 assert(tick==previous+1,'player probe tick discontinuity')
 previous=tick
 local player,container,occupied,controlled=ffi.C.GetPlayerID(),ffi.C.GetPlayerContainerID(),ffi.C.GetPlayerOccupiedShipID(),ffi.C.GetPlayerControlledShipID()
 local saved={}
 for i,value in ipairs({player,container,occupied,controlled}) do
  saved[i]=assert(gate.saved_identity(id(value)),'player identity query failed')
  assert(value==0 or saved[i]~='0000000000000000','unmapped native player identity')
 end
 if mode=='agreed-getup' or mode=='diagnostic-getup' then
  assert(saved[1]=='0000000000011e12','GetUp player identity changed')
  if not bound then
   assert(tick==1 and saved[2]=='00000000000ae79f' and saved[3]=='00000000000ae79c' and saved[4]=='00000000000ae79c','GetUp initial selected context unavailable');bound=saved
  end
  local state=assert(gate.player_getup_state(tick),'GetUp selected lifetime/transition unavailable')
  local prefix=mode=='diagnostic-getup' and 'PLAYER_GETUP_LOCAL_1' or 'PLAYER_GETUP_STATE_1'
  local run,processed,claimed,accepted,phase=state:match('^'..prefix..':([0-9a-f]+):1:(%d+):([01]):([01]):([0-5]):')
  assert(run==X4LAN_NATIVE_GETUP_RUN and tonumber(processed)==tick,'GetUp state schema/run/tick mismatch')
  if tonumber(phase)<4 then assert(table.concat(saved,':')==table.concat(bound,':'),'GetUp context changed without publication')
  else
   -- Same selected ship only. Native view separately proves the original
   -- player ancestry and publication's runtime/object/Agent/faction continuity.
   assert(saved[4]=='0000000000000000' and (saved[3]==bound[3] or saved[3]=='0000000000000000') and
    (saved[2]==bound[2] or saved[2]=='00000000000ae79c'),'unsupported GetUp public context transition')
  end
  state=state..'|PLAYER_GETUP_UI_1:'..table.concat(saved,':')
  DebugError('[X4LAN] NATIVE_GATE PLAYER_GETUP_STATE tick='..tick..' data='..state)
  return state
 end
 if bound then assert(table.concat(saved,':')==table.concat(bound,':'),'native player context changed') else bound=saved end
 X4LAN_NATIVE_PLAYER_IDENTITY=function()
  local result={}
  for i,value in ipairs({ffi.C.GetPlayerID(),ffi.C.GetPlayerContainerID(),ffi.C.GetPlayerOccupiedShipID(),ffi.C.GetPlayerControlledShipID()}) do
   result[i]=assert(gate.saved_identity(id(value)))
  end
  return table.concat(result,':')
 end
 if tick==1 then
  DebugError('[X4LAN] NATIVE_GATE PLAYER_CONTEXT slot='..cfg.slot..' player='..saved[1]..' container='..saved[2]..' occupied='..saved[3]..' controlled='..saved[4]..' native_contexts=1 input_capture=none')
  if cfg.native_gate_player_observer then
   assert(gate.player_observe_enable()==1,'native player observer install refused')
   DebugError('[X4LAN] NATIVE_GATE PLAYER_OBSERVER_ENABLED tick=1 early_coverage=0 input_changes=0')
   if cfg.native_gate_player_solver_observer then
    assert(gate.player_solver_enable()==1,'native solver observer install refused')
    if cfg.native_gate_player_request_observer then
     assert(gate.player_request_enable()==1,'native request observer install refused')
     DebugError('[X4LAN] NATIVE_GATE PLAYER_REQUEST_ENABLED tick=1 schema=2 input_changes=0 early_coverage=0 generation_coverage=0 native_faction_coverage=0 raw_bank_runtime_refs=1')
    end
    DebugError('[X4LAN] NATIVE_GATE PLAYER_SOLVER_ENABLED tick=1 schema=1 early_coverage=0 input_changes=0')
   end
  end
  if mode=='agreed-fire' then
   assert(gate.player_fire_enable()==1,'native player firing manifest refused')
   if cfg.native_gate_player_weapon_rng then
    assert(gate.player_launch_enable()==1,'native player launch RNG manifest refused')
    DebugError('[X4LAN] NATIVE_GATE PLAYER_LAUNCH_ENABLED tick=1 schema=1 domain=504c5957504e3031 bullet_rng=0')
   end
   DebugError('[X4LAN] NATIVE_GATE PLAYER_FIRE_ENABLED tick=1 native_trigger=1 rng_changes='..(cfg.native_gate_player_weapon_rng and '1' or '0')..' trace=2')
  end
 end
 if cfg.native_gate_player_observer then X4LAN_NATIVE_PLAYER_TRACE(gate,false) end
 if mode=='native-local-negative' and tick==30 then
  assert(controlled~=0,'native local action requires an actually controlled ship')
  local action
  if cfg.slot=='p1' then action='flight_assist_toggle';ffi.C.ToggleFlightAssist()
  else
   assert(ffi.C.CanStartTravelMode(controlled),'native travel unavailable')
   action='travel_start';ffi.C.StartPlayerActivity('travel')
  end
  DebugError('[X4LAN] NATIVE_GATE PLAYER_ACTION tick='..tick..' slot='..cfg.slot..' actor='..saved[4]..' action='..action..' source=native_export local_only=1')
 end
 local speed={GetPlayerSpeed()}
 assert(#speed==8,'incomplete native player speed tuple')
 for i,value in ipairs(speed) do speed[i]=type(value)=='boolean' and flag(value) or atom(value) end
 local activity=atom(GetPlayerActivity())
 local state='PLAYER_SINGLETON_1:'..table.concat(saved,':')..':'..flag(ffi.C.IsFlightAssistActive())..':'..flag(ffi.C.IsPlayerOccupiedShipDocked())..':'..activity..':'..table.concat(speed,',')
 DebugError('[X4LAN] NATIVE_GATE PLAYER_STATE tick='..tick..' data='..state)
 if mode=='agreed-throttle' or mode=='agreed-fire' then
  local axes=assert(gate.player_controls(),'native player controls unavailable')
  assert(axes:match('^PLAYER_AXES_1:'..saved[4]..':[0-9a-f]+$') and #axes==87,'invalid native axes state')
  local pose=ffi.C.GetObjectPositionInSector(controlled)
  local geometry='PLAYER_POSE_1:'..table.concat({atom(pose.x),atom(pose.y),atom(pose.z),atom(pose.yaw),atom(pose.pitch),atom(pose.roll)},',')
  DebugError('[X4LAN] NATIVE_GATE PLAYER_CONTROLS tick='..tick..' data='..axes..'|'..geometry)
  state=state..'|'..axes..'|'..geometry
 end
 if mode=='agreed-fire' then
  local projectiles=X4LAN_NATIVE_PLAYER_FIRE_TRACE(gate,false)
  local fire=assert(gate.player_fire_state(),'native trigger state unavailable')
  local target=ffi.C.GetSofttarget2().softtargetID
  local saved=assert(gate.saved_identity(id(target)),'native target identity unavailable')
  assert(target==0 or saved~='0000000000000000','native target identity unmapped')
  local aim=target~=0 and ffi.C.GetRelativeAimOffset(target) or {x=0,y=0,z=0,yaw=0,pitch=0,roll=0}
  local pointing='PLAYER_AIM_1:'..saved..':'..table.concat({atom(aim.x),atom(aim.y),atom(aim.z),atom(aim.yaw),atom(aim.pitch),atom(aim.roll)},',')
  DebugError('[X4LAN] NATIVE_GATE PLAYER_FIRE_STATE tick='..tick..' data='..fire..'|'..pointing..'|'..projectiles)
  state=state..'|'..fire..'|'..pointing..'|'..projectiles
 end
 return state
end
