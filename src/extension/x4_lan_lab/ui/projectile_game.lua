-- Guarded observed shots. No native or network
-- work occurs without the complete damage/proof/capture stack and explicit flag.
local A, cfg = X4LAN_BUBBLE_API, X4LAN_CONFIG
if
    not cfg
    or not cfg.projectile_visuals
    or not cfg.shot_visual
    or not cfg.shot_capture
    or not cfg.shot_claims
    or not cfg.pilot_health
    or not cfg.fire2
    or not A
    or not A.coop
    or not A.health
    or not A.shot
    or not A.shotVisual
then
    return
end
local P, W = assert(X4LAN_PROJECTILE_MOTION), assert(X4LAN_PROJECTILE_PROTOCOL)
local ffi = require('ffi')
local C = ffi.C
pcall(ffi.cdef, 'typedef uint64_t UniverseID;')
pcall(ffi.cdef, 'typedef struct { float x,y,z,yaw,pitch,roll; } UIPosRot;')
for _, definition in ipairs({
    'UIPosRot GetPositionalOffset(UniverseID,UniverseID);',
    'bool IsComponentClass(UniverseID,const char*);',
    'UIPosRot GetObjectPositionInSector(UniverseID);',
    'void SetObjectSectorPos(UniverseID,UniverseID,UIPosRot);',
}) do
    pcall(ffi.cdef, definition)
end
local c, h, d, v, s = A.coop, A.health, A.shot, A.shotVisual, A.state
local x = { contexts = {}, slots = {}, captures = {}, serial = 0, shotId = 0, now = 0 }
A.projectiles = x
local KINDS = {
    weapon_ter_s_plasma_01_mk1_macro = 1,
    weapon_gen_s_laser_01_mk1_macro = 2,
    weapon_gen_s_laser_01_mk2_macro = 3,
}
local TTL = { 1.7, 0.7, 0.7 }
local DELAY = 0.15
local function log(t)
    DebugError('[X4LAN] PROJECTILE ' .. t)
end
local function liveBullet(id)
    -- The engine logs an error if IsComponentClass sees an already expired ID.
    return IsValidComponent(A.id(id)) and C.IsComponentClass(id, 'bullet')
end
local function event(control, args)
    AddUITriggeredEvent('X4LAN_Projectile', control, args or {})
end
local function serial()
    x.serial = x.serial + 1
    assert(x.serial <= 2147483647, 'operation exhausted')
    return x.serial
end
local function closePosition(actual, wanted)
    -- Sector = zone + local can round again after the float-native command.
    return math.abs(actual - wanted) <= math.max(0.01, math.abs(wanted) * 2 ^ -21)
end
local function window(a)
    a.revision, a.stamp, a.from, a.deadline = d.stateRevision, h.mdStamp, h.mdAge, h.mdAge + 0.3
    return a
end
local function send(packet)
    if packet and s.client and s.client.lease then
        s.transport:send(packet)
    end
end
local function fresh(now)
    return not x.failed
        and not s.failed
        and not d.failed
        and not h.failed
        and not v.failed
        and s.client
        and s.client.lease
        and h.mdStamp
        and now - (h.mdAt or -100) <= 0.3
        and not A.paused()
