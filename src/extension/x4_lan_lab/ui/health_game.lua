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
local A = X4LAN_BUBBLE_API
local cfg = X4LAN_CONFIG
if not cfg or not cfg.coop or not cfg.pilot_health or not A or not A.coop or not A.state.active then
    return
end
local c = A.coop
local s = A.state
local h = {
    seq = 0,
    valid = {},
    gen = { 0, 0 },
    last = { 0, 0 },
    closed = { 0, 0 },
    stale = { 0, 0 },
    remote = {},
    auth = { { 0, false }, { 0, false } },
}
A.health = h
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
    if s.client and s.client.lease then
        s.transport:send(v)
    end
end
local function paused()
    return A.paused and A.paused()
end
function h.mine(e)
    return c.owners[e] == cfg.slot and c.pilots[e] == cfg.slot and c.epochs[e] ~= nil
end
-- Only from a fresh readback (MD running and unpaused), never while paused here.
local function writable()
    return h.mdStamp and (h.now or 0) - h.mdAt <= READING and not paused()
end
-- The MD freshness window of a queued write: the readback's wall second and game-age window.
local function window(args)
    args.stamp, args.from, args.deadline = h.mdStamp, h.mdAge, h.mdAge + TTL
    return args
end

-- Guest: MD keeps the authority it may write under; Base and the WORLD guards check it when they
-- execute, so a queued write of an older or ended generation never lands.
-- own=true carries the window of the current readback; MD refuses a stale one (self-healing:
-- without own, readbacks report gen 0, no frames go out and the relay ends the generation).
local function authority(e, gen, own)
    local a = h.auth[e]
    if a[1] ~= gen or a[2] ~= own then
        local args = { entity = e, gen = gen, own = own, ended = gen == 0 and a[1] or nil } -- a revocation names its generation
        h.auth[e] = { gen, own }
        if own and h.mdStamp then
            args.stamp, args.from, args.deadline = h.mdStamp, h.mdAge, h.mdAge + TTL
        end
        AddUITriggeredEvent('X4LAN_Health', 'Authority', args)
    end
end

-- End this machine's use of every generation up to gen for good (guest: its own; host: the
-- remote pilot's). The host always tells MD, even after a stale release removed the floor, so a
-- delayed queued Apply of that generation is refused natively too.
function h.close(e, gen)
    if gen <= h.closed[e] then
        return
    end
    h.closed[e] = gen
    if cfg.slot == 'p1' then
        if h.remote[e] and h.remote[e].gen <= gen then
            h.remote[e] = nil
        end
        AddUITriggeredEvent('X4LAN_Health', 'Release', { entity = e, gen = gen, closed = true })
        return
    end
    for _, k in ipairs({ 'live', 'basing' }) do
        if h[k] and h[k].entity == e and h[k].gen <= h.closed[e] then
            h[k] = nil
            authority(e, 0, false)
        end
    end
end

local function valid(e, gen)
    return gen == h.valid[e] and gen > h.closed[e]
end

-- A failure ends this module for the session: every generation it used is closed at once
-- (guest: revocation, MD authority 0 and no WORLD guard; host: closed Release), and nothing is
-- adopted, acknowledged, reported or applied again. Ordinary WORLD fallback continues.
function h.fail(err)
    if h.failed then
        return
    end
    h.failed = true
    log('HEALTH STOPPED ' .. tostring(err))
    for e = 1, 2 do
        local used = math.max(h.gen[e], h.valid[e] or 0, h.remote[e] and h.remote[e].gen or 0)
        for _, k in ipairs({ 'live', 'basing' }) do
            if h[k] and h[k].entity == e then
                used = math.max(used, h[k].gen)
            end
        end
        if used > 0 then
            pcall(h.close, e, used)
        end
        pcall(authority, e, 0, false)
    end
    h.live, h.basing, h.remote, h.valid = nil, nil, {}, {}
end

-- Pilot: adopt the current generation's baseline once; acknowledge every repeat of it.
function h.base(f, now)
    if cfg.slot == 'p1' or #f ~= 7 or not s.client or f[3] ~= s.client.lease then
        return
    end
    local e, gen, hu, sh =
        integer(f[4], 2), integer(f[5], 2147483647), number(f[6], 100), number(f[7], 100)
    if not e or e < 1 or not gen or not hu or not sh then
        return
    end
    if not valid(e, gen) or not h.mine(e) then
        return
    end
    local live = h.live
    if live and live.entity == e and live.gen == gen then
        if now >= (h.nextAck or 0) then
            h.nextAck = now + 0.25
            send(string.format('HACK|1|%s|%d|%d', s.client.lease, e, gen))
        end
        return
    end
    -- An expired or refused Base never confirms: retry it, bounded by RETRY. MD applies a
    -- generation's baseline once and only confirms repeats, so a retry never resets HP again.
    local b = h.basing
    if (b and b.entity == e and b.gen == gen and now - b.at < RETRY) or not writable() then
        return
    end
    if live then
        authority(live.entity, 0, false)
    end
    h.live, h.basing = nil, { entity = e, gen = gen, at = now }
    authority(e, gen, false)
    AddUITriggeredEvent(
        'X4LAN_Health',
        'Base',
        window({ entity = e, gen = gen, hull = hu, shield = sh })
    )
