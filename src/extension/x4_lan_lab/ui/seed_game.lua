-- Optional pristine-host-save admission. The launcher verifies bytes/content
-- before staging save_010; the native gate verifies the loaded MD world marker.
local cfg = X4LAN_CONFIG
if not cfg or not cfg.enabled or cfg.seed_id == nil then
    return
end
local gate = { ready = false }
X4LAN_SEED_GATE = gate
local ffi = require('ffi')
local engine = ffi.C
ffi.cdef([[
bool IsPlayerValid(void);bool IsSaveListLoadingComplete(void);
void ReloadSaveList(void);const char* GetSaveFolderPath(void);
]])
local function log(text)
    DebugError('[X4LAN] SEED_GATE ' .. text)
end
local function fail(reason)
    gate.failed = reason
    gate.ready = false
    log('STOPPED ' .. reason)
end
local function valid()
    if cfg.seed_content then
        X4LAN_SEED_CONTENT(cfg.seed_content)
    end
    -- Isolated profiles 45841..45848: the server/first client uses 45841, further clients
    -- on one test PC use their own profile.
    local profile = type(cfg.test_profile) == 'string' and cfg.test_profile:match('^4584[1-8]$')
    assert(
        profile and cfg.bubble_actions and cfg.coop and cfg.world_sync,
        'seed admission requires isolated cooperative world mode'
    )
    assert(
        type(cfg.seed_id) == 'string' and #cfg.seed_id == 32 and cfg.seed_id:match('^[a-f0-9]+$'),
        'invalid seed identity'
    )
    assert(
        cfg.bubble == cfg.seed_id and cfg.seed_verified == true,
        'unverified seed or relay session mismatch'
    )
    local path = ffi.string(engine.GetSaveFolderPath()):gsub('\\', '/'):gsub('/+$', '')
    assert(
        path:sub(-#('/' .. profile .. '/save')) == '/' .. profile .. '/save',
        'seed admission outside isolated profile'
    )
end
local ok, err = pcall(valid)
if not ok then
    fail(tostring(err))
    return
end
RegisterEvent('X4LAN_Seed.Loaded', function()
    -- Reloading an active multiplayer game would retain stale Lua/network state.
    if gate.ready then
        fail('active session reload is unsupported')
        return
    end
    gate.loaded = true
    gate.pending = false
    gate.started = GetCurRealTime()
    engine.ReloadSaveList()
end)
RegisterEvent('X4LAN_Seed.State', function(_, payload)
    if gate.failed or gate.ready or not gate.pending then
        return
    end
    gate.pending = false
    local marker, pristine, loads = tostring(payload):match('^(.*)|([01])|(%d+)$')
    if not marker then
        fail('malformed world marker')
        return
    end
    loads = tonumber(loads)
    if pristine ~= '1' then
        fail('snapshot already contains multiplayer fixtures')
        return
    end
    if marker == cfg.seed_id then
        -- UI handlers may be installed after event_game_loaded. MD retains the load
        -- count, so inspect native state instead of relying on one Lua notification.
        -- A restored marker can become readable before the load-completed MD event.
        -- Zero remains pending and is bounded by the same admission timeout.
        if loads == 0 then
            if not gate.waitLogged then
                gate.waitLogged = true
                log('WAIT_NATIVE_LOAD seed=' .. marker)
            end
            return
        end
        if loads ~= 1 then
            fail('snapshot has unexpected native load count ' .. loads)
            return
        end
        gate.loaded = true
        gate.ready = true
        log('READY seed=' .. marker .. ' slot=' .. cfg.slot)
        return
    end
    if marker ~= '' or loads ~= 0 then
        fail('loaded world differs from the staged session')
        return
    end
    if gate.loadRequested then
        return
    end -- The old world may answer while loading starts.
    gate.loadRequested = true
    gate.started = GetCurRealTime()
    log('LOAD_REQUESTED seed=' .. cfg.seed_id)
    LoadGame('save_010')
end)
function gate.frame(now)
    if gate.failed or gate.ready then
        return gate.ready
    end
    gate.started = gate.started or now
    if now - gate.started > 180 then
        fail('native seed load timed out')
        return false
    end
    if not engine.IsPlayerValid() or gate.pending or now < (gate.nextInspect or 0) then
        return false
    end
    if not gate.refreshed then
        gate.refreshed = true
        engine.ReloadSaveList()
        return false
    end
    if not engine.IsSaveListLoadingComplete() then
        return false
    end
    local found = false
    for _, entry in pairs(GetSaveList({ save_010 = true }) or {}) do
        if entry.filename == 'save_010' then
            found = true
        end
    end
    if not found then
        fail('verified isolated seed slot is absent')
        return false
    end
    gate.pending = true
    gate.nextInspect = now + 1
    AddUITriggeredEvent('X4LAN_Seed', 'Inspect')
    return false
end
