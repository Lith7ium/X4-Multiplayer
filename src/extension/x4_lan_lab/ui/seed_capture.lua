-- One-time host bootstrap. Saves a pristine native world before any MP fixtures,
-- then exits through the normal game API. Loading/playing is a separate launch.
local cfg = X4LAN_CONFIG
if not cfg or not cfg.seed_capture then
    return
end
local ffi = require('ffi')
local engine = ffi.C
ffi.cdef(
    [[bool IsPlayerValid(void);bool IsGamePaused(void);
bool IsSaveListLoadingComplete(void);void ReloadSaveList(void);const char* GetSaveFolderPath(void);]]
)
local capture = {}
X4LAN_SEED_CAPTURE = capture
local function log(text)
    DebugError('[X4LAN] WORLD_CAPTURE ' .. text)
end
local function fail(reason)
    capture.failed = tostring(reason)
    log('STOPPED ' .. capture.failed)
    if capture.paused and engine.IsGamePaused() then
        Pause()
    end
    capture.paused = false
end
log('BEGIN id=' .. tostring(cfg.seed_id))
local ok, err = pcall(function()
    assert(
        cfg.enabled == false and cfg.test_profile == '45841',
        'capture requires disabled gameplay in isolated profile'
    )
    assert(
        type(cfg.seed_id) == 'string' and #cfg.seed_id == 32 and cfg.seed_id:match('^[a-f0-9]+$'),
        'invalid world identity'
    )
    local path = ffi.string(engine.GetSaveFolderPath()):gsub('\\', '/'):gsub('/+$', '')
    assert(path:sub(-#'/45841/save') == '/45841/save', 'capture outside isolated profile')
    X4LAN_SEED_CONTENT(cfg.seed_content)
end)
if not ok then
    fail(err)
    return
end
RegisterEvent('gameSaved', function(_, success)
    if not capture.saving or capture.done or capture.failed then
        return
    end
    if success ~= true and success ~= 1 then
        fail('native save failed')
        return
    end
    capture.done = true
    capture.exitAt = GetCurRealTime() + 1
    log('SAVED id=' .. cfg.seed_id .. ' slot=save_010')
end)
-- Optional saved-reference registry. It must be sealed into the same pristine
-- snapshot before SaveGame; a missing/rejected acknowledgement never saves.
RegisterEvent('X4LAN_SeedDocks.Marked', function(_, id)
    if not capture.pendingDocks or capture.failed or capture.done then
        return
    end
    if id ~= cfg.seed_id then
        fail('dock registry belongs to another world')
        return
    end
    capture.pendingDocks = false
    capture.docksMarked = true
    -- Inspect again on the next frame, so the ordinary pristine-world gate still
    -- runs after native registry construction and before saving.
    log('DOCKS_MARKED id=' .. id)
end)
RegisterEvent('X4LAN_SeedDocks.Error', function(_, reason)
    if capture.pendingDocks and not capture.failed then
        fail('dock registry: ' .. tostring(reason))
    end
end)
RegisterEvent('X4LAN_Seed.State', function(_, payload)
    if not capture.pending or capture.saving or capture.done or capture.failed then
        return
    end
    capture.pending = false
    local ok, err = pcall(function()
        if payload == '|1|0' and not capture.marked then
            capture.marked = true
            AddUITriggeredEvent('X4LAN_Seed', 'Mark', cfg.seed_id)
            return
        end
        assert(payload == cfg.seed_id .. '|1|0', 'world is not a pristine new host world')
        if cfg.coop_docks and not capture.docksMarked then
            capture.pendingDocks = true
            AddUITriggeredEvent('X4LAN_SeedDocks', 'Mark', cfg.seed_id)
            return
        end
        assert(IsSavingPossible(false), 'native saving unavailable')
        if not engine.IsGamePaused() then
            Pause()
            capture.paused = true
        end
        capture.saving = true
        log('SAVE_REQUESTED id=' .. cfg.seed_id)
        SaveGame('save_010', 'X4 Multiplayer shared starting world ' .. cfg.seed_id)
    end)
    if not ok then
        fail(err)
    end
end)
local function frame()
    local now = GetCurRealTime()
    if capture.failed then
        return
    end
    if capture.done then
        if now >= capture.exitAt and not capture.exiting then
            capture.exiting = true
            log('EXIT id=' .. cfg.seed_id)
            QuitGame()
        end
        return
    end
    if not engine.IsPlayerValid() then
        return
    end
    capture.started = capture.started or now
    assert(now - capture.started < 180, 'native capture timed out')
    if
        capture.saving
        or capture.pending
        or capture.pendingDocks
        or now - capture.started < 3
        or now < (capture.next or 0)
    then
        return
    end
    if not capture.refreshed then
        engine.ReloadSaveList()
        capture.refreshed = true
        return
    end
    if not engine.IsSaveListLoadingComplete() then
        return
    end
    for _, entry in pairs(GetSaveList({ save_010 = true }) or {}) do
        assert(
            entry.filename ~= 'save_010',
            'reserved world slot already exists; refusing overwrite'
        )
    end
    capture.pending = true
    capture.next = now + 1
    AddUITriggeredEvent('X4LAN_Seed', 'Inspect')
end
SetScript('onUpdate', function()
    local ok, err = pcall(frame)
    if not ok then
        fail(err)
    end
end)