end

-- MD 'entity|gen' once it wrote that baseline on the occupied ship.
function h.based(payload)
    if h.failed then
        return
    end
    local e, gen = tostring(payload):match('^(%d)|(%d+)$')
    e, gen = tonumber(e), tonumber(gen)
    local b = h.basing
    if b and b.entity == e and b.gen == gen and valid(e, gen) and h.mine(e) then
        h.live, h.basing, h.reading, h.seq = b, nil, nil, 0
        authority(e, gen, true)
        -- Acknowledge at once; every repeated HBASE of this generation re-sends it (bounded 0.25 s).
        -- No HEALTH goes out before this first HACK.
        if s.client and s.client.lease then
            send(string.format('HACK|1|%s|%d|%d', s.client.lease, e, gen))
            h.nextAck = (h.now or 0) + 0.25
        end
        log(string.format('HEALTH_LIVE entity=%d gen=%d', e, gen))
    end
end

-- MD readback at 10 Hz while unpaused: 'gameSeconds|wallSecond|entity|gen|hull%|shield%'.
-- entity 0 = not in a co-op ship; gen is the authority MD wrote under (0 until own), so a
-- reading never crosses a baseline.
function h.own(payload)
    if h.failed then
        return false
    end
    local age, stamp, e, gen, hu, sh =
        tostring(payload):match('^([^|]+)|([%d%-T:]+)|(%d)|(%d+)|([^|]+)|([^|]+)$')
    age, e, gen, hu, sh =
        number(age, 1e12), integer(e, 2), tonumber(gen), number(hu, 100), number(sh, 100)
    if not age or #stamp ~= 19 then
        return false
    end
    h.mdAge, h.mdStamp, h.mdAt = age, stamp, h.now or 0
    local live = h.live
    if not e or e < 1 or not hu or not sh or not live or live.entity ~= e or live.gen ~= gen then
        return false
    end
    local old = h.reading
    h.reading = { entity = e, hull = hu, shield = sh, at = h.now or 0 }
    if old and (hu < old.hull or sh < old.shield) then
        h.urgent = true
    end
    return true
end

function h.receive(f, now)
    if h.failed then
        return f[1] == 'HGEN' or f[1] == 'HBASE' or f[1] == 'HEALTH'
    end -- consumed, never acted on
    if f[1] == 'HGEN' then
        local rev = integer(f[4], 2147483647)
        if #f == 6 and s.client and f[3] == s.client.lease and rev and rev == c.revision then
            for e = 1, 2 do
                local gen = integer(f[4 + e], 2147483647)
                if gen and gen > h.closed[e] and gen > (h.valid[e] or 0) then
                    if h.valid[e] then
                        h.close(e, gen - 1)
                    end -- a newer generation ends every older one
                    h.valid[e] = gen
                end
            end
        end
        return true
    end
    if f[1] == 'HBASE' then
        h.base(f, now)
        return true
    end
    if f[1] ~= 'HEALTH' then
        return false
    end
    -- Host: HEALTH|1|hostLease|sourceLease|entity|gen|seq|hull%|shield%
    if cfg.slot ~= 'p1' or #f ~= 9 or not hex(f[3]) or not hex(f[4]) then
        return true
    end
    local e, gen, seq = integer(f[5], 2), integer(f[6], 2147483647), integer(f[7], 2147483647)
    local hu, sh = number(f[8], 100), number(f[9], 100)
    if not e or e < 1 or not gen or not seq or not hu or not sh then
        return true
    end
    local owner = c.owners[e]
    if not valid(e, gen) or not owner or owner == cfg.slot or c.pilots[e] ~= owner then
        return true
    end
    if gen == h.gen[e] and seq <= h.last[e] then
        return true
    end
    if not writable() then
        return true
    end -- a later frame carries the newer value
    h.gen[e], h.last[e] = gen, seq
    h.remote[e] = { gen = gen, at = now }
    AddUITriggeredEvent(
        'X4LAN_Health',
        'Apply',
        window({ entity = e, gen = gen, hull = hu, shield = sh })
    )
    return true
