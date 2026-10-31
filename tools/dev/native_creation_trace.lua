-- Local read-only constructor evidence. No commands, native getters or hashes.
local M={}
function M.new(gate,config,log)
 local schema=config.native_gate_creation_trace_schema
 assert((schema==1 or schema==2) and config.native_gate_construction_test and config.native_gate_registry_md_trace,'creation trace prerequisites')
 local parts=schema==2 and 18 or 17
 local manifest='CREATION_TRACE_'..schema..';handle=0000000500000003;macro=ship_arg_xl_builder_01_a_macro;owner=player;tick=5'
 assert(gate.poll().creation_trace_schema==schema and type(gate.creation_trace_config)=='function' and type(gate.creation_trace_status)=='function' and type(gate.creation_trace_row)=='function','creation trace capability')
 assert(gate.creation_trace_config(manifest)==1,'creation trace configuration refused')
 local next_index,next_part,record_complete,finished=1,0,nil,false
 return function(final)
  if finished then return end
  local status=assert(gate.creation_trace_status(),'creation trace status missing')
  assert(type(status)=='string' and #status<=1535,'creation trace status size')
  assert(status:sub(1,9)=='schema='..schema..' ','creation trace status schema')
  local count=status:match(' count=(%d+) parts='..parts..' ')
  count=tonumber(count);assert(count and count<=128 and count>=next_index-1,'creation trace count invalid or regressed')
  while next_index<=count do
   local first
   if next_part==0 then
    first=gate.creation_trace_row(next_index,0)
    if not first and not final then break end
    local prefix='schema='..schema..' index='..next_index..' part=0 complete='
    assert(type(first)=='string' and first:sub(1,#prefix)==prefix,'creation trace first row missing')
    record_complete=first:sub(#prefix+1,#prefix+1)
    assert(record_complete=='1' or (final and record_complete=='0'),'creation trace open record before stop')
   end
   for part=next_part,parts-1 do
    local row=part==0 and first or gate.creation_trace_row(next_index,part)
    local expected='schema='..schema..' index='..next_index..' part='..part..' complete='..record_complete..' '
    assert(type(row)=='string' and #row<=1535 and row:sub(1,#expected)==expected,'creation trace changed, missing or oversized part')
    log('CREATION_TRACE '..row)
    next_part=part+1
   end
   next_index=next_index+1;next_part=0;record_complete=nil
  end
  if final then log('CREATION_TRACE_STOP '..status);finished=true end
 end
end
X4LAN_NATIVE_CREATION_TRACE=M
return M
