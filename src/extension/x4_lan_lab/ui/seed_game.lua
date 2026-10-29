-- Optional pristine-host-save admission. The launcher verifies bytes/content
-- before staging save_010; the native gate verifies the loaded MD world marker.
local cfg = X4LAN_CONFIG
if not cfg or not cfg.enabled or cfg.seed_id == nil then
    return
end
local G = { ready = false }
X4LAN_SEED_GATE = G
local ffi = require('ffi')
local C = ffi.C
ffi.cdef([[
bool IsPlayerValid(void);bool IsSaveListLoadingComplete(void);
void ReloadSaveList(void);const char* GetSaveFolderPath(void);
]])
local function log(text)
    DebugError('[X4LAN] SEED_GATE ' .. text)
end
local function fail(reason)
    G.failed = reason
    G.ready = false
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
    local path = ffi.string(C.GetSaveFolderPath()):gsub('\\', '/'):gsub('/+$', '')
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
    if G.ready then
        fail('active session reload is unsupported')
        return
    end
    G.loaded = true
    G.pending = false
    G.started = GetCurRealTime()
    C.ReloadSaveList()
end)
RegisterEvent('X4LAN_Seed.State', function(_, payload)
    if G.failed or G.ready or not G.pending then
        return
    end
    G.pending = false
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
            if not G.waitLogged then
                G.waitLogged = true
                log('WAIT_NATIVE_LOAD seed=' .. marker)
            end
            return
        end
        if loads ~= 1 then
            fail('snapshot has unexpected native load count ' .. loads)
            return
        end
        G.loaded = true
        G.ready = true
        log('READY seed=' .. marker .. ' slot=' .. cfg.slot)
        return
    end
    if marker ~= '' or loads ~= 0 then
        fail('loaded world differs from the staged session')
        return
    end
    if G.loadRequested then
        return
    end -- The old world may answer while loading starts.
    G.loadRequested = true
    G.started = GetCurRealTime()
    log('LOAD_REQUESTED seed=' .. cfg.seed_id)
    LoadGame('save_010')
end)
function G.frame(now)
    if G.failed or G.ready then
        return G.ready
    end
    G.started = G.started or now
    if now - G.started > 180 then
        fail('native seed load timed out')
        return false
    end
    if not C.IsPlayerValid() or G.pending or now < (G.nextInspect or 0) then
        return false
    end
    if not G.refreshed then
        G.refreshed = true
        C.ReloadSaveList()
        return false
    end
    if not C.IsSaveListLoadingComplete() then
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
    G.pending = true
    G.nextInspect = now + 1
    AddUITriggeredEvent('X4LAN_Seed', 'Inspect')
    return false
end