end

function h.frame(now)
    if cfg.slot == 'p1' then
        -- A stale pilot stream is an authority boundary: close the generation (the ship returns to the
        -- host from its canonical value) and tell the relay, which starts a new generation whose
        -- baseline is the host's WORLD again; the old one never resumes.
        for e, r in pairs(h.remote) do
            if now - r.at > FRESH then
                h.stale[e] = r.gen
                h.close(e, r.gen)
            end
        end
        for e = 1, 2 do
            if h.stale[e] > 0 and h.stale[e] == h.valid[e] and now >= (h.nextStale or 0) then
                h.nextStale = now + 0.25
                send(string.format('HSTALE|1|%s|%d|%d', s.client.lease, e, h.stale[e]))
            end
        end
        return
    end
    -- Guest keepalive: MD drops the WORLD guard of its ship without it (hung or stopped Lua).
    if h.live and now >= (h.nextKeep or 0) then
        h.nextKeep = now + 0.5
        AddUITriggeredEvent('X4LAN_Health', 'Keep', { entity = h.live.entity, gen = h.live.gen })
    end
    -- Each fresh MD readback is reported at most once; nothing while paused or from a stale one.
    local r = h.reading
    if
        not h.live
        or not h.nextAck
        or not r
        or r.sent
        or r.entity ~= h.live.entity
        or paused()
        or now - r.at > READING
    then
        return
    end
    if not h.urgent and now < (h.nextSend or 0) then
        return
    end
    r.sent, h.urgent, h.nextSend, h.seq = true, false, now + INTERVAL, h.seq + 1
    send(
        string.format(
            'HEALTH|1|%s|%d|%d|%d|%.3f|%.3f',
            s.client.lease,
            r.entity,
            h.live.gen,
            h.seq,
            r.hull,
            r.shield
        )
    )
end

-- Any accepted AUTH transition of a ship (owner, epoch or pilot) ends its known generations at
-- once, before any later frame is handled; frame sampling alone could miss a leave/re-enter.
local receive, frame = c.receive, c.frame
local function key(e)
    return tostring(c.owners[e]) .. '|' .. tostring(c.epochs[e]) .. '|' .. tostring(c.pilots[e])
end
function c.receive(f, now)
    local ok, handled = pcall(h.receive, f, now)
    if not ok then
        h.fail(handled)
        handled = f[1] == 'HGEN' or f[1] == 'HBASE' or f[1] == 'HEALTH'
    end
    if handled then
        return true
    end
    if f[1] ~= 'AUTH' or h.failed then
        return receive(f, now)
    end
    local before = { key(1), key(2) }
    local result = receive(f, now)
    for e = 1, 2 do
        if before[e] ~= key(e) then
            -- Close every generation used so far; the next valid one comes with an HGEN of this revision.
            local used = math.max(h.gen[e], h.valid[e] or 0, h.remote[e] and h.remote[e].gen or 0)
            for _, k in ipairs({ 'live', 'basing' }) do
                if h[k] and h[k].entity == e then
                    used = math.max(used, h[k].gen)
                end
            end
            h.valid[e] = nil
            h.close(e, used)
        end
    end
    return result
end
function c.frame(now)
    frame(now)
    h.now = now
    if c.ready and not h.setup then
        h.setup = true
        AddUITriggeredEvent('X4LAN_Health', 'Setup')
    end
    if h.ready and not h.failed then
        local ok, err = pcall(h.frame, now)
        if not ok then
            h.fail(err)
        end
    end
end
RegisterEvent('X4LAN_Health.Ready', function()
    h.ready = true
    log('HEALTH_READY slot=' .. cfg.slot)
end)
RegisterEvent('X4LAN_Health.Own', function(_, payload)
    if h.ready then
        h.own(payload)
    end
end)
RegisterEvent('X4LAN_Health.Based', function(_, payload)
    if h.ready then
        h.based(payload)
    end
end)
-- An MD watchdog closed a generation natively (host: no applied value for 2 s; guest: no keepalive):
-- close it here too; the host reports it so the relay starts a new generation from its WORLD.
RegisterEvent('X4LAN_Health.Expired', function(_, payload)
    local e, gen = tostring(payload):match('^(%d)|(%d+)$')
    e, gen = tonumber(e), tonumber(gen)
    if not h.ready or h.failed or not e or e < 1 or e > 2 or not gen or gen < 1 then
        return
    end
    if cfg.slot == 'p1' then
        h.stale[e] = gen
    end
    h.close(e, gen)
end)
