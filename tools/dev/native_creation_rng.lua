-- Exact internal fixture only; independent 64-bit keys, pre-load agreement and
-- bounded evidence. This does not issue commands or accept player authority.
local ffi,bit=require('ffi'),require('bit')
local M={}
local snapshot='7816351056e6040d92ae9a4619ae2e15a577fa26a19134f11e20746317e61aa8'
local plan='ed86c6c938e57dd0cc15046bf49ed814f0e7b4833241f04010e016179924585c'
local macro='ship_arg_xl_builder_01_a_macro'
local fixture={'0000000000176a13','0000000000176a74','0000000000176ad5','0000000000176b36','0000000000176b97','0000000000176bf8','0000000000176c59','0000000000176c77','0000000000176c95','0000000000176cb3','0000000000176cd1','0000000000176cef'}
local prefix='CREATION_RNG_1;epoch='
local suffix=';handle=0000000500000003;reserved_tick=5;sequence=3;authority=0;world_seed=276944566;macro='..macro..';owner=player;snapshot='..snapshot..';plan='..plan..';policy=AB;schema=1'
local base='SNAPSHOT_BYTES_1|'..snapshot..'|SNAPSHOT_FIXTURE_1|'..table.concat(fixture,',')
local options='serial_pathing seed_rng flight_diag flight_rng flight_full ai_diag ai_rng agent_diag agent_rng '..
 'agent_full ship_diag ship_rng params_init weapon_diag weapon_rng params_diag nav_diag nav_rng '..
 'nav_initial_refresh velocity_diag shape_diag shape_rng shape_initial_refresh bullet_diag bullet_rng '..
 'death_diag death_rng mass_diag attention_diag attention_rng attention_initial_refresh avoidance_refresh '..
 'steering_init spline_diag spline_refresh spline_join radar_diag radar_serial radar_rng '..
 'radar_initial_refresh damage_diag interrupt_diag interrupt_rng contact_diag docking_shape_rng '..
 'docking_shape_refresh world_identities npc_rng npc_diag npc_deadline player_solver_observer '..
 'registry_md_trace player_request_observer'
local function mix(x)
 x=bit.bxor(x,bit.rshift(x,30))*0xbf58476d1ce4e5b9ULL
 x=bit.bxor(x,bit.rshift(x,27))*0x94d049bb133111ebULL
 return bit.bxor(x,bit.rshift(x,31))
end
local function fnv(text)
 local x=14695981039346656037ULL
 for i=1,#text do x=bit.bxor(x,ffi.new('uint64_t',text:byte(i)))*1099511628211ULL end
 return x
end
local function hexword(text)
 local x=0ULL
 for i=1,16 do x=x*16ULL+ffi.new('uint64_t',assert(tonumber(text:sub(i,i),16))) end
 return x
end
local function hex(x) return string.format('%08x%08x',tonumber(bit.rshift(x,32)),tonumber(bit.band(x,0xffffffffULL))) end
local function key(domain,epoch)
 local x=mix(bit.bxor(0x58344c5345454432ULL,domain))
 for _,part in ipairs({5ULL,0x0000000500000003ULL,ffi.new('uint64_t',epoch),3ULL,1ULL,276944566ULL,fnv(macro),fnv('player')}) do x=mix(bit.bxor(x,part)) end
 for _,sha in ipairs({snapshot,plan}) do for offset=1,64,16 do x=mix(bit.bxor(x,hexword(sha:sub(offset,offset+15)))) end end
 return hex(x)
