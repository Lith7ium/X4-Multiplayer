-- Optional saved-bay observations. A matching NAV/CDOCK pair is required before
-- free-flight writes; neither a missing packet nor an unknown dock means FREE.
-- Attachment execution is separate and is not enabled by this observer alone.
local cfg, API = X4LAN_CONFIG, X4LAN_BUBBLE_API
if not cfg or not cfg.coop_docks or not API or not API.coop then
    return
end
local coopAdapter, sessionState = API.coop, API.state
local C = require('ffi').C
local dockAdapter = { localRows = {}, remote = {}, published = {}, closed = false }
API.coopDocks = dockAdapter
local modes = { FREE = true, UNKNOWN = true, APPROACH = true, EXTERNAL = true, STORED = true }
local function integer(value, max)
    local parsedNumber = tonumber(value)
    if
        parsedNumber
        and parsedNumber == parsedNumber
        and parsedNumber >= 0
        and parsedNumber % 1 == 0
        and parsedNumber <= max
    then
        return parsedNumber
    end
end

local function valid(mode, dock)
    return modes[mode]
        and dock
        and (
            (mode == 'FREE' or mode == 'UNKNOWN') and dock == 0
            or (mode == 'APPROACH' or mode == 'EXTERNAL' or mode == 'STORED') and dock > 0
        )
end

local function log(value)
    DebugError('[X4LAN] COOP_DOCK ' .. value)
end

local function send(value)
    if sessionState.client and sessionState.client.lease then
        sessionState.transport:send(value)
    end
end

local function stop(reason)
    dockAdapter.closed = true
    dockAdapter.ready = false
    dockAdapter.localRows = {}
    dockAdapter.remote = {}
    coopAdapter.stopVelocity(false)
    log('STOPPED ' .. tostring(reason))
end

local function localRow(entity, now)
    local row = dockAdapter.localRows[entity]
    if
        not dockAdapter.ready
        or not row
        or now - row.at > 0.3
        or row.ship ~= coopAdapter.ships[entity]
    then
        return nil
    end
    -- A native attachment can change between the MD read and this render frame.
    local docked = GetComponentData(API.id(coopAdapter.ships[entity]), 'isdocked') == true
    if (row.mode == 'FREE' or row.mode == 'APPROACH') and docked then
        return nil
    end
    if (row.mode == 'EXTERNAL' or row.mode == 'STORED') and not docked then
        return nil
    end
    return row
end
dockAdapter.localRow = localRow
dockAdapter.stop = stop
function dockAdapter.intent(entity, now)
    local row, intent = coopAdapter.remote[entity], dockAdapter.remote[entity]
    if
        dockAdapter.closed
        or not dockAdapter.ready
        or not row
        or not intent
        or coopAdapter.owners[entity] == cfg.slot
        or C.GetPlayerControlledShipID() == coopAdapter.ships[entity]
        or API.paused()
    then
        return nil
    end
    if
        intent.epoch ~= coopAdapter.epochs[entity]
        or intent.lease ~= row.sourceLease
        or now - intent.at > 0.75
        or intent.mode == 'UNKNOWN'
    then
        return nil
    end
    return intent
end

function dockAdapter.goal(entity, now)
    local intent = dockAdapter.intent(entity, now)
    if intent and intent.seq == coopAdapter.remote[entity].seq then
        return intent
    end
end

function coopAdapter.publishDock(entity, seq, now)
    if not sessionState.client or not sessionState.client.lease then
        return
    end
    local row = localRow(entity, now)
    local old = dockAdapter.published[entity]
    local mode, index = row and row.mode or 'UNKNOWN', row and row.dock or 0
    dockAdapter.published[entity] = {
        seq = seq,
        first = old
                and old.epoch == coopAdapter.epochs[entity]
                and old.mode == mode
                and old.dock == index
                and old.first
            or seq,
        epoch = coopAdapter.epochs[entity],
        mode = mode,
        dock = index,
        at = now,
    }
    send(
        string.format(
            'CDOCK|1|%s|%d|%d|%d|%s|%d',
            sessionState.client.lease,
            entity,
            coopAdapter.epochs[entity],
            seq,
            row and row.mode or 'UNKNOWN',
            row and row.dock or 0
        )
    )
end

function coopAdapter.dockPoseAllowed(entity, row, now)
    local intent, own = dockAdapter.goal(entity, now), localRow(entity, now)
    if
        not intent
        or not own
        or not dockAdapter.authorized
        or not dockAdapter.authorized(entity, now)
    then
        return false
    end
    return intent.mode == 'FREE' and own.mode == 'FREE'
        or intent.mode == 'APPROACH' and own.mode == 'APPROACH' and intent.dock == own.dock
