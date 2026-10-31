-- Opt-in measurement support: a wall clock for frame_profile.lua and net_rtt.lua.
-- Loads nothing and defines nothing unless a matched package enables a measurement.
local cfg=X4LAN_CONFIG
if not cfg or not cfg.enabled or (cfg.frame_profile~=true and cfg.net_probe~=true) then return end
local M={}
X4LAN_MEASURE=M

-- Wall-clock seconds. Preference: test/diagnostic injection, QueryPerformanceCounter
-- (Windows) or clock_gettime(CLOCK_MONOTONIC) (Linux) through LuaJIT FFI, then
-- GetCurRealTime. The chosen source and its observed resolution are logged.
local function ffiClock()
 local ok,ffi=pcall(require,'ffi')
 if not ok or type(ffi)~='table' or not ffi.cdef or not ffi.new then return nil end
 if ffi.os=='Windows' then
  -- Another module may already have declared these; a redefinition error is harmless.
  pcall(ffi.cdef,'int QueryPerformanceCounter(int64_t *count); int QueryPerformanceFrequency(int64_t *frequency);')
  local okFreq,clock=pcall(function()
   local C=ffi.C;local value=ffi.new('int64_t[1]')
   assert(C.QueryPerformanceFrequency(value)~=0,'QueryPerformanceFrequency failed')
   local frequency=tonumber(value[0]);assert(frequency and frequency>0,'invalid QPC frequency')
   assert(C.QueryPerformanceCounter(value)~=0,'QueryPerformanceCounter failed')
   local base=value[0];local scale=1/frequency
   return function() C.QueryPerformanceCounter(value);return tonumber(value[0]-base)*scale end
  end)
  if okFreq then return clock,'qpc' end
 else
  pcall(ffi.cdef,'typedef struct { long tv_sec; long tv_nsec; } x4lan_measure_timespec; int clock_gettime(int clock, x4lan_measure_timespec *value);')
  local okClock,clock=pcall(function()
   local C=ffi.C;local value=ffi.new('x4lan_measure_timespec[1]')
   assert(C.clock_gettime(1,value)==0,'clock_gettime(CLOCK_MONOTONIC) failed')
   local base=tonumber(value[0].tv_sec)
   return function() C.clock_gettime(1,value);return (tonumber(value[0].tv_sec)-base)+tonumber(value[0].tv_nsec)*1e-9 end
  end)
  if okClock then return clock,'clock_gettime' end
 end
end

local function usable(clock)
 local ok,a=pcall(clock);if not ok or type(a)~='number' or a~=a then return false end
 local b=clock();return type(b)=='number' and b>=a
end

-- Smallest positive step seen over a bounded number of reads; nil means the clock did not
-- advance at all (for example a per-frame cached time), so durations cannot be measured.
function M.resolution(clock,reads)
 local previous=clock();local best
 for _=1,reads or 2000 do
  local value=clock();local step=value-previous
  if step>0 then
   if not best or step<best then best=step end
   if best<=1e-7 then break end
  end
  previous=value
 end
 return best
end

local injected=rawget(_G,'X4LAN_MEASURE_CLOCK')
if type(injected)=='function' and usable(injected) then
 M.clock,M.source=injected,'injected'
else
 local clock,source=ffiClock()
 if clock and usable(clock) then M.clock,M.source=clock,source
 else M.clock,M.source=GetCurRealTime,'GetCurRealTime' end
end
M.resolutionSeconds=M.resolution(M.clock)
M.timed=M.resolutionSeconds~=nil
-- Mean cost of one clock read; each profiled call performs two, so FRAMECOST can state
-- an estimate of its own overhead.
do
 local reads=1000;local first=M.clock();for _=1,reads do M.clock() end
 M.clockCostSeconds=M.timed and math.max(0,(M.clock()-first)/(reads+1)) or 0
end

-- Nearest-rank percentile of an unsorted numeric list (copied, never reordered in place).
function M.percentile(values,p)
 local n=#values;if n==0 then return nil end
 local sorted={};for i=1,n do sorted[i]=values[i] end
 table.sort(sorted)
 local rank=math.ceil(p*n);if rank<1 then rank=1 elseif rank>n then rank=n end
 return sorted[rank]
end
function M.log(text) DebugError('[X4LAN] '..text) end
M.log(string.format('MEASURE_CLOCK v=1 source=%s resolution_us=%s read_cost_us=%.3f frame_profile=%s net_probe=%s',
 M.source,M.timed and string.format('%.3f',M.resolutionSeconds*1e6) or 'none',M.clockCostSeconds*1e6,
 tostring(cfg.frame_profile==true),tostring(cfg.net_probe==true)))
