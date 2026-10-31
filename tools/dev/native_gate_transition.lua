-- Read-only transition capture. This adapter never admits a control action.
local cfg=X4LAN_CONFIG
local queue_evidence=cfg and cfg.native_gate_player_transition_queue_evidence
local prebind=cfg and cfg.native_gate_player_transition_prebind_receipt
if prebind~=nil then assert(prebind==true and queue_evidence==true,'prebind receipt requires copied local queue evidence') end
if queue_evidence~=nil then
 assert(queue_evidence==true and cfg.native_gate_player_probe=='diagnostic-getup' and
  cfg.native_gate_diagnostic_contract=='diagnostic-getup-1' and cfg.native_gate_player_transition_observer==true and
  cfg.native_gate_player_transition_manifest=='2|00000000000ae79c/p/ship_arg_s_scout_01_a_macro/player' and
  cfg.native_gate_player_transition_last_tick==120,'queue evidence requires exact local GetUp watch')
end
if not cfg or not cfg.native_gate_player_transition_observer then return end
assert(cfg.native_gate_player_probe=='observe' or cfg.native_gate_player_probe=='agreed-getup' or cfg.native_gate_player_probe=='diagnostic-getup','transition observer requires explicit player probe')
assert(cfg.native_gate_world_trace_early and cfg.native_gate_world_queue_trace,'transition observer requires early queue trace')
local emitted,watch_rows,ticket_rows,configured,closed=0,0,0,false,false
local exception_mode
local schema=cfg.native_gate_player_transition_manifest and 2 or 1
local keys={schema=true,capacity=true,count=true,complete=true,rows=true,failures=true,excluded=true,dropped=true,
 open=true,peak=true,restore_open=true,sites=true,enabled=true,atomic_lease=true,generation_exclusion=true,
 faction_exclusion=true,early_import_coverage=true,raw_pointers=true}
local exception_keys={}
for key in ('exception_schema direct_calls direct_returns direct_unwinds queue_exception_cleanup'):gmatch('%S+') do
 keys[key]=true;exception_keys[key]=true
end
if schema==2 then
 for key in ('watches bound watch_failures import_calls prebind last queue_calls queue_returns queue_other queue_unknown queue_failures tickets outstanding raw_world_coverage watch_rows ticket_rows'):gmatch('%S+') do keys[key]=true end