end
local receive, frame = coopAdapter.receive, coopAdapter.frame
function coopAdapter.receive(fields, now)
    if fields[1] ~= 'CDOCK' and fields[1] ~= 'CDOCK_DENIED' then
        local result = receive(fields, now)
        if fields[1] == 'AUTH' then
            for entity, intent in pairs(dockAdapter.remote) do
                if
                    coopAdapter.owners[entity] == cfg.slot
                    or intent.epoch ~= coopAdapter.epochs[entity]
                then
                    dockAdapter.remote[entity] = nil
                end
            end
        end
        return result
    end
    if not sessionState.client or fields[3] ~= sessionState.client.lease or dockAdapter.closed then
        return true
    end
    if fields[1] == 'CDOCK_DENIED' then
        if #fields ~= 7 then
            return true
        end
        local entity, epoch, seq, dock =
            integer(fields[4], coopAdapter.count),
            integer(fields[5], 2147483647),
            integer(fields[6], 2147483647),
            integer(fields[7], 250000)
        local published = entity and dockAdapter.published[entity]
        if
            entity
            and entity > 0
            and epoch == coopAdapter.epochs[entity]
            and coopAdapter.owners[entity] == cfg.slot
            and seq
            and dock
            and dock > 0
            and published
            and published.epoch == epoch
            and published.dock == dock
            and seq >= published.first
            and seq <= published.seq
        then
            local old = dockAdapter.denied
            if old and old.entity == entity and old.epoch == epoch and seq <= old.seq then
                return true
            end
            dockAdapter.denied =
                { entity = entity, epoch = epoch, seq = seq, dock = dock, at = now }
            if not old or old.entity ~= entity or old.epoch ~= epoch or old.dock ~= dock then
                log(string.format('RESERVATION_DENIED entity=%d dock=%d', entity, dock))
            end
        end
        return true
    end
    if
        #fields ~= 9
        or type(fields[4]) ~= 'string'
        or #fields[4] ~= 32
        or not fields[4]:match('^[a-f0-9]+$')
    then
        return true
    end
    local entity, epoch, seq, dock =
        integer(fields[5], coopAdapter.count),
        integer(fields[6], 2147483647),
        integer(fields[7], 2147483647),
        integer(fields[9], 250000)
    if
        not entity
        or entity < 1
        or not epoch
        or not seq
        or not valid(fields[8], dock)
        or coopAdapter.owners[entity] == cfg.slot
        or coopAdapter.epochs[entity] ~= epoch
    then
        return true
    end
    local old = dockAdapter.remote[entity]
    if old and old.epoch == epoch and old.lease == fields[4] and seq <= old.seq then
        return true
    end
    dockAdapter.remote[entity] =
        { epoch = epoch, seq = seq, lease = fields[4], mode = fields[8], dock = dock, at = now }
    return true
end

local function cancelDenied(now)
    local value = dockAdapter.denied
    local x = dockAdapter.execution
    if
        not value
        or not x
        or not x.ready
        or now - value.at > 0.75
        or API.paused()
        or coopAdapter.owners[value.entity] ~= cfg.slot
        or coopAdapter.epochs[value.entity] ~= value.epoch
    then
        return
    end
    local own = localRow(value.entity, now)
    local sent = dockAdapter.published[value.entity]
    if
        not own
        or own.dock ~= value.dock
        or not sent
        or sent.first > value.seq
        or sent.dock ~= value.dock
    then
        return
    end
    if now < (dockAdapter.nextDenied or 0) then
        return
    end
    dockAdapter.nextDenied = now + 0.25
    AddUITriggeredEvent('X4LAN_CoopDocks', 'Denied', {
        seed = cfg.seed_id,
        epoch = x.epoch,
        ship = value.entity,
        native = API.id(coopAdapter.ships[value.entity]),
        generation = x.generation[value.entity],
        dock = value.dock,
        stamp = own.stamp,
        age = own.age,
    })
end

function coopAdapter.frame(now)
    if coopAdapter.ready and not dockAdapter.setup and not dockAdapter.closed then
        dockAdapter.setup = true
        if
            type(cfg.seed_id) ~= 'string'
            or #cfg.seed_id ~= 32
            or not cfg.seed_id:match('^[a-f0-9]+$')
        then
            stop('saved world identity is required')
        else
            AddUITriggeredEvent('X4LAN_CoopDocks', 'Setup', cfg.seed_id)
        end
    end
    if dockAdapter.ready and not API.paused() and now >= (dockAdapter.nextRead or 0) then
        dockAdapter.nextRead = now + 0.05
        AddUITriggeredEvent('X4LAN_CoopDocks', 'Read')
    end
    if dockAdapter.executeFrame then
        dockAdapter.executeFrame(now)
    end
    cancelDenied(now)
    frame(now)
end
RegisterEvent('X4LAN_CoopDocks.Ready', function(_, id)
    if not dockAdapter.closed and id == cfg.seed_id then
        dockAdapter.ready = true
        log('READY')
    end
end)
RegisterEvent('X4LAN_CoopDocks.Error', function(_, reason)
    stop(reason)
end)
RegisterEvent('X4LAN_CoopDocks.Row', function(_, payload)
    if not dockAdapter.ready or dockAdapter.closed then
        return
    end
    local age, stamp, entity, ship, mode, dock =
        tostring(payload):match('^([^|]+)|([%d%-T:]+)|(%d)|([^|]+)|(%u+)|(%d+)$')
    age, entity, dock = tonumber(age), integer(entity, coopAdapter.count), integer(dock, 250000)
    if
        not age
        or age ~= age
        or not stamp
        or #stamp ~= 19
        or not entity
        or entity < 1
        or not valid(mode, dock)
    then
        return
    end
    local ok, id = pcall(ConvertIDTo64Bit, ship)
    if not ok or id ~= coopAdapter.ships[entity] then
        return
    end
    dockAdapter.localRows[entity] =
        { ship = id, age = age, stamp = stamp, mode = mode, dock = dock, at = GetCurRealTime() }
end)
