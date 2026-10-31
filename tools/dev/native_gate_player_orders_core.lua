-- Lockstep player orders: versioned binary command codec, strict validation and deterministic
-- scheduling. Pure Lua, no engine calls (LuaJIT-tested). Engine glue: native_gate_player_orders.lua.
-- Format and rules: reports/LOCKSTEP-PLAYER-ORDERS-20261029.txt; Python reference decoder:
-- native_gate_player_orders.py. Only portable saved IDs and exact float32 bits travel on the
-- wire; runtime IDs, pointers and display codes are process-local and never encoded.
X4LAN_PLAYER_ORDERS_CORE={}
local K=X4LAN_PLAYER_ORDERS_CORE
K.CHANNEL,K.VERSION=0x50,1   -- 'P', format v1
K.HEADER_HEX=20              -- channel, version, slot, apply tick u32, seq u16, count
K.MAX_HEX=240                -- per-slot proposal budget on the variable-length wire
K.MAX_SLOTS,K.MAX_COMMANDS,K.MAX_DELAY,K.MAX_PENDING,K.MAX_QUEUE=16,15,64,256,64
K.MAX_TICK,K.MAX_SEQ=4294967295,65535
K.COORD_LIMIT=1e7            -- sector-local metres
K.MAX_AMOUNT,K.MAX_PRICE,K.MAX_GROUP=1000000000,4294967295,10
K.ZERO=string.rep('0',16)
K.CLEAR,K.IMMEDIATE,K.HOLDFIRE=1,2,4
-- Opcodes are wire values: append only, never renumber.
K.OPS={
 {name='move',kind='position',flags=3},   -- MoveWait to [sector, x/y/z]
 {name='attack',kind='object',flags=3},   -- Attack primarytarget
 {name='follow',kind='object',flags=3},   -- Follow target
 {name='protect',kind='object',flags=3},  -- ProtectShip target (escort)
 {name='dock',kind='object',flags=3},     -- DockAndWait destination
 {name='hold',kind='none',flags=7},       -- Wait (optional hold fire)
 {name='buy',kind='trade',flags=3},       -- SingleBuy ware at one station
 {name='sell',kind='trade',flags=3},      -- SingleSell ware at one station
 {name='assign',kind='assign',flags=0},   -- AssignCommander (none: leave the fleet)
}
K.OP={};for i,o in ipairs(K.OPS) do K.OP[o.name]=i end
K.ASSIGNMENTS={'defence','attack','interception','bombardment','mining','trade','supplyfleet','positiondefence','assist','salvage'}
K.ASSIGNMENT={};for i,a in ipairs(K.ASSIGNMENTS) do K.ASSIGNMENT[a]=i end

-- Strict refusals travel as a private error value; anything else is a programming error.
local Refusal={}
local function refuse(reason) error(setmetatable({reason=reason},Refusal),0) end
local function guarded(fn,...)
 local ok,value=pcall(fn,...)
 if ok then return value end
 if getmetatable(value)==Refusal then return nil,value.reason end
 error(value,0)
end
K.guarded=guarded
local function integer(v,lo,hi) return type(v)=='number' and v==math.floor(v) and v>=lo and v<=hi end
K.integer=integer
local function has(flags,bit) return flags%(2*bit)>=bit end
K.has=has
local function hex8(n) return string.format('%02x',n) end
local function hex16(n) return string.format('%02x%02x',math.floor(n/256),n%256) end
local function hex32(n) return string.format('%04x%04x',math.floor(n/65536),n%65536) end
K.hex32=hex32
function K.saved(id) return type(id)=='string' and #id==16 and not id:find('[^0-9a-f]') and id~=K.ZERO end

-- IEEE-754 binary32 with round-to-nearest-even, computed exactly in doubles. -0 encodes as +0.
local function round_even(x)
 local f=math.floor(x);local d=x-f
 if d>0.5 or (d==0.5 and f%2==1) then f=f+1 end
 return f
end
function K.f32_bits(v)
 if type(v)~='number' or v~=v or v==math.huge or v==-math.huge then refuse('coordinate_nonfinite') end
 if v==0 then return 0 end
 local sign=0;if v<0 then sign=2147483648;v=-v end
 local m,e=math.frexp(v)
 local biased,mant=e+126
 if biased>=1 then
  mant=round_even((m*2-1)*8388608)
  if mant==8388608 then mant=0;biased=biased+1 end
  if biased>=255 then refuse('coordinate_range') end
 else
  mant=round_even(math.ldexp(v,149));biased=0
  if mant==8388608 then mant=0;biased=1 end
  if mant==0 and biased==0 then return 0 end
 end
 return sign+biased*8388608+mant
