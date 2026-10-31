-- The production Linux gate module through LuaJIT's package.loadlib, outside X4:
-- the Lua 5.1 ABI resolves from the running LuaJIT, the `gate` table holds the
-- core methods and none of the unported module methods, and every start refuses
-- at the file pin without touching memory.  luajit test_gate_lua.lua x4lan_gate.so
local path=assert(arg[1],'module path required')
local open=assert(package.loadlib(path,'luaopen_x4lan_gate'))
local gate=assert(open(),'module did not resolve the Lua ABI')
local core={'sha256','start','boot','load_requested','report','poll','stop','frame_timing','qpc_us','tick_rate','wire','propose',
 'relay_host','lookahead','lookahead_status','platform','verify_stage',
 'world_watch','world_capture','world_capture_nav_v2','world_page','world_detail','world_banks','saved_identity',
 'path_determinism','path_determinism_status','path_budget','path_budget_status','region_rng','region_rng_status','region_rng_row',
 'ai_wait_rng','ai_wait_rng_status','ai_wait_rng_row','read_cost',
 'md_resume_rng','md_resume_rng_status','md_resume_rng_row','boot_hold','boot_hold_status',
 'event_order','event_order_status','event_rows','event_order_row',
 'gravidar_key','gravidar_time','gravidar_key_status','gravidar','ai_schedule','ai_schedule_status','ai_schedule_fallbacks',
 'gravidar_deadlines','gravidar_deadlines_status','travel_snapshot','travel_snapshot_status',
 'runtime_ids','runtime_ids_status','runtime_ids_word','runtime_ids_map_row','runtime_ids_caller_row','runtime_ids_callers',
 'script_rng','script_rng_status','script_rng_row','script_rng_own_entries','md_timed_rng','md_timed_rng_status','find_order','find_order_status','find_order_row','loadout_order','loadout_order_status','loadout_order_row','nav_key','attention_all','attention_all_status','step_trace','step_trace_arm','step_trace_status','step_trace_row','step_chain','fp_redirect','fp_redirect_watch','fp_redirect_status','fp_redirect_row','coverage','coverage_frame','coverage_status','coverage_row','nav_cadence','nav_cadence_status','nav_cadence_row','nav_cadence_watch','nav_probe_init','nav_params_init','created_identities','portable_created','created_load','created_identities_status','created_row','created_identity','created_runtime','gravidar_wide','gravidar_wide_status','spawn_rng','spawn_rng_status','spawn_rng_row'}
local count=0
for _,name in ipairs(core) do assert(type(gate[name])=='function','missing gate.'..name) end
for _ in pairs(gate) do count=count+1 end
assert(count==#core,'unexpected gate methods: '..count)
for _,name in ipairs({'actor','world_trace','thread_rng','boot_rng_anchor','pilot_inputs','snapshot_fixture'}) do
 assert(gate[name]==nil,'unported method present: '..name)
end
assert(gate.platform()=='linux')
assert(gate.sha256('')=='e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855')
assert(gate.sha256('abc')=='ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad')
assert(gate.sha256(string.rep('a',1000000))=='cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0')
assert(gate.sha256('a\0b')~=gate.sha256('a'),'embedded zero bytes must count')
assert(gate.poll()==nil and gate.frame_timing()==nil)
assert(gate.wire(3,4)==1 and gate.wire(3,9)==0 and gate.wire(2,3)==0 and gate.wire(1.5,2)==0)
assert(gate.lookahead(2)==1 and gate.lookahead(9)==0 and gate.lookahead(-1)==0)
assert(gate.wire(2,2)==1 and gate.lookahead(1)==0,'protocol 4 needs wire 3')
assert(gate.relay_host('10.1.2.3')==1 and gate.relay_host('256.1.1.1')==0 and gate.relay_host('1.2.3')==0)
assert(gate.relay_host('127.0.0.1')==1)
local token,digest=string.rep('0',32),string.rep('a',64)
-- Not the pinned X4 build: refused at the file pin; the gate stays unused.
assert(gate.boot(1,47000,token,5000,digest,0)==11 and gate.verify_stage()=='file')
assert(gate.start(1,47000,token,5000,digest,0)==11)
assert(gate.boot(1,47000,'short',5000,digest,0)==10)
assert(gate.boot(1,80,token,5000,digest,0)==10)
assert(gate.boot(1,47000,token,5000,digest,4)==105,'unported option bits refuse before anything else')
assert(gate.boot(1,47000,token,5000,digest,0.5)==10)
-- World identities (2^46) are a ported option: refused only at the file pin here.
assert(gate.boot(1,47000,token,5000,digest,70368744177664)==11)
assert(gate.boot(1,47000,token,5000,digest,70368744177664+2)==11) -- with the frame seed
assert(gate.boot(1,47000,token,5000,digest,70368744177664+3)==11) -- with serial pathing
assert(gate.boot(1,47000,token,5000,digest,70368744177664+4)==105)
local summary,why=gate.world_capture(1)
assert(summary==nil and why:match('stage=') and gate.world_page(0)==nil and gate.world_banks()==nil)
assert(gate.poll()==nil and gate.stop()==0 and gate.load_requested()==0)
assert(gate.report(0,digest)==0 and gate.propose(0,'')==0)
assert(gate.wire(3,3)==1,'still configurable: never started')
assert(type(gate.qpc_us())=='number' and gate.qpc_us()>0)
assert(gate.lookahead_status():match('^lookahead=0 '))
-- A second load returns the same module state (X4 reloads its UI).
local again=assert(package.loadlib(path,'luaopen_x4lan_gate'))()
assert(again.wire(2,2)==1 and again.poll()==nil)
print('LUA BRIDGE PASS methods='..count)
