-- Experimental X4 adapter. Export availability is checked; engine behaviour
-- still needs live validation on the user's exact Linux and Windows builds.
local cfg = X4LAN_CONFIG
local function log(message)
    DebugError('[X4LAN] ' .. tostring(message))
end
if not cfg or not cfg.enabled then
    log('disabled; prepare a client package first')
    return
end
if cfg.bubble_actions then
    return
end
if cfg.seed_id then
    log('ADAPTER STOPPED: seed admission requires cooperative bubble mode')
    return
end
local ffi = require('ffi')
local C = ffi.C
local function declareType(name, definition)
    if not pcall(ffi.typeof, name) then
        ffi.cdef(definition)
    end
end
declareType('UniverseID', 'typedef uint64_t UniverseID;')
declareType('UIPosRot', 'typedef struct { float x, y, z, yaw, pitch, roll; } UIPosRot;')
declareType('GameVersion', 'typedef struct { int major, minor; } GameVersion;')
ffi.cdef([[
    bool IsPlayerValid(void);
    bool IsGamePaused(void);
    UniverseID GetPlayerControlledShipID(void);
    UniverseID GetContextByClass(UniverseID componentid, const char* classname, bool includeself);
    UIPosRot GetObjectPositionInSector(UniverseID objectid);
    bool IsComponentOperational(UniverseID componentid);
    GameVersion GetGameVersion(void);
    const char* GetSaveFolderPath(void);
    UniverseID SpawnObjectAtPos2(const char* macroname, UniverseID sectorid, UIPosRot offset, const char* ownerid);
    void SetObjectSectorPos(UniverseID objectid, UniverseID sectorid, UIPosRot offset);
    void RemoveComponent(UniverseID componentid);
]])
-- Wait for MD's loaded/started notification. The player can appear valid
-- earlier during gamestart; spawning then would orphan a proxy on that event.
local state = { ready = false, nextTry = 0, lastLog = -math.huge }
local function removeProxy()
    if state.proxy and C.IsComponentOperational(state.proxy) then
        C.RemoveComponent(state.proxy)
    end
    state.proxy, state.lastWritten = nil, nil
end
local function reset(oldWorld, preserveFrame)
    local origin, ship, sector = state.origin, state.ship, state.sector
    if not oldWorld and cfg.mode == 'proxy' then
        removeProxy()
    end
    if state.transport then
        state.transport:close()
    end
    state = { ready = not oldWorld, nextTry = GetCurRealTime() + 1, lastLog = -math.huge }
    if preserveFrame then
        state.origin, state.ship, state.sector = origin, ship, sector
    end
end
local function nonce()
    -- Does not reseed the game's global RNG. Session identifier, not a secret.
    local parts = {}
    for i = 1, 8 do
        parts[i] = string.format('%04x', math.random(0, 65535))
    end
    return table.concat(parts)
end
local function componentData(id, key)
    -- Keep the full 64-bit ID; tonumber() would lose precision.
    return GetComponentData(ConvertStringToLuaID((tostring(id):gsub('ULL$', ''))), key)
