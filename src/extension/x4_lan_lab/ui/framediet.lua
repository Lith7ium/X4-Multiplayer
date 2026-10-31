-- Server frame diet and render RNG split (Windows). Engine instrumentation under docs/ENGINE-INSTRUMENTATION.txt;
-- evidence, sites and safety: reports/REWRITE-FRAME-DIET.md.
--   X4LAN_CONFIG.frame_diet  true: on a Windows dedicated server (dedicated=true, slot p1) skip presentation work
--                            the simulation does not read: the Anark scene layer walk and the main-thread draw
--                            (`server` set). Refused everywhere else, so a playing host or client never loses
--                            its picture. The Anark element update (`ui_elements`) is never skipped.
--   X4LAN_CONFIG.rng_split   true: GameRender::Move (render-list preparation) draws from a private RNG stream, so
--                            the camera view no longer changes the simulation's thread RNG stream. Server and
--                            clients arm it for RNG parity; it skips nothing.
-- Logs one FEATURES line once the outcome is known; every refusal or disarm logs
-- FEATURE framediet|rng_split DISABLED reason=... (no automatic restart).
local cfg=X4LAN_CONFIG
if not cfg or not cfg.enabled then return end
if X4LAN_FRAMEDIET_LOADER then return end
X4LAN_FRAMEDIET_LOADER=true
local function log(text) DebugError('[X4LAN] '..text) end
local state={framediet=cfg.frame_diet==true and 'pending' or 'off',rng_split=cfg.rng_split==true and 'pending' or 'off'}
local reported
local function features()
 if reported then return end
 reported=true
 log('FEATURES process=x4lan-framediet framediet='..state.framediet..' rng_split='..state.rng_split)
end
-- A requested feature that cannot (or can no longer) run: one loud line, then off for this session.
local function refuse(name,reason)
 if state[name]~='pending' and state[name]~='on' then return end
 state[name]='auto-disabled:'..reason
 log('FEATURE '..name..' DISABLED reason='..reason)
end
local function refuseAll(reason) refuse('framediet',reason);refuse('rng_split',reason) end
if state.framediet=='pending' and not (cfg.dedicated==true and cfg.slot=='p1') then
 refuse('framediet','not_dedicated_server')
end
if not (package and package.config and package.config:sub(1,1)=='\\') then refuseAll('windows_only') end
-- Disposable test profiles only: 45841 (p1) .. 45848 (np8 assigns one per slot).
local profile=tonumber(cfg.test_profile)
if not (profile and profile>=45841 and profile<=45848) then refuseAll('not_test_profile') end
if not (package and package.loadlib) then refuseAll('native_modules_unavailable') end
local diet,split=state.framediet=='pending',state.rng_split=='pending'
if not diet and not split then features();return end
local initialise,message=package.loadlib('./extensions/x4_lan_lab/native/x4lan_framediet_64.dll','luaopen_x4lan_framediet')
local native
if initialise then
 local ok,result=pcall(initialise)
 if ok and type(result)=='table' and type(result.start)=='function' then native=result
 else message='module_init_failed:'..tostring(result) end
else
 message='load_failed:'..tostring(message)
end
if not native then refuseAll((tostring(message):gsub('%s+','_')));features();return end
local mode=diet and 'diet' or 'pass'
local sites=diet and (split and 'server,rng_split' or 'server') or 'rng_split'
local owner,finished,nextTry,lastStalls=nil,false,0,0
SetScript('onUpdate',function()
 if finished then return end
 if owner then
  -- active() is the heartbeat of the module's UI Lua stall watchdog: call it every update.
  local on,stalls=owner:active()
  if not on then
   local s=owner:stats()
   refuseAll(((s and s.reason) or 'disarmed'):gsub('%s+','_'))
   owner:stop();owner=nil;finished=true
   return
  end
  if (stalls or 0)>lastStalls then
   -- UI Lua stopped while skipping: the module went back to pass mode for good. The split stays armed.
   lastStalls=stalls
   refuse('framediet','ui_lua_stalled')
  end
  return
 end
 local now=GetCurRealTime()
 if now<nextTry then return end
 nextTry=now+1
 local ok,result,why=pcall(native.start,mode,sites)
 if ok and result then
  owner=result
  if diet then state.framediet='on' end
  if split then state.rng_split='on' end
  features()
  return
 end
 local reason=ok and tostring(why) or tostring(result)
 if ok and string.sub(reason,1,7)=='loading' then return end   -- loading screens: retry every second
 refuseAll((reason:gsub('%s+','_')))
 features()
 finished=true
end)
