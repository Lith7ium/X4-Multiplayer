-- Closed origin export for the explicit selected-ship experiment. No action here.
local cfg=X4LAN_CONFIG
if not cfg or not cfg.native_gate_player_origin_token then return end
assert((cfg.native_gate_player_probe=='agreed-getup' or cfg.native_gate_player_probe=='diagnostic-getup'),'origin adapter requires the exclusive GetUp fixture')
local configured,closed=false,false
local schema,getup_configured=2,false
local fixed={};for k in ('player_saved ship_saved player_runtime ship_runtime'):gmatch('%S+') do fixed[k]=true end
local pointer={};for k in ('player ship agent owner'):gmatch('%S+') do pointer[k]=true end
local keys={};for k in ('schema first last imports bound sites closing calls returns open peak restore_open prebind native_failures tickets edges post_calls post_returns delivery_calls delivery_returns core_open core_peak core_stop_open failures other unknown excluded ticket_rows edge_rows player_saved ship_saved player_runtime ship_runtime player ship agent owner raw_world_coverage action_authority atomic_lease early_coverage raw_pointers due_is_current capture_complete native_unwinds post_unwinds delivery_unwinds'):gmatch('%S+') do keys[k]=true end
local function log(s) DebugError('[X4LAN] NATIVE_GATE '..s) end
local function status(gate)
 local raw=assert(gate.player_origin_status(),'origin status missing')
 assert(type(raw)=='string' and #raw<1536,'origin status length')
 local out,parts={},{}
 for part in raw:gmatch('%S+') do
  local k,v=part:match('^([a-z_]+)=([0-9a-f]+)$');assert(k and keys[k] and not out[k],'origin status field')
  if fixed[k] then assert(#v==16,'origin fixed identity width');out[k]=v
  elseif pointer[k] then assert(#v<=16 and (#v==1 or v:sub(1,1)~='0'),'origin pointer width');out[k]=v
  else assert(v:match('^%d+$') and (#v==1 or v:sub(1,1)~='0') and #v<=16,'origin number encoding');out[k]=tonumber(v);assert(out[k]<=9007199254740991,'origin number width') end
  parts[#parts+1]=part
 end
 assert(table.concat(parts,' ')==raw,'origin whitespace')
 for k in pairs(keys) do assert(out[k]~=nil,'origin field missing') end
 assert(out.schema==schema and out.first==1 and out.last==120 and out.tickets<=32 and out.edges<=128 and
  out.ticket_rows<=out.tickets and out.edge_rows<=out.edges and out.raw_world_coverage==0 and
  out.action_authority==0 and out.atomic_lease==0 and out.early_coverage==0 and out.raw_pointers==1 and out.due_is_current==0,'origin schema/coverage')
 return out,raw
end
X4LAN_NATIVE_ORIGIN={
 configure=function(gate)
  assert(not configured and not closed,'origin already configured')
  assert(gate.player_origin_config(cfg.native_gate_player_origin_token)==1,'origin configuration refused')
  configured=true -- Preserve raw observer STOP if the following action config fails.
  local run=X4LAN_SHA256.digest(cfg.token)
  local contract=cfg.native_gate_player_probe=='diagnostic-getup' and 'diagnostic-getup-1' or nil
  assert(gate.player_getup_config(run,contract)==1,'GetUp configuration refused')
  schema=3;getup_configured=true
  X4LAN_NATIVE_GETUP_GATE=gate;X4LAN_NATIVE_GETUP_RUN=run
  local s=status(gate);assert(s.sites==3 and s.closing==0,'origin sites unavailable');configured=true
 end,
 drain=function(gate,stopped)
  if closed then return end
  assert(configured,'origin adapter not configured')
  local s,raw=status(gate)
  if stopped then
   local ok,error=pcall(function()
    for i=s.ticket_rows+1,s.tickets do
     local row=assert(gate.player_origin_ticket(i),'origin ticket unavailable')
     assert(#row<1024 and row:match('^PLAYER_ORIGIN_TICKET schema='..schema..' ticket='..i..' '),'origin ticket shape');log(row)
    end
    for i=s.edge_rows+1,s.edges do
     local row=assert(gate.player_origin_edge(i),'origin edge unavailable')
     assert(#row<192 and row:match('^PLAYER_ORIGIN_EDGE schema='..schema..' edge='..i..' '),'origin edge shape');log(row)
    end
   end)
   s,raw=status(gate);closed=true;log('PLAYER_ORIGIN_STOP '..raw)
   if getup_configured then log('PLAYER_GETUP_STOP '..assert(gate.player_getup_status(),'GetUp status missing')) end
   assert(ok,tostring(error))
   assert(s.sites==0 and s.closing==1 and s.open==0 and s.restore_open==0 and s.core_open==0 and s.core_stop_open==0 and
    s.calls==s.returns and s.post_calls==s.post_returns and s.delivery_calls==s.delivery_returns and
    s.ticket_rows==s.tickets and s.edge_rows==s.edges,'origin restoration/return coverage incomplete')
  end
  assert(s.native_failures==0 and s.failures==0 and s.native_unwinds==0 and s.post_unwinds==0 and s.delivery_unwinds==0,'origin capture failed')
 end,
}