end
local function connect(ship, sector, now)
    local sectorMacro = componentData(sector, 'macro')
    assert(type(sectorMacro) == 'string' and #sectorMacro > 0, 'sector macro unavailable')
    local transport = X4LAN_TRANSPORT.open(cfg.host, cfg.port)
    state.transport = transport
    local config = {}
    for configKey, configValue in pairs(cfg) do
        config[configKey] = configValue
    end
    config.sector = sectorMacro
    state.client = X4LAN_CLIENT.new(config, transport, nonce(), log)
    state.ship, state.sector = ship, sector
    local pos = C.GetObjectPositionInSector(ship)
    -- Two local origins represent one lab coordinate frame. Existing NPCs and
    -- scenery are NOT synchronised. Slot separation is 2 km on the X axis.
    local slotX = cfg.slot == 'p1' and -1000 or 1000
    state.origin = state.origin or { pos.x - slotX, pos.y, pos.z }
    state.started = now
    local version = C.GetGameVersion()
    log(
        string.format(
            'telemetry ready; OS=%s game=%d.%02d sector=%s mode=%s',
            ffi.os,
            version.major,
            version.minor,
            sectorMacro,
            cfg.mode
        )
    )
end
local function position(pose)
    local deg = 180 / math.pi
    return ffi.new('UIPosRot', {
        x = state.origin[1] + pose[1],
        y = state.origin[2] + pose[2],
        z = state.origin[3] + pose[3],
        yaw = pose[4] * deg,
        pitch = pose[5] * deg,
        roll = pose[6] * deg,
    })
end
local function mirror(pose, now)
    if not pose then
        removeProxy()
        return
    end
    local target = position(pose)
    if not state.proxy then
        state.proxy = C.SpawnObjectAtPos2(cfg.proxy_macro, state.sector, target, 'player')
        assert(state.proxy ~= 0 and C.IsComponentOperational(state.proxy), 'proxy spawn failed')
        log('proxy created; position setter experiment starting')
    end
    -- Explicit isolated-profile experiment: hand the existing network proxy
    -- to the engine so scripted docking is not overwritten every frame.
    if cfg.interaction_test then
        if not state.interactionStarted then
            state.interactionStarted = true
            AddUITriggeredEvent(
                'X4LAN',
                'InteractionTest',
                ConvertStringToLuaID((tostring(state.proxy):gsub('ULL$', '')))
            )
            log('interaction harness owns proxy movement; network pose writes suspended')
        end
        return
    end
    assert(state.proxy ~= state.ship, "refusing to move the player's ship")
    if not C.IsComponentOperational(state.proxy) then
        state.proxy = nil
        error('proxy no longer operational; restart test in free space')
    end
    if state.lastWritten and now - state.lastDriftLog >= 1 then
        local actual = C.GetObjectPositionInSector(state.proxy)
        local previous = state.lastWritten
        local drift = math.sqrt(
            (actual.x - previous.x) ^ 2 + (actual.y - previous.y) ^ 2 + (actual.z - previous.z) ^ 2
        )
        log(string.format('proxy drift before next write: %.3f m', drift))
        state.lastDriftLog = now
    end
    if not state.lastWritten then
        -- A no-op setter would pass a test at the exact spawn position.
        -- Deliberately move ONLY the newly created proxy by 25 metres first.
        local probe = ffi.new('UIPosRot', {
            x = target.x + 25,
            y = target.y,
            z = target.z,
            yaw = target.yaw,
            pitch = target.pitch,
            roll = target.roll,
        })
        C.SetObjectSectorPos(state.proxy, state.sector, probe)
        local actual = C.GetObjectPositionInSector(state.proxy)
        local errorMetres = math.sqrt(
            (actual.x - probe.x) ^ 2 + (actual.y - probe.y) ^ 2 + (actual.z - probe.z) ^ 2
        )
        log(
            string.format(
                'first position readback error: %.3f m; rotation/visibility still need observation',
                errorMetres
            )
        )
        assert(errorMetres < 10, 'position setter did not take effect')
        state.lastDriftLog = now
    end
    C.SetObjectSectorPos(state.proxy, state.sector, target)
    state.lastWritten = target
    if not state.lastRotationLog or now - state.lastRotationLog >= 5 then
        local actual = C.GetObjectPositionInSector(state.proxy)
        local function angleError(readRadians, writtenDegrees)
            return math.abs((readRadians * 180 / math.pi - writtenDegrees + 180) % 360 - 180)
        end
        log(
            string.format(
                'rotation readback error degrees: yaw=%.3f pitch=%.3f roll=%.3f',
                angleError(actual.yaw, target.yaw),
                angleError(actual.pitch, target.pitch),
                angleError(actual.roll, target.roll)
            )
        )
        state.lastRotationLog = now
    end
end
local function frame()
    local now = GetCurRealTime()
    if state.failed or now < state.nextTry then
        return
    end
    if not state.ready then
        -- A start notification may predate UI readiness. Ask MD explicitly
        -- until its running world acknowledges us, including after UI reload.
        if C.IsPlayerValid() and now >= (state.nextReadyProbe or 0) then
            AddUITriggeredEvent('X4LAN', 'Ready')
            state.nextReadyProbe = now + 1
        end
        return
    end
    if not C.IsPlayerValid() then
        if state.client then
            reset(true)
        end
        return
    end
    local ship = C.GetPlayerControlledShipID()
    if ship == 0 then
        if state.client then
            reset(false)
        end
        return
    end
    local sector = C.GetContextByClass(ship, 'sector', false)
    if sector == 0 then
        return
    end
    if state.client and (ship ~= state.ship or sector ~= state.sector) then
        reset(false)
        return
    end
    if not state.client then
        connect(ship, sector, now)
    end
    local pose
    if not C.IsGamePaused() then
        local sectorPosition = C.GetObjectPositionInSector(ship)
        pose = {
            sectorPosition.x - state.origin[1],
            sectorPosition.y - state.origin[2],
            sectorPosition.z - state.origin[3],
            sectorPosition.yaw,
            sectorPosition.pitch,
            sectorPosition.roll,
        }
        if math.max(math.abs(pose[1]), math.abs(pose[2]), math.abs(pose[3])) > 50000 then
            pose = nil
        end
    end
    if not state.client:update(now, pose) then
        reset(false, true)
        return
    end
    local remote = state.client:render(now)
    if cfg.mode == 'proxy' then
        if not C.IsGamePaused() then
            mirror(remote, now)
        end
    end
    if now - state.lastLog >= 5 then
        log(
            string.format(
                'relay=%s remote=%s samples=%d',
                state.client.lease and 'connected' or 'waiting',
                remote and 'receiving' or 'absent',
                #state.client.samples
            )
        )
        state.lastLog = now
    end
end
local function safeFrame()
    local ok, err = pcall(frame)
    if not ok then
        log('STOPPED: ' .. tostring(err))
        pcall(function()
            reset(false)
        end)
        state.failed = true
    end
end
local ok, err = pcall(function()
    assert(cfg.mode == 'telemetry' or cfg.mode == 'proxy', 'unknown mode')
    local profile = cfg.test_profile or '45841'
    assert(type(profile) == 'string' and profile:match('^%d+$'), 'invalid test profile')
    local savePath = ffi.string(C.GetSaveFolderPath()):gsub('\\', '/'):gsub('/+$', '')
    local suffix = '/' .. profile .. '/save'
    assert(
        savePath:sub(-#suffix) == suffix,
        'inactive outside isolated test profile; start X4 with -personalfolderid ' .. profile
    )
    local required = {
        'IsPlayerValid',
        'IsGamePaused',
        'GetPlayerControlledShipID',
        'GetContextByClass',
        'GetObjectPositionInSector',
        'IsComponentOperational',
        'GetGameVersion',
    }
    if cfg.mode == 'proxy' then
        required[#required + 1] = 'SpawnObjectAtPos2'
        required[#required + 1] = 'SetObjectSectorPos'
        required[#required + 1] = 'RemoveComponent'
    end
    for _, name in ipairs(required) do
        assert(C[name], 'missing game export: ' .. name)
    end
    RegisterEvent('X4LAN.Loaded', function()
        reset(true)
        state.ready, state.nextTry = true, GetCurRealTime()
    end)
    SetScript('onUpdate', safeFrame)
    log('adapter armed in isolated profile ' .. profile .. '; awaiting world readiness')
end)
if not ok then
    log('initialisation failed: ' .. tostring(err))
end
