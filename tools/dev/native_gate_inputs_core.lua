-- Protocol 3 input multiplexer: one blob per slot and tick carries pilot records, hit claims and player
-- orders. Pure Lua, no engine calls (LuaJIT-tested). The blob format is the container of
-- src/native/engine/lockstep_hit_claims.h (input_mux_decode/encode, hit_payload_decode); this file is
-- its Lua mirror, tools/dev/native_gate_inputs.py the Python reference. Report:
-- reports/LOCKSTEP-INPUT-MUX-20261029.txt.
--   ''                       no input
--   28 bytes starting 0x01   pilot record v1 alone (legacy form)
--   starting 0x50 'P'        player orders v1 alone (legacy form)
--   0x4d 'M', version 1, sections kind u8 | length u8 | payload, kinds strictly ascending:
--     0x01 pilot (28 bytes, version byte 1), 0x48 'H' hit claims, 0x4f 'O' nav octree report (20 bytes,
--     native_gate_nav_octree 'agreed'), 0x50 'P' orders (own v1 bytes), 0x53 'S' stock piloting
--     record (lockstep_pilot_stock.h, version byte 3), 0x55 'U' UI replay events
--     (lockstep_ui_replay.h, version byte 1)
-- A container holding only a channel that has a legacy form is refused: one encoding per content.
X4LAN_INPUT_MUX_CORE={}
local K=X4LAN_INPUT_MUX_CORE
K.TAG,K.VERSION=0x4d,1
K.MAX_BYTES=240                 -- INPUT_MUX_MAX; the wire carries 480 hex per slot and tick
K.MAX_HEX=2*K.MAX_BYTES
K.PILOT_BYTES,K.PILOT_VERSION=28,1
K.HIT_BYTES,K.HIT_VERSION,K.HITS_MAX,K.HIT_WINDOW=26,1,8,8
K.INCOMING,K.OUTGOING=1,2
K.KIND={pilot=0x01,hits=0x48,octree=0x4f,orders=0x50,stock=0x53,ui=0x55}
K.SECTIONS={'pilot','hits','octree','orders','stock','ui'}   -- wire order (ascending kind)
K.PRIORITY={'stock','ui','pilot','orders','hits','octree'}   -- default packing order (stock flies a slot: first; octree waits for room)
K.STOCK_VERSION=3
K.UI_VERSION=1
K.OCTREE_BYTES=20
K.ZERO=string.rep('0',16)

local function hex_ok(h) return type(h)=='string' and #h%2==0 and not h:find('[^0-9a-f]') end
local function byte(h,i) return tonumber(h:sub(2*i-1,2*i),16) end   -- 1-based byte index
local function integer(v,lo,hi) return type(v)=='number' and v==math.floor(v) and v>=lo and v<=hi end
K.integer=integer
local function id_ok(id) return type(id)=='string' and #id==16 and not id:find('[^0-9a-f]') and id~=K.ZERO end

-- Blob (lowercase hex) -> {pilot=hex,hits=hex,orders=hex} (absent sections nil) or nil,reason.
-- Reasons equal input_mux_decode's; 'encoding' is the Lua-only text check.
function K.decode(hex)
 if not hex_ok(hex) then return nil,'encoding' end
 local n=#hex/2
 if n>K.MAX_BYTES then return nil,'too_long' end
 if n==0 then return {} end
 local first=byte(hex,1)
 if first==K.KIND.pilot then
  if n~=K.PILOT_BYTES then return nil,'pilot_length' end
  return {pilot=hex}
 end
 if first==K.KIND.orders then return {orders=hex} end
 if first~=K.TAG then return nil,'channel' end
 if n<2 or byte(hex,2)~=K.VERSION then return nil,'version' end
 local out,last,count,at={},0,0,3
 while at<=n do
  if n-at+1<2 then return nil,'truncated' end
  local kind,length=byte(hex,at),byte(hex,at+1)
  at=at+2
  if kind<=last then return nil,'order' end
  if length==0 or length>n-at+1 then return nil,'section_length' end
  local payload=hex:sub(2*at-1,2*(at+length-1))
  if kind==K.KIND.pilot then
   if length~=K.PILOT_BYTES or byte(hex,at)~=K.PILOT_VERSION then return nil,'pilot_length' end
   out.pilot=payload
  elseif kind==K.KIND.hits then out.hits=payload
  elseif kind==K.KIND.octree then
   if length~=K.OCTREE_BYTES then return nil,'octree_length' end
   out.octree=payload
  elseif kind==K.KIND.orders then
   if byte(hex,at)~=K.KIND.orders then return nil,'orders_channel' end
   out.orders=payload
  elseif kind==K.KIND.stock then
   if byte(hex,at)~=K.STOCK_VERSION then return nil,'stock_version' end
   out.stock=payload
  elseif kind==K.KIND.ui then
   if byte(hex,at)~=K.UI_VERSION then return nil,'ui_version' end
   out.ui=payload
  else return nil,'kind' end
  last=kind;count=count+1;at=at+length
 end
 if count==0 or (count==1 and not out.hits and not out.octree and not out.stock and not out.ui) then return nil,'noncanonical' end
 return out