end
local function wanted(e, now)
    if not fresh(now) then
        return nil
    end
    local gen, ship, owner = h.valid[e], c.ships[e], c.owners[e]
    local kind = d.support[e] and KINDS[d.support[e].macro]
    if
        not kind
        or not gen
        or gen <= (h.closed[e] or 0)
        or not owner
        or c.pilots[e] ~= owner
        or not ship
        or ship == 0
        or not C.IsComponentOperational(ship)
    then
        return nil
    end
    local sector = C.GetContextByClass(ship, 'sector', false)
    if sector == 0 then
        return nil
    end
    local mode = owner == cfg.slot and 1 or 2
    local lease, sourceNow = s.client.lease, now
    if mode == 1 then
        if C.GetPlayerControlledShipID() ~= ship or not v.readyOriginal(e, gen, now) then
            return nil
        end
    else
        local r = c.remote[e]
        local player = C.GetPlayerContainerID()
        if
            not r
            or not r.track
            or not r.track.offset
            or now - r.at > 0.35
            or not r.sourceLease
            or player == 0
            or C.GetContextByClass(player, 'sector', false) ~= sector
            or not v.readyReplay(e, gen, now)
        then
            return nil
        end
        if r.sector ~= GetComponentData(A.id(sector), 'macro') then
            return nil
        end
        lease, sourceNow = r.sourceLease, now - r.track.offset
    end
    local name = GetComponentData(A.id(sector), 'macro')
    -- A.id creates engine LuaID userdata for MD/API arguments. It is not a string
    -- and may be a new wrapper on every call; key the stable native numeric IDs.
    local key = table.concat(
        { s.client.lease, lease, e, c.epochs[e], gen, tostring(ship), tostring(sector), mode, kind },
        '|'
    )
    return {
        key = key,
        entity = e,
        gen = gen,
        epoch = c.epochs[e],
        kind = kind,
        mode = mode,
        ship = ship,
        sector = sector,
        sectorName = name,
        sourceLease = lease,
        sourceNow = sourceNow,
    }
end
local function remove(slot)
    local r = x.slots[slot]
    if not r then
        return
    end
    event('Remove', { slot = slot, operation = r.operation })
    x.slots[slot] = nil
end
local function revoke(q)
    if not q.enabled then
        return
    end
    q.enabled = false
    q.ticket = serial()
    q.ackAt = nil
    event('Revoke', { entity = q.entity, ticket = q.ticket })
    P.clear(q.buffer, q.sourceNow)
    for slot, r in pairs(x.slots) do
        if r.entity == q.entity then
            remove(slot)
        end
    end
end
local function ready(q, now)
    return q and q.enabled and q.ackAt and now - q.ackAt <= 0.3
end
function x.contextsFrame(now)
    for e = 1, 2 do
        local cur = wanted(e, now)
        local q = x.contexts[e]
        if not cur then
            if q then
                local track = c.remote[e] and c.remote[e].track
                if track and track.offset then
                    q.sourceNow = math.max(q.sourceNow, now - track.offset)
                end
                revoke(q)
            end
        else
            if not q or q.key ~= cur.key then
                if q then
                    revoke(q)
                end
                q = cur
                q.buffer = P.new(q.key, 0)
                q.ticket = serial()
                q.enabled = true
                q.started = now
                x.contexts[e] = q
            else
                q.sourceNow = cur.sourceNow
                if
                    not q.enabled
                    or not q.ackAt and now - q.started > 0.3
                    or q.ackAt and now - q.ackAt > 0.3
                then
                    revoke(q)
                    q.ticket = serial()
                    q.enabled = true
                    q.ackAt = nil
                    q.started = now
                    q.nextState = 0
                end
            end
            if now >= (q.nextState or 0) then
                q.nextState = now + 0.1
                event(
                    'State',
                    window({
                        entity = e,
                        gen = q.gen,
                        ticket = q.ticket,
                        mode = q.mode,
                        kind = q.kind,
                        sector = A.id(q.sector),
                    })
                )
            end
        end
    end
end
function x.state(payload)
    local e, t = tostring(payload):match('^(%d)|(%d+)$')
    e, t = tonumber(e), tonumber(t)
    local q = e and x.contexts[e]
    local cur = e and wanted(e, x.now)
    if q and cur and cur.key == q.key and q.enabled and q.ticket == t then
        q.ackAt = x.now
    end
