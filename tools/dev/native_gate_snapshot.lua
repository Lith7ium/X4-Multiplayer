-- Owned save_009 in a disposable lane only; the runner archives/removes that slot.
local cfg=X4LAN_CONFIG
if not cfg or cfg.native_gate_snapshot~='capture' then return end
local ffi=require('ffi');local C=ffi.C
ffi.cdef[[bool IsPlayerValid(void);bool IsGamePaused(void);double GetCurrentGameTime(void);
bool IsSaveListLoadingComplete(void);void ReloadSaveList(void);const char* GetSaveFolderPath(void);
bool IsFullscreenCutsceneActive(void);const char* GetGameStartName(void);]]
local path=ffi.string(C.GetSaveFolderPath()):gsub('\\','/'):gsub('/+$','')
assert(cfg.test_profile=='45841' and path:sub(-#'/45841/save')=='/45841/save','snapshot outside disposable profile')
assert(cfg.enabled==false,'snapshot requires disabled multiplayer gameplay')
-- Optional: other player starts (tools/dev/native_gate_scenarios.py capture-overlay).
local gamestart=cfg.native_gate_snapshot_gamestart
assert(gamestart==nil or (type(gamestart)=='string' and gamestart:match('^[A-Za-z0-9_]+$')),'invalid snapshot gamestart')
local fixed=assert(package.loadlib('./extensions/x4_lan_lab/native/x4lan_fixedtick_64.dll','luaopen_x4lan_fixedtick'))()
local owner,spawned,ready,refreshed,saving,done,inspecting,nearby
local function log(s) DebugError('[X4LAN] NATIVE_SNAPSHOT '..s) end
RegisterEvent('X4LAN_NativeGate.Ready',function(_,count)
 assert(tonumber(count)==12,'snapshot fixture incomplete');ready=C.GetCurrentGameTime()
 log('ACTORS_READY time='..string.format('%.17g',ready))
end)
RegisterEvent('gameSaved',function(_,success)
 if saving and not done then
  assert(success==true or success==1,'native snapshot save failed');done=true
  log('SAVED slot=save_009 time='..string.format('%.17g',C.GetCurrentGameTime()))
 end
end)
RegisterEvent('X4LAN_NativeGate.Attention',function(_,count) nearby=tonumber(count) end)
local function frame()
 if done then return end
 if not owner then owner=assert(fixed.start('fixed60'));log('FIXED60 armed') end
 if not C.IsPlayerValid() then return end
 -- Story starts may open with a fullscreen intro; spawn and inspect only outside it.
 if C.IsFullscreenCutsceneActive() then return end
 local time=C.GetCurrentGameTime()
 if not spawned and time>=5 then
  local started=ffi.string(C.GetGameStartName())
  assert(gamestart==nil or started==gamestart,'snapshot gamestart mismatch '..started)
  log('GAMESTART id='..started..' time='..string.format('%.17g',time))
  spawned=true;AddUITriggeredEvent('X4LAN_NativeGate','Spawn',{});return
 end
 if not ready or time<ready+2 or saving then return end
 if not inspecting then inspecting=true;AddUITriggeredEvent('X4LAN_NativeGate','InspectAttention',{});return end
 if nearby==nil then return end
 assert(nearby==12,'snapshot requires all 12 actors at nearby attention')
 if not refreshed then C.ReloadSaveList();refreshed=true;return end
 if not C.IsSaveListLoadingComplete() then return end
 for _,entry in pairs(GetSaveList({save_009=true}) or {}) do assert(entry.filename~='save_009','snapshot slot occupied') end
 assert(IsSavingPossible(false),'native saving unavailable')
 if not C.IsGamePaused() then Pause() end
 saving=true;log('SAVE_REQUEST time='..string.format('%.17g',time))
 SaveGame('save_009','Native lockstep disposable starting fixture')
end
SetScript('onUpdate',function()
 local ok,why=pcall(frame)
 if not ok then done=true;log('ERROR '..tostring(why)) end
end)