end
function M.parse(text)
 assert(type(text)=='string' and #text<512 and text:sub(1,#prefix)==prefix and text:sub(-#suffix)==suffix,'construction RNG manifest grammar')
 local digits=text:sub(#prefix+1,#text-#suffix)
 assert(#digits>=1 and #digits<=10 and digits:match('^[1-9]%d*$'),'construction RNG epoch grammar')
 local epoch=tonumber(digits);assert(epoch and epoch<=4294967295,'construction RNG epoch range')
 return epoch,key(0x43524541545f4131ULL,epoch),key(0x43524541545f4231ULL,epoch)
end
function M.preimage(config,data)
 assert(config.native_gate_creation_rng_schema==1 and (config.native_gate_creation_rng_ack_schema==1 or config.native_gate_creation_rng_ack_schema==2) and config.native_gate_creation_trace_schema==2 and
  config.native_gate_construction_test==true and config.native_gate_registry_md_trace==true,'construction RNG prerequisites')
 for _,name in ipairs({'native_gate_player_probe','native_gate_management_test','native_gate_player_origin_token',
  'native_gate_world_wait_manifest','native_gate_world_wait_rng_manifest','native_gate_world_find_rng_manifest','native_gate_player_launch_rng',
  'native_gate_world_action_manifest','native_gate_world_values','native_gate_player_transition_observer','native_gate_npc_pilot','native_gate_world_trace_early'}) do
  assert(config[name]==nil,'construction RNG unsupported composition '..name)
 end
 local mask,index=0ULL,0
 for name in options:gmatch('[%w_]+') do
  local value=config['native_gate_'..name]
  assert(value==nil or value==true or value==false,'construction RNG option grammar '..name)
  if value then mask=mask+bit.lshift(1ULL,index) end
  index=index+1
 end
 assert(index==53 and mask==0x84fffffffffffULL,'construction RNG unsupported option mask')
 assert(config.native_gate_snapshot_sha==snapshot and config.native_gate_construction_plan_sha==plan and data==base,'construction RNG bootstrap differs')
 M.parse(config.native_gate_creation_rng_manifest)
 assert(config.native_gate_registry_creation_schema==nil or config.native_gate_registry_creation_schema==1,'provisional registry schema')
 return data..'|'..config.native_gate_creation_rng_manifest..'|CREATION_RNG_ACK_'..config.native_gate_creation_rng_ack_schema..
  (config.native_gate_registry_creation_schema==1 and '|REGISTRY_CREATION_1;handle=0000000500000003;policy=observe' or '')
end
function M.new(gate,config,data,log)
 local manifest=config.native_gate_creation_rng_manifest
 assert(M.preimage(config,base)==data,'construction RNG bootstrap changed before configuration')
 local _,a,b=M.parse(manifest)
 local ack_mode=config.native_gate_creation_rng_ack_schema
 assert(gate.poll().creation_rng_schema==1 and gate.poll().creation_rng_ack_schema==1 and type(gate.creation_rng_ack_status)=='function' and type(gate.creation_rng_config)=='function' and
  type(gate.creation_rng_status)=='function' and type(gate.creation_rng_row)=='function','construction RNG native capability')
 if ack_mode==2 then assert(gate.poll().creation_rng_ack2_schema==2,'construction ACK2 native capability') end
 local receipt=manifest..';seed_a='..a..';seed_b='..b
 local parts={0,0};local finished=false;local footer_logged=false;local prior_count=0;local configured=false;local logged=false;local ack_logged=false;local ack
 local function header()
  if configured and not logged then log('CREATION_RNG_CONFIG '..receipt);logged=true end
  if configured and not ack_logged then log('CREATION_RNG_ACK '..ack);ack_logged=true end
 end
 local function drain(final)
  if finished then return end
  header()
  local status=assert(gate.creation_rng_status(),'construction RNG status missing')
  assert(type(status)=='string' and #status<=767 and status:sub(1,9)=='schema=1 ','construction RNG status grammar')
  local count=tonumber(status:match(' count=(%d+) '));assert(count and count>=prior_count and count<=2,'construction RNG count invalid')
  prior_count=count
  for scope=1,2 do
   if parts[scope]<2 then
    local start=parts[scope];local first=gate.creation_rng_row(scope,start)
    if first then
     for part=start,1 do
      local row=part==start and first or gate.creation_rng_row(scope,part)
      local expected='schema=1 scope='..scope..' part='..part..' '
      assert(type(row)=='string' and #row<=1535 and row:sub(1,#expected)==expected,'construction RNG part missing/oversized')
      log('CREATION_RNG '..row);parts[scope]=part+1
     end
    end
   end
  end
  if final then
   if not footer_logged then log('CREATION_RNG_STOP '..status);footer_logged=true end
   local closure=assert(gate.creation_rng_ack_status(),'construction RNG acknowledgement footer missing')
   assert(type(closure)=='string' and #closure<=383 and closure:sub(1,9)=='schema='..ack_mode..' ','construction RNG acknowledgement footer grammar')
   log('CREATION_RNG_ACK_STOP '..closure);finished=true
  end
 end
 -- Publish the drain closure to the caller before configuration or logging can
 -- fail. A failed first log is retried by the protected abort drain.
 return drain,function()
  assert(not configured,'construction RNG duplicate activation')
  local accepted;accepted,ack=gate.creation_rng_config(manifest,data)
  assert(accepted==receipt,'construction RNG native manifest/key refusal')
  local grammar=ack_mode==1 and '^schema=1 ordinal=[1-8] boot=[0-9a-f]+ digest=[0-9a-f]+ slot=[12] phase=0 issued=0 state=0 records=0 scopes=0 attempted=0$' or
   '^schema=2 ordinal=[1-8] boot=[0-9a-f]+ digest=[0-9a-f]+ slot=[12] phase=[0-3] registry_phase=[0-3] reattach=[01] registry_acks=[0-8] issued=0 state=0 records=0 scopes=0 attempted=0$'
  assert(type(ack)=='string' and #ack<=383 and ack:match(grammar),'construction RNG acknowledgement grammar')
  assert(#ack:match(' boot=([0-9a-f]+) ')==32 and #ack:match(' digest=([0-9a-f]+) ')==64,'construction RNG acknowledgement identity width')
  if ack_mode==2 then
   local ordinal,phase,registry_phase,reattach,registry_acks=ack:match('ordinal=(%d+).* phase=(%d+) registry_phase=(%d+) reattach=(%d+) registry_acks=(%d+)')
   ordinal,phase,registry_phase,reattach,registry_acks=tonumber(ordinal),tonumber(phase),tonumber(registry_phase),tonumber(reattach),tonumber(registry_acks)
   assert((ordinal==1 and phase==0 and reattach==0) or (ordinal>1 and phase>=1 and reattach==1),'construction ACK2 lifecycle')
   assert(registry_phase<=phase and registry_acks==(config.native_gate_registry_creation_schema==1 and ordinal or 0),'construction ACK2 registry pair')
   assert(ordinal==1 or config.native_gate_registry_creation_schema~=1 or registry_phase>=1,'construction ACK2 registry lifecycle')
   assert(config.native_gate_registry_creation_schema==1 or registry_phase==0,'construction ACK2 absent registry phase')
  end
  configured=true;header()
 end
end
X4LAN_NATIVE_CREATION_RNG=M
return M
