-- Completion of the one-native transport only. Never grants an input or parity.
local cfg=X4LAN_CONFIG
if not cfg or cfg.native_gate_player_probe~='diagnostic-getup' then return end
assert(cfg.slot=='p1' and cfg.native_gate_diagnostic_contract=='diagnostic-getup-1','diagnostic identity')
local closed=false
local fields={};for k in ('schema contract run epoch native_clients physical_slot options tick issued calls returns unwinds failures terminal_started terminal_finished closed restored reason stage session_pass parity_checked independent_native_players completed_release'):gmatch('%S+') do fields[k]=true end
local function read(gate)
 local raw=assert(gate.player_getup_diagnostic_status(),'diagnostic native status missing')
 assert(type(raw)=='string' and #raw<640,'diagnostic status length')
 local out,parts={},{}
 for part in raw:gmatch('%S+') do
  local k,v=part:match('^([a-z_]+)=([a-z0-9_-]+)$');assert(k and fields[k] and out[k]==nil,'diagnostic status field')
  if k=='contract' or k=='stage' then out[k]=v
  elseif k=='run' then assert(#v==64 and v:match('^[0-9a-f]+$'),'diagnostic run');out[k]=v
  else assert(v:match('^%d+$') and (#v==1 or v:sub(1,1)~='0') and #v<=(k=='options' and 16 or 10),'diagnostic integer');out[k]=tonumber(v);assert(out[k]<=(k=='options' and 9007199254740991 or 4294967295),'diagnostic width') end
  parts[#parts+1]=part
 end
 assert(table.concat(parts,' ')==raw,'diagnostic whitespace')
 for k in pairs(fields) do assert(out[k]~=nil,'diagnostic field missing') end
 assert(out.schema==1 and out.contract==cfg.native_gate_diagnostic_contract and out.run==X4LAN_SHA256.digest(cfg.token) and out.epoch==1 and out.native_clients==1 and out.physical_slot==1 and
  out.options==70377334178981 and out.tick<=120 and out.issued<=120 and out.calls<=121 and out.returns<=out.calls and out.unwinds<=out.calls and
  out.terminal_started<=1 and out.terminal_finished<=1 and out.closed<=1 and out.restored<=1 and
  out.session_pass==0 and out.parity_checked==0 and out.independent_native_players==0 and out.completed_release==0,'diagnostic schema/claim')
 return out,raw
end
X4LAN_NATIVE_DIAGNOSTIC={drain=function(gate,failed)
 if closed then return true end
 local ok,s,raw=pcall(read,gate)
 if not ok then if failed then closed=true;DebugError('[X4LAN] NATIVE_GATE GETUP_DIAGNOSTIC_DRAIN_ERROR '..tostring(s));return true end;error(s) end
 if s.terminal_finished==0 then return false end
 closed=true;DebugError('[X4LAN] NATIVE_GATE GETUP_DIAGNOSTIC_STOP '..raw)
 -- Raw failures remain visible. The independent validator checks every receipt;
 -- this UI adapter does not turn a clean transport closure into action success.
 return true
end}
