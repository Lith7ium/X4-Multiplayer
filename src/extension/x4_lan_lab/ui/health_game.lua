-- Victim-pilot health authority (HEALTH), dev scope: reports/VICTIM-PILOT-HEALTH-DESIGN.txt.
-- The machine that owns AND pilots a co-op ship reports that ship's own hull/shield; every hit
-- it takes in that pilot's world counts, so no attacker attribution is needed. The host sets
-- the reported values on its proxy (ordinary 1% hull floor); while a pilot value is live the
-- life tick reads that canonical value instead of the proxy hull, so host-world damage on the
-- proxy never decides death and a reported lethal value makes the existing life tick declare it.
-- The relay keeps one generation per (owner, epoch, pilot) of a ship and announces the current
-- ones with its AUTH revision in every snapshot. Every such change bumps that revision, so a
-- generation is valid here only once an HGEN of the AUTH revision this machine holds names it.
-- The pilot first adopts the host's canonical WORLD baseline of its generation, acknowledges it,
-- and only then reports:
--  HGEN|1|lease|authRevision|gen1|gen2           relay -> both peers (current generations)
--  HBASE|1|lease|entity|gen|hull%|shield%        relay -> pilot (repeated until acknowledged)
--  HACK|1|lease|entity|gen                       pilot -> relay
--  HEALTH|1|lease|entity|gen|seq|hull%|shield%   pilot -> relay -> host (with the source lease)
-- A generation ends for good on any accepted AUTH transition of its ship or when a newer one is
-- valid; nothing of it is adopted or applied again. A frame that overtook its AUTH is ignored
-- (its sender repeats it), so reordering delays a generation but never revives or stalls one.
-- Queued MD writes carry their generation plus the wall-clock second and game-age window of the
-- MD readback they were derived from (MD's own player.systemtime, as x4lan_attachment.xml); MD
-- executes them only unpaused, in that same second and window, so nothing queued before a pause
-- or a stall lands after it. A refused write is simply issued again from a fresh readback.
local API = X4LAN_BUBBLE_API
local cfg = X4LAN_CONFIG
if
    not cfg
    or not cfg.coop
    or not cfg.pilot_health
    or not API
    or not API.coop
    or not API.state.active
then
    return
end
local coopAdapter = API.coop
local sessionState = API.state
local healthAdapter = {
    seq = 0,
    valid = {},
    gen = { 0, 0 },
    last = { 0, 0 },
    closed = { 0, 0 },
    stale = { 0, 0 },
    remote = {},
    auth = { { 0, false }, { 0, false } },
}
API.health = healthAdapter
-- FRESH: a pilot stream older than this closes the generation. On the dedicated Internet server
-- (UI-paced networking, ~20 Hz) 1 s flapped, re-granting authority every few seconds; LAN keeps 1 s.
local INTERVAL, FRESH, TTL, READING, RETRY =
    0.1, (cfg.dedicated_session and 3.0 or 1.0), 1.0, 0.5, 0.5
local function log(v)
    DebugError('[X4LAN] ' .. v)
end

local function number(v, max)
    local n = tonumber(v)
    if not n or n ~= n or n < 0 or n > max then
        return nil
    end
    return n
end

local function integer(v, max)
    local n = number(v, max)
    if n and n % 1 == 0 then
        return n
    end
end

local function hex(v)
    return type(v) == 'string' and #v == 32 and v:match('^[a-f0-9]+$') ~= nil
end

local function send(v)
    if sessionState.client and sessionState.client.lease then
        sessionState.transport:send(v)
    end
end

local function paused()
    return API.paused and API.paused()
end

function healthAdapter.mine(entity)
    return coopAdapter.owners[entity] == cfg.slot
        and coopAdapter.pilots[entity] == cfg.slot
        and coopAdapter.epochs[entity] ~= nil
end

-- Only from a fresh readback (MD running and unpaused), never while paused here.
local function writable()
    return healthAdapter.mdStamp
        and (healthAdapter.now or 0) - healthAdapter.mdAt <= READING
        and not paused()
end

-- The MD freshness window of a queued write: the readback's wall second and game-age window.
local function window(args)
    args.stamp, args.from, args.deadline =
        healthAdapter.mdStamp, healthAdapter.mdAge, healthAdapter.mdAge + TTL
    return args
end

-- Guest: MD keeps the authority it may write under; Base and the WORLD guards check it when they
-- execute, so a queued write of an older or ended generation never lands.
-- own=true carries the window of the current readback; MD refuses a stale one (self-healing:
-- without own, readbacks report gen 0, no frames go out and the relay ends the generation).
local function authority(entity, generation, own)
    local authorityState = healthAdapter.auth[entity]
    if authorityState[1] ~= generation or authorityState[2] ~= own then
        local args = {
            entity = entity,
            gen = generation,
            own = own,
            ended = generation == 0 and authorityState[1] or nil,
        } -- a revocation names its generation
        healthAdapter.auth[entity] = { generation, own }
        if own and healthAdapter.mdStamp then
            args.stamp, args.from, args.deadline =
                healthAdapter.mdStamp, healthAdapter.mdAge, healthAdapter.mdAge + TTL
        end
        AddUITriggeredEvent('X4LAN_Health', 'Authority', args)
    end
end

-- End this machine's use of every generation up to gen for good (guest: its own; host: the
-- remote pilot's). The host always tells MD, even after a stale release removed the floor, so a
-- delayed queued Apply of that generation is refused natively too.
function healthAdapter.close(entity, generation)
    if generation <= healthAdapter.closed[entity] then
        return
    end
    healthAdapter.closed[entity] = generation
    if cfg.slot == 'p1' then
        if healthAdapter.remote[entity] and healthAdapter.remote[entity].gen <= generation then
            healthAdapter.remote[entity] = nil
        end
        AddUITriggeredEvent(
            'X4LAN_Health',
            'Release',
            { entity = entity, gen = generation, closed = true }
        )
        return
    end
    for _, k in ipairs({ 'live', 'basing' }) do
        if
            healthAdapter[k]
            and healthAdapter[k].entity == entity
            and healthAdapter[k].gen <= healthAdapter.closed[entity]
        then
            healthAdapter[k] = nil
            authority(entity, 0, false)
        end
    end
end

local function valid(entity, generation)
    return generation == healthAdapter.valid[entity] and generation > healthAdapter.closed[entity]
end

-- A failure ends this module for the session: every generation it used is closed at once
-- (guest: revocation, MD authority 0 and no WORLD guard; host: closed Release), and nothing is
-- adopted, acknowledged, reported or applied again. Ordinary WORLD fallback continues.
function healthAdapter.fail(err)
    if healthAdapter.failed then
        return
    end
    healthAdapter.failed = true
    log('HEALTH STOPPED ' .. tostring(err))
    for entity = 1, 2 do
        local used = math.max(
            healthAdapter.gen[entity],
            healthAdapter.valid[entity] or 0,
            healthAdapter.remote[entity] and healthAdapter.remote[entity].gen or 0
        )
        for _, k in ipairs({ 'live', 'basing' }) do
            if healthAdapter[k] and healthAdapter[k].entity == entity then
                used = math.max(used, healthAdapter[k].gen)
            end
        end
        if used > 0 then
            pcall(healthAdapter.close, entity, used)
        end
        pcall(authority, entity, 0, false)
    end
    healthAdapter.live, healthAdapter.basing, healthAdapter.remote, healthAdapter.valid =
        nil, nil, {}, {}
end

-- Pilot: adopt the current generation's baseline once; acknowledge every repeat of it.
function healthAdapter.base(fields, now)
    if
        cfg.slot == 'p1'
        or #fields ~= 7
        or not sessionState.client
        or fields[3] ~= sessionState.client.lease
    then
        return
    end
    local entity, generation, hull, shield =
        integer(fields[4], 2),
        integer(fields[5], 2147483647),
        number(fields[6], 100),
        number(fields[7], 100)
    if not entity or entity < 1 or not generation or not hull or not shield then
        return
    end
    if not valid(entity, generation) or not healthAdapter.mine(entity) then
        return
    end
    local live = healthAdapter.live
    if live and live.entity == entity and live.gen == generation then
        if now >= (healthAdapter.nextAck or 0) then
            healthAdapter.nextAck = now + 0.25
            send(string.format('HACK|1|%s|%d|%d', sessionState.client.lease, entity, generation))
        end
        return
    end
    -- An expired or refused Base never confirms: retry it, bounded by RETRY. MD applies a
    -- generation's baseline once and only confirms repeats, so a retry never resets HP again.
    local baseline = healthAdapter.basing
    if
        (
            baseline
            and baseline.entity == entity
            and baseline.gen == generation
            and now - baseline.at < RETRY
        ) or not writable()
    then
        return
    end
    if live then
        authority(live.entity, 0, false)
    end
    healthAdapter.live, healthAdapter.basing = nil, { entity = entity, gen = generation, at = now }
    authority(entity, generation, false)
    AddUITriggeredEvent(
        'X4LAN_Health',
        'Base',
        window({ entity = entity, gen = generation, hull = hull, shield = shield })
    )
end

-- MD 'entity|gen' once it wrote that baseline on the occupied ship.
function healthAdapter.based(payload)
    if healthAdapter.failed then
        return
    end
    local entity, generation = tostring(payload):match('^(%d)|(%d+)$')
    entity, generation = tonumber(entity), tonumber(generation)
    local baseline = healthAdapter.basing
    if
        baseline
        and baseline.entity == entity
        and baseline.gen == generation
        and valid(entity, generation)
        and healthAdapter.mine(entity)
    then
        healthAdapter.live, healthAdapter.basing, healthAdapter.reading, healthAdapter.seq =
            baseline, nil, nil, 0
        authority(entity, generation, true)
        -- Acknowledge at once; every repeated HBASE of this generation re-sends it (bounded 0.25 s).
        -- No HEALTH goes out before this first HACK.
        if sessionState.client and sessionState.client.lease then
            send(string.format('HACK|1|%s|%d|%d', sessionState.client.lease, entity, generation))
            healthAdapter.nextAck = (healthAdapter.now or 0) + 0.25
        end
        log(string.format('HEALTH_LIVE entity=%d gen=%d', entity, generation))
    end
end

-- MD readback at 10 Hz while unpaused: 'gameSeconds|wallSecond|entity|gen|hull%|shield%'.
-- entity 0 = not in a co-op ship; gen is the authority MD wrote under (0 until own), so a
-- reading never crosses a baseline.
function healthAdapter.own(payload)
    if healthAdapter.failed then
        return false
    end
    local age, stamp, entity, generation, hull, shield =
        tostring(payload):match('^([^|]+)|([%d%-T:]+)|(%d)|(%d+)|([^|]+)|([^|]+)$')
    age, entity, generation, hull, shield =
        number(age, 1e12),
        integer(entity, 2),
        tonumber(generation),
        number(hull, 100),
        number(shield, 100)
    if not age or #stamp ~= 19 then
        return false
    end
    healthAdapter.mdAge, healthAdapter.mdStamp, healthAdapter.mdAt =
        age, stamp, healthAdapter.now or 0
    local live = healthAdapter.live
    if
        not entity
        or entity < 1
        or not hull
        or not shield
        or not live
        or live.entity ~= entity
        or live.gen ~= generation
    then
        return false
    end
    local old = healthAdapter.reading
    healthAdapter.reading =
        { entity = entity, hull = hull, shield = shield, at = healthAdapter.now or 0 }
    if old and (hull < old.hull or shield < old.shield) then
        healthAdapter.urgent = true
    end
    return true
end

function healthAdapter.receive(fields, now)
    if healthAdapter.failed then
        return fields[1] == 'HGEN' or fields[1] == 'HBASE' or fields[1] == 'HEALTH'
    end -- consumed, never acted on
    if fields[1] == 'HGEN' then
        local rev = integer(fields[4], 2147483647)
        if
            #fields == 6
            and sessionState.client
            and fields[3] == sessionState.client.lease
            and rev
            and rev == coopAdapter.revision
        then
            for entity = 1, 2 do
                local generation = integer(fields[4 + entity], 2147483647)
                if
                    generation
                    and generation > healthAdapter.closed[entity]
                    and generation > (healthAdapter.valid[entity] or 0)
                then
                    if healthAdapter.valid[entity] then
                        healthAdapter.close(entity, generation - 1)
                    end -- a newer generation ends every older one
                    healthAdapter.valid[entity] = generation
                end
            end
        end
        return true
    end
    if fields[1] == 'HBASE' then
        healthAdapter.base(fields, now)
        return true
    end
    if fields[1] ~= 'HEALTH' then
        return false
    end
    -- Host: HEALTH|1|hostLease|sourceLease|entity|gen|seq|hull%|shield%
    if cfg.slot ~= 'p1' or #fields ~= 9 or not hex(fields[3]) or not hex(fields[4]) then
        return true
    end
    local entity, generation, seq =
        integer(fields[5], 2), integer(fields[6], 2147483647), integer(fields[7], 2147483647)
    local hull, shield = number(fields[8], 100), number(fields[9], 100)
    if not entity or entity < 1 or not generation or not seq or not hull or not shield then
        return true
    end
    local owner = coopAdapter.owners[entity]
    if
        not valid(entity, generation)
        or not owner
        or owner == cfg.slot
        or coopAdapter.pilots[entity] ~= owner
    then
        return true
    end
    if generation == healthAdapter.gen[entity] and seq <= healthAdapter.last[entity] then
        return true
    end
    if not writable() then
        return true
    end -- a later frame carries the newer value
    healthAdapter.gen[entity], healthAdapter.last[entity] = generation, seq
    healthAdapter.remote[entity] = { gen = generation, at = now }
    AddUITriggeredEvent(
        'X4LAN_Health',
        'Apply',
        window({ entity = entity, gen = generation, hull = hull, shield = shield })
    )
    return true
end

function healthAdapter.frame(now)
    if cfg.slot == 'p1' then
        -- A stale pilot stream is an authority boundary: close the generation (the ship returns to the
        -- host from its canonical value) and tell the relay, which starts a new generation whose
        -- baseline is the host's WORLD again; the old one never resumes.
        for entity, readingState in pairs(healthAdapter.remote) do
            if now - readingState.at > FRESH then
                healthAdapter.stale[entity] = readingState.gen
                healthAdapter.close(entity, readingState.gen)
            end
        end
        for entity = 1, 2 do
            if
                healthAdapter.stale[entity] > 0
                and healthAdapter.stale[entity] == healthAdapter.valid[entity]
                and now >= (healthAdapter.nextStale or 0)
            then
                healthAdapter.nextStale = now + 0.25
                send(
                    string.format(
                        'HSTALE|1|%s|%d|%d',
                        sessionState.client.lease,
                        entity,
                        healthAdapter.stale[entity]
                    )
                )
            end
        end
        return
    end
    -- Guest keepalive: MD drops the WORLD guard of its ship without it (hung or stopped Lua).
    if healthAdapter.live and now >= (healthAdapter.nextKeep or 0) then
        healthAdapter.nextKeep = now + 0.5
        AddUITriggeredEvent(
            'X4LAN_Health',
            'Keep',
            { entity = healthAdapter.live.entity, gen = healthAdapter.live.gen }
        )
    end
    -- Each fresh MD readback is reported at most once; nothing while paused or from a stale one.
    local readingState = healthAdapter.reading
    if
        not healthAdapter.live
        or not healthAdapter.nextAck
        or not readingState
        or readingState.sent
        or readingState.entity ~= healthAdapter.live.entity
        or paused()
        or now - readingState.at > READING
    then
        return
    end
    if not healthAdapter.urgent and now < (healthAdapter.nextSend or 0) then
        return
    end
    readingState.sent, healthAdapter.urgent, healthAdapter.nextSend, healthAdapter.seq =
        true, false, now + INTERVAL, healthAdapter.seq + 1
    send(
        string.format(
            'HEALTH|1|%s|%d|%d|%d|%.3f|%.3f',
            sessionState.client.lease,
            readingState.entity,
            healthAdapter.live.gen,
            healthAdapter.seq,
            readingState.hull,
            readingState.shield
        )
    )
end

-- Any accepted AUTH transition of a ship (owner, epoch or pilot) ends its known generations at
-- once, before any later frame is handled; frame sampling alone could miss a leave/re-enter.
local receive, frame = coopAdapter.receive, coopAdapter.frame
local function key(entity)
    return tostring(coopAdapter.owners[entity])
        .. '|'
        .. tostring(coopAdapter.epochs[entity])
        .. '|'
        .. tostring(coopAdapter.pilots[entity])
end

function coopAdapter.receive(fields, now)
    local ok, handled = pcall(healthAdapter.receive, fields, now)
    if not ok then
        healthAdapter.fail(handled)
        handled = fields[1] == 'HGEN' or fields[1] == 'HBASE' or fields[1] == 'HEALTH'
    end
    if handled then
        return true
    end
    if fields[1] ~= 'AUTH' or healthAdapter.failed then
        return receive(fields, now)
    end
    local before = { key(1), key(2) }
    local result = receive(fields, now)
    for entity = 1, 2 do
        if before[entity] ~= key(entity) then
            -- Close every generation used so far; the next valid one comes with an HGEN of this revision.
            local used = math.max(
                healthAdapter.gen[entity],
                healthAdapter.valid[entity] or 0,
                healthAdapter.remote[entity] and healthAdapter.remote[entity].gen or 0
            )
            for _, k in ipairs({ 'live', 'basing' }) do
                if healthAdapter[k] and healthAdapter[k].entity == entity then
                    used = math.max(used, healthAdapter[k].gen)
                end
            end
            healthAdapter.valid[entity] = nil
            healthAdapter.close(entity, used)
        end
    end
    return result
end

function coopAdapter.frame(now)
    frame(now)
    healthAdapter.now = now
    if coopAdapter.ready and not healthAdapter.setup then
        healthAdapter.setup = true
        AddUITriggeredEvent('X4LAN_Health', 'Setup')
    end
    if healthAdapter.ready and not healthAdapter.failed then
        local ok, err = pcall(healthAdapter.frame, now)
        if not ok then
            healthAdapter.fail(err)
        end
    end
end
RegisterEvent('X4LAN_Health.Ready', function()
    healthAdapter.ready = true
    log('HEALTH_READY slot=' .. cfg.slot)
end)
RegisterEvent('X4LAN_Health.Own', function(_, payload)
    if healthAdapter.ready then
        healthAdapter.own(payload)
    end
end)
RegisterEvent('X4LAN_Health.Based', function(_, payload)
    if healthAdapter.ready then
        healthAdapter.based(payload)
    end
end)
-- An MD watchdog closed a generation natively (host: no applied value for 2 s; guest: no keepalive):
-- close it here too; the host reports it so the relay starts a new generation from its WORLD.
RegisterEvent('X4LAN_Health.Expired', function(_, payload)
    local entity, generation = tostring(payload):match('^(%d)|(%d+)$')
    entity, generation = tonumber(entity), tonumber(generation)
    if
        not healthAdapter.ready
        or healthAdapter.failed
        or not entity
        or entity < 1
        or entity > 2
        or not generation
        or generation < 1
    then
        return
    end
    if cfg.slot == 'p1' then
        healthAdapter.stale[entity] = generation
    end
    healthAdapter.close(entity, generation)
end)
