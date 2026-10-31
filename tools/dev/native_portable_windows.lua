-- Disposable, command-free native peer. The main-thread DLL owns NGP traffic.
local cfg=X4MP_PORTABLE_CONFIG
if type(cfg)~='table' or cfg.enabled~=true then return end
local ffi=require('ffi')
local function hex(s,n) return type(s)=='string' and #s==n and s:match('^[0-9a-f]+$') end
local function integer(n,low,high) return type(n)=='number' and n>=low and n<=high and n==math.floor(n) end
local euler=cfg.mode=='portable-fixture12-euler-v2'
assert(cfg.rotation_rejection_diagnostics==nil or type(cfg.rotation_rejection_diagnostics)=='boolean','rotation diagnostic option must be boolean')
local rejection_enabled=cfg.rotation_rejection_diagnostics==true
assert(not rejection_enabled or euler,'rotation diagnostic requires Euler mode')
local euler_contract='77b19d6f210426c86c73bafdf3a3f2531747968819a5ef61e93344f902da2790'
local euler_descriptor='27c9bd768900673531c698e59b410fc38940b036ef9563e5e9483e9f3303537b'
local euler_inputs='8b8ab6d4697192f708c192c5e2c236e9e9eaf7a2faf35246456e592ea318fa70'
local euler_snapshot='7816351056e6040d92ae9a4619ae2e15a577fa26a19134f11e20746317e61aa8'
assert(ffi.os=='Windows' and (cfg.mode=='portable-native-v1' or euler) and cfg.slot==1,'Windows portable slot1 required')
if euler then
 assert(cfg.contract_sha256==euler_contract and cfg.import_math_sha256==euler_descriptor and
  cfg.snapshot_sha256==euler_snapshot,'reviewed Euler import capability required')
else
 assert(cfg.import_math_sha256==nil and cfg.contract_sha256~=euler_contract and
  cfg.contract_sha256~='d1e810d9180594338f64af4fe287834f618da24181e1f5b53d4924ff58717d82','Euler capability requires its explicit mode')
end
local profiles={['45850']=true,['45851']=true,['45853']=true,['45854']=true,
 ['45855']=true,['45856']=true,['45857']=true,['45858']=true}