end
-- Shape of one section payload as the decoder requires it.
function K.section_ok(name,hex)
 if not hex_ok(hex) or hex=='' or #hex/2>255 then return false,'section_length' end
 if name=='pilot' then return #hex==2*K.PILOT_BYTES and byte(hex,1)==K.PILOT_VERSION,'pilot_length' end
 if name=='orders' then return byte(hex,1)==K.KIND.orders,'orders_channel' end
 if name=='hits' then local ok,why=K.hits_decode(hex);return ok~=nil,why end
 if name=='octree' then return #hex==2*K.OCTREE_BYTES,'octree_length' end
 if name=='stock' then return byte(hex,1)==K.STOCK_VERSION,'stock_version' end
 if name=='ui' then return byte(hex,1)==K.UI_VERSION,'ui_version' end
 return false,'kind'
end
-- Sections -> canonical blob ('' when empty) or nil,reason. encode(decode(x)) == x for every valid x.
function K.encode(sections)
 sections=sections or {}
 local count=0
 for name,hex in pairs(sections) do
  if not K.KIND[name] then return nil,'kind' end
  local ok,why=K.section_ok(name,hex)
  if not ok then return nil,why end
  count=count+1
 end
 if count==0 then return '' end
 if count==1 and not sections.hits and not sections.octree and not sections.stock and not sections.ui then
  local hex=sections.pilot or sections.orders
  if #hex>K.MAX_HEX then return nil,'too_long' end
  return hex
 end
 local parts,size={string.format('%02x%02x',K.TAG,K.VERSION)},2
 for _,name in ipairs(K.SECTIONS) do
  local hex=sections[name]
  if hex then
   size=size+2+#hex/2
   if size>K.MAX_BYTES then return nil,'too_long' end
   parts[#parts+1]=string.format('%02x%02x',K.KIND[name],#hex/2)..hex
  end
 end
 return table.concat(parts)
end
-- Encoded size in bytes of a section set (no validation).
function K.size(sections)
 local count,bytes=0,0
 for _,hex in pairs(sections) do count=count+1;bytes=bytes+#hex/2 end
 if count==0 then return 0 end
 if count==1 and not sections.hits and not sections.octree and not sections.stock and not sections.ui then return bytes end
 return 2+2*count+bytes
end

-- Hit claim v1 payload (hit_payload_encode/decode): count u8 1..8, then 26-byte claims, little endian:
-- version<<4|kind, flags 0, observe16, projectile key u64, target portable ID u64, point 3 x int16
-- (1/8 m in the target frame, -32768 invalid). IDs are 16-hex big-endian strings in Lua.
local function le(hex16) local out={};for i=8,1,-1 do out[#out+1]=hex16:sub(2*i-1,2*i) end;return table.concat(out) end
local function u16le(v) return string.format('%02x%02x',v%256,math.floor(v/256)) end
function K.hits_encode(claims)
 if type(claims)~='table' or #claims<1 or #claims>K.HITS_MAX then return nil,'count' end
 local parts,seen={string.format('%02x',#claims)},{}
 for _,c in ipairs(claims) do
  if c.kind~=K.INCOMING and c.kind~=K.OUTGOING then return nil,'claim' end
  if not integer(c.observe16,0,65535) then return nil,'claim' end
  if not id_ok(c.projectile) or not id_ok(c.target) then return nil,'identity' end
  if seen[c.projectile] then return nil,'duplicate' end
  seen[c.projectile]=true
  local p=c.point
  if type(p)~='table' or #p~=3 then return nil,'point' end
  local point={}
  for i=1,3 do
   if not integer(p[i],-32767,32767) then return nil,'point' end
   point[i]=u16le(p[i]<0 and p[i]+65536 or p[i])
  end
  parts[#parts+1]=string.format('%02x00',K.HIT_VERSION*16+c.kind)..u16le(c.observe16)..le(c.projectile)..le(c.target)..table.concat(point)
 end
 return table.concat(parts)
end
function K.hits_decode(hex)
 if not hex_ok(hex) then return nil,'encoding' end
 local n=#hex/2
 local count=n>=1 and byte(hex,1) or 0
 if n<1 or count==0 or count>K.HITS_MAX or n~=1+count*K.HIT_BYTES then return nil,'length' end
 local claims,seen={},{}
 for i=1,count do
  local at=2+(i-1)*K.HIT_BYTES
  local head,flags=byte(hex,at),byte(hex,at+1)
  if math.floor(head/16)~=K.HIT_VERSION or (head%16~=K.INCOMING and head%16~=K.OUTGOING) or flags~=0 then return nil,'claim' end
  local function u16(j) return byte(hex,j)+256*byte(hex,j+1) end
  local function id(j) return le(hex:sub(2*j-1,2*(j+7))) end
  local c={kind=head%16,observe16=u16(at+2),projectile=id(at+4),target=id(at+12),point={}}
  for k=1,3 do
   local v=u16(at+20+2*(k-1))
   if v==32768 then return nil,'point' end
   c.point[k]=v>=32768 and v-65536 or v
  end
  if c.projectile==K.ZERO or c.target==K.ZERO then return nil,'identity' end
  if seen[c.projectile] then return nil,'duplicate' end
  seen[c.projectile]=true
  claims[i]=c
 end
 return claims
end
-- Observe tick from its low 16 bits for a blob admitted at r (sealed with report r-1), as hit_stage.
function K.hit_observe(observe16,r)
 if r<2 then return nil,'far' end -- hit_stage admits from tick 2
 local back=((r-1)-observe16)%65536
 if back>K.HIT_WINDOW or back>r-1 then return nil,'far' end
 return r-1-back
end

-- The multiplexer: X4LAN_NATIVE_INPUTS for the probe (apply/proposal contract of
-- docs/LOCKSTEP-NSLOT-WIRE.txt) over registered sections. A section adapter keeps that contract
-- for its own payload: apply(tick, inputs, clients, slot) gets inputs[1..N] = this section's
-- payload of every slot ('' when absent) and may return a receipt string; proposal(tick, budget)
-- returns its payload for the local blob (or nil/'') within budget hex characters; optional
-- refused(tick, reason) learns that a proposal was dropped. Registration is per section name with
-- an owner tag; only the same owner may replace a section (UI reload).
function K.mux(log)
 log=log or function() end
 local M={owner='mux',sections={},priority=K.PRIORITY,applied=0,proposed=0,cache=nil,refusals=0,overflows=0}
 function M.register(name,owner,adapter)
  assert(K.KIND[name],'unknown input section '..tostring(name))
  assert(type(owner)=='string' and owner:match('^[a-z][a-z0-9_]*$'),'input section owner tag must be an identifier')
  assert(type(adapter)=='table' and (adapter.apply==nil or type(adapter.apply)=='function') and
   (adapter.proposal==nil or type(adapter.proposal)=='function') and (adapter.refused==nil or type(adapter.refused)=='function'),
   'invalid input section adapter '..name)
  local old=M.sections[name]
  assert(not old or old.owner==owner,'input section '..name..' is owned by '..(old and old.owner or '?'))
  M.sections[name]={owner=owner,adapter=adapter}
  log('INPUT_SECTION ARMED section='..name..' owner='..owner)
 end
 -- pilot, orders and hits once each; octree may be listed too, otherwise it is offered last; stock may
 -- be listed too, otherwise it is offered first; ui may be listed too, otherwise it follows stock.
 function M.set_priority(list)
  assert(type(list)=='table' and #list>=#K.PRIORITY-3 and #list<=#K.PRIORITY,'input priority lists pilot, orders and hits once each')
  local seen,out={},{}
  for _,name in ipairs(list) do
   assert(K.KIND[name] and not seen[name],'input priority lists pilot, orders and hits once each');seen[name]=true;out[#out+1]=name
  end
  assert(seen.pilot and seen.orders and seen.hits,'input priority lists pilot, orders and hits once each')
  if not seen.octree then out[#out+1]='octree' end
  if not seen.stock then table.insert(out,1,'stock') end
  if not seen.ui then
   for i,name in ipairs(out) do if name=='stock' then table.insert(out,i+1,'ui');break end end
  end
  M.priority=out
 end
 function M.names()
  local out={}
  for _,name in ipairs(K.SECTIONS) do if M.sections[name] then out[#out+1]=name..'/'..M.sections[name].owner end end
  return table.concat(out,',')
 end
 -- Every client, the identical admitted blobs: demultiplex, hand each section its slot list and
 -- fold one canonical receipt (per-slot decode result, then per section present/bytes/receipt).
 function M.apply(tick,inputs,clients,slot)
  assert(integer(tick,1,4294967295) and tick>M.applied,'input mux tick must increase')
  assert(type(inputs)=='table' and #inputs>=1,'input mux needs the admitted blobs')
  M.applied=tick
  local lists,rows={},{}
  for name in pairs(M.sections) do lists[name]={} end
  for k=1,#inputs do
   local s,why=K.decode(inputs[k])
   if s then
    for _,name in ipairs(K.SECTIONS) do
     if s[name] and not M.sections[name] then s,why=nil,'unregistered_'..name;break end
    end
   end
   if not s then
    M.refusals=M.refusals+1
    rows[k]=k..':refused_'..why
    log('INPUT_MUX_REFUSED tick='..tick..' slot='..k..' reason='..why)
    s={}
   else
    local kinds={}
    for _,name in ipairs(K.SECTIONS) do if s[name] then kinds[#kinds+1]=name end end
    rows[k]=k..':'..(#kinds>0 and table.concat(kinds,'+') or '-')
   end
   for name,list in pairs(lists) do list[k]=s[name] or '' end
  end
  local parts={'INPUTS_1',tostring(tick),table.concat(rows,',')}
  for _,name in ipairs(K.SECTIONS) do
   local section=M.sections[name]
   if section then
    local present,bytes=0,0
    for k=1,#inputs do local hex=lists[name][k];if hex~='' then present=present+1;bytes=bytes+#hex/2 end end
    local receipt
    if section.adapter.apply then receipt=section.adapter.apply(tick,lists[name],clients,slot) end
    assert(receipt==nil or type(receipt)=='string','input section '..name..' receipt must be a string')
    parts[#parts+1]=name..':'..present..'/'..bytes
    parts[#parts+1]=receipt or '-'
   end
  end
  return table.concat(parts,'|')
 end
 -- This client's blob for report tick: sections in priority order, each within the bytes left
 -- (container accounting: tag+version, 2 bytes per section header). A payload larger than its
 -- budget is refused for this tick (logged; adapter.refused), later sections are still offered.
 -- Idempotent per tick.
 function M.proposal(tick)
  if tick==M.proposed then return M.cache end
  assert(integer(tick,1,4294967295) and tick>M.proposed,'input mux proposal tick must increase')
  M.proposed=tick
  local chosen,used={},2
  for _,name in ipairs(M.priority) do
   local section=M.sections[name]
   if section and section.adapter.proposal then
    local budget=K.MAX_BYTES-used-2
    local hex=section.adapter.proposal(tick,2*budget)
    if hex~=nil and hex~='' then
     local ok,why=K.section_ok(name,hex)
     assert(ok,'invalid '..name..' proposal: '..tostring(why))
     if #hex/2>budget then
      M.overflows=M.overflows+1
      log('INPUT_MUX_OVERFLOW tick='..tick..' section='..name..' bytes='..(#hex/2)..' budget='..budget)
      if section.adapter.refused then section.adapter.refused(tick,'overflow') end
     else
      chosen[name]=hex;used=used+2+#hex/2
     end
    end
   end
  end
  M.cache=assert(K.encode(chosen))
  return M.cache
 end
 return M
end

-- Hit claims carrier (native_gate_hit_claims = 'carry'): carries claims on the wire and folds the
-- admitted ones into the receipt; it never applies a hit (the native claim staging and resolution
-- of lockstep_hit_claims.h is not bound to Lua yet). Claims come from a scripted plan
-- {[slot]={[tick]={{kind=,back=,projectile=,target=,point={x,y,z}},...}}} (back = observe offset
-- 0..8 before the report tick). The future native adapter registers the same section name.
function K.hit_carrier(p)
 local own,script,log=p.slot,p.script or {},p.log or function() end
 assert(integer(own,1,8),'hit claims need slot p1..p8')
 assert(type(script)=='table','invalid hit claim script')
 for slot,plan in pairs(script) do
  assert(integer(slot,1,8) and type(plan)=='table','invalid hit claim script slot')
  for tick,claims in pairs(plan) do
   assert(integer(tick,1,18000) and type(claims)=='table','invalid hit claim script tick')
   for _,c in ipairs(claims) do
    assert(integer(c.back or 0,0,K.HIT_WINDOW),'hit claim back must be 0..'..K.HIT_WINDOW)
    assert(K.hits_encode({{kind=c.kind,observe16=0,projectile=c.projectile,target=c.target,point=c.point}}),'invalid scripted hit claim at tick '..tick)
   end
  end
 end
 local queue,last={},0
 local A={}
 function A.proposal(tick,budget)
  for _,c in ipairs((script[own] or {})[tick] or {}) do
   queue[#queue+1]={kind=c.kind,observe16=(tick-(c.back or 0))%65536,projectile=c.projectile,target=c.target,point=c.point}
  end
  local fit=math.min(K.HITS_MAX,#queue,math.floor(((budget or K.MAX_HEX)/2-1)/K.HIT_BYTES))
  if fit<1 then return nil end
  local take={}
  for i=1,fit do take[i]=queue[i] end
  local hex=assert(K.hits_encode(take))
  for _=1,fit do table.remove(queue,1) end
  log('HIT_CLAIMS_PROPOSED tick='..tick..' slot='..own..' count='..fit..' queued='..#queue)
  return hex
 end
 function A.apply(tick,inputs)
  assert(tick>last,'hit claim tick must increase');last=tick
  local rows={}
  for slot=1,#inputs do
   local hex=inputs[slot]
   if hex~='' then
    local claims,why=K.hits_decode(hex)
    if not claims then rows[#rows+1]=slot..':refused_'..why
    else
     for i,c in ipairs(claims) do
      local observe,far=K.hit_observe(c.observe16,tick)
      local row=string.format('%d/%d/%s/%s/%s/%s/%d,%d,%d/%s',slot,i,c.kind==K.INCOMING and 'in' or 'out',observe and tostring(observe) or '-',
       c.projectile,c.target,c.point[1],c.point[2],c.point[3],observe and 'carried' or 'refused_'..far)
      rows[#rows+1]=row
      log('HIT_CLAIM tick='..tick..' '..row)
     end
    end
   end
  end
  return 'HITS_1|'..tick..'|'..(#rows>0 and table.concat(rows,';') or '-')
 end
 function A.queued() return #queue end
 return A
end
