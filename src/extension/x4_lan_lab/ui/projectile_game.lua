-- Guarded observed shots. No native or network
-- work occurs without the complete damage/proof/capture stack and explicit flag.
local API, cfg = X4LAN_BUBBLE_API, X4LAN_CONFIG
if
    not cfg
    or not cfg.projectile_visuals
    or not cfg.shot_visual
    or not cfg.shot_capture
    or not cfg.shot_claims
    or not cfg.pilot_health
    or not cfg.fire2
    or not API
    or not API.coop
    or not API.health
    or not API.shot
    or not API.shotVisual
then
    return
end
local motion, wireProtocol = assert(X4LAN_PROJECTILE_MOTION), assert(X4LAN_PROJECTILE_PROTOCOL)
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
local coopAdapter, healthAdapter, shotAdapter, shotVisual, sessionState =
    API.coop, API.health, API.shot, API.shotVisual, API.state
local projectiles = { contexts = {}, slots = {}, captures = {}, serial = 0, shotId = 0, now = 0 }
API.projectiles = projectiles
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
    return IsValidComponent(API.id(id)) and C.IsComponentClass(id, 'bullet')
end
local function event(control, args)
    AddUITriggeredEvent('X4LAN_Projectile', control, args or {})
end
local function serial()
    projectiles.serial = projectiles.serial + 1
    assert(projectiles.serial <= 2147483647, 'operation exhausted')
    return projectiles.serial
end
local function closePosition(actual, wanted)
    -- Sector = zone + local can round again after the float-native command.
    return math.abs(actual - wanted) <= math.max(0.01, math.abs(wanted) * 2 ^ -21)
end
local function window(a)
    a.revision, a.stamp, a.from, a.deadline =
        shotAdapter.stateRevision,
        healthAdapter.mdStamp,
        healthAdapter.mdAge,
        healthAdapter.mdAge + 0.3
    return a
end
local function send(packet)
    if packet and sessionState.client and sessionState.client.lease then
        sessionState.transport:send(packet)
    end
end
local function fresh(now)
    return not projectiles.failed
        and not sessionState.failed
        and not shotAdapter.failed
        and not healthAdapter.failed
        and not shotVisual.failed
        and sessionState.client
        and sessionState.client.lease
        and healthAdapter.mdStamp
        and now - (healthAdapter.mdAt or -100) <= 0.3
        and not API.paused()