end
function x.born(payload)
    local e, g, t, id, sector, kind, vx, vy, vz =
        tostring(payload):match('^(%d)|(%d+)|(%d+)|([^|]+)|([^|]+)|(%d)|([^|]+)|([^|]+)|([^|]+)$')
    e, g, t, kind = tonumber(e), tonumber(g), tonumber(t), tonumber(kind)
    local velocity = { tonumber(vx), tonumber(vy), tonumber(vz) }
    for i = 1, 3 do
        local n = velocity[i]
        if not n or n ~= n or math.abs(n) > W.MAX_VELOCITY then
            return
        end
    end
    local q = e and x.contexts[e]
    local cur = e and wanted(e, x.now)
    if
        not ready(q, x.now)
        or not cur
        or cur.key ~= q.key
        or q.mode ~= 1
        or q.gen ~= g
        or q.ticket ~= t
        or q.kind ~= kind
    then
        return
    end
    local native, space = ConvertIDTo64Bit(id), ConvertIDTo64Bit(sector)
    if space ~= q.sector or not liveBullet(native) then
        return
    end
    local key = q.key .. '|' .. id
    local n = 0
    for _ in pairs(x.captures) do
        n = n + 1
    end
    if x.captures[key] or n >= P.capacity then
        return
    end
    x.shotId = x.shotId + 1
    assert(x.shotId <= 2147483647, 'shot IDs exhausted')
    x.captures[key] =
        { native = native, space = space, q = q, ticket = q.ticket, id = x.shotId, velocity = velocity }
end
local function message(b, kind, time)
    local q = b.q
    return {
        type = kind,
        direction = 'client',
        lease = s.client.lease,
        entity = q.entity,
        epoch = q.epoch,
        gen = q.gen,
        id = b.id,
        time = time,
    }
end
function x.captureFrame(now)
    for key, b in pairs(x.captures) do
        local q = b.q
        local cur = wanted(q.entity, now)
        if not cur or cur.key ~= q.key or not ready(q, now) or b.ticket ~= q.ticket then
            x.captures[key] = nil
        else
            if not b.ended then
                if liveBullet(b.native) then
                    local p = C.GetPositionalOffset(b.native, b.space)
                    local sample = { time = now, pose = { p.x, p.y, p.z, p.yaw, p.pitch, p.roll } }
                    b.first = b.first or sample
                    if not b.birth then
                        -- Native velocity includes inherited motion and actual barrel spread.
                        -- Differencing float sector positions amplifies rounding far from origin.
                        local velocity = b.velocity
                        local m = message(b, 'BOLT', b.first.time)
                        local a = b.first.pose
                        m.sector, m.kind, m.x, m.y, m.z, m.vx, m.vy, m.vz, m.yaw, m.pitch, m.roll, m.ttl =
                            q.sectorName,
                            q.kind,
                            a[1],
                            a[2],
                            a[3],
                            velocity[1],
                            velocity[2],
                            velocity[3],
                            a[4],
                            a[5],
                            a[6],
                            TTL[q.kind]
                        b.birth = assert(W.serialize(m))
                        b.birthAt = now
                        b.nextBirth = now
                        b.repeats = 0
                    end
                else
                    b.ended = now
                    if b.birth then
                        b.finish = assert(W.serialize(message(b, 'BEND', now)))
                        b.nextEnd = now
                        b.endRepeats = 0
                    else
                        x.captures[key] = nil
                    end
                end
            end
            if b.birth and not b.ended and b.repeats < 3 and now >= b.nextBirth then
                send(b.birth)
                b.repeats = b.repeats + 1
                b.nextBirth = now + 0.05
            end
            if b.finish and now >= b.nextEnd then
                send(b.finish)
                b.endRepeats = b.endRepeats + 1
                b.nextEnd = now + 0.05
                if b.endRepeats >= 3 then
                    x.captures[key] = nil
                end
            end
            if b.first and now - b.first.time > 3.1 then
                x.captures[key] = nil
            end
        end
    end
