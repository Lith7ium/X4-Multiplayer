-- Disposable snapshot bootstrap only; no actor spawning, orders or clock writes.
local cfg=X4MP_PORTABLE_CONFIG
if type(cfg)~='table' or cfg.enabled~=true then return end
local ffi=require('ffi')
ffi.cdef[[const char* GetSaveFolderPath(void);bool IsStartmenu(void);
bool IsSaveListLoadingComplete(void);void ReloadSaveList(void);
bool IsPlayerValid(void);bool IsGamePaused(void);double GetCurrentGameTime(void);]]
local C=ffi.C
local function hex(s,n) return type(s)=='string' and #s==n and s:match('^[0-9a-f]+$') end
local windows_profiles={['45850']=true,['45851']=true,['45853']=true,['45854']=true,
 ['45855']=true,['45856']=true,['45857']=true,['45858']=true}
-- Keep common source bytes unchanged by the released lane's quoted-profile
-- substitution. The Linux namespace still accepts exactly its private profile.
assert((ffi.os=='Linux' and cfg.profile==tostring(45841)) or
 (ffi.os=='Windows' and windows_profiles[cfg.profile]),'portable loader requires an allowed disposable profile')
assert(type(cfg.run)=='string' and #cfg.run<=80 and cfg.run:match('^[a-z0-9_-]+$'),'invalid owned run')
assert(hex(cfg.snapshot_sha256,64) and hex(cfg.contract_sha256,64),'missing owned snapshot/contract identity')
assert(cfg.schema_sha256=='57edd192fe98d5daa46df95e7d84d17a76976a51776cf06fcf9e4e5e8e5dde3e','unsupported portable schema')
local function path(p)
 p=tostring(p):gsub('\\','/'):gsub('/+$','')
 return ffi.os=='Windows' and p:lower() or p
end
local suffix='/'..cfg.profile..'/save'
assert(type(cfg.save_path)=='string' and path(cfg.save_path):sub(-#suffix)==suffix,'invalid private save path')
local function owned_profile() return path(ffi.string(C.GetSaveFolderPath()))==path(cfg.save_path) end
assert(owned_profile(),'portable loader outside owned disposable profile')
local backend=assert(X4MP_PORTABLE_BACKEND,'portable native backend missing')
assert(type(backend.ready)=='function' and type(backend.before_load)=='function','portable backend incomplete')
local s={}
local function log(message) DebugError('[X4LAN] PORTABLE_BOOT '..message..' run='..cfg.run) end
local function update()
 if s.failed then return end
 if not C.IsStartmenu() then
  if C.IsPlayerValid() and not s.loaded then
   s.loaded=true
   log(string.format('LOADED game=%.17g paused=%s snapshot=%s',C.GetCurrentGameTime(),tostring(C.IsGamePaused()),cfg.snapshot_sha256))
  end
  return
 end
 if not s.menu then s.menu=true;log('MENU schema='..cfg.schema_sha256..' snapshot='..cfg.snapshot_sha256) end
 if s.requested then return end
 if not s.refreshed then C.ReloadSaveList();s.refreshed=true;return end
 if not C.IsSaveListLoadingComplete() then return end
 local count=0
 for _,entry in pairs(GetSaveList({save_009=true}) or {}) do if entry.filename=='save_009' then count=count+1 end end
 assert(count==1,'exact owned snapshot slot missing or duplicated')
 -- No timing substitute: native installation acknowledgment is mandatory.
 if backend.ready()~=true then return end
 assert(owned_profile(),'private save path changed before load')
 assert(backend.before_load(cfg.snapshot_sha256)==true,'native load request refused')
 s.requested=true;log('LOAD_REQUEST snapshot='..cfg.snapshot_sha256..' contract='..cfg.contract_sha256)
 LoadGame('save_009')
end
SetScript('onUpdate',function()
 local ok,why=pcall(update)
 if not ok then
  s.failed=true
  if type(backend.cancel)=='function' then
   local cancelled,restored=pcall(backend.cancel)
   log('CANCEL acknowledged='..tostring(cancelled and restored==true))
  end
  log('ERROR '..tostring(why))
 end
 -- The common loader owns onUpdate. An optional observer may read native
 -- terminal diagnostics after closure or cancellation; it never gates loading.
 if type(backend.observe)=='function' and not s.observe_failed then
  local observed,message=pcall(backend.observe)
  if not observed then
   s.observe_failed=true
   if not s.failed and type(backend.cancel)=='function' then
    local cancelled,restored=pcall(backend.cancel)
    log('CANCEL acknowledged='..tostring(cancelled and restored==true))
   end
   s.failed=true
   log('ERROR observation='..tostring(message))
  end
 end
end)
