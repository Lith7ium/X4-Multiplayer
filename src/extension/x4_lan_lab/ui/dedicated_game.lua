-- Unattended Linux authority lifecycle. No engine hooks or network file paths.
local cfg=X4LAN_CONFIG
if not cfg or not cfg.enabled or cfg.dedicated~=true then return end
local ffi=require('ffi');local C=ffi.C
ffi.cdef[[
const char* GetSaveFolderPath(void);
double GetCurrentGameTime(void);
bool IsGamePaused(void);
UniverseID GetPlayerContainerID(void);
UniverseID GetPlayerID(void);
UniverseID GetContextByClass(UniverseID,const char*,bool);
]]
local D={failed=false,seen=nil,frames=0};X4LAN_DEDICATED=D
local function log(text) DebugError('[X4LAN] DEDICATED '..text) end
local function stop(reason)
 if not D.failed then log('STOPPED reason='..tostring(reason)) end
 D.failed=true
end
local function valid()
 assert(cfg.slot=='p1','dedicated authority requires slot p1')
 assert(cfg.test_profile=='45841','dedicated authority requires isolated profile 45841')
 assert(type(cfg.build)=='string' and cfg.build:match('^[%w_%.%-]+$'),'invalid dedicated build')
 assert(type(cfg.bubble)=='string' and #cfg.bubble==32 and cfg.bubble:match('^[a-f0-9]+$'),'invalid dedicated session')
 assert(type(cfg.dedicated_control_path)=='string' and #cfg.dedicated_control_path<=260 and
  cfg.dedicated_control_path:match('^/[%w_%./%-]+$'),'dedicated control must be an absolute local path')
 local interval=tonumber(cfg.dedicated_heartbeat_interval or 5)
 assert(interval and interval>=1 and interval<=60,'dedicated heartbeat interval must be 1..60 seconds')
 D.interval=interval
 local path=ffi.string(C.GetSaveFolderPath()):gsub('\\','/'):gsub('/+$','')
 assert(path:sub(-#'/45841/save')=='/45841/save','dedicated authority outside isolated profile')
end
local ok,err=pcall(valid)
if not ok then stop(err);return end
-- X4's UI Lua has no io library and restricts libc through FFI; the native
-- transport module reads the supervisor's control file (tests supply io).
local function read_control_line(path)
 if io and io.open then
  local file=io.open(path,'r')
  if not file then return false end
  local line=file:read('*l');file:close()
  return true,line
 end
 return X4LAN_TRANSPORT.read_control(path)
end
local function control()
 local found,line=read_control_line(cfg.dedicated_control_path)
 if not found then return end
 if not line or #line>256 then stop('invalid control record');return end
 local generation,session,action=tostring(line):match('^(%d+)|([a-f0-9]+)|(%a+)$')
 generation=tonumber(generation)
 if not generation or session~=cfg.bubble or not ({shutdown=true,pause=true,resume=true})[action] then
  stop('invalid control identity or action');return
 end
 -- A control left by an earlier process is acknowledged, never executed.
 if not D.seen then D.seen=generation;return end
 if generation<=D.seen then return end
 D.seen=generation
 if action=='shutdown' then
  D.shutdown=true;log('SHUTDOWN requested');QuitGame();return
 end
 local wanted=action=='pause'
 if C.IsGamePaused()~=wanted then Pause() end
 log(action=='pause' and 'PAUSED' or 'RESUMED')
end
local function frame()
 if D.failed or D.shutdown then return end
 local now=GetCurRealTime()
 D.frames=D.frames+1
 if now>=(D.nextControl or 0) then D.nextControl=now+1;control() end
 if D.failed or D.shutdown then return end
 local A=X4LAN_BUBBLE_API
 if not A or not A.state.active or not A.state.ready then return end
 if not D.ready then D.ready=true;D.nextHeartbeat=0;log('READY build='..cfg.build..' slot='..cfg.slot) end
 if now<(D.nextHeartbeat or 0) then return end
 D.nextHeartbeat=now+D.interval
 -- The authority's player may be outside any container (e.g. in its observer suit).
 local object=C.GetPlayerContainerID();if object==0 then object=C.GetPlayerID() end;local sector=object~=0 and C.GetContextByClass(object,'sector',false) or 0
 -- Same conversion as X4's own UI: tostring(UniverseID) keeps its ULL suffix.
 local macro=sector~=0 and GetComponentData(ConvertStringToLuaID(tostring(sector)),'macro') or ''
 if (macro or '')=='' and not D.sectorDiag then
  D.sectorDiag=true;log(string.format('SECTOR_UNRESOLVED object=%s sector=%s',tostring(object),tostring(sector)))
 end
 local elapsed=D.heartbeatAt and now-D.heartbeatAt or 0
 local rate=D.heartbeatAt and elapsed>0 and (D.frames-(D.heartbeatFrames or 0))/elapsed or 0
 D.heartbeatAt=now;D.heartbeatFrames=D.frames
 log(string.format('HEARTBEAT build=%s game=%.6f real=%.6f paused=%d sector=%s callbacks=%.3f',
  cfg.build,C.GetCurrentGameTime(),now,C.IsGamePaused() and 1 or 0,tostring(macro),rate))
end
local function safeFrame()
 local ok,err=pcall(frame)
 if not ok then stop(err) end
end
SetScript('onUpdate',safeFrame)
log('ARMED isolated Linux authority')