end
function x.receive(f, now)
    if f[1] ~= 'BOLT' and f[1] ~= 'BEND' then
        return false
    end
    local m = W.parse(table.concat(f, '|'), 'forwarded')
    local function drop(reason)
        if c.dropped then
            c.dropped(f[1], m and m.entity or '?', reason, now)
        end
        return true
    end
    if not m then
        return drop('malformed')
    end
    if m.lease ~= s.client.lease then
        return drop('lease')
    end
    if not fresh(now) then
        return drop('not-fresh(md/health/paused)')
    end
    local q = x.contexts[m.entity]
    local cur = wanted(m.entity, now)
    if not cur then
        return drop('no-replay-context(gen/support/track/sector)')
    end
    if not ready(q, now) then
        return drop('md-context-not-acknowledged')
    end
    if cur.key ~= q.key then
        return drop('context-changed')
    end
    if q.mode ~= 2 then
        return drop('own-ship')
    end
    if m.source_lease ~= q.sourceLease then
        return drop('source-lease')
    end
    if m.epoch ~= q.epoch then
        return drop('epoch')
    end
    if m.gen ~= q.gen then
        return drop('health-generation ' .. tostring(q.gen) .. '~=' .. tostring(m.gen))
    end
    if m.type == 'BOLT' then
        if m.sector == q.sectorName and m.kind == q.kind then
            P.birth(
                q.buffer,
                q.key,
                {
                    id = m.id,
                    time = m.time,
                    ttl = m.ttl,
                    kind = m.kind,
                    pose = { m.x, m.y, m.z, m.yaw, m.pitch, m.roll },
                    velocity = { m.vx, m.vy, m.vz },
                },
                cur.sourceNow,
                cur.sourceNow - DELAY
            )
        end
    else
        P.finish(q.buffer, q.key, m.id, m.time)
    end
    return true
end
function x.created(payload)
    local e, t, slot, op, id, sector =
        tostring(payload):match('^(%d)|(%d+)|(%d+)|(%d+)|([^|]+)|([^|]+)$')
    e, t, slot, op = tonumber(e), tonumber(t), tonumber(slot), tonumber(op)
    if not e or not slot or slot < 1 or slot > P.capacity then
        return
    end
    local q = x.contexts[e]
    local r = x.slots[slot]
    local cur = wanted(e, x.now)
    if
        not r
        or r.operation ~= op
        or r.entity ~= e
        or not ready(q, x.now)
        or not cur
        or cur.key ~= q.key
        or q.ticket ~= t
        or r.ticket ~= t
    then
        event('Remove', { slot = slot, operation = op })
        return
    end
    local object, space = ConvertIDTo64Bit(id), ConvertIDTo64Bit(sector)
    if space ~= q.sector or not C.IsComponentClass(object, 'object') then
        remove(slot)
        return
    end
    r.object = object
end
function x.rejected(payload)
    local e, t, slot, op = tostring(payload):match('^(%d)|(%d+)|(%d+)|(%d+)$')
    e, t, slot, op = tonumber(e), tonumber(t), tonumber(slot), tonumber(op)
    local r = slot and x.slots[slot]
    if r and r.entity == e and r.ticket == t and r.operation == op and not r.object then
        -- A queued request can cross a freshness boundary. Retry while this exact
        -- observation is still live, without spending most of a short pulse's TTL.
        r.retryAt = x.now + 0.05
    end
