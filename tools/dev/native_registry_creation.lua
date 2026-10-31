-- Observer-owned evidence only. No native enrollment or player authority.
local M={}
M.manifest='REGISTRY_CREATION_1;handle=0000000500000003;policy=observe'
function M.new(gate,config,data,log)
 assert(config.native_gate_registry_creation_schema==1 and config.native_gate_creation_trace_schema==2 and
  config.native_gate_creation_rng_schema==1 and (config.native_gate_creation_rng_ack_schema==1 or config.native_gate_creation_rng_ack_schema==2) and
  config.native_gate_construction_test,'provisional registry prerequisites')
 assert(type(data)=='string' and data:sub(-#M.manifest-1)=='|'..M.manifest,'provisional registry agreement suffix')
 assert(gate.poll().registry_creation_schema==1 and type(gate.registry_creation_config)=='function' and
  type(gate.registry_creation_status)=='function' and type(gate.registry_creation_identity)=='function' and
  type(gate.registry_creation_operation)=='function','provisional registry native capability')
 local configured,header,finished=false,false,false
 local next_identity,next_part,next_operation=1,0,1
 local function write_header()
  if configured and not header then log('REGISTRY_CREATION_CONFIG '..M.manifest);header=true end
 end
 local function drain(final)
  if finished then return end
  write_header()
  local status=assert(gate.registry_creation_status(),'provisional registry status missing')
  assert(type(status)=='string' and #status<=1023 and status:sub(1,9)=='schema=1 ','provisional registry status grammar')
  local identities,operations=status:match(' identities=(%d+) operations=(%d+) ')
  identities,operations=tonumber(identities),tonumber(operations)
  assert(identities and identities>=next_identity-1 and identities<=4 and operations and operations>=next_operation-1 and operations<=256,'provisional registry counts')
  while next_identity<=identities do
   local start=next_part
   local row=gate.registry_creation_identity(next_identity,next_part)
   if not row and not final then break end
   for part=next_part,2 do
    row=part==start and row or gate.registry_creation_identity(next_identity,part)
    local prefix='schema=1 index='..next_identity..' part='..part..' '
    assert(type(row)=='string' and #row<=1535 and row:sub(1,#prefix)==prefix,'provisional identity part')
    log('REGISTRY_CREATION_IDENTITY '..row);next_part=part+1
   end
   next_identity=next_identity+1;next_part=0
  end
  while next_operation<=operations do
   local row=gate.registry_creation_operation(next_operation)
   if not row and not final then break end
   local prefix='schema=1 index='..next_operation..' '
   assert(type(row)=='string' and #row<=1535 and row:sub(1,#prefix)==prefix,'provisional operation row')
   log('REGISTRY_CREATION_OPERATION '..row);next_operation=next_operation+1
  end
  if final then log('REGISTRY_CREATION_STOP '..status);finished=true end
 end
 return drain,function()
  assert(not configured,'duplicate provisional registry activation')
  assert(gate.registry_creation_config(M.manifest,config.native_gate_creation_rng_manifest,data)==1,'provisional registry configuration refused')
  configured=true;write_header()
 end
end
X4LAN_NATIVE_REGISTRY_CREATION=M
return M