assert(profiles[cfg.profile] and type(cfg.save_path)=='string','owned released-lane profile required')
assert(type(cfg.run)=='string' and #cfg.run<=80 and cfg.run:match('^[a-z0-9_-]+$'),'invalid owned run')
assert(hex(cfg.token,32) and hex(cfg.snapshot_sha256,64) and hex(cfg.contract_sha256,64) and hex(cfg.agent_sha256,64),'portable identity invalid')
assert(cfg.schema_sha256=='57edd192fe98d5daa46df95e7d84d17a76976a51776cf06fcf9e4e5e8e5dde3e','portable schema invalid')
assert(integer(cfg.port,1024,65535) and integer(cfg.timeout_ms,1000,120000) and integer(cfg.ticks,1,120),'portable bounds invalid')
local startup=cfg.startup_timeout_ms or 600000
assert(integer(startup,cfg.timeout_ms,900000),'portable startup bound invalid')
assert(type(cfg.fixture_manifest)=='string' and #cfg.fixture_manifest==203,'portable fixture manifest missing')
ffi.cdef[[const char* GetSaveFolderPath(void);]]
local function path(s) return tostring(s):gsub('\\','/'):gsub('/+$',''):lower() end
local function owned() return path(ffi.string(ffi.C.GetSaveFolderPath()))==path(cfg.save_path) end
local suffix='/'..cfg.profile..'/save'
assert(path(cfg.save_path):sub(-#suffix)==suffix and owned(),'portable adapter outside owned profile')
if GetUISafeModeOption then assert(GetUISafeModeOption()==false,'owned profile requires native modules') end
local native=assert(package.loadlib('./extensions/x4_lan_lab/native/x4lan_gate_64.dll','luaopen_x4lan_gate'))()
assert(type(native)=='table','portable native module unavailable')
local armed,cancelled=false,false
local function cancel()
 if cancelled or not armed then return false end
 cancelled=true
 local ok,restored=pcall(native.portable_cancel)
 DebugError('[X4LAN] PORTABLE_BOOT CANCEL restored='..tostring(ok and restored==1)..' run='..tostring(cfg.run))
 return ok and restored==1
end
local rejection_logged=false
local rejection_attempts=0
local function rejection_terminal(final_attempt)
 if not rejection_enabled or rejection_logged then return end
 rejection_attempts=rejection_attempts+1
 local ok,pending=pcall(function()
  local function fields(raw,schema,keys)
   assert(type(raw)=='string' and #raw<=2047 and #raw>0 and raw:match('^[a-z0-9_=%- ]+$'),'rejection record bounds')
   local values,order={},{}
   for key,value in raw:gmatch('([a-z0-9_]+)=([^ ]+)') do
    assert(values[key]==nil,'duplicate rejection field');values[key]=value;order[#order+1]=key
   end
   assert(table.concat(order,' ')==keys and values.schema==schema,'rejection record schema')
   local rebuilt={};for _,key in ipairs(order) do rebuilt[#rebuilt+1]=key..'='..values[key] end
   assert(table.concat(rebuilt,' ')==raw,'rejection record encoding');return values
  end
  local function number(v,high)
   assert(type(v)=='string' and v:match('^%d+$') and (#v==1 or v:sub(1,1)~='0') and #v<=16,'rejection integer')
   local n=tonumber(v);assert(n and n<=high,'rejection integer bound');return n
  end
  local prefix='[X4LAN] PORTABLE_ROTATION_REJECT slot=1 run='..cfg.run..' agent='..cfg.agent_sha256..' '
  local raw=native.portable_rotation_rejection(0)
  if raw==nil then return true end -- Only copied readout readiness; no engine retry.
  local h=fields(raw,'x4-rotation-reject-v1','schema enabled present readout_drained capture_scope context_scope stage predicate helper_known helper_result operands_known actual expected serial role class accounted owner thread phase scope_open load_open primary_failure_at_entry selected0 selected1 load_calls load_returns image_base basis xml agent movement bank records bytes truncated failed_reads attempted_reads dropped_records')
  assert(h.enabled=='1' and h.readout_drained=='1' and h.capture_scope=='failing_stage' and h.context_scope=='predicate_only','rejection boundary')
  local count,bytes=number(h.records,192),number(h.bytes,8192)
  assert(h.present=='0' or h.present=='1','rejection presence')
  local rows={raw};local copied,failed=0,0
  for i=1,count do
   local row=native.portable_rotation_rejection(i)
   local f=fields(row,'x4-rotation-reject-read-v1','schema index address requested_bytes copied_bytes ok data')
   assert(number(f.index,192)==i and hex(f.address,16),'rejection read identity')
   local n=number(f.copied_bytes,512);number(f.requested_bytes,9007199254740991)
   assert(f.ok=='0' or f.ok=='1','rejection read result')
   assert((n==0 and f.data=='-') or (n>0 and f.ok=='1' and hex(f.data,n*2)),'rejection read bytes')
   assert(n==0 or n==tonumber(f.requested_bytes),'rejection copied length')
   if f.ok=='0' then failed=failed+1 end
   copied=copied+n;rows[#rows+1]=row
  end
  assert(copied==bytes and failed<=number(h.failed_reads,4294967295),'rejection aggregate')
  local dropped=number(h.dropped_records,4294967295)
  assert(number(h.attempted_reads,4294967295)==count+dropped and (dropped==0 or count==192),'rejection record coverage')
  if h.present=='1' then
   local row=native.portable_rotation_rejection(count+1)
   local c=fields(row,'x4-rotation-reject-completion-v1','schema return_valid raw_return unwind primary_failure_after scope_open load_open role0_returns role1_returns')
   assert((c.return_valid=='0' and c.raw_return=='-') or (c.return_valid=='1' and hex(c.raw_return,16)),'rejection completion')
   rows[#rows+1]=row
  else assert(count==0 and bytes==0,'empty rejection capture') end
  for _,row in ipairs(rows) do assert(#prefix+#row<=2048,'rejection log bound') end
  for _,row in ipairs(rows) do DebugError(prefix..row) end
 end)
 if ok and pending and not final_attempt and rejection_attempts<600 then return end
 rejection_logged=true
 if not ok or pending then DebugError('[X4LAN] PORTABLE_ROTATION_REJECT_ERROR run='..cfg.run..' agent='..cfg.agent_sha256..' reason='..(ok and 'unavailable' or 'invalid')) end
end
local ok,why=pcall(function()
 assert(native.boot(1,cfg.port,cfg.token,cfg.timeout_ms,cfg.snapshot_sha256,70368744177664)==0,'portable native boot refused')
 armed=true
 assert(native.portable_config(cfg.fixture_manifest,cfg.contract_sha256,cfg.ticks)==1,'portable native fixture refused')
 if euler then
  assert(native.portable_rotation_rejection_enable(rejection_enabled and 1 or 0)==1,'rotation diagnostic binding refused')
  assert(native.portable_rotation_config(cfg.mode,cfg.import_math_sha256,cfg.snapshot_sha256,cfg.contract_sha256)==1,
   'portable native Euler import refused')
 end
 assert(native.portable_transport(cfg.agent_sha256,startup)==1,'portable native transport refused')
end)
if not ok then
 -- Keep a bounded pre-cancellation diagnostic when configuration refuses. This
 -- is never a terminal import/restoration receipt and cannot grant success.
 if euler then pcall(function()
  local raw=native.portable_rotation_status()
  if type(raw)=='string' and #raw<=2047 and raw:match('^schema=x4%-rotation%-status%-v2 ') and
   raw:match('^[a-z0-9_=%- ]+$') then
   DebugError('[X4LAN] PORTABLE_ROTATION STARTUP_REFUSED run='..cfg.run..' agent='..cfg.agent_sha256..' '..raw)
  end
 end) end
 cancel()
 -- No callback takeover or fallback backend after a fatal configuration error.
 -- Already drained copied evidence may be read now; later drain is unavailable.
 rejection_terminal(true)
 error(why)
end
local function ready()
 if cancelled then return false end
 assert(owned(),'portable adapter left owned profile')
 return native.portable_ready()==1 and (not euler or native.portable_rotation_ready()==1) and
  native.portable_transport(cfg.agent_sha256,startup)==1
end
local terminal_logged=false
local function rotation_terminal()
 local text=native.portable_rotation_status()
 if text==nil then return nil end
 assert(type(text)=='string' and #text>0 and #text<=2047,'rotation status bounds')
 local names={schema='x4-rotation-status-v2',mode=cfg.mode,descriptor=euler_descriptor,
  contract=euler_contract,snapshot=euler_snapshot,inputs=euler_inputs}
 local numbers={configured=true,phase=true,stop=true,failure=true,load_calls=true,load_returns=true,
  load_true=true,load_false=true,load_abnormal=true,load_open=true,role0_calls=true,role0_returns=true,
  role0_euler=true,role1_calls=true,role1_returns=true,role1_euler=true,foreign=true,open=true,owned=true,
  restore_error=true,restored=true,complete=true,lifetimes=true,paired=true}
 for _,role in ipairs({'role0','role1'}) do
  for _,class in ipairs({'nav','player'}) do
   for _,field in ipairs({'calls','returns','euler'}) do numbers[role..'_'..class..'_'..field]=true end
  end
 end
 local fields,parts={},{}
 for part in text:gmatch('[^ ]+') do
  assert(part:match('^[a-z0-9_]+=[a-z0-9_-]+$'),'rotation status encoding')
  local key,value=part:match('^([^=]+)=(.+)$')
  assert(fields[key]==nil and (names[key] or numbers[key]),'rotation status fields')
  if numbers[key] then
   assert(value:match('^%d+$') and (#value==1 or value:sub(1,1)~='0') and #value<=16,'rotation status number')
   value=assert(tonumber(value));assert(value<=9007199254740991,'rotation status number')
  else assert(value==names[key],'rotation status capability') end
  fields[key]=value;parts[#parts+1]=part
 end
 assert(#parts==42 and table.concat(parts,' ')==text,'rotation status incomplete or noncanonical')
 for _,key in ipairs({'configured','stop','restored','complete'}) do assert(fields[key]<=1,'rotation status flag') end
 assert(fields.phase<=5 and fields.owned<=3,'rotation status phase or patch mask')
 local complete=fields.configured==1 and fields.phase==4 and fields.stop==1 and fields.failure==0 and
  fields.load_calls==1 and fields.load_returns==1 and fields.load_true==1 and fields.load_false==0 and
  fields.load_abnormal==0 and fields.load_open==0 and fields.open==0 and fields.owned==0 and
  fields.restore_error==0 and fields.restored==1 and fields.complete==1
 for _,role in ipairs({'role0','role1'}) do
  for _,name in ipairs({'calls','returns','euler'}) do complete=complete and fields[role..'_'..name]==10006 end
 end
 for _,role in ipairs({'role0','role1'}) do
  for class,expected in pairs({nav=10005,player=1}) do
   for _,field in ipairs({'calls','returns','euler'}) do
    complete=complete and fields[role..'_'..class..'_'..field]==expected
   end
  end
 end
 complete=complete and fields.lifetimes==10006 and fields.paired==10006
 return complete,text
end
local function observe()
 if terminal_logged then rejection_terminal();return end
 -- Fresh native reads only. A nil status during a UI transition is not a
 -- terminal receipt, and the observer never calls readiness or game getters.
 local text=native.portable_status()
 if text==nil then return end
 assert(type(text)=='string' and #text<=639,'portable native status bounds')
 local fields={}
 local numbers={configured=true,frames=true,last=true,calls=true,returns=true,failures=true,
  wire=true,draining=true,closed=true,terminal_restored=true,active=true,reason=true,issued=true,completed=true}
 local names={schema=true,saved=true,stage=true,wire_stage=true}
 local count=0
 for key,value in text:gmatch('([^ ]+)=([^ ]+)') do
  assert(not fields[key] and (numbers[key] or names[key]),'portable native status fields')
  fields[key]=value;count=count+1
 end
 assert(count==18,'portable native status incomplete')
 local rebuilt={}
 for item in text:gmatch('[^ ]+') do
  assert(item:match('^[a-z_]+=[a-z0-9_]+$'),'portable native status encoding')
  rebuilt[#rebuilt+1]=item
 end
 assert(table.concat(rebuilt,' ')==text and #rebuilt==18,'portable native status encoding')
 for key in pairs(numbers) do
  local value=fields[key]
  assert(value and value:match('^%d+$') and (#value==1 or value:sub(1,1)~='0') and #value<=16,
   'portable native status number')
  fields[key]=assert(tonumber(value))
  assert(fields[key]<=9007199254740991,'portable native status number')
 end
 assert(fields.schema==cfg.schema_sha256 and hex(fields.saved,16) and
  #fields.stage<=64 and #fields.wire_stage<=64,'portable native status identity')
 for _,key in ipairs({'configured','wire','draining','closed','terminal_restored','active'}) do
  assert(fields[key]<=1,'portable native status flag')
 end
 if fields.active~=0 or fields.reason==0 or fields.draining~=0 then return end
 local success=fields.configured==1 and fields.wire==1 and fields.last==cfg.ticks and
  fields.completed==cfg.ticks and fields.issued==cfg.ticks and fields.frames==cfg.ticks+1 and
  fields.calls==cfg.ticks+1 and fields.returns==cfg.ticks+1 and fields.failures==0 and
  fields.reason==1 and fields.closed==1 and fields.terminal_restored==1 and
  fields.saved=='0000000000000000' and fields.stage=='complete' and fields.wire_stage=='closed'
 -- ERROR/CANCEL remain fatal even if a prior native close was successful.
 if success and cancelled then return end
 if euler then
  local math_complete,math_text=rotation_terminal()
  if math_complete==nil then return end
  rejection_terminal()
  local receipt='[X4LAN] PORTABLE_ROTATION TERMINAL result='..(math_complete and 'complete' or 'failure')..
   ' slot=1 run='..cfg.run..' agent='..cfg.agent_sha256..' '..math_text
  assert(#receipt<=2048,'rotation terminal bounds')
  DebugError(receipt)
  success=success and math_complete
 end
 local line='[X4LAN] PORTABLE_NATIVE TERMINAL result='..(success and 'success' or 'failure')..
  ' slot=1 run='..cfg.run..' contract='..cfg.contract_sha256..' snapshot='..cfg.snapshot_sha256..
  ' agent='..cfg.agent_sha256..' '..text
 assert(#line<=1024,'portable native terminal bounds')
 DebugError(line);terminal_logged=true
end
X4MP_PORTABLE_BACKEND={
 ready=ready,
 before_load=function(snapshot)
  return snapshot==cfg.snapshot_sha256 and ready() and native.load_requested()==1
 end,
 cancel=cancel,
 status=function() return native.portable_status() end,
 observe=observe,
}