end
function x.renderFrame(now)
    local wantedSlots = {}
    for e, q in pairs(x.contexts) do
        if q.mode == 2 and ready(q, now) then
            local cur = wanted(e, now)
            if cur and cur.key == q.key then
                local samples = P.sample(q.buffer, cur.sourceNow - DELAY)
                for id, sample in pairs(samples) do
                    local slot
                    for i, r in pairs(x.slots) do
                        if r.entity == e and r.id == id and r.ticket == q.ticket then
                            slot = i
                            break
                        end
                    end
                    if not slot then
                        for i = 1, P.capacity do
                            if not x.slots[i] then
                                slot = i
                                break
                            end
                        end
                        if slot then
                            local op = serial()
                            local p = sample.pose
                            local record = q.buffer.active[id]
                            x.slots[slot] =
                                { entity = e, id = id, ticket = q.ticket, operation = op, at = now }
                            event(
                                'Create',
                                window({
                                    entity = e,
                                    gen = q.gen,
                                    ticket = q.ticket,
                                    slot = slot,
                                    operation = op,
                                    kind = q.kind,
                                    ttl = math.min(
                                        3,
                                        record.time + record.ttl - (cur.sourceNow - DELAY)
                                    ),
                                    x = p[1],
                                    y = p[2],
                                    z = p[3],
                                    yaw = p[4] * 180 / math.pi,
                                    pitch = p[5] * 180 / math.pi,
                                    roll = p[6] * 180 / math.pi,
                                })
                            )
                        end
                    end
                    if slot then
                        local r = x.slots[slot]
                        wantedSlots[slot] = true
                        if r.object then
                            if
                                C.IsComponentClass(r.object, 'object')
                                and C.GetContextByClass(r.object, 'sector', false) == q.sector
                            then
                                local p = sample.pose
                                local pose = ffi.new(
                                    'UIPosRot',
                                    {
                                        x = p[1],
                                        y = p[2],
                                        z = p[3],
                                        yaw = p[4] * 180 / math.pi,
                                        pitch = p[5] * 180 / math.pi,
                                        roll = p[6] * 180 / math.pi,
                                    }
                                )
                                -- A zone reparent can retain the previous zone's local coordinates on
                                -- the first write. Correct once within this frame, then remove a carrier
                                -- that still disagrees. Never apply this repair to ships or projectiles.
                                local placed = false
                                for attempt = 1, 2 do
                                    C.SetObjectSectorPos(r.object, q.sector, pose)
                                    local actual = C.GetObjectPositionInSector(r.object)
                                    if
                                        closePosition(actual.x, pose.x)
                                        and closePosition(actual.y, pose.y)
                                        and closePosition(actual.z, pose.z)
                                    then
                                        placed = true
                                        break
                                    end
                                end
                                if not placed then
                                    remove(slot)
                                end
                            else
                                remove(slot)
                            end
                        elseif
                            r.retryAt and now >= r.retryAt or not r.retryAt and now - r.at > 0.3
                        then
                            remove(slot)
                        end
                    end
                end
            end
        end
    end
    for slot in pairs(x.slots) do
        if not wantedSlots[slot] then
            remove(slot)
        end
    end
end
function x.fail(err)
    if x.failed then
        return
    end
    x.failed = true
    log('STOPPED ' .. tostring(err))
    x.captures = {}
    pcall(event, 'Stop')
    for _, q in pairs(x.contexts) do
        q.enabled = false
    end
    x.slots = {}
end
local frame, receive = c.frame, c.receive
function c.frame(now)
    frame(now)
    x.now = now
    if d.ready and v.ready and not x.setup then
        x.setup = true
        event('Setup')
    end
    if x.ready and not x.failed then
        local ok, err = pcall(function()
            x.contextsFrame(now)
            x.captureFrame(now)
            x.renderFrame(now)
        end)
        if not ok then
            x.fail(err)
        end
    end
end
function c.receive(f, now)
    local ok, handled = pcall(x.receive, f, now)
    if not ok then
        x.fail(handled)
        handled = f[1] == 'BOLT' or f[1] == 'BEND'
    end
    if handled then
        return true
    end
    return receive(f, now)
end
local function guard(fn)
    return function(_, p)
        local ok, err = pcall(fn, p)
        if not ok then
            x.fail(err)
        end
    end
end
RegisterEvent('X4LAN_Projectile.Ready', function()
    x.ready = true
    log('READY')
end)
RegisterEvent('X4LAN_Projectile.State', guard(x.state))
RegisterEvent('X4LAN_Projectile.Born', guard(x.born))
RegisterEvent('X4LAN_Projectile.Created', guard(x.created))
RegisterEvent('X4LAN_Projectile.Rejected', guard(x.rejected))