end
local function log(text) DebugError('[X4LAN] NATIVE_GATE '..text) end
local prebind_rows,prebind_closed=0,false
local prebind_keys={};for k in ('schema capacity count rows source returns pending finalized poison open restore_open closing run image atomic_lease allocation_generation_known queue_exception_cleanup action_tick action_clock action_thread action_ticket action_dispatch'):gmatch('%S+') do prebind_keys[k]=true end
local function prebind_status(gate)
 local raw=assert(gate.player_transition_prebind_status(),'prebind status missing')
 assert(type(raw)=='string' and #raw<768,'prebind status length')
 local out,parts={},{}
 for part in raw:gmatch('%S+') do
  local k,v=part:match('^([a-z_]+)=([0-9a-f]+)$');assert(k and prebind_keys[k] and out[k]==nil,'prebind field')
  if k=='run' then assert(#v==64,'prebind run');out[k]=v
  elseif k=='image' then assert(#v<=16 and (#v==1 or v:sub(1,1)~='0'),'prebind image');out[k]=v
  else assert(v:match('^%d+$') and (#v==1 or v:sub(1,1)~='0') and #v<=16,'prebind integer');out[k]=tonumber(v);assert(out[k]<=9007199254740991,'prebind width') end
  parts[#parts+1]=part
 end
 assert(table.concat(parts,' ')==raw,'prebind whitespace');for k in pairs(prebind_keys) do assert(out[k]~=nil,'prebind missing field') end
 assert(out.schema==1 and out.capacity==3 and out.count<=3 and out.rows<=out.count and out.source<=1 and out.pending<=1 and
  out.finalized<=1 and out.closing<=1 and out.run==X4LAN_SHA256.digest(cfg.token) and out.atomic_lease==0 and
  out.allocation_generation_known==0 and out.queue_exception_cleanup==0,'prebind schema')
 assert((out.action_tick==0 and out.action_clock==0 and out.action_thread==0 and out.action_ticket==0 and out.action_dispatch==0) or
  (out.action_tick==30 and out.action_clock>0 and out.action_thread>0 and out.action_thread<4294967296 and
   out.action_ticket==1 and out.action_dispatch>0 and out.action_dispatch<=512),'prebind action prerequisite shape')
 return out,raw
end
local function prebind_drain(gate,stopped)
 if prebind_closed then return end
 local s=prebind_status(gate);assert(s.rows==prebind_rows,'prebind cursor changed')
 local ok,why=pcall(function()
  while prebind_rows<s.count do
   local row=assert(gate.player_transition_prebind_row(prebind_rows+1),'prebind row missing')
   assert(type(row)=='string' and #row<1536 and row:match('^PLAYER_TRANSITION_PREBIND schema=1 ordinal='..(prebind_rows+1)..' '),'prebind row shape')
   log(row);prebind_rows=prebind_rows+1
  end
 end)
 local raw;s,raw=prebind_status(gate)
 if stopped then prebind_closed=true;log('PLAYER_TRANSITION_PREBIND_STOP '..raw) end
 assert(ok,tostring(why));assert(s.rows==prebind_rows and s.poison==0,'prebind receipt failed')
 if stopped then assert(s.count==3 and s.rows==3 and s.source==1 and s.returns==1 and s.pending==0 and
  s.finalized==1 and s.open==0 and s.restore_open==0 and s.closing==1,'prebind incomplete') end
end
local queue_rows,queue_closed=0,false
local queue_keys={};for k in ('schema capacity count rows calls unknown unreadable ambiguous dropped open restore_open closing run image allocation_generation_known ticket_authority atomic_lease'):gmatch('%S+') do queue_keys[k]=true end
local function queue_status(gate)
 local raw=assert(gate.player_transition_queue_evidence_status(),'queue evidence status missing')
 assert(type(raw)=='string' and #raw<768,'queue evidence status length')
 local out,parts={},{}
 for part in raw:gmatch('%S+') do
  local k,v=part:match('^([a-z_]+)=([0-9a-f]+)$');assert(k and queue_keys[k] and out[k]==nil,'queue evidence field')
  if k=='run' then assert(#v==64,'queue evidence run');out[k]=v
  elseif k=='image' then assert(#v<=16 and (#v==1 or v:sub(1,1)~='0'),'queue evidence image');out[k]=v
  else assert(v:match('^%d+$') and (#v==1 or v:sub(1,1)~='0') and #v<=16,'queue evidence integer');out[k]=tonumber(v);assert(out[k]<=9007199254740991,'queue evidence width') end
  parts[#parts+1]=part
 end
 assert(table.concat(parts,' ')==raw,'queue evidence whitespace')
 for k in pairs(queue_keys) do assert(out[k]~=nil,'queue evidence missing field') end
 assert(out.schema==1 and out.capacity==256 and out.count<=256 and out.rows<=out.count and
  out.calls==out.count+out.unknown+out.dropped and out.unreadable<=out.calls and out.ambiguous<=out.count+out.dropped and
  out.closing<=1 and out.run==X4LAN_SHA256.digest(cfg.token) and out.allocation_generation_known==0 and
  out.ticket_authority==0 and out.atomic_lease==0,'queue evidence schema/coverage')
 return out,raw
end
local function queue_drain(gate,stopped)
 if queue_closed then return end
 local s=queue_status(gate)
 assert(s.rows==queue_rows,'queue evidence cursor changed')
 local ok,why=pcall(function()
  while queue_rows<s.count do
   local row=assert(gate.player_transition_queue_evidence_row(queue_rows+1),'queue evidence row missing')
   assert(type(row)=='string' and #row<1024 and row:match('^PLAYER_TRANSITION_QUEUE_DIAG schema=1 ordinal='..(queue_rows+1)..' '),'queue evidence row shape')
   log(row);queue_rows=queue_rows+1
  end
 end)
 local raw;s,raw=queue_status(gate)
 if stopped then queue_closed=true;log('PLAYER_TRANSITION_QUEUE_DIAG_STOP '..raw) end
 assert(ok,tostring(why));assert(s.rows==queue_rows and s.dropped==0,'queue evidence capture incomplete')
 if stopped then assert(s.count==queue_rows and s.open==0 and s.restore_open==0 and s.closing==1,'queue evidence stop incomplete') end
end
local function status(gate)
 local text=assert(gate.player_transition_status(),'transition status unavailable')
 assert(type(text)=='string' and #text>0 and #text<1536,'invalid transition status length')
 local values,n,parts={},0,{}
 for part in text:gmatch('%S+') do
  local key,value=part:match('^([%w_]+)=(%d+)$')
  assert(key and (#value==1 or value:sub(1,1)~='0') and #value<=16,'invalid transition status encoding')
  assert(keys[key] and not values[key],'invalid transition status field')
  values[key]=tonumber(value);assert(values[key]<=9007199254740991,'invalid transition status number')
  n=n+1;parts[n]=part
 end
 assert(table.concat(parts,' ')==text,'invalid transition status whitespace')
 local exception=values.exception_schema~=nil
 for key in pairs(keys) do
  assert((values[key]~=nil)==(not exception_keys[key] or exception),'invalid transition status field set')
 end
 assert(n==(schema==2 and 34 or 18)+(exception and 5 or 0) and values.schema==schema and values.capacity==512 and values.count<=512 and
  values.complete<=values.count and values.rows<=values.complete and values.atomic_lease==0 and
  values.generation_exclusion==0 and values.faction_exclusion==0 and values.early_import_coverage==0 and
  values.raw_pointers==1,'invalid transition status/coverage')
 if exception then
  assert(values.exception_schema==1 and values.queue_exception_cleanup==0 and
   values.direct_returns+values.direct_unwinds<=values.direct_calls,'invalid direct transition exception coverage')
 end
 local version=values.exception_schema or 0
 assert(exception_mode==nil or exception_mode==version,'transition exception schema changed')
 exception_mode=version
 if schema==2 then
  assert(values.watches>=1 and values.watches<=2 and values.bound<=values.watches and values.watch_rows<=values.bound and
   values.tickets<=256 and values.ticket_rows<=values.tickets and values.outstanding<=values.tickets and
   values.last==cfg.native_gate_player_transition_last_tick and values.raw_world_coverage==0,'invalid separate transition watch coverage')
 end
 return values,text
end
X4LAN_NATIVE_TRANSITION={
 configure=function(gate)
  assert(not configured and not closed,'transition adapter already configured')
  if schema==2 then assert(gate.player_transition_watch_config(cfg.native_gate_player_transition_manifest,cfg.native_gate_player_transition_last_tick)==1,'separate transition watch refused') end
  if prebind then assert(gate.player_transition_prebind_config(X4LAN_SHA256.digest(cfg.token))==1,'prebind configuration refused') end
  if queue_evidence then assert(gate.player_transition_queue_evidence_config(X4LAN_SHA256.digest(cfg.token))==1,'queue evidence configuration refused') end
  assert(gate.player_transition_config()==1,'transition configuration refused')
  local s=status(gate)
  assert(s.enabled==1 and s.sites==7,'transition sites not installed')
  -- Native export is consuming. A UI reload resumes its acknowledged cursor;
  -- the independent validator still requires every earlier log row.
  emitted=s.rows;configured=true
  watch_rows=s.watch_rows or 0;ticket_rows=s.ticket_rows or 0
  if prebind then
   local p=prebind_status(gate);assert(p.closing==0 and p.poison==0,'prebind already closed/failed');prebind_rows=p.rows
   log('PLAYER_TRANSITION_PREBIND_ENABLED schema=1 resumed='..prebind_rows..' run='..p.run..' atomic_lease=0')
  end
  if queue_evidence then
   local q=queue_status(gate);assert(q.closing==0,'queue evidence already closed');queue_rows=q.rows
   log('PLAYER_TRANSITION_QUEUE_DIAG_ENABLED schema=1 resumed='..queue_rows..' run='..q.run..' allocation_generation_known=0 ticket_authority=0')
  end
  log('PLAYER_TRANSITION_ENABLED schema='..schema..' resumed='..emitted..' actions=0 singleton=1 early_import_coverage=0'..
   (schema==2 and (' watches='..watch_rows..' tickets='..ticket_rows..' raw_world_coverage=0') or ''))
 end,
 drain=function(gate,stopped)
  if closed then return end
  assert(configured,'transition adapter not configured')
  local before=status(gate)
  if schema==2 then
   while watch_rows<before.bound do
    local row=assert(gate.player_transition_watch(watch_rows+1),'transition initial watch missing')
    assert(#row<1024 and row:match('^PLAYER_TRANSITION_WATCH schema=2 index='..(watch_rows+1)..' '),'invalid transition watch row')
    log(row);watch_rows=watch_rows+1
   end
  end
  local ready=assert(gate.player_transition_row(0),'transition ready count unavailable')
  assert(type(ready)=='number' and ready==math.floor(ready) and ready>=emitted and ready<=512,'invalid transition ready count')
  while emitted<ready do
   local row=assert(gate.player_transition_row(emitted+1),'transition row unavailable')
   assert(type(row)=='string' and #row<2048 and row:match('^PLAYER_TRANSITION event='..(emitted+1)..' '),'invalid transition row')
   log(row);emitted=emitted+1
  end
  local queue_ok,queue_error=true,nil
  if queue_evidence then queue_ok,queue_error=pcall(queue_drain,gate,stopped) end
  local prebind_ok,prebind_error=true,nil
  if prebind then prebind_ok,prebind_error=pcall(prebind_drain,gate,stopped) end
  local s,text=status(gate)
  local tickets_ok=true
  if stopped and schema==2 then
   tickets_ok=pcall(function()
    while ticket_rows<s.tickets do
     local row=assert(gate.player_transition_ticket(ticket_rows+1),'transition ticket missing/open')
     assert(#row<768 and row:match('^PLAYER_TRANSITION_TICKET schema=2 ticket='..(ticket_rows+1)..' '),'invalid transition ticket row')
     log(row);ticket_rows=ticket_rows+1
    end
   end)
   s,text=status(gate)
  end
  if stopped then closed=true;log('PLAYER_TRANSITION_STOP '..text) end
  assert(queue_ok,tostring(queue_error))
  assert(prebind_ok,tostring(prebind_error))
  assert(s.rows==emitted and s.failures==0 and s.dropped==0,'transition observation failed')
  if s.exception_schema then assert(s.direct_unwinds==0,'direct transition unwound') end
  if schema==2 then assert(s.watch_rows==watch_rows and s.watch_failures==0 and s.queue_failures==0,'separate transition watch/queue failed') end
  if stopped then
   assert(s.count==emitted and s.complete==emitted and s.open==0 and s.restore_open==0 and
    s.sites==0 and s.enabled==0,'transition restoration/capture incomplete')
   if s.exception_schema then assert(s.direct_calls==s.direct_returns,'direct transition return coverage incomplete') end
   if schema==2 then assert(tickets_ok and s.watches==s.bound and s.bound==watch_rows and
    s.ticket_rows==ticket_rows and s.tickets==ticket_rows and s.outstanding==0 and s.queue_calls==s.queue_returns,'transition ticket/binding coverage incomplete') end
  end
 end,
}
