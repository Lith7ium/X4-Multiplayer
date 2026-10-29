-- Optional saved-bay observations. A matching NAV/CDOCK pair is required before
-- free-flight writes; neither a missing packet nor an unknown dock means FREE.
-- Attachment execution is separate and is not enabled by this observer alone.
local cfg, A = X4LAN_CONFIG, X4LAN_BUBBLE_API
if not cfg or not cfg.coop_docks or not A or not A.coop then
    return
end
local c, s = A.coop, A.state
local C = require('ffi').C
local d = { localRows = {}, remote = {}, published = {}, closed = false }
A.coopDocks = d
local modes = { FREE = true, UNKNOWN = true, APPROACH = true, EXTERNAL = true, STORED = true }
local function integer(v, max)
    local n = tonumber(v)
    if n and n == n and n >= 0 and n % 1 == 0 and n <= max then
        return n
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
local function log(v)
    DebugError('[X4LAN] COOP_DOCK ' .. v)
end
local function send(v)
    if s.client and s.client.lease then
        s.transport:send(v)
    end
end
local function stop(reason)
    d.closed = true
    d.ready = false
    d.localRows = {}
    d.remote = {}
    c.stopVelocity(false)
    log('STOPPED ' .. tostring(reason))
end
local function localRow(e, now)
    local r = d.localRows[e]
    if not d.ready or not r or now - r.at > 0.3 or r.ship ~= c.ships[e] then
        return nil
    end
    -- A native attachment can change between the MD read and this render frame.
    local docked = GetComponentData(A.id(c.ships[e]), 'isdocked') == true
    if (r.mode == 'FREE' or r.mode == 'APPROACH') and docked then
        return nil
    end
    if (r.mode == 'EXTERNAL' or r.mode == 'STORED') and not docked then
        return nil
    end
    return r
end
d.localRow = localRow
d.stop = stop
function d.intent(e, now)
    local r, q = c.remote[e], d.remote[e]
    if
        d.closed
        or not d.ready
        or not r
        or not q
        or c.owners[e] == cfg.slot
        or C.GetPlayerControlledShipID() == c.ships[e]
        or A.paused()
    then
        return nil
    end
    if
        q.epoch ~= c.epochs[e]
        or q.lease ~= r.sourceLease
        or now - q.at > 0.75
        or q.mode == 'UNKNOWN'
    then
        return nil
    end
    return q
end
function d.goal(e, now)
    local q = d.intent(e, now)
    if q and q.seq == c.remote[e].seq then
        return q
    end
end
function c.publishDock(e, seq, now)
    if not s.client or not s.client.lease then
        return
    end
    local r = localRow(e, now)
    local old = d.published[e]
    local mode, index = r and r.mode or 'UNKNOWN', r and r.dock or 0
    d.published[e] = {
        seq = seq,
        first = old and old.epoch == c.epochs[e] and old.mode == mode and old.dock == index and old.first
            or seq,
        epoch = c.epochs[e],
        mode = mode,
        dock = index,
        at = now,
    }
    send(
        string.format(
            'CDOCK|1|%s|%d|%d|%d|%s|%d',
            s.client.lease,
            e,
            c.epochs[e],
            seq,
            r and r.mode or 'UNKNOWN',
            r and r.dock or 0
        )
    )
end
function c.dockPoseAllowed(e, r, now)
    local q, own = d.goal(e, now), localRow(e, now)
    if not q or not own or not d.authorized or not d.authorized(e, now) then
        return false
    end
    return q.mode == 'FREE' and own.mode == 'FREE'
        or q.mode == 'APPROACH' and own.mode == 'APPROACH' and q.dock == own.dock
