-- The read-only Linux import observer is deliberately not an admission backend.
local cfg=X4MP_PORTABLE_CONFIG
if type(cfg)~='table' or cfg.enabled~=true then return end
local ffi=require('ffi')
assert(ffi.os=='Linux' and cfg.mode=='snapshot-import-observer-v1','Linux observer mode required')
-- X4 deliberately restricts ffi.C and removes ffi.load. Use its supported
-- native Lua module path; this module exposes only the atomic ready accessor.
assert(package and package.loadlib,'native Lua modules unavailable')
if GetUISafeModeOption then assert(GetUISafeModeOption()==false,'owned profile requires unprotected native modules') end
local initialise,message=package.loadlib('./extensions/x4_lan_lab/native/x4mp_load_probe.so','luaopen_x4mp_load_probe')
assert(initialise,message)
local native,reason=initialise()
assert(type(native)=='table' and type(native.ready)=='function',reason or 'observer readiness module unavailable')
X4MP_PORTABLE_BACKEND={
 ready=function() return native.ready()==true end,
 before_load=function() return native.ready()==true end,
}
