-- The preloaded native agent owns frame capture, permits and restoration.
-- This module only exposes the verified one-shot installation acknowledgement.
local cfg=X4MP_PORTABLE_CONFIG
if type(cfg)~='table' or cfg.enabled~=true then return end
local ffi=require('ffi')
assert(ffi.os=='Linux','Linux portable native mode required')
if cfg.mode=='portable-fixture12-euler-v2' then
 assert(cfg.contract_sha256=='77b19d6f210426c86c73bafdf3a3f2531747968819a5ef61e93344f902da2790','Euler contract differs')
 assert(cfg.import_math_sha256=='27c9bd768900673531c698e59b410fc38940b036ef9563e5e9483e9f3303537b','Euler descriptor differs')
 assert(cfg.snapshot_sha256=='7816351056e6040d92ae9a4619ae2e15a577fa26a19134f11e20746317e61aa8','Euler snapshot differs')
else
 assert(cfg.mode=='portable-fixture12-zero-v1','Linux portable native mode required')
 assert(cfg.import_math_sha256==nil,'legacy mode cannot carry Euler capability')
 assert(cfg.contract_sha256=='516ef8728fad1e9c99da5e3fa864d5d05366525ba7576c5256b2e9926b10bd50','legacy contract differs')
end
assert(cfg.profile==tostring(45841),'private Linux namespace profile required')
assert(cfg.schema_sha256=='57edd192fe98d5daa46df95e7d84d17a76976a51776cf06fcf9e4e5e8e5dde3e','portable schema differs')
assert(type(cfg.snapshot_sha256)=='string' and #cfg.snapshot_sha256==64 and cfg.snapshot_sha256:match('^[0-9a-f]+$'),'owned snapshot identity required')
assert(package and package.loadlib,'native Lua modules unavailable')
if GetUISafeModeOption then assert(GetUISafeModeOption()==false,'owned profile requires native modules') end
local init=assert(package.loadlib('./extensions/x4_lan_lab/native/x4mp_load_probe.so','luaopen_x4mp_load_probe'))
local bridge,why=init()
assert(type(bridge)=='table' and type(bridge.ready)=='function',why or 'verified native readiness bridge unavailable')
local function ready() return bridge.ready()==true end
X4MP_PORTABLE_BACKEND={
 ready=ready,
 before_load=function(snapshot) return snapshot==cfg.snapshot_sha256 and ready() end,
}