end
local receive, frame = c.receive, c.frame
function c.receive(f, now)
    if f[1] ~= 'CDOCK' and f[1] ~= 'CDOCK_DENIED' then
        local result = receive(f, now)
        if f[1] == 'AUTH' then
            for e, q in pairs(d.remote) do
                if c.owners[e] == cfg.slot or q.epoch ~= c.epochs[e] then
                    d.remote[e] = nil
                end
            end
        end
        return result
    end
    if not s.client or f[3] ~= s.client.lease or d.closed then
        return true
    end
    if f[1] == 'CDOCK_DENIED' then
        if #f ~= 7 then
            return true
        end
        local e, epoch, seq, dock =
            integer(f[4], c.count),
            integer(f[5], 2147483647),
            integer(f[6], 2147483647),
            integer(f[7], 250000)
        local published = e and d.published[e]
        if
            e
            and e > 0
            and epoch == c.epochs[e]
            and c.owners[e] == cfg.slot
            and seq
            and dock
            and dock > 0
            and published
            and published.epoch == epoch
            and published.dock == dock
            and seq >= published.first
            and seq <= published.seq
        then
            local old = d.denied
            if old and old.entity == e and old.epoch == epoch and seq <= old.seq then
                return true
            end
            d.denied = { entity = e, epoch = epoch, seq = seq, dock = dock, at = now }
            if not old or old.entity ~= e or old.epoch ~= epoch or old.dock ~= dock then
                log(string.format('RESERVATION_DENIED entity=%d dock=%d', e, dock))
            end
        end
        return true
    end
    if #f ~= 9 or type(f[4]) ~= 'string' or #f[4] ~= 32 or not f[4]:match('^[a-f0-9]+$') then
        return true
    end
    local e, epoch, seq, dock =
        integer(f[5], c.count),
        integer(f[6], 2147483647),
        integer(f[7], 2147483647),
        integer(f[9], 250000)
    if
        not e
        or e < 1
        or not epoch
        or not seq
        or not valid(f[8], dock)
        or c.owners[e] == cfg.slot
        or c.epochs[e] ~= epoch
    then
        return true
    end
    local old = d.remote[e]
    if old and old.epoch == epoch and old.lease == f[4] and seq <= old.seq then
        return true
    end
    d.remote[e] = { epoch = epoch, seq = seq, lease = f[4], mode = f[8], dock = dock, at = now }
    return true
end
local function cancelDenied(now)
    local v = d.denied
    local x = d.execution
    if
        not v
        or not x
        or not x.ready
        or now - v.at > 0.75
        or A.paused()
        or c.owners[v.entity] ~= cfg.slot
        or c.epochs[v.entity] ~= v.epoch
    then
        return
    end
    local own = localRow(v.entity, now)
    local sent = d.published[v.entity]
    if not own or own.dock ~= v.dock or not sent or sent.first > v.seq or sent.dock ~= v.dock then
        return
    end
    if now < (d.nextDenied or 0) then
        return
    end
    d.nextDenied = now + 0.25
    AddUITriggeredEvent(
        'X4LAN_CoopDocks',
        'Denied',
        {
            seed = cfg.seed_id,
            epoch = x.epoch,
            ship = v.entity,
            native = A.id(c.ships[v.entity]),
            generation = x.generation[v.entity],
            dock = v.dock,
            stamp = own.stamp,
            age = own.age,
        }
    )
end
function c.frame(now)
    if c.ready and not d.setup and not d.closed then
        d.setup = true
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
    if d.ready and not A.paused() and now >= (d.nextRead or 0) then
        d.nextRead = now + 0.05
        AddUITriggeredEvent('X4LAN_CoopDocks', 'Read')
    end
    if d.executeFrame then
        d.executeFrame(now)
    end
    cancelDenied(now)
    frame(now)
end
RegisterEvent('X4LAN_CoopDocks.Ready', function(_, id)
    if not d.closed and id == cfg.seed_id then
        d.ready = true
        log('READY')
    end
end)
RegisterEvent('X4LAN_CoopDocks.Error', function(_, reason)
    stop(reason)
end)
RegisterEvent('X4LAN_CoopDocks.Row', function(_, payload)
    if not d.ready or d.closed then
        return
    end
    local age, stamp, e, ship, mode, dock =
        tostring(payload):match('^([^|]+)|([%d%-T:]+)|(%d)|([^|]+)|(%u+)|(%d+)$')
    age, e, dock = tonumber(age), integer(e, c.count), integer(dock, 250000)
    if
        not age
        or age ~= age
        or not stamp
        or #stamp ~= 19
        or not e
        or e < 1
        or not valid(mode, dock)
    then
        return
    end
    local ok, id = pcall(ConvertIDTo64Bit, ship)
    if not ok or id ~= c.ships[e] then
        return
    end
    d.localRows[e] =
        { ship = id, age = age, stamp = stamp, mode = mode, dock = dock, at = GetCurRealTime() }
end)
