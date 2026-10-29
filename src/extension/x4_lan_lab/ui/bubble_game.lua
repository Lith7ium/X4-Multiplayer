-- Host-authoritative test bubble. Coordinates, damage and inventory are separate channels.
local cfg = X4LAN_CONFIG
if not cfg or not cfg.enabled or not cfg.bubble_actions then
    return
end
local ffi = require('ffi')
local C = ffi.C
local function typedef(name, def)
    if not pcall(ffi.typeof, name) then
        ffi.cdef(def)
    end
end
typedef('UniverseID', 'typedef uint64_t UniverseID;')
typedef('UIPosRot', 'typedef struct { float x,y,z,yaw,pitch,roll; } UIPosRot;')
ffi.cdef([[
bool IsPlayerValid(void); bool IsGamePaused(void);
UniverseID GetPlayerControlledShipID(void);
UniverseID GetPlayerOccupiedShipID(void);
UniverseID GetPlayerContainerID(void);
UniverseID GetContextByClass(UniverseID,const char*,bool);
UIPosRot GetObjectPositionInSector(UniverseID);
bool IsComponentOperational(UniverseID);
const char* GetSaveFolderPath(void);
UniverseID SpawnObjectAtPos2(const char*,UniverseID,UIPosRot,const char*);
void SetObjectSectorPos(UniverseID,UniverseID,UIPosRot);
void RemoveComponent(UniverseID);
]])
local function log(s)
    DebugError('[X4LAN] ' .. tostring(s))
