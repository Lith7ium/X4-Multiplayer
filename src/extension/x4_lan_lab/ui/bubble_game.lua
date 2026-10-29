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
local function log(message)
    DebugError('[X4LAN] ' .. tostring(message))
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
    return {
        players = players,
        ships = ships,
        slot = slot,
        own = own,
        dedicated = dedicated,
        fixture = 2,
    }
end
local bubbleState = {
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
    if bubbleState.firing then
        AddUITriggeredEvent('X4LAN_Bubble', 'FireOff')
        bubbleState.firing = false
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
    if bubbleState.client and bubbleState.client.lease then
        bubbleState.resume = {
            lease = bubbleState.client.lease,
            nonce = bubbleState.client.nonce,
            sequence = bubbleState.client.sequence,
            tick = bubbleState.client.tick,
        }
        bubbleState.pendingNonce = nil
    elseif bubbleState.client then
        -- The relay may already have accepted this handshake before the socket failed.
        bubbleState.pendingNonce = bubbleState.client.nonce
    end
    if bubbleState.transport then
        pcall(function()
            bubbleState.transport:close()
        end)
    end
    bubbleState.client, bubbleState.transport = nil, nil
    bubbleState.retry = GetCurRealTime() + 1
end

local function receiveExtension(client, fields, now)
    if X4LAN_BUBBLE_API.universe and X4LAN_BUBBLE_API.universe.receive(fields, now) then
        return true
    end
    if X4LAN_BUBBLE_API.missions and X4LAN_BUBBLE_API.missions.receive(fields, now) then
        return true
    end
    if X4LAN_BUBBLE_API.players and X4LAN_BUBBLE_API.players.receive(fields, now) then
        return true
    end
    if X4LAN_BUBBLE_API.quest and X4LAN_BUBBLE_API.quest.receive(fields, now) then
        return true
    end
    if X4LAN_BUBBLE_API.coop and X4LAN_BUBBLE_API.coop.receive(fields, now) then
        return true
    end
    if X4LAN_BUBBLE_API.life and X4LAN_BUBBLE_API.life.receive(client, fields, now) then
        return true
    end
    if X4LAN_BUBBLE_API.trade and X4LAN_BUBBLE_API.trade.receive(client, fields, now) then
        return true
    end
    if fields[1] == 'WORLD' then
        if #fields ~= 22 or not fields[4]:match('^[a-f0-9]+$') or #fields[4] ~= 32 then
            return true
        end
        local rev = tonumber(fields[5])
        if not rev or rev % 1 ~= 0 then
            return true
        end
        if bubbleState.worldOwner == fields[4] and rev <= (bubbleState.remoteRevision or -1) then
            return true
        end
        local v = {}
        for i = 1, 17 do
            local n = tonumber(fields[i + 5])
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
        bubbleState.worldOwner, bubbleState.remoteRevision = fields[4], rev
        if cfg.slot ~= 'p1' and bubbleState.ready then
            bubbleState.dock = { v[14], v[15] }
            local key = table.concat(v, '|')
            if
                not cfg.coop
                or key ~= bubbleState.appliedWorld
                or now >= (bubbleState.nextWorldApply or 0)
            then
                AddUITriggeredEvent('X4LAN_Bubble', 'Apply', { values = v })
                bubbleState.appliedWorld = key
                bubbleState.nextWorldApply = now + 1
            end
        end
        return true
    elseif fields[1] == 'XSHIP' then
        -- Server-owned hull/shield/cargo/dock of ships 3.. (client roster beyond the WORLD rows).
        if
            #fields ~= 10
            or not fields[4]:match('^[a-f0-9]+$')
            or #fields[4] ~= 32
            or cfg.slot == 'p1'
        then
            return true
        end
        local ship = tonumber(fields[6])
        local v = {}
        for i = 7, 10 do
            local n = tonumber(fields[i])
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
        local old = bubbleState.extraApplied[ship]
        if bubbleState.ready and (not old or old.key ~= key or now >= old.next) then
            bubbleState.extraApplied[ship] = { key = key, next = now + 1 }
            AddUITriggeredEvent(
                'X4LAN_Bubble',
                'ApplyExtra',
                { ship = ship, hull = v[1], shield = v[2], cargo = v[3], dock = v[4] }
            )
        end
        return true
    elseif fields[1] == 'FIRE' then
        if #fields ~= 5 or not tonumber(fields[5]) then
            return true
        end
        local value = tonumber(fields[5])
        if bubbleState.fireOwner ~= fields[4] then
            bubbleState.fireOwner, bubbleState.remoteFire = fields[4], -1
        end
        if value > bubbleState.remoteFire and value > 0 then
            bubbleState.remoteFire = value
            bubbleState.fireDeadline = now + 0.45
            if not bubbleState.firing and bubbleState.ready then
                AddUITriggeredEvent('X4LAN_Bubble', 'FireOn')
                bubbleState.firing = true
            end
        end
        return true
    elseif fields[1] == 'COMMAND' then
        if
            cfg.slot ~= 'p1'
            or #fields ~= 6
            or not fields[4]:match('^[a-f0-9]+$')
            or #fields[4] ~= 32
        then
            return true
        end
        if fields[5] ~= 'p1' and fields[5] ~= 'p2' then
            return true
        end
        if fields[6] ~= 'PICKUP' and fields[6] ~= 'TRADE' then
            return true
        end
        if C.IsGamePaused() then
            return true
        end
        local old = bubbleState.commands[fields[4]]
        if old and old ~= 'PENDING' then
            bubbleState.transport:send('DONE|1|' .. client.lease .. '|' .. fields[4] .. '|' .. old)
        elseif not old and bubbleState.ready then
            if cfg.trading and fields[6] == 'TRADE' then
                bubbleState.commands[fields[4]] = 'REJECTED'
                bubbleState.transport:send(
                    'DONE|1|' .. client.lease .. '|' .. fields[4] .. '|REJECTED'
                )
                return true
            end
            if fields[6] == 'TRADE' then
                bubbleState.tradeSlot = fields[5] == 'p1' and 1 or 2
                bubbleState.tradeRequest = fields[4]
            end
            bubbleState.commands[fields[4]] = 'PENDING' -- Deduplicate before asking the engine to change anything.
            AddUITriggeredEvent(
                'X4LAN_Bubble',
                'Command',
                { request = fields[4], slot = fields[5] == 'p1' and 1 or 2, verb = fields[6] }
            )
        end
        return true
    end
    return false
end
X4LAN_BUBBLE_API = {
    state = bubbleState,
    id = id,
    nonce = nonce,
    paused = function()
        return C.IsGamePaused()
    end,
}
local function frame()
    local now = GetCurRealTime()
    if bubbleState.failed then
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
    if not bubbleState.loaded then
        if C.IsPlayerValid() and now >= (bubbleState.probe or 0) then
            AddUITriggeredEvent('X4LAN', 'Ready')
            bubbleState.probe = now + 1
        end
        return
    end
    if now < (bubbleState.setupAt or 0) then
        return
    end
    if not C.IsPlayerValid() then
        return
    end
    local controlled = C.GetPlayerControlledShipID()
    -- Leaving the pilot seat returns no controlled ship. Keep the session bound
    -- to its original ship; the walking avatar is not a replacement network ship.
    local ship = bubbleState.ship or (controlled ~= 0 and controlled or nil)
    if not ship then
        return
    end -- Initial setup still requires a pilot-controlled ship.
    local operational = C.IsComponentOperational(ship)
    local sector = operational and C.GetContextByClass(ship, 'sector', false) or bubbleState.sector
    if sector == 0 then
        if cfg.coop and bubbleState.sector then
            sector = bubbleState.sector
        else
            return
        end
    end
    if not bubbleState.ship then
        local R = X4LAN_BUBBLE_API.roster
        local own = R.own
        local x = own == 1 and 1000 or -1000
        if cfg.ship_macro then
            -- MD also registers the ships as native sensor sources. SpawnObjectAtPos2
            -- objects report radar range but fail discovery away from the starter ship.
            if not bubbleState.shipRequest then
                bubbleState.shipRequest = { sector = sector, original = ship }
                AddUITriggeredEvent('X4LAN_Bubble', 'PrepareShips', {
                    sector = id(sector),
                    localX = -x,
                    macro = cfg.ship_macro,
                    loadout = cfg.ship_loadout or '',
                    weapons = cfg.ship_weapons or -1,
                    teleport = not cfg.dedicated,
                    count = R.ships,
                    ownIndex = own,
                })
                log(
                    'shared ship requested: '
                        .. cfg.ship_macro
                        .. ' loadout='
                        .. tostring(cfg.ship_loadout)
                )
            end
            if not bubbleState.shipsReady then
                return
            end
            bubbleState.ship, bubbleState.sector, bubbleState.proxy =
                bubbleState.shipRequest.own,
                bubbleState.shipRequest.sector,
                bubbleState.shipRequest.proxy
            -- Native objects of every shared ship by index: own at R.own, the remaining indices
            -- take the proxy and then the extra ships in ascending order.
            local pool = { bubbleState.proxy }
            for k = 1, R.ships - 2 do
                pool[#pool + 1] = bubbleState.shipRequest.extra[k]
            end
            bubbleState.ships = {}
            local n = 0
            for i = 1, R.ships do
                if i == own then
                    bubbleState.ships[i] = bubbleState.ship
                else
                    n = n + 1
                    bubbleState.ships[i] = pool[n]
                end
            end
        else
            bubbleState.ship, bubbleState.sector = ship, sector
            bubbleState.proxy = C.SpawnObjectAtPos2(
                cfg.proxy_macro,
                sector,
                ffi.new('UIPosRot', { x = x, y = 10000, z = 0 }),
                'player'
            )
            assert(
                bubbleState.proxy ~= 0 and C.IsComponentOperational(bubbleState.proxy),
                'bubble proxy spawn failed'
            )
        end
        AddUITriggeredEvent(
            'X4LAN_Bubble',
            cfg.slot == 'p1' and 'SetupHost' or 'SetupGuest',
            id(bubbleState.proxy)
        )
        log('bubble setup requested; slot=' .. cfg.slot)
    end
    local control = controlled == 0 and 'on-foot'
        or (controlled == bubbleState.ship and 'piloting' or 'visiting-other-ship')
    if bubbleState.control ~= control then
        bubbleState.control = control
        log('control=' .. control .. '; syncing original ship=' .. tostring(id(ship)))
    end
    if not bubbleState.ready then
        return
    end
    if X4LAN_BUBBLE_API.coop then
        X4LAN_BUBBLE_API.coop.prepare(now)
    end
    if X4LAN_BUBBLE_API.life then
        X4LAN_BUBBLE_API.life.frame(now)
    end
    if bubbleState.networkBlocked then
        return
    end
    if not bubbleState.client and now >= (bubbleState.retry or 0) then
        local c = {}
        for k, v in pairs(cfg) do
            c[k] = v
        end
        c.sector = GetComponentData(id(bubbleState.sector), 'macro')
        assert(c.sector == 'cluster_14_sector001_macro', 'test bubble requires Argon Prime')
        local ok, transport = pcall(X4LAN_TRANSPORT.open, cfg.host, cfg.port)
        if not ok then
            bubbleState.retry = now + 5
            log('connection unavailable; retrying in 5 seconds: ' .. tostring(transport))
            return
        end
        bubbleState.transport = transport
        bubbleState.client = X4LAN_CLIENT.new(
            c,
            bubbleState.transport,
            bubbleState.pendingNonce or nonce(),
            log,
            bubbleState.resume
        )
        bubbleState.client.receiveExtension = receiveExtension
    end
    if not bubbleState.client then
        return
    end
    local pose
    if not cfg.coop and operational and sector == bubbleState.sector and not C.IsGamePaused() then
        local p = C.GetObjectPositionInSector(ship)
        pose = { p.x, p.y - 10000, p.z, p.yaw, p.pitch, p.roll }
        if math.max(math.abs(pose[1]), math.abs(pose[2]), math.abs(pose[3])) > 50000 then
            pose = nil
        end
    end
    local ok, connected, reason = pcall(bubbleState.client.update, bubbleState.client, now, pose)
    if not ok or not connected then
        closeNetwork()
        if reason == 'initial-handshake-timeout' then
            bubbleState.pendingNonce = nil
        end
        if reason == 'relay-reset' then
            bubbleState.networkBlocked = true
            log('relay session was reset; restart both test worlds to create a new shared economy')
        else
            log('connection interrupted; reconnecting automatically')
        end
        return
    end
    local client = bubbleState.client
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
    if not cfg.coop and client.lease and now >= (bubbleState.nextFire or 0) then
        bubbleState.transport:send('FIRE|1|' .. client.lease .. '|' .. tostring(bubbleState.fire))
        bubbleState.nextFire = now + 0.05
    end
    if
        client.lease
        and cfg.slot == 'p1'
        and bubbleState.world
        and now >= (bubbleState.nextWorld or 0)
    then
        bubbleState.transport:send(
            'WORLD|1|'
                .. client.lease
                .. '|'
                .. bubbleState.worldRevision
                .. '|'
                .. bubbleState.world
        )
        bubbleState.nextWorld = now + 0.1
    end
    if
        client.lease
        and cfg.slot == 'p1'
        and next(bubbleState.extra)
        and now >= (bubbleState.nextExtra or 0)
    then
        bubbleState.nextExtra = now + 0.2
        bubbleState.extraRevision = bubbleState.extraRevision + 1
        for ship = 3, X4LAN_BUBBLE_API.roster.ships do
            if bubbleState.extra[ship] then
                bubbleState.transport:send(
                    'XSHIP|1|'
                        .. client.lease
                        .. '|'
                        .. bubbleState.extraRevision
                        .. '|'
                        .. ship
                        .. '|'
                        .. bubbleState.extra[ship]
                )
            end
        end
    end
    if bubbleState.firing and (now > (bubbleState.fireDeadline or 0) or C.IsGamePaused()) then
        stopRemoteFire()
    end
    local remote = client:render(now)
    local other = cfg.slot == 'p1' and 2 or 1
    if
        not cfg.coop
        and remote
        and not bubbleState.remoteDockLock
        and bubbleState.tradeSlot ~= other
        and (cfg.trading or bubbleState.dock[other] == 0)
        and not C.IsGamePaused()
    then
        if
            C.IsComponentOperational(bubbleState.proxy)
            and controlled ~= bubbleState.proxy
            and C.GetPlayerContainerID() ~= bubbleState.proxy
        then
            local deg = 180 / math.pi
            C.SetObjectSectorPos(
                bubbleState.proxy,
                bubbleState.sector,
                ffi.new('UIPosRot', {
                    x = remote[1],
                    y = 10000 + remote[2],
                    z = remote[3],
                    yaw = remote[4] * deg,
                    pitch = remote[5] * deg,
                    roll = remote[6] * deg,
                })
            )
        end
    end
    if now - bubbleState.lastLog >= 5 then
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
                .. tostring(
                    cfg.slot == 'p1' and bubbleState.worldRevision or bubbleState.remoteRevision
                )
        )
        bubbleState.lastLog = now
    end
end

-- Looked up per frame so the opt-in frame profiler can wrap it; nothing else replaces it.
X4LAN_BUBBLE_API.frame = frame
local function safeFrame()
    local ok, e = pcall(X4LAN_BUBBLE_API.frame)
    if not ok then
        log('BUBBLE STOPPED: ' .. tostring(e))
        pcall(closeNetwork)
        bubbleState.failed = true
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
    bubbleState.active = true
    RegisterEvent('X4LAN.Loaded', function()
        if not bubbleState.ship then
            bubbleState.loaded = true
            bubbleState.setupAt = bubbleState.setupAt or GetCurRealTime() + 10
        end
    end)
    RegisterEvent('X4LAN_Bubble.Ready', function()
        bubbleState.ready = true
        log('bubble engine ready')
    end)
    for _, role in ipairs({ 'Own', 'Proxy' }) do
        local field = role:lower()
        RegisterEvent('X4LAN_Bubble.Prepared' .. role, function(_, object)
            if
                bubbleState.shipRequest
                and not bubbleState.failed
                and not bubbleState.shipsReady
            then
                bubbleState.shipRequest[field] = ConvertIDTo64Bit(object)
            end
        end)
    end
    -- Ships 3.. of a dedicated N-player session (proxies of further clients).
    for k = 1, X4LAN_BUBBLE_API.roster.ships - 2 do
        RegisterEvent('X4LAN_Bubble.PreparedExtra.' .. k, function(_, object)
            if
                bubbleState.shipRequest
                and not bubbleState.failed
                and not bubbleState.shipsReady
            then
                bubbleState.shipRequest.extra = bubbleState.shipRequest.extra or {}
                bubbleState.shipRequest.extra[k] = ConvertIDTo64Bit(object)
            end
        end)
    end
    RegisterEvent('X4LAN_Bubble.ShipsReady', function()
        local r = bubbleState.shipRequest
        if not r or bubbleState.failed or bubbleState.shipsReady then
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
            bubbleState.failed = true
            log('BUBBLE STOPPED: incomplete native shared ship preparation')
            return
        end
        bubbleState.shipsReady = true
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
            bubbleState.extra[ship] = rest
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
        bubbleState.failed = true
    end)
    RegisterEvent('X4LAN_Bubble.Fire', function()
        bubbleState.fire = bubbleState.fire + 1
    end)
    RegisterEvent('X4LAN_Bubble.World', function(_, payload)
        if cfg.slot ~= 'p1' then
            return
        end
        bubbleState.worldRevision = bubbleState.worldRevision + 1
        bubbleState.world = payload
        local v = {}
        for value in payload:gmatch('[^|]+') do
            v[#v + 1] = tonumber(value)
        end
        bubbleState.dock = { v[14] or 0, v[15] or 0 }
    end)
    RegisterEvent('X4LAN_Bubble.TradeFinished', function(_, slot)
        bubbleState.tradeSlot = nil
        AddUITriggeredEvent('X4LAN_Bubble', 'TradeFinished', slot)
    end)
    RegisterEvent('X4LAN_Bubble.Result', function(_, payload)
        local request, result = payload:match('^([a-f0-9]+)|([A-Z]+)$')
        if request and bubbleState.commands[request] == 'PENDING' then
            if bubbleState.tradeRequest == request then
                bubbleState.tradeSlot, bubbleState.tradeRequest = nil, nil
            end
            bubbleState.commands[request] = result
            if bubbleState.client and bubbleState.client.lease then
                bubbleState.transport:send(
                    'DONE|1|' .. bubbleState.client.lease .. '|' .. request .. '|' .. result
                )
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