end
function K.f32_value(bits)
 if not integer(bits,0,4294967295) then refuse('coordinate_bits') end
 local sign=1;if bits>=2147483648 then sign=-1;bits=bits-2147483648 end
 local biased,mant=math.floor(bits/8388608),bits%8388608
 if biased==255 then refuse('coordinate_nonfinite') end
 if biased==0 then
  if mant==0 then if sign<0 then refuse('coordinate_negative_zero') end;return 0 end
  return sign*math.ldexp(mant,-149)
 end
 return sign*math.ldexp(mant+8388608,biased-150)
end
local function coordinate_bits(c,axis)
 local bits=c[axis..'bits']
 if bits==nil then bits=K.f32_bits(c[axis]) end
 local value=K.f32_value(bits)
 if math.abs(value)>K.COORD_LIMIT then refuse('coordinate_range') end
 return bits,value
end

-- References: tag byte = kind<<4 | length; kind 1 = saved component ID, minimal big-endian bytes.
-- Tag 0 = none (only where an op allows it). Kinds 2..15 are reserved for later identity spaces.
local function ref_hex(id,allow_none)
 if id==nil then if allow_none then return '00' end;refuse('reference_missing') end
 if not K.saved(id) then refuse('reference_invalid') end
 local h=id;while h:sub(1,2)=='00' do h=h:sub(3) end
 return hex8(16+#h/2)..h
end
local function ware_hex(ware)
 if type(ware)~='string' or #ware<1 or #ware>40 or not ware:match('^[a-z][a-z0-9_]*$') then refuse('ware_invalid') end
 return hex8(#ware)..ware:gsub('.',function(ch) return hex8(ch:byte()) end)
end

local function command_hex(c)
 if type(c)~='table' then refuse('command_invalid') end
 local code=K.OP[c.op];if not code then refuse('op_unknown') end
 local spec=K.OPS[code];local flags=c.flags or 0
 if not integer(flags,0,7) then refuse('flags_invalid') end
 for _,bit in ipairs({1,2,4}) do if has(flags,bit) and not has(spec.flags,bit) then refuse('flags_invalid') end end
 local parts={hex8(code),hex8(flags),ref_hex(c.subject)}
 if spec.kind=='position' then
  parts[#parts+1]=ref_hex(c.sector)
  for _,axis in ipairs({'x','y','z'}) do parts[#parts+1]=hex32((coordinate_bits(c,axis))) end
 elseif spec.kind=='object' then
  parts[#parts+1]=ref_hex(c.target)
 elseif spec.kind=='trade' then
  parts[#parts+1]=ref_hex(c.target);parts[#parts+1]=ware_hex(c.ware)
  if not integer(c.amount,spec.name=='sell' and 0 or 1,K.MAX_AMOUNT) then refuse('amount_invalid') end
  if not integer(c.price,0,K.MAX_PRICE) then refuse('price_invalid') end
  parts[#parts+1]=hex32(c.amount);parts[#parts+1]=hex32(c.price)
 elseif spec.kind=='assign' then
  parts[#parts+1]=ref_hex(c.target,true)
  local assignment,group=0,c.group or 0
  if c.target then
   assignment=K.ASSIGNMENT[c.assignment]
   if not assignment then refuse('assignment_invalid') end
  elseif c.assignment~=nil or group~=0 then refuse('assignment_invalid') end
  if not integer(group,0,K.MAX_GROUP) then refuse('group_invalid') end
  parts[#parts+1]=hex8(assignment);parts[#parts+1]=hex8(group)
 end
 return table.concat(parts)
end
function K.command_hex(c) return guarded(command_hex,c) end

local function header(slot,tick,seq,count)
 if not integer(slot,1,K.MAX_SLOTS) then refuse('slot_invalid') end
 if not integer(tick,1,K.MAX_TICK) then refuse('tick_invalid') end
 if not integer(seq,1,K.MAX_SEQ) then refuse('seq_invalid') end
 if not integer(count,1,K.MAX_COMMANDS) then refuse('count_invalid') end
 return hex8(K.CHANNEL)..hex8(K.VERSION)..hex8(slot)..hex32(tick)..hex16(seq)..hex8(count)
end
-- blob={slot=,tick=apply tick,seq=,commands={...}} -> lowercase hex
function K.encode(blob)
 return guarded(function()
  if type(blob)~='table' or type(blob.commands)~='table' then refuse('blob_invalid') end
  local parts={header(blob.slot,blob.tick,blob.seq,#blob.commands)}
  for i=1,#blob.commands do parts[#parts+1]=command_hex(blob.commands[i]) end
  local hex=table.concat(parts)
  if #hex>K.MAX_HEX then refuse('too_long') end
  return hex
 end)
end

local function reader(hex)
 local r={pos=1}
 function r.bytes(n)
  if r.pos+2*n-1>#hex then refuse('truncated') end
  local s=hex:sub(r.pos,r.pos+2*n-1);r.pos=r.pos+2*n;return s
 end
 function r.u8() return tonumber(r.bytes(1),16) end
 function r.u16() return tonumber(r.bytes(2),16) end
 function r.u32() local s=r.bytes(4);return tonumber(s:sub(1,4),16)*65536+tonumber(s:sub(5,8),16) end
 function r.ref(allow_none)
  local tag=r.u8()
  if tag==0 then if allow_none then return nil end;refuse('reference_missing') end
  local kind,length=math.floor(tag/16),tag%16
  if kind~=1 then refuse('reference_kind') end
  if length<1 or length>8 then refuse('reference_length') end
  local h=r.bytes(length)
  if h:sub(1,2)=='00' then refuse('reference_noncanonical') end
  return string.rep('0',16-#h)..h
 end
 function r.done() return r.pos>#hex end
 return r
end
local function read_command(r)
 local code=r.u8();local spec=K.OPS[code]
 if not spec then refuse('op_unknown') end
 local flags=r.u8()
 if flags>7 then refuse('flags_invalid') end
 for _,bit in ipairs({1,2,4}) do if has(flags,bit) and not has(spec.flags,bit) then refuse('flags_invalid') end end
 local c={op=spec.name,code=code,flags=flags,subject=r.ref()}
 if spec.kind=='position' then
  c.sector=r.ref()
  for _,axis in ipairs({'x','y','z'}) do
   local bits=r.u32();local value=K.f32_value(bits)
   if math.abs(value)>K.COORD_LIMIT then refuse('coordinate_range') end
   c[axis..'bits'],c[axis]=bits,value
  end
 elseif spec.kind=='object' then
  c.target=r.ref()
 elseif spec.kind=='trade' then
  c.target=r.ref()
  local length=r.u8()
  if length<1 or length>40 then refuse('ware_invalid') end
  c.ware=r.bytes(length):gsub('..',function(h) return string.char(tonumber(h,16)) end)
  if not c.ware:match('^[a-z][a-z0-9_]*$') then refuse('ware_invalid') end
  c.amount,c.price=r.u32(),r.u32()
  if c.amount>K.MAX_AMOUNT or (spec.name=='buy' and c.amount==0) then refuse('amount_invalid') end
 elseif spec.kind=='assign' then
  c.target=r.ref(true)
  local assignment,group=r.u8(),r.u8()
  if c.target then
   c.assignment=K.ASSIGNMENTS[assignment]
   if not c.assignment then refuse('assignment_invalid') end
  elseif assignment~=0 or group~=0 then refuse('assignment_invalid') end
  if group>K.MAX_GROUP then refuse('group_invalid') end
  c.group=group
 end
 return c
end
-- Decode one slot's blob. wire = the slot the relay delivered it for; a blob naming another slot is
-- refused. Returns blob or nil,reason. Decoding is total: every byte must be consumed canonically.
function K.decode(hex,wire)
 return guarded(function()
  if type(hex)~='string' or hex:find('[^0-9a-f]') or #hex%2~=0 then refuse('encoding') end
  if #hex>K.MAX_HEX then refuse('too_long') end
  local r=reader(hex)
  if r.u8()~=K.CHANNEL then refuse('channel') end
  if r.u8()~=K.VERSION then refuse('version') end
  local blob={slot=r.u8(),tick=r.u32(),seq=r.u16(),commands={}}
  local count=r.u8()
  if not integer(blob.slot,1,K.MAX_SLOTS) then refuse('slot_invalid') end
  if blob.slot~=wire then refuse('slot_mismatch') end
  if blob.tick<1 then refuse('tick_invalid') end
  if blob.seq<1 then refuse('seq_invalid') end
  if count<1 or count>K.MAX_COMMANDS then refuse('count_invalid') end
  for i=1,count do blob.commands[i]=read_command(r) end
  if not r.done() then refuse('trailing') end
  return blob
 end)
end

-- Labels used in PLAYER_ORDER_APPLIED lines: exact and machine-neutral (saved IDs, float bits).
function K.target_label(c)
 local kind=K.OPS[K.OP[c.op]].kind
 if kind=='position' then
  return string.format('%s@%s,%s,%s',c.sector,hex32(c.xbits),hex32(c.ybits),hex32(c.zbits))
 elseif kind=='object' then return c.target
 elseif kind=='trade' then return string.format('%s/%s/%d/%d',c.target,c.ware,c.amount,c.price)
 elseif kind=='assign' then return string.format('%s/%s/%d',c.target or 'none',c.assignment or 'none',c.group)
 end
 return '-'
end

-- Session constants must be identical on every client (agreed at session start); owners maps a
-- saved subject ID to the human slot that may command it.
function K.session(p)
 p=p or {}
 local S={owners={},ownership=p.ownership or 'table',players=p.players,last={},pending={},count=0,tick=0}
 assert(S.ownership=='table' or S.ownership=='faction','ownership must be table or faction')
 assert(S.players==nil or integer(S.players,1,K.MAX_SLOTS),'invalid player count')
 -- Faction ownership mode: slot k's faction (default x4lan_player<k>). The player identity plan
 -- (native_gate_player_identity.lua) may give one slot the vanilla 'player' faction.
 if p.factions~=nil then
  assert(type(p.factions)=='table' and #p.factions>=1 and #p.factions<=K.MAX_SLOTS,'invalid slot faction list')
  local seen={}
  for k,f in ipairs(p.factions) do
   assert((f=='player' or f=='x4lan_player'..k) and not seen[f],'invalid slot faction '..tostring(f))
   seen[f]=true
  end
  S.factions=p.factions
 end
 for saved,slot in pairs(p.owners or {}) do
  assert(K.saved(saved) and integer(slot,1,K.MAX_SLOTS),'invalid ownership row')
  S.owners[saved]=slot
 end
 local keys={};for saved in pairs(S.owners) do keys[#keys+1]=saved end
 table.sort(keys)
 local rows={};for _,saved in ipairs(keys) do rows[#rows+1]=saved..'='..S.owners[saved] end
 S.owner_rows=table.concat(rows,',')
 if S.factions then S.owner_rows=S.owner_rows..';factions='..table.concat(S.factions,',') end
 return S
end
local function blob_row(slot,seq,result)
 return {slot=slot,head=string.format('slot=%d op=- subject=- target=-',slot),result='refused_'..result,
  tail=string.format('seq=%s index=0',seq and tostring(seq) or '-')}
end
-- Admit the identical slot-ordered input list of one gated tick. Valid blobs schedule their
-- commands for blob.tick; blob-level refusals are returned in slot order.
function K.receive(S,tick,inputs)
 assert(integer(tick,1,K.MAX_TICK) and tick>S.tick,'player order tick must increase')
 assert(type(inputs)=='table','player order inputs missing')
 local n=#inputs
 assert(n>=1 and n<=K.MAX_SLOTS and (S.players==nil or n==S.players),'player order input count mismatch')
 S.players=n;S.tick=tick
 local rows={}
 for slot=1,n do
  local hex=inputs[slot]
  assert(type(hex)=='string','player order input is not a string')
  if hex~='' then
   local blob,why=K.decode(hex,slot)
   if not blob then rows[#rows+1]=blob_row(slot,nil,why)
   elseif blob.seq<=(S.last[slot] or 0) then rows[#rows+1]=blob_row(slot,blob.seq,'duplicate')
   else
    S.last[slot]=blob.seq
    if blob.tick<tick then rows[#rows+1]=blob_row(slot,blob.seq,'late')
    elseif blob.tick>tick+K.MAX_DELAY then rows[#rows+1]=blob_row(slot,blob.seq,'far')
    elseif S.count+#blob.commands>K.MAX_PENDING then rows[#rows+1]=blob_row(slot,blob.seq,'backlog')
    else
     local list=S.pending[blob.tick] or {};S.pending[blob.tick]=list
     for i,c in ipairs(blob.commands) do list[#list+1]={slot=slot,seq=blob.seq,index=i,due=blob.tick,received=tick,cmd=c} end
     S.count=S.count+#blob.commands
    end
   end
  end
 end
 return rows
end
-- Commands due at or before tick, in canonical (due, slot, seq, index) order.
function K.due(S,tick)
 local ticks={};for due in pairs(S.pending) do if due<=tick then ticks[#ticks+1]=due end end
 table.sort(ticks)
 local out={}
 for _,due in ipairs(ticks) do
  local list=S.pending[due];S.pending[due]=nil;S.count=S.count-#list
  table.sort(list,function(a,b)
   if a.slot~=b.slot then return a.slot<b.slot end
   if a.seq~=b.seq then return a.seq<b.seq end
   return a.index<b.index
  end)
  for _,e in ipairs(list) do out[#out+1]=e end
 end
 return out
end
-- Pure semantic checks plus saved-ID resolution. resolve(saved) -> runtime or nil,'unmapped'|'ambiguous'.
-- World-state checks (operational, class, faction owner) run in MD at application.
function K.faction(S,slot) return S.factions and S.factions[slot] or 'x4lan_player'..slot end
function K.check(S,e,resolve)
 local c=e.cmd;local refs={}
 local function need(role,saved)
  local runtime,why=resolve(saved)
  if not runtime then return why..'_'..role end
  refs[role]=runtime
 end
 local why=need('subject',c.subject);if why then return why end
 if S.ownership=='table' and S.owners[c.subject]~=e.slot then return 'not_owner' end
 if c.target and c.target==c.subject then return 'self_target' end
 if c.sector then why=need('sector',c.sector);if why then return why end end
 if c.target then why=need('target',c.target);if why then return why end end
 if S.ownership=='table' then
  if c.op=='attack' and S.owners[c.target]==e.slot then return 'own_target' end
  if c.op=='assign' and c.target and S.owners[c.target]~=e.slot then return 'commander_not_owned' end
 end
 return nil,refs
end
-- One gated tick: blob refusals, then due commands. Each row carries head/result/tail for the
-- PLAYER_ORDER_APPLIED line; result 'queued' rows go to MD, which logs their final result.
-- The returned summary is identical on every client and is folded into the gate state hash.
function K.decide(S,tick,inputs,resolve)
 local rows=K.receive(S,tick,inputs)
 for _,row in ipairs(rows) do row.head='tick='..tick..' '..row.head end
 for _,e in ipairs(K.due(S,tick)) do
  local c=e.cmd;local why,refs=K.check(S,e,resolve)
  rows[#rows+1]={entry=e,refs=refs,result=why and 'refused_'..why or 'queued',
   head=string.format('tick=%d slot=%d op=%s subject=%s target=%s',tick,e.slot,c.op,c.subject,K.target_label(c)),
   tail=string.format('seq=%d index=%d flags=%d due=%d received=%d',e.seq,e.index,c.flags,e.due,e.received)}
 end
 local parts={'PORDERS_1',tostring(tick)}
 for _,row in ipairs(rows) do parts[#parts+1]=row.head..' result='..row.result..' '..row.tail end
 return rows,table.concat(parts,'|')
end

-- Local outbox: captured or scripted commands wait here until the next proposal.
function K.outbox(slot)
 assert(integer(slot,1,K.MAX_SLOTS),'invalid outbox slot')
 return {slot=slot,queue={},seq=0,tick=0,hex=''}
end
function K.submit(O,c)
 local hex,why=K.command_hex(c)
 if not hex then return nil,why end
 if c.target and c.target==c.subject then return nil,'self_target' end
 if #O.queue>=K.MAX_QUEUE then return nil,'queue_full' end
 O.queue[#O.queue+1]={hex=hex,cmd=c}
 return true
end
-- Proposal for the report of tick (idempotent per tick); commands apply at tick+delay. budget: hex
-- characters the input multiplexer has left for this section (default and cap K.MAX_HEX); commands
-- that do not fit stay queued for a later tick.
function K.propose(O,tick,delay,budget)
 if tick==O.tick then return O.hex,O.blob end
 assert(integer(tick,1,K.MAX_TICK) and tick>O.tick,'proposal tick must increase')
 assert(integer(delay,1,K.MAX_DELAY),'invalid input delay')
 assert(budget==nil or integer(budget,0,4294967295),'invalid proposal budget')
 local limit=math.min(K.MAX_HEX,budget or K.MAX_HEX)
 O.tick,O.hex,O.blob=tick,'',nil
 local size,take=K.HEADER_HEX,0
 while take<#O.queue and take<K.MAX_COMMANDS and size+#O.queue[take+1].hex<=limit do
  take=take+1;size=size+#O.queue[take].hex
 end
 if take==0 then return '' end
 assert(O.seq<K.MAX_SEQ,'player order sequence exhausted')
 local commands={}
 for i=1,take do commands[i]=table.remove(O.queue,1).cmd end
 O.seq=O.seq+1
 local blob={slot=O.slot,tick=tick+delay,seq=O.seq,commands=commands}
 O.hex=assert(K.encode(blob));O.blob=blob
 return O.hex,blob
end
