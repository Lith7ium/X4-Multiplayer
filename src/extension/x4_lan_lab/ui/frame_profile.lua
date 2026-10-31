-- Opt-in per-frame wall-time profiler for the mod's UI Lua (config flag frame_profile).
-- With the flag false or absent this file returns before defining or wrapping anything.
-- Loaded early (before the adapters) so it can count Lua->MD and MD->Lua events;
-- frame_profile_install.lua wraps the subsystem entry points after they exist.
local cfg=X4LAN_CONFIG
if not cfg or not cfg.enabled or cfg.frame_profile~=true then return end
local M=X4LAN_MEASURE
if not M then return end
local clock=M.clock
local P={sections={},order={},lua2md={},md2lua={},frames={},intervals={},wrapped=0}
X4LAN_FRAME_PROFILE=P

-- Exclusive ("self") timing: a wrapped call's time minus the time of wrapped calls nested
-- inside it. Self times of all sections therefore add up to the profiled mod time without
-- double counting chained adapters (coop -> health -> shot ...).
local depth,child=0,{}
local outside=0 -- top-level time spent outside the frame root (event callbacks) since the last frame
local function section(name)
 local r=P.sections[name]
 if not r then r={name=name,calls=0,total=0,max=0};P.sections[name]=r;P.order[#P.order+1]=name end
 return r
end
P.section=section
local function finish(r,root,t0,ok,...)
 local elapsed=clock()-t0
 local own=elapsed-child[depth]
 depth=depth-1
 if depth>0 then child[depth]=child[depth]+elapsed elseif not root then outside=outside+elapsed end
 r.calls=r.calls+1;r.total=r.total+own
 if own>r.max then r.max=own end
 if not ok then error((...),0) end
 return ...
end
function P.wrap(name,fn,root)
 assert(type(fn)=='function','cannot profile '..tostring(name))
 local r=section(name);P.wrapped=P.wrapped+1
 return function(...)
  local t0=clock();depth=depth+1;child[depth]=0
  return finish(r,root,t0,pcall(fn,...))
 end
end
-- Replace table[field] with a profiled function when it is a function and not yet wrapped.
function P.install(name,owner,field)
 if type(owner)~='table' or type(owner[field])~='function' then return false end
 P.installed=P.installed or {}
 local key=tostring(owner)..'|'..field
 if P.installed[key] then return false end
 owner[field]=P.wrap(name,owner[field]);P.installed[key]=true
 return true
end

local function ms(seconds) return seconds*1000 end
local function count(map,key) map[key]=(map[key] or 0)+1 end
local function counts(map)
 local keys,total={},0
 for k,v in pairs(map) do keys[#keys+1]=k;total=total+v end
 table.sort(keys,function(a,b) if map[a]~=map[b] then return map[a]>map[b] end return a<b end)
 local parts={};for i,k in ipairs(keys) do parts[i]=k..':'..map[k] end
 return total,(#parts>0 and table.concat(parts,',') or '-')
end
P.windowStart=nil;P.lastFrameStart=nil

function P.flush(now)
 local window=now-P.windowStart
 local frames=#P.frames
 local sum=0;for i=1,frames do sum=sum+P.frames[i] end
 local intervalSum=0;for i=1,#P.intervals do intervalSum=intervalSum+P.intervals[i] end
 local names={};for _,name in ipairs(P.order) do if P.sections[name].calls>0 then names[#names+1]=name end end
 table.sort(names,function(a,b) return P.sections[a].total>P.sections[b].total end)
 local parts,calls={},0
 for _,name in ipairs(names) do calls=calls+P.sections[name].calls end
 for i,name in ipairs(names) do
  local r=P.sections[name]
  parts[i]=string.format('%s:%d:%.3f:%.3f',name,r.calls,ms(r.total),ms(r.max))
 end
 M.log(string.format('FRAMECOST v=1 window_s=%.3f frames=%d mod_ms_mean=%.3f mod_ms_p95=%.3f mod_ms_max=%.3f '..
  'frame_interval_ms_mean=%.3f frame_interval_ms_p95=%.3f frame_interval_ms_max=%.3f profiled_calls=%d overhead_ms_est=%.3f clock=%s sections=%s',
  window,frames,frames>0 and ms(sum/frames) or 0,ms(M.percentile(P.frames,.95) or 0),ms(M.percentile(P.frames,1) or 0),
  #P.intervals>0 and ms(intervalSum/#P.intervals) or 0,ms(M.percentile(P.intervals,.95) or 0),ms(M.percentile(P.intervals,1) or 0),
  calls,ms(calls*2*M.clockCostSeconds),M.timed and M.source or (M.source..'-untimed'),#parts>0 and table.concat(parts,',') or '-'))
 local sent,sentText=counts(P.lua2md);local received,receivedText=counts(P.md2lua)
 M.log(string.format('FRAMECOST_MD v=1 window_s=%.3f lua_to_md=%d md_to_lua=%d lua_to_md_per_s=%.1f md_to_lua_per_s=%.1f sent=%s received=%s',
  window,sent,received,window>0 and sent/window or 0,window>0 and received/window or 0,sentText,receivedText))
 for _,name in ipairs(P.order) do local r=P.sections[name];r.calls,r.total,r.max=0,0,0 end
 P.frames,P.intervals,P.lua2md,P.md2lua={},{},{},{}
 P.windowStart=now
end

-- The onUpdate root. Mod time per frame = root time + event callbacks that ran outside it
-- since the previous frame. One FRAMECOST line is written per elapsed second.
local function rootDone(t0,before,ok,...)
 local t1=clock()
 P.frames[#P.frames+1]=(t1-t0)+before
 if t1-P.windowStart>=1 then P.flush(t1) end
 if not ok then error((...),0) end
 return ...
end
function P.root(fn)
 local profiled=P.wrap('bubble',fn,true)
 return function(...)
  local t0=clock()
  if P.lastFrameStart then P.intervals[#P.intervals+1]=t0-P.lastFrameStart end
  P.lastFrameStart=t0;P.windowStart=P.windowStart or t0
  local before=outside;outside=0
  return rootDone(t0,before,pcall(profiled,...))
 end
end

-- Lua->MD: count the mod's own UI-triggered events (other addons pass through untouched).
local addEvent=AddUITriggeredEvent
if type(addEvent)=='function' then
 AddUITriggeredEvent=function(screen,control,...)
  if type(screen)=='string' and screen:sub(1,5)=='X4LAN' then count(P.lua2md,screen..'.'..tostring(control)) end
  return addEvent(screen,control,...)
 end
end
-- MD->Lua: count and time the mod's event callbacks, including ones registered later at runtime.
local registerEvent=RegisterEvent
if type(registerEvent)=='function' then
 RegisterEvent=function(name,callback,...)
  if type(name)=='string' and name:sub(1,5)=='X4LAN' and type(callback)=='function' then
   local timed=P.wrap('md:'..(name:match('^([^.]+)') or name),callback)
   callback=function(...) count(P.md2lua,name);return timed(...) end
  end
  return registerEvent(name,callback,...)
 end
end
-- Network client update (send/receive/dispatch). Receive handlers are separate sections.
local client=X4LAN_CLIENT
if client and type(client.new)=='function' then
 local new=client.new
 client.new=function(...)
  local c=new(...)
  if type(c)=='table' and type(c.update)=='function' then c.update=P.wrap('net',c.update) end
  return c
 end
end
M.log('FRAMECOST armed; subsystem wrappers are installed after all adapters load')