end
-- Player roster shared by every co-op adapter (relay rules): slots p1..p<players>,
-- shared ships 1..<ships>. Two players/ships outside a dedicated session; a dedicated
-- session gives every client one ship (the server's observer takes none), at least 2.
-- Default local ships: p1->1, p2->2, p3->1 (legacy), pK->K-1 for K>=4.
local function makeRoster()
    local dedicated = cfg.dedicated_session == true
    local players = tonumber(cfg.max_players) or (dedicated and 3 or 2)
    assert(
        players % 1 == 0 and players >= 2 and players <= 8 and (dedicated or players == 2),
        'max_players must be 2, or 2..8 in a dedicated session'
    )
    local slot = tonumber(tostring(cfg.slot):match('^p([1-8])$'))
    assert(slot and slot <= players, 'slot outside max_players')
    local ships = dedicated and math.max(2, players - 1) or 2
    local own = tonumber(cfg.local_ship) or 0
    if own == 0 then
        own = (slot == 1 or slot == 3) and 1 or (slot == 2 and 2 or slot - 1)
    end
    assert(
        own % 1 == 0 and own >= 1 and own <= ships and (dedicated or own == slot),
        'invalid dedicated local ship mapping'
    )
    return { players = players, ships = ships, slot = slot, own = own, dedicated = dedicated, fixture = 2 }
end
local s = {
    loaded = false,
    ready = false,
    fire = 0,
    worldRevision = 0,
    commands = {},
    dock = { 0, 0 },
    lastLog = -math.huge,
    extra = {},
    extraRevision = 0,
    extraApplied = {},
}
local function id(n)
    return ConvertStringToLuaID((tostring(n):gsub('ULL$', '')))
end
local function nonce()
    local p = {}
    for i = 1, 8 do
        p[i] = string.format('%04x', math.random(0, 65535))
    end
    return table.concat(p)
end
local function stopRemoteFire()
    if s.firing then
        AddUITriggeredEvent('X4LAN_Bubble', 'FireOff')
        s.firing = false
    end
end
local function closeNetwork()
    stopRemoteFire()
    if X4LAN_BUBBLE_API.coop then
        X4LAN_BUBBLE_API.coop.stopFire()
    end
    if X4LAN_BUBBLE_API.coop and X4LAN_BUBBLE_API.coop.stopVelocity then
        X4LAN_BUBBLE_API.coop.stopVelocity(true)
    end
    if s.client and s.client.lease then
        s.resume = {
            lease = s.client.lease,
            nonce = s.client.nonce,
            sequence = s.client.sequence,
            tick = s.client.tick,
        }
        s.pendingNonce = nil
    elseif s.client then
        -- The relay may already have accepted this handshake before the socket failed.
        s.pendingNonce = s.client.nonce
    end
    if s.transport then
        pcall(function()
            s.transport:close()
        end)
    end
    s.client, s.transport = nil, nil
    s.retry = GetCurRealTime() + 1
end
local function receiveExtension(client, f, now)
    if X4LAN_BUBBLE_API.universe and X4LAN_BUBBLE_API.universe.receive(f, now) then
        return true
    end
    if X4LAN_BUBBLE_API.missions and X4LAN_BUBBLE_API.missions.receive(f, now) then
        return true
    end
    if X4LAN_BUBBLE_API.players and X4LAN_BUBBLE_API.players.receive(f, now) then
        return true
    end
    if X4LAN_BUBBLE_API.quest and X4LAN_BUBBLE_API.quest.receive(f, now) then
        return true
    end
    if X4LAN_BUBBLE_API.coop and X4LAN_BUBBLE_API.coop.receive(f, now) then
        return true
    end
    if X4LAN_BUBBLE_API.life and X4LAN_BUBBLE_API.life.receive(client, f, now) then
        return true
    end
    if X4LAN_BUBBLE_API.trade and X4LAN_BUBBLE_API.trade.receive(client, f, now) then
        return true
    end
    if f[1] == 'WORLD' then
        if #f ~= 22 or not f[4]:match('^[a-f0-9]+$') or #f[4] ~= 32 then
            return true
        end
        local rev = tonumber(f[5])
        if not rev or rev % 1 ~= 0 then
            return true
        end
        if s.worldOwner == f[4] and rev <= (s.remoteRevision or -1) then
            return true
        end
        local v = {}
        for i = 1, 17 do
            local n = tonumber(f[i + 5])
            if not n or n ~= n or n < 0 or n > 1000000 then
                return true
            end
            v[i] = n
        end
        if
            cfg.slot ~= 'p1'
            and cfg.trading
            and (not X4LAN_BUBBLE_API.trade or not X4LAN_BUBBLE_API.trade.state.ready)
        then
            return true
        end
        s.worldOwner, s.remoteRevision = f[4], rev
        if cfg.slot ~= 'p1' and s.ready then
            s.dock = { v[14], v[15] }
            local key = table.concat(v, '|')
            if not cfg.coop or key ~= s.appliedWorld or now >= (s.nextWorldApply or 0) then
                AddUITriggeredEvent('X4LAN_Bubble', 'Apply', { values = v })
                s.appliedWorld = key
                s.nextWorldApply = now + 1
            end
        end
        return true
    elseif f[1] == 'XSHIP' then
        -- Server-owned hull/shield/cargo/dock of ships 3.. (client roster beyond the WORLD rows).
        if #f ~= 10 or not f[4]:match('^[a-f0-9]+$') or #f[4] ~= 32 or cfg.slot == 'p1' then
            return true
        end
        local ship = tonumber(f[6])
        local v = {}
        for i = 7, 10 do
            local n = tonumber(f[i])
            if not n or n ~= n or n < 0 or n > 1000000 then
                return true
            end
            v[#v + 1] = n
        end
        if
            not ship
            or ship % 1 ~= 0
            or ship <= 2
            or ship > X4LAN_BUBBLE_API.roster.ships
            or v[1] > 100
            or v[2] > 100
        then
            return true
        end
        local key = table.concat(v, '|')
        local old = s.extraApplied[ship]
        if s.ready and (not old or old.key ~= key or now >= old.next) then
            s.extraApplied[ship] = { key = key, next = now + 1 }
            AddUITriggeredEvent(
                'X4LAN_Bubble',
                'ApplyExtra',
                { ship = ship, hull = v[1], shield = v[2], cargo = v[3], dock = v[4] }
            )
        end
        return true
    elseif f[1] == 'FIRE' then
        if #f ~= 5 or not tonumber(f[5]) then
            return true
        end
        local value = tonumber(f[5])
        if s.fireOwner ~= f[4] then
            s.fireOwner, s.remoteFire = f[4], -1
        end
        if value > s.remoteFire and value > 0 then
            s.remoteFire = value
            s.fireDeadline = now + 0.45
            if not s.firing and s.ready then
                AddUITriggeredEvent('X4LAN_Bubble', 'FireOn')
                s.firing = true
            end
        end
        return true
    elseif f[1] == 'COMMAND' then
        if cfg.slot ~= 'p1' or #f ~= 6 or not f[4]:match('^[a-f0-9]+$') or #f[4] ~= 32 then
            return true
        end
        if f[5] ~= 'p1' and f[5] ~= 'p2' then
            return true
        end
        if f[6] ~= 'PICKUP' and f[6] ~= 'TRADE' then
            return true
        end
        if C.IsGamePaused() then
            return true
        end
        local old = s.commands[f[4]]
        if old and old ~= 'PENDING' then
            s.transport:send('DONE|1|' .. client.lease .. '|' .. f[4] .. '|' .. old)
        elseif not old and s.ready then
            if cfg.trading and f[6] == 'TRADE' then
                s.commands[f[4]] = 'REJECTED'
                s.transport:send('DONE|1|' .. client.lease .. '|' .. f[4] .. '|REJECTED')
                return true
            end
            if f[6] == 'TRADE' then
                s.tradeSlot = f[5] == 'p1' and 1 or 2
                s.tradeRequest = f[4]
            end
            s.commands[f[4]] = 'PENDING' -- Deduplicate before asking the engine to change anything.
            AddUITriggeredEvent(
                'X4LAN_Bubble',
                'Command',
                { request = f[4], slot = f[5] == 'p1' and 1 or 2, verb = f[6] }
            )
        end
        return true
    end
    return false
end
X4LAN_BUBBLE_API = {
    state = s,
    id = id,
    nonce = nonce,
    paused = function()
        return C.IsGamePaused()
    end,
}
local function frame()
    local now = GetCurRealTime()
    if s.failed then
        return
    end
    if cfg.seed_id then
        local gate = X4LAN_SEED_GATE
        if not gate or gate.failed then
            error('native seed admission unavailable: ' .. tostring(gate and gate.failed))
        end
        if not gate.frame(now) then
            return
        end
    end
    if not s.loaded then
        if C.IsPlayerValid() and now >= (s.probe or 0) then
            AddUITriggeredEvent('X4LAN', 'Ready')
            s.probe = now + 1
        end
        return
    end
    if now < (s.setupAt or 0) then
        return
    end
    if not C.IsPlayerValid() then
        return
    end
    local controlled = C.GetPlayerControlledShipID()
    -- Leaving the pilot seat returns no controlled ship. Keep the session bound
    -- to its original ship; the walking avatar is not a replacement network ship.
    local ship = s.ship or (controlled ~= 0 and controlled or nil)
    if not ship then
        return
    end -- Initial setup still requires a pilot-controlled ship.
    local operational = C.IsComponentOperational(ship)
    local sector = operational and C.GetContextByClass(ship, 'sector', false) or s.sector
    if sector == 0 then
        if cfg.coop and s.sector then
            sector = s.sector
        else
            return
        end
    end
    if not s.ship then
        local R = X4LAN_BUBBLE_API.roster
        local own = R.own
        local x = own == 1 and 1000 or -1000
        if cfg.ship_macro then
            -- MD also registers the ships as native sensor sources. SpawnObjectAtPos2
            -- objects report radar range but fail discovery away from the starter ship.
            if not s.shipRequest then
                s.shipRequest = { sector = sector, original = ship }
                AddUITriggeredEvent(
                    'X4LAN_Bubble',
                    'PrepareShips',
                    {
                        sector = id(sector),
                        localX = -x,
                        macro = cfg.ship_macro,
                        loadout = cfg.ship_loadout or '',
                        weapons = cfg.ship_weapons or -1,
                        teleport = not cfg.dedicated,
                        count = R.ships,
                        ownIndex = own,
                    }
                )
                log(
                    'shared ship requested: '
                        .. cfg.ship_macro
                        .. ' loadout='
                        .. tostring(cfg.ship_loadout)
                )
            end
            if not s.shipsReady then
                return
            end
            s.ship, s.sector, s.proxy = s.shipRequest.own, s.shipRequest.sector, s.shipRequest.proxy
            -- Native objects of every shared ship by index: own at R.own, the remaining indices
            -- take the proxy and then the extra ships in ascending order.
            local pool = { s.proxy }
            for k = 1, R.ships - 2 do
                pool[#pool + 1] = s.shipRequest.extra[k]
            end
            s.ships = {}
            local n = 0
            for i = 1, R.ships do
                if i == own then
                    s.ships[i] = s.ship
                else
                    n = n + 1
                    s.ships[i] = pool[n]
                end
            end
        else
            s.ship, s.sector = ship, sector
            s.proxy = C.SpawnObjectAtPos2(
                cfg.proxy_macro,
                sector,
                ffi.new('UIPosRot', { x = x, y = 10000, z = 0 }),
                'player'
            )
            assert(s.proxy ~= 0 and C.IsComponentOperational(s.proxy), 'bubble proxy spawn failed')
        end
        AddUITriggeredEvent(
            'X4LAN_Bubble',
            cfg.slot == 'p1' and 'SetupHost' or 'SetupGuest',
            id(s.proxy)
        )
        log('bubble setup requested; slot=' .. cfg.slot)
    end
    local control = controlled == 0 and 'on-foot'
        or (controlled == s.ship and 'piloting' or 'visiting-other-ship')
    if s.control ~= control then
        s.control = control
        log('control=' .. control .. '; syncing original ship=' .. tostring(id(ship)))
    end
    if not s.ready then
        return
    end
    if X4LAN_BUBBLE_API.coop then
        X4LAN_BUBBLE_API.coop.prepare(now)
    end
    if X4LAN_BUBBLE_API.life then
        X4LAN_BUBBLE_API.life.frame(now)
    end
    if s.networkBlocked then
        return
    end
    if not s.client and now >= (s.retry or 0) then
        local c = {}
        for k, v in pairs(cfg) do
            c[k] = v
        end
        c.sector = GetComponentData(id(s.sector), 'macro')
        assert(c.sector == 'cluster_14_sector001_macro', 'test bubble requires Argon Prime')
        local ok, transport = pcall(X4LAN_TRANSPORT.open, cfg.host, cfg.port)
        if not ok then
            s.retry = now + 5
            log('connection unavailable; retrying in 5 seconds: ' .. tostring(transport))
            return
        end
        s.transport = transport
        s.client = X4LAN_CLIENT.new(c, s.transport, s.pendingNonce or nonce(), log, s.resume)
        s.client.receiveExtension = receiveExtension
    end
    if not s.client then
        return
    end
    local pose
    if not cfg.coop and operational and sector == s.sector and not C.IsGamePaused() then
        local p = C.GetObjectPositionInSector(ship)
        pose = { p.x, p.y - 10000, p.z, p.yaw, p.pitch, p.roll }
        if math.max(math.abs(pose[1]), math.abs(pose[2]), math.abs(pose[3])) > 50000 then
            pose = nil
        end
    end
    local ok, connected, reason = pcall(s.client.update, s.client, now, pose)
    if not ok or not connected then
        closeNetwork()
        if reason == 'initial-handshake-timeout' then
            s.pendingNonce = nil
        end
        if reason == 'relay-reset' then
            s.networkBlocked = true
            log('relay session was reset; restart both test worlds to create a new shared economy')
        else
            log('connection interrupted; reconnecting automatically')
        end
        return
    end
    local client = s.client
    if X4LAN_BUBBLE_API.coop then
        X4LAN_BUBBLE_API.coop.frame(now)
    end
    if X4LAN_BUBBLE_API.universe then
        X4LAN_BUBBLE_API.universe.frame(now)
    end
    if X4LAN_BUBBLE_API.trade then
        X4LAN_BUBBLE_API.trade.frame(now)
    end
    if X4LAN_BUBBLE_API.missions then
        X4LAN_BUBBLE_API.missions.frame(now)
    end
    if X4LAN_BUBBLE_API.quest then
        X4LAN_BUBBLE_API.quest.frame(now)
    end
    if X4LAN_BUBBLE_API.players then
        X4LAN_BUBBLE_API.players.frame(now)
    end
    if not cfg.coop and client.lease and now >= (s.nextFire or 0) then
        s.transport:send('FIRE|1|' .. client.lease .. '|' .. tostring(s.fire))
        s.nextFire = now + 0.05
    end
    if client.lease and cfg.slot == 'p1' and s.world and now >= (s.nextWorld or 0) then
        s.transport:send('WORLD|1|' .. client.lease .. '|' .. s.worldRevision .. '|' .. s.world)
        s.nextWorld = now + 0.1
    end
    if client.lease and cfg.slot == 'p1' and next(s.extra) and now >= (s.nextExtra or 0) then
        s.nextExtra = now + 0.2
        s.extraRevision = s.extraRevision + 1
        for ship = 3, X4LAN_BUBBLE_API.roster.ships do
            if s.extra[ship] then
                s.transport:send(
                    'XSHIP|1|'
                        .. client.lease
                        .. '|'
                        .. s.extraRevision
                        .. '|'
                        .. ship
                        .. '|'
                        .. s.extra[ship]
                )
            end
        end
    end
    if s.firing and (now > (s.fireDeadline or 0) or C.IsGamePaused()) then
        stopRemoteFire()
    end
    local remote = client:render(now)
    local other = cfg.slot == 'p1' and 2 or 1
    if
        not cfg.coop
        and remote
        and not s.remoteDockLock
        and s.tradeSlot ~= other
        and (cfg.trading or s.dock[other] == 0)
        and not C.IsGamePaused()
    then
        if
            C.IsComponentOperational(s.proxy)
            and controlled ~= s.proxy
            and C.GetPlayerContainerID() ~= s.proxy
        then
            local deg = 180 / math.pi
            C.SetObjectSectorPos(
                s.proxy,
                s.sector,
                ffi.new(
                    'UIPosRot',
                    {
                        x = remote[1],
                        y = 10000 + remote[2],
                        z = remote[3],
                        yaw = remote[4] * deg,
                        pitch = remote[5] * deg,
                        roll = remote[6] * deg,
                    }
                )
            )
        end
    end
    if now - s.lastLog >= 5 then
        if cfg.coop and X4LAN_BUBBLE_API.coop then
            for _, v in pairs(X4LAN_BUBBLE_API.coop.remote) do
                if now - v.at < 0.75 then
                    remote = true
                end
            end
        end
        log(
            'bubble slot='
                .. cfg.slot
                .. ' relay='
                .. (client.lease and 'connected' or 'waiting')
                .. ' remote='
                .. (remote and 'receiving' or 'absent')
                .. ' revision='
                .. tostring(cfg.slot == 'p1' and s.worldRevision or s.remoteRevision)
        )
        s.lastLog = now
    end
end
-- Looked up per frame so the opt-in frame profiler can wrap it; nothing else replaces it.
X4LAN_BUBBLE_API.frame = frame
local function safeFrame()
    local ok, e = pcall(X4LAN_BUBBLE_API.frame)
    if not ok then
        log('BUBBLE STOPPED: ' .. tostring(e))
        pcall(closeNetwork)
        s.failed = true
    end
end
local ok, e = pcall(function()
    local path = ffi.string(C.GetSaveFolderPath()):gsub('\\', '/'):gsub('/+$', '')
    local profile = cfg.test_profile or '45841'
    -- Isolated test profiles 45841..45848: one per client on a shared test PC.
    assert(type(profile) == 'string' and profile:match('^4584[1-8]$'), 'unsupported test profile')
    X4LAN_BUBBLE_API.roster = makeRoster()
    local suffix = '/' .. profile .. '/save'
    assert(path:sub(-#suffix) == suffix, 'inactive outside isolated test profile ' .. profile)
    s.active = true
    RegisterEvent('X4LAN.Loaded', function()
        if not s.ship then
            s.loaded = true
            s.setupAt = s.setupAt or GetCurRealTime() + 10
        end
    end)
    RegisterEvent('X4LAN_Bubble.Ready', function()
        s.ready = true
        log('bubble engine ready')
    end)
    for _, role in ipairs({ 'Own', 'Proxy' }) do
        local field = role:lower()
        RegisterEvent('X4LAN_Bubble.Prepared' .. role, function(_, object)
            if s.shipRequest and not s.failed and not s.shipsReady then
                s.shipRequest[field] = ConvertIDTo64Bit(object)
            end
        end)
    end
    -- Ships 3.. of a dedicated N-player session (proxies of further clients).
    for k = 1, X4LAN_BUBBLE_API.roster.ships - 2 do
        RegisterEvent('X4LAN_Bubble.PreparedExtra.' .. k, function(_, object)
            if s.shipRequest and not s.failed and not s.shipsReady then
                s.shipRequest.extra = s.shipRequest.extra or {}
                s.shipRequest.extra[k] = ConvertIDTo64Bit(object)
            end
        end)
    end
    RegisterEvent('X4LAN_Bubble.ShipsReady', function()
        local r = s.shipRequest
        if not r or s.failed or s.shipsReady then
            return
        end
        local count = X4LAN_BUBBLE_API.roster.ships
        local objects = { r.own or false, r.proxy or false }
        for k = 1, count - 2 do
            objects[k + 2] = r.extra and r.extra[k] or false
        end
        local seen, complete = {}, true
        for i = 1, count do
            local object = objects[i]
            if
                not object
                or object == 0
                or object == r.original
                or seen[object]
                or not C.IsComponentOperational(object)
            then
                complete = false
                break
            end
            seen[object] = true
        end
        if not complete then
            s.failed = true
            log('BUBBLE STOPPED: incomplete native shared ship preparation')
            return
        end
        s.shipsReady = true
        log('shared ships ready: ' .. cfg.ship_macro .. ' count=' .. count)
    end)
    RegisterEvent('X4LAN_Bubble.XShip', function(_, payload)
        if cfg.slot ~= 'p1' then
            return
        end
        local ship, rest = tostring(payload):match('^(%d+)|([%d.|]+)$')
        ship = tonumber(ship)
        if
            ship
            and ship > 2
            and ship <= X4LAN_BUBBLE_API.roster.ships
            and select(2, rest:gsub('|', '')) == 3
        then
            s.extra[ship] = rest
        end
    end)
    RegisterEvent('X4LAN_Bubble.ShipsRefused', function(_, reason)
        log(
            'BUBBLE STOPPED: shared ship '
                .. tostring(cfg.ship_macro)
                .. ' refused ('
                .. tostring(reason)
                .. '); no substitute ship is used'
        )
        pcall(closeNetwork)
        s.failed = true
    end)
    RegisterEvent('X4LAN_Bubble.Fire', function()
        s.fire = s.fire + 1
    end)
    RegisterEvent('X4LAN_Bubble.World', function(_, payload)
        if cfg.slot ~= 'p1' then
            return
        end
        s.worldRevision = s.worldRevision + 1
        s.world = payload
        local v = {}
        for value in payload:gmatch('[^|]+') do
            v[#v + 1] = tonumber(value)
        end
        s.dock = { v[14] or 0, v[15] or 0 }
    end)
    RegisterEvent('X4LAN_Bubble.TradeFinished', function(_, slot)
        s.tradeSlot = nil
        AddUITriggeredEvent('X4LAN_Bubble', 'TradeFinished', slot)
    end)
    RegisterEvent('X4LAN_Bubble.Result', function(_, payload)
        local request, result = payload:match('^([a-f0-9]+)|([A-Z]+)$')
        if request and s.commands[request] == 'PENDING' then
            if s.tradeRequest == request then
                s.tradeSlot, s.tradeRequest = nil, nil
            end
            s.commands[request] = result
            if s.client and s.client.lease then
                s.transport:send('DONE|1|' .. s.client.lease .. '|' .. request .. '|' .. result)
            end
            log('action result ' .. request .. ' ' .. result)
        end
    end)
    SetScript('onUpdate', safeFrame)
    log('authoritative bubble adapter armed; slot=' .. cfg.slot)
end)
if not ok then
    log('bubble initialisation failed: ' .. tostring(e))
end