end
local function wanted(entity, now)
    if not fresh(now) then
        return nil
    end
    local gen, ship, owner =
        healthAdapter.valid[entity], coopAdapter.ships[entity], coopAdapter.owners[entity]
    local kind = shotAdapter.support[entity] and KINDS[shotAdapter.support[entity].macro]
    if
        not kind
        or not gen
        or gen <= (healthAdapter.closed[entity] or 0)
        or not owner
        or coopAdapter.pilots[entity] ~= owner
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
    local lease, sourceNow = sessionState.client.lease, now
    if mode == 1 then
        if
            C.GetPlayerControlledShipID() ~= ship or not shotVisual.readyOriginal(entity, gen, now)
        then
            return nil
        end
    else
        local r = coopAdapter.remote[entity]
        local player = C.GetPlayerContainerID()
        if
            not r
            or not r.track
            or not r.track.offset
            or now - r.at > 0.35
            or not r.sourceLease
            or player == 0
            or C.GetContextByClass(player, 'sector', false) ~= sector
            or not shotVisual.readyReplay(entity, gen, now)
        then
            return nil
        end
        if r.sector ~= GetComponentData(API.id(sector), 'macro') then
            return nil
        end
        lease, sourceNow = r.sourceLease, now - r.track.offset
    end
    local name = GetComponentData(API.id(sector), 'macro')
    -- A.id creates engine LuaID userdata for MD/API arguments. It is not a string
    -- and may be a new wrapper on every call; key the stable native numeric IDs.
    local key = table.concat(
        {
            sessionState.client.lease,
            lease,
            entity,
            coopAdapter.epochs[entity],
            gen,
            tostring(ship),
            tostring(sector),
            mode,
            kind,
        },
        '|'
    )
    return {
        key = key,
        entity = entity,
        gen = gen,
        epoch = coopAdapter.epochs[entity],
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
    local r = projectiles.slots[slot]
    if not r then
        return
    end
    event('Remove', { slot = slot, operation = r.operation })
    projectiles.slots[slot] = nil
end
local function revoke(context)
    if not context.enabled then
        return
    end
    context.enabled = false
    context.ticket = serial()
    context.ackAt = nil
    event('Revoke', { entity = context.entity, ticket = context.ticket })
    motion.clear(context.buffer, context.sourceNow)
    for slot, r in pairs(projectiles.slots) do
        if r.entity == context.entity then
            remove(slot)
        end
    end
end
local function ready(context, now)
    return context and context.enabled and context.ackAt and now - context.ackAt <= 0.3
end
function projectiles.contextsFrame(now)
    for entity = 1, 2 do
        local cur = wanted(entity, now)
        local context = projectiles.contexts[entity]
        if not cur then
            if context then
                local track = coopAdapter.remote[entity] and coopAdapter.remote[entity].track
                if track and track.offset then
                    context.sourceNow = math.max(context.sourceNow, now - track.offset)
                end
                revoke(context)
            end
        else
            if not context or context.key ~= cur.key then
                if context then
                    revoke(context)
                end
                context = cur
                context.buffer = motion.new(context.key, 0)
                context.ticket = serial()
                context.enabled = true
                context.started = now
                projectiles.contexts[entity] = context
            else
                context.sourceNow = cur.sourceNow
                if
                    not context.enabled
                    or not context.ackAt and now - context.started > 0.3
                    or context.ackAt and now - context.ackAt > 0.3
                then
                    revoke(context)
                    context.ticket = serial()
                    context.enabled = true
                    context.ackAt = nil
                    context.started = now
                    context.nextState = 0
                end
            end
            if now >= (context.nextState or 0) then
                context.nextState = now + 0.1
                event(
                    'State',
                    window({
                        entity = entity,
                        gen = context.gen,
                        ticket = context.ticket,
                        mode = context.mode,
                        kind = context.kind,
                        sector = API.id(context.sector),
                    })
                )
            end
        end
    end
end
function projectiles.state(payload)
    local entity, t = tostring(payload):match('^(%d)|(%d+)$')
    entity, t = tonumber(entity), tonumber(t)
    local context = entity and projectiles.contexts[entity]
    local cur = entity and wanted(entity, projectiles.now)
    if context and cur and cur.key == context.key and context.enabled and context.ticket == t then
        context.ackAt = projectiles.now
    end
end
function projectiles.born(payload)
    local entity, generation, t, id, sector, kind, vx, vy, vz =
        tostring(payload):match('^(%d)|(%d+)|(%d+)|([^|]+)|([^|]+)|(%d)|([^|]+)|([^|]+)|([^|]+)$')
    entity, generation, t, kind =
        tonumber(entity), tonumber(generation), tonumber(t), tonumber(kind)
    local velocity = { tonumber(vx), tonumber(vy), tonumber(vz) }
    for i = 1, 3 do
        local n = velocity[i]
        if not n or n ~= n or math.abs(n) > wireProtocol.MAX_VELOCITY then
            return
        end
    end
    local context = entity and projectiles.contexts[entity]
    local cur = entity and wanted(entity, projectiles.now)
    if
        not ready(context, projectiles.now)
        or not cur
        or cur.key ~= context.key
        or context.mode ~= 1
        or context.gen ~= generation
        or context.ticket ~= t
        or context.kind ~= kind
    then
        return
    end
    local native, space = ConvertIDTo64Bit(id), ConvertIDTo64Bit(sector)
    if space ~= context.sector or not liveBullet(native) then
        return
    end
    local key = context.key .. '|' .. id
    local n = 0
    for _ in pairs(projectiles.captures) do
        n = n + 1
    end
    if projectiles.captures[key] or n >= motion.capacity then
        return
    end
    projectiles.shotId = projectiles.shotId + 1
    assert(projectiles.shotId <= 2147483647, 'shot IDs exhausted')
    projectiles.captures[key] = {
        native = native,
        space = space,
        q = context,
        ticket = context.ticket,
        id = projectiles.shotId,
        velocity = velocity,
    }
end
local function message(bullet, kind, time)
    local context = bullet.q
    return {
        type = kind,
        direction = 'client',
        lease = sessionState.client.lease,
        entity = context.entity,
        epoch = context.epoch,
        gen = context.gen,
        id = bullet.id,
        time = time,
    }
end
function projectiles.captureFrame(now)
    for key, bullet in pairs(projectiles.captures) do
        local context = bullet.q
        local cur = wanted(context.entity, now)
        if
            not cur
            or cur.key ~= context.key
            or not ready(context, now)
            or bullet.ticket ~= context.ticket
        then
            projectiles.captures[key] = nil
        else
            if not bullet.ended then
                if liveBullet(bullet.native) then
                    local p = C.GetPositionalOffset(bullet.native, bullet.space)
                    local sample = { time = now, pose = { p.x, p.y, p.z, p.yaw, p.pitch, p.roll } }
                    bullet.first = bullet.first or sample
                    if not bullet.birth then
                        -- Native velocity includes inherited motion and actual barrel spread.
                        -- Differencing float sector positions amplifies rounding far from origin.
                        local velocity = bullet.velocity
                        local m = message(bullet, 'BOLT', bullet.first.time)
                        local a = bullet.first.pose
                        m.sector, m.kind, m.x, m.y, m.z, m.vx, m.vy, m.vz, m.yaw, m.pitch, m.roll, m.ttl =
                            context.sectorName,
                            context.kind,
                            a[1],
                            a[2],
                            a[3],
                            velocity[1],
                            velocity[2],
                            velocity[3],
                            a[4],
                            a[5],
                            a[6],
                            TTL[context.kind]
                        bullet.birth = assert(wireProtocol.serialize(m))
                        bullet.birthAt = now
                        bullet.nextBirth = now
                        bullet.repeats = 0
                    end
                else
                    bullet.ended = now
                    if bullet.birth then
                        bullet.finish = assert(wireProtocol.serialize(message(bullet, 'BEND', now)))
                        bullet.nextEnd = now
                        bullet.endRepeats = 0
                    else
                        projectiles.captures[key] = nil
                    end
                end
            end
            if
                bullet.birth
                and not bullet.ended
                and bullet.repeats < 3
                and now >= bullet.nextBirth
            then
                send(bullet.birth)
                bullet.repeats = bullet.repeats + 1
                bullet.nextBirth = now + 0.05
            end
            if bullet.finish and now >= bullet.nextEnd then
                send(bullet.finish)
                bullet.endRepeats = bullet.endRepeats + 1
                bullet.nextEnd = now + 0.05
                if bullet.endRepeats >= 3 then
                    projectiles.captures[key] = nil
                end
            end
            if bullet.first and now - bullet.first.time > 3.1 then
                projectiles.captures[key] = nil
            end
        end
    end
end
function projectiles.receive(fields, now)
    if fields[1] ~= 'BOLT' and fields[1] ~= 'BEND' then
        return false
    end
    local m = wireProtocol.parse(table.concat(fields, '|'), 'forwarded')
    local function drop(reason)
        if coopAdapter.dropped then
            coopAdapter.dropped(fields[1], m and m.entity or '?', reason, now)
        end
        return true
    end
    if not m then
        return drop('malformed')
    end
    if m.lease ~= sessionState.client.lease then
        return drop('lease')
    end
    if not fresh(now) then
        return drop('not-fresh(md/health/paused)')
    end
    local context = projectiles.contexts[m.entity]
    local cur = wanted(m.entity, now)
    if not cur then
        return drop('no-replay-context(gen/support/track/sector)')
    end
    if not ready(context, now) then
        return drop('md-context-not-acknowledged')
    end
    if cur.key ~= context.key then
        return drop('context-changed')
    end
    if context.mode ~= 2 then
        return drop('own-ship')
    end
    if m.source_lease ~= context.sourceLease then
        return drop('source-lease')
    end
    if m.epoch ~= context.epoch then
        return drop('epoch')
    end
    if m.gen ~= context.gen then
        return drop('health-generation ' .. tostring(context.gen) .. '~=' .. tostring(m.gen))
    end
    if m.type == 'BOLT' then
        if m.sector == context.sectorName and m.kind == context.kind then
            motion.birth(context.buffer, context.key, {
                id = m.id,
                time = m.time,
                ttl = m.ttl,
                kind = m.kind,
                pose = { m.x, m.y, m.z, m.yaw, m.pitch, m.roll },
                velocity = { m.vx, m.vy, m.vz },
            }, cur.sourceNow, cur.sourceNow - DELAY)
        end
    else
        motion.finish(context.buffer, context.key, m.id, m.time)
    end
    return true
end
function projectiles.created(payload)
    local entity, t, slot, op, id, sector =
        tostring(payload):match('^(%d)|(%d+)|(%d+)|(%d+)|([^|]+)|([^|]+)$')
    entity, t, slot, op = tonumber(entity), tonumber(t), tonumber(slot), tonumber(op)
    if not entity or not slot or slot < 1 or slot > motion.capacity then
        return
    end
    local context = projectiles.contexts[entity]
    local r = projectiles.slots[slot]
    local cur = wanted(entity, projectiles.now)
    if
        not r
        or r.operation ~= op
        or r.entity ~= entity
        or not ready(context, projectiles.now)
        or not cur
        or cur.key ~= context.key
        or context.ticket ~= t
        or r.ticket ~= t
    then
        event('Remove', { slot = slot, operation = op })
        return
    end
    local object, space = ConvertIDTo64Bit(id), ConvertIDTo64Bit(sector)
    if space ~= context.sector or not C.IsComponentClass(object, 'object') then
        remove(slot)
        return
    end
    r.object = object
end
function projectiles.rejected(payload)
    local entity, t, slot, op = tostring(payload):match('^(%d)|(%d+)|(%d+)|(%d+)$')
    entity, t, slot, op = tonumber(entity), tonumber(t), tonumber(slot), tonumber(op)
    local r = slot and projectiles.slots[slot]
    if r and r.entity == entity and r.ticket == t and r.operation == op and not r.object then
        -- A queued request can cross a freshness boundary. Retry while this exact
        -- observation is still live, without spending most of a short pulse's TTL.
        r.retryAt = projectiles.now + 0.05
    end
end
function projectiles.renderFrame(now)
    local wantedSlots = {}
    for entity, context in pairs(projectiles.contexts) do
        if context.mode == 2 and ready(context, now) then
            local cur = wanted(entity, now)
            if cur and cur.key == context.key then
                local samples = motion.sample(context.buffer, cur.sourceNow - DELAY)
                for id, sample in pairs(samples) do
                    local slot
                    for i, r in pairs(projectiles.slots) do
                        if r.entity == entity and r.id == id and r.ticket == context.ticket then
                            slot = i
                            break
                        end
                    end
                    if not slot then
                        for i = 1, motion.capacity do
                            if not projectiles.slots[i] then
                                slot = i
                                break
                            end
                        end
                        if slot then
                            local op = serial()
                            local p = sample.pose
                            local record = context.buffer.active[id]
                            projectiles.slots[slot] = {
                                entity = entity,
                                id = id,
                                ticket = context.ticket,
                                operation = op,
                                at = now,
                            }
                            event(
                                'Create',
                                window({
                                    entity = entity,
                                    gen = context.gen,
                                    ticket = context.ticket,
                                    slot = slot,
                                    operation = op,
                                    kind = context.kind,
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
                        local r = projectiles.slots[slot]
                        wantedSlots[slot] = true
                        if r.object then
                            if
                                C.IsComponentClass(r.object, 'object')
                                and C.GetContextByClass(r.object, 'sector', false)
                                    == context.sector
                            then
                                local p = sample.pose
                                local pose = ffi.new('UIPosRot', {
                                    x = p[1],
                                    y = p[2],
                                    z = p[3],
                                    yaw = p[4] * 180 / math.pi,
                                    pitch = p[5] * 180 / math.pi,
                                    roll = p[6] * 180 / math.pi,
                                })
                                -- A zone reparent can retain the previous zone's local coordinates on
                                -- the first write. Correct once within this frame, then remove a carrier
                                -- that still disagrees. Never apply this repair to ships or projectiles.
                                local placed = false
                                for attempt = 1, 2 do
                                    C.SetObjectSectorPos(r.object, context.sector, pose)
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
    for slot in pairs(projectiles.slots) do
        if not wantedSlots[slot] then
            remove(slot)
        end
    end
end
function projectiles.fail(err)
    if projectiles.failed then
        return
    end
    projectiles.failed = true
    log('STOPPED ' .. tostring(err))
    projectiles.captures = {}
    pcall(event, 'Stop')
    for _, context in pairs(projectiles.contexts) do
        context.enabled = false
    end
    projectiles.slots = {}
end
local frame, receive = coopAdapter.frame, coopAdapter.receive
function coopAdapter.frame(now)
    frame(now)
    projectiles.now = now
    if shotAdapter.ready and shotVisual.ready and not projectiles.setup then
        projectiles.setup = true
        event('Setup')
    end
    if projectiles.ready and not projectiles.failed then
        local ok, err = pcall(function()
            projectiles.contextsFrame(now)
            projectiles.captureFrame(now)
            projectiles.renderFrame(now)
        end)
        if not ok then
            projectiles.fail(err)
        end
    end
end
function coopAdapter.receive(fields, now)
    local ok, handled = pcall(projectiles.receive, fields, now)
    if not ok then
        projectiles.fail(handled)
        handled = fields[1] == 'BOLT' or fields[1] == 'BEND'
    end
    if handled then
        return true
    end
    return receive(fields, now)
end
local function guard(fn)
    return function(_, p)
        local ok, err = pcall(fn, p)
        if not ok then
            projectiles.fail(err)
        end
    end
end
RegisterEvent('X4LAN_Projectile.Ready', function()
    projectiles.ready = true
    log('READY')
end)
RegisterEvent('X4LAN_Projectile.State', guard(projectiles.state))
RegisterEvent('X4LAN_Projectile.Born', guard(projectiles.born))
RegisterEvent('X4LAN_Projectile.Created', guard(projectiles.created))
RegisterEvent('X4LAN_Projectile.Rejected', guard(projectiles.rejected))
