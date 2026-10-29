-- INTERNAL opt-in NPC combat. Native MD owns capture, health and deduplication.
-- Initially free-flying S/M NPC hulls only, with the existing admitted stock guns.
local API = X4LAN_BUBBLE_API
local cfg = X4LAN_CONFIG
if not cfg or not cfg.npc_combat or not API or not API.universe or not API.shotCapture then
    return
end
local ffi = require('ffi')
local C = ffi.C
pcall(ffi.cdef, 'const char* GetObjectIDCode(UniverseID);')
local nativeCode = C.GetObjectIDCode -- Before identity_game's display-only override.
local universe, protocol, sessionState, coopAdapter, healthAdapter, shotAdapter =
    API.universe, X4LAN_UNIVERSE_PROTOCOL, API.state, API.coop, API.health, API.shot
local npcCombat =
    { records = {}, states = {}, claims = {}, pending = {}, seq = 0, serial = 0, cursor = 0 }
API.npcCombat = npcCombat
local function log(v)
    DebugError('[X4LAN] NPC_COMBAT ' .. v)
end
-- Host NHP cadence. A record is read every poll, but NHP is sent only when its health or
-- death changes, and otherwise repeated with a new sequence every KEEPALIVE seconds. The
-- relay forwards new sequences only; a guest binds health while its newest sequence is
-- under 5 s old, so 1.5 s survives two consecutive lost datagrams. Formerly every read
-- was sent: up to 128 NHP per 0.1 s.
npcCombat.KEEPALIVE = 1.5
local function integer(v, max)
    local i = protocol.integer(tostring(v))
    return i and i <= (max or 2147483647) and i
end
local function percent(v)
    local x = tonumber(v)
    return x and x == x and x >= 0 and x <= 100 and x
end
local function key(id, generation, e, shooterGeneration)
    return table.concat({ id, generation, e, shooterGeneration }, '|')
end
local function fresh(now)
    return not API.paused() and healthAdapter.mdStamp and now - healthAdapter.mdAt <= 0.5
end
local function validGen(e)
    local g = healthAdapter.valid[e]
    return g and g > (healthAdapter.closed[e] or 0) and g
end
local function pilot(e, shooterGeneration, slot)
    return coopAdapter.owners[e] == slot
        and coopAdapter.pilots[e] == slot
        and validGen(e) == shooterGeneration
        and (
            slot == 'p1'
            or cfg.slot ~= 'p1' and healthAdapter.live and healthAdapter.live.entity == e and healthAdapter.live.gen == shooterGeneration
            or cfg.slot == 'p1'
                and healthAdapter.remote[e]
                and healthAdapter.remote[e].gen == shooterGeneration
        )
end
local function send(fields)
    sessionState.transport:send(table.concat(fields, '|'))
end
local function call(action, args)
    args.stamp, args.from, args.deadline =
        healthAdapter.mdStamp, healthAdapter.mdAge, healthAdapter.mdAge + 1
    AddUITriggeredEvent('X4LAN_NpcCombat', action, args)
end
function npcCombat.code(object)
    return ffi.string(nativeCode(object))
end
function npcCombat.fail(reason)
    if npcCombat.failed then
        return
    end
    npcCombat.failed = true
    log('STOPPED ' .. tostring(reason))
    AddUITriggeredEvent('X4LAN_NpcCombat', 'Stop', {})
end
local function safe(r)
    if r.retired then
        return false
    end
    if not r.object or not C.IsComponentOperational(r.object) then
        return false
    end
    if
        r.object == C.GetPlayerContainerID()
        or r.object == C.GetPlayerOccupiedShipID()
        or r.object == C.GetPlayerControlledShipID()
        or coopAdapter.entity(r.object) > 0
    then
        return false
    end
    if cfg.slot == 'p1' then
        local identity = universe.identities[tostring(r.object):gsub('ULL$', '')]
        if not identity or identity.id ~= r.id then
            return false
        end
    end
    local macro, owner = GetComponentData(API.id(r.object), 'macro', 'owner')
    return npcCombat.code(r.object) == r.code
        and macro == r.macro
        and owner == r.owner
        and C.GetContextByClass(r.object, 'sector', false) == coopAdapter.sectorId(r.sector)
end
local function make(id, object, sector, macro, owner, generation)
    if npcCombat.serial >= 32768 then
        npcCombat.fail('binding limit; reconnect required')
        return
    end
    if not (C.IsComponentClass(object, 'ship_s') or C.IsComponentClass(object, 'ship_m')) then
        return
    end
    npcCombat.serial = npcCombat.serial + 1
    local r = {
        id = id,
        object = object,
        sector = sector,
        macro = macro,
        owner = owner,
        code = npcCombat.code(object),
        gen = generation,
        serial = npcCombat.serial,
    }
    if not r.code:match('^[A-Z0-9-]+$') or #r.code > 32 then
        npcCombat.fail('native identity unavailable')
        return
    end
    npcCombat.records[id] = r
    return r
end
local function args(r)
    return {
        id = r.id,
        gen = r.gen,
        serial = r.serial,
        object = API.id(r.object),
        code = r.code,
        sector = r.sector,
        macro = r.macro,
        owner = r.owner,
    }
end
-- Called before a replica may respawn. A native local death is not host permission.
function npcCombat.hidden(r)
    local state = npcCombat.states[r.entity.id]
    if state and not state.alive then
        return true
    end
    if r.npcBound and (not r.object or not C.IsComponentOperational(r.object)) then
        npcCombat.fail('replica died without a host destruction state')
        return true
    end
    return false
end
function npcCombat.hide(replica, now)
    local state = npcCombat.states[replica.entity.id]
    local r = npcCombat.records[replica.entity.id]
    if
        state
        and not state.alive
        and not r
        and replica.object
        and C.IsComponentOperational(replica.object)
    then
        r = make(
            replica.entity.id,
            replica.object,
            replica.sector,
            replica.entity.macro,
            replica.entity.owner,
            state.gen
        )
    end
    if
        state
        and not state.alive
        and r
        and replica.object == r.object
        and safe(r)
        and fresh(now)
        and now >= (r.nextDestroy or 0)
    then
        r.nextDestroy = now + 0.5
        local bind = args(r)
        bind.hull, bind.shield, bind.sequence = 0.000001, 0, 0
        call('Bind', bind) -- Covers a death overtaking the first healthy binding.
        local a = args(r)
        a.sequence = state.seq
        call('Destroy', a)
    end
end
function npcCombat.receive(packetFields, now)
    if packetFields[1] ~= 'NHP' and packetFields[1] ~= 'NCLAIM' and packetFields[1] ~= 'NACK' then
        return false
    end
    if
        npcCombat.failed
        or not sessionState.client
        or packetFields[3] ~= sessionState.client.lease
    then
        return true
    end
    if
        packetFields[1] == 'NHP'
        and cfg.slot ~= 'p1'
        and #packetFields == 14
        and universe.latest
        and packetFields[4] == universe.latest.epoch
    then
        local id, generation, seq =
            integer(packetFields[5]), integer(packetFields[6]), integer(packetFields[7])
        local hu, sh = percent(packetFields[12]), percent(packetFields[13])
        local r = id and universe.replicas[id]
        local old = id and npcCombat.states[id]
        if
            not id
            or not generation
            or generation == 0
            or not seq
            or not hu
            or not sh
            or not r
            or r.entity.kind ~= 'ship'
            or packetFields[8] ~= r.sector
            or packetFields[10] ~= r.entity.macro
            or packetFields[11] ~= r.entity.owner
            or packetFields[11] == 'player'
            or not packetFields[9]:match('^[A-Z0-9-]+$')
            or #packetFields[9] > 32
            or (packetFields[14] ~= '0' and packetFields[14] ~= '1')
            or (packetFields[14] == '0' and hu ~= 0)
            or (packetFields[14] == '1' and hu <= 0)
        then
            return true
        end
        if
            old
            and (
                seq <= old.seq
                or generation ~= old.gen
                or packetFields[9] ~= old.code
                or not old.alive
            )
        then
            return true
        end
        npcCombat.states[id] = {
            id = id,
            gen = generation,
            seq = seq,
            code = packetFields[9],
            hull = hu,
            shield = sh,
            alive = packetFields[14] == '1',
            at = now,
            epoch = packetFields[4],
        }
    elseif
        packetFields[1] == 'NCLAIM'
        and cfg.slot == 'p1'
        and #packetFields == 12
        and packetFields[5] == sessionState.client.lease
    then
        local id, generation, e, shooterGeneration, seq, hits =
            integer(packetFields[6]),
            integer(packetFields[7]),
            integer(packetFields[8], 2),
            integer(packetFields[9]),
            integer(packetFields[10]),
            integer(packetFields[11], 1000000)
        local r = id and npcCombat.records[id]
        local sup = e and shotAdapter.support[e]
        if
            not r
            or r.dead
            or r.gen ~= generation
            or not e
            or e < 1
            or not shooterGeneration
            or not pilot(e, shooterGeneration, coopAdapter.owners[e])
            or not seq
            or not hits
            or not sup
            or packetFields[12] ~= sup.digest
            or not safe(r)
            or not packetFields[4]:match('^[a-f0-9]+$')
            or #packetFields[4] ~= 32
        then
            return true
        end
        local k = key(id, generation, e, shooterGeneration)
        local p = npcCombat.pending[k]
        if p and (seq <= p.seq or hits < p.hits or p.source ~= packetFields[4]) then
            return true
        end
        npcCombat.pending[k] = {
            id = id,
            gen = generation,
            e = e,
            sg = shooterGeneration,
            seq = seq,
            hits = hits,
            source = packetFields[4],
            digest = packetFields[12],
            strength = sup.strength,
            confirmed = p and p.confirmed or 0,
            next = p and p.next or 0,
        }
    elseif
        packetFields[1] == 'NACK'
        and cfg.slot ~= 'p1'
        and #packetFields == 10
        and universe.latest
        and packetFields[4] == universe.latest.epoch
        and packetFields[5] == sessionState.client.lease
    then
        local id, generation, e, shooterGeneration, count =
            integer(packetFields[6]),
            integer(packetFields[7]),
            integer(packetFields[8], 2),
            integer(packetFields[9]),
            integer(packetFields[10], 1000000)
        if id and generation and e and shooterGeneration and count then
            local p = npcCombat.claims[key(id, generation, e, shooterGeneration)]
            if p and count <= p.hits then
                p.ack = math.max(p.ack, count)
            end
        end
    end
    return true
end
function npcCombat.count(payload)
    local packetFields = protocol.split(tostring(payload), '|')
    if #packetFields ~= 6 or npcCombat.failed or cfg.slot == 'p1' then
        return
    end
    local id, generation, serial, e, shooterGeneration, hits =
        integer(packetFields[1]),
        integer(packetFields[2]),
        integer(packetFields[3]),
        integer(packetFields[4], 2),
        integer(packetFields[5]),
        integer(packetFields[6], 1000000)
    local r = id and npcCombat.records[id]
    local state = id and npcCombat.states[id]
    if
        not r
        or r.gen ~= generation
        or r.serial ~= serial
        or not state
        or not state.alive
        or not e
        or e < 1
        or not shooterGeneration
        or not hits
        or not pilot(e, shooterGeneration, coopAdapter.owners[e])
        or not shotAdapter.support[e]
        or not safe(r)
        or (healthAdapter.now or 0) - state.at >= 5
    then
        return
    end
    local k = key(id, generation, e, shooterGeneration)
    local p = npcCombat.claims[k]
    if not p then
        p = { id = id, gen = generation, e = e, sg = shooterGeneration, hits = 0, ack = 0 }
        npcCombat.claims[k] = p
    end
    p.hits = math.max(p.hits, hits)
end
function npcCombat.read(payload)
    local packetFields = protocol.split(tostring(payload), '|')
    if #packetFields ~= 6 or npcCombat.failed or cfg.slot ~= 'p1' then
        return
    end
    local id, generation, serial =
        integer(packetFields[1]), integer(packetFields[2]), integer(packetFields[3])
    local r = id and npcCombat.records[id]
    local hull, shield = percent(packetFields[4]), percent(packetFields[5])
    if
        not r
        or r.gen ~= generation
        or r.serial ~= serial
        or not hull
        or not shield
        or (packetFields[6] ~= '0' and packetFields[6] ~= '1')
    then
        return
    end
    if packetFields[6] == '1' and (r.dead or not safe(r)) then
        return
    end
    r.hull, r.shield, r.dead, r.readAt = hull, shield, packetFields[6] == '0', healthAdapter.now
    local now = healthAdapter.now or 0
    local sent = r.sent
    if
        sent
        and sent.hull == hull
        and sent.shield == shield
        and sent.dead == r.dead
        and now - sent.at < npcCombat.KEEPALIVE
    then
        return
    end
    r.sent = { hull = hull, shield = shield, dead = r.dead, at = now }
    npcCombat.seq = npcCombat.seq + 1
    send({
        'NHP',
        1,
        sessionState.client.lease,
        sessionState.client.lease,
        id,
        generation,
        npcCombat.seq,
        r.sector,
        r.code,
        r.macro,
        r.owner,
        hull,
        shield,
        r.dead and 0 or 1,
    })
end
function npcCombat.applied(payload)
    local packetFields = protocol.split(tostring(payload), '|')
    if #packetFields ~= 6 or npcCombat.failed or cfg.slot ~= 'p1' then
        return
    end
    local id, generation, serial, e, shooterGeneration, count =
        integer(packetFields[1]),
        integer(packetFields[2]),
        integer(packetFields[3]),
        integer(packetFields[4], 2),
        integer(packetFields[5]),
        integer(packetFields[6], 1000000)
    if not id or not generation or not serial or not e or not shooterGeneration or not count then
        return
    end
    local p = npcCombat.pending[key(id, generation, e, shooterGeneration)]
    local r = npcCombat.records[id]
    if
        not p
        or not r
        or serial ~= r.serial
        or count > p.hits
        or not pilot(e, shooterGeneration, coopAdapter.owners[e])
    then
        return
    end
    p.confirmed = math.max(p.confirmed, count)
    send({
        'NACK',
        1,
        sessionState.client.lease,
        sessionState.client.lease,
        p.source,
        id,
        generation,
        e,
        shooterGeneration,
        p.confirmed,
    })
end
function npcCombat.retired(payload)
    local packetFields = protocol.split(tostring(payload), '|')
    if #packetFields ~= 3 or cfg.slot ~= 'p1' then
        return
    end
    local r = npcCombat.records[tonumber(packetFields[1])]
    if not r or r.gen ~= tonumber(packetFields[2]) or r.serial ~= tonumber(packetFields[3]) then
        return
    end
    r.retired = true
    local identity = universe.identities[tostring(r.object):gsub('ULL$', '')]
    if identity and identity.id == r.id then
        identity.retired = true
    end
end
function npcCombat.frame(now)
    if npcCombat.failed then
        return
    end
    if universe.failed or healthAdapter.failed or shotAdapter.failed or API.shotCapture.failed then
        npcCombat.fail('dependency stopped')
        return
    end
    if not sessionState.client or not sessionState.client.lease or not fresh(now) then
        return
    end
    if not npcCombat.setup and API.shotCapture.ready then
        npcCombat.setup = true
        AddUITriggeredEvent('X4LAN_NpcCombat', 'Setup')
        return
    end
    if not npcCombat.ready then
        return
    end
    if now >= (npcCombat.nextPoll or 0) then
        npcCombat.nextPoll = now + 0.1
        local list = cfg.slot == 'p1' and universe.publish or universe.renderIds or {}
        local count = #list
        for _ = 1, math.min(count, 128) do
            npcCombat.cursor = npcCombat.cursor % count + 1
            local entry = list[npcCombat.cursor]
            local id = cfg.slot == 'p1' and entry.id or entry
            local replica = cfg.slot ~= 'p1' and universe.replicas[id]
            local state = npcCombat.states[id]
            local r = npcCombat.records[id]
            -- A terminal host fact remains actionable when the motion/baseline feed has
            -- expired. Never wait for another pose packet to retire a confirmed death.
            if replica and state and not state.alive then
                npcCombat.hide(replica, now)
            end
            if cfg.slot == 'p1' then
                if not r and C.IsComponentOperational(entry.object) then
                    local macro, owner = GetComponentData(API.id(entry.object), 'macro', 'owner')
                    r = make(id, entry.object, entry.sector, macro, owner, 1)
                end
            elseif
                replica
                and state
                and state.alive
                and now - state.at < 5
                and replica.object
                and C.IsComponentOperational(replica.object)
            then
                if not r or r.object ~= replica.object then
                    r = make(
                        id,
                        replica.object,
                        replica.sector,
                        replica.entity.macro,
                        replica.entity.owner,
                        state.gen
                    )
                end
            end
            if
                r
                and safe(r)
                and (
                    cfg.slot == 'p1'
                    or replica
                        and replica.object == r.object
                        and state
                        and state.alive
                        and now - state.at < 5
                )
            then
                local a = args(r)
                if cfg.slot ~= 'p1' then
                    a.hull, a.shield, a.sequence = state.hull, state.shield, state.seq
                    replica.npcBound = true
                end
                call('Bind', a)
            end
        end
        -- Destruction is reported by a native event and retained after baseline removal.
        if cfg.slot == 'p1' then
            local dead = {}
            for _, r in pairs(npcCombat.records) do
                if r.dead and universe.wanted[r.sector] then
                    dead[#dead + 1] = r
                end
            end
            for i = 1, math.min(#dead, 128) do
                npcCombat.deadCursor = (npcCombat.deadCursor or 0) % #dead + 1
                local r = dead[npcCombat.deadCursor]
                -- The death itself was sent by n.read; repeat it at the keepalive cadence only.
                if not r.sent or not r.sent.dead or now - r.sent.at >= npcCombat.KEEPALIVE then
                    r.sent = { hull = 0, shield = 0, dead = true, at = now }
                    npcCombat.seq = npcCombat.seq + 1
                    send({
                        'NHP',
                        1,
                        sessionState.client.lease,
                        sessionState.client.lease,
                        r.id,
                        r.gen,
                        npcCombat.seq,
                        r.sector,
                        r.code,
                        r.macro,
                        r.owner,
                        0,
                        0,
                        0,
                    })
                end
            end
        end
    end
    for k, p in pairs(npcCombat.claims) do
        local state = npcCombat.states[p.id]
        if
            not state
            or not state.alive
            or state.gen ~= p.gen
            or not pilot(p.e, p.sg, coopAdapter.owners[p.e])
        then
            npcCombat.claims[k] = nil
        elseif
            now - state.at < 5
            and p.hits > p.ack
            and now >= (p.next or 0)
            and shotAdapter.support[p.e]
        then
            p.next = now + 0.25
            npcCombat.seq = npcCombat.seq + 1
            send({
                'NCLAIM',
                1,
                sessionState.client.lease,
                state.epoch,
                p.id,
                p.gen,
                p.e,
                p.sg,
                npcCombat.seq,
                p.hits,
                shotAdapter.support[p.e].digest,
            })
        end
    end
    for k, p in pairs(npcCombat.pending) do
        local r = npcCombat.records[p.id]
        local sup = shotAdapter.support[p.e]
        if not r or not pilot(p.e, p.sg, coopAdapter.owners[p.e]) then
            npcCombat.pending[k] = nil
        elseif
            not r.dead
            and safe(r)
            and sup
            and sup.digest == p.digest
            and p.hits > p.confirmed
            and now >= p.next
        then
            p.next = now + 0.5
            local a = args(r)
            a.shooter, a.sgen, a.base, a.count, a.strength =
                p.e, p.sg, p.confirmed, math.min(p.hits, p.confirmed + 20), p.strength
            a.state = shotAdapter.stateAck
            if a.state and a.state == shotAdapter.stateRevision then
                call('Apply', a)
            end
        end
    end
end
local receive, frame = universe.receive, universe.frame
function universe.receive(packetFields, now)
    local ok, handled = pcall(npcCombat.receive, packetFields, now)
    if not ok then
        npcCombat.fail(handled)
        handled = packetFields[1] == 'NHP'
            or packetFields[1] == 'NCLAIM'
            or packetFields[1] == 'NACK'
    end
    if handled then
        return true
    end
    return receive(packetFields, now)
end
function universe.frame(now)
    frame(now)
    local ok, err = pcall(npcCombat.frame, now)
    if not ok then
        npcCombat.fail(err)
    end
end
RegisterEvent('X4LAN_NpcCombat.Ready', function()
    npcCombat.ready = true
    log('READY slot=' .. cfg.slot)
end)
RegisterEvent('X4LAN_NpcCombat.Read', function(_, p)
    npcCombat.read(p)
end)
RegisterEvent('X4LAN_NpcCombat.Count', function(_, p)
    npcCombat.count(p)
end)
RegisterEvent('X4LAN_NpcCombat.Applied', function(_, p)
    npcCombat.applied(p)
end)
RegisterEvent('X4LAN_NpcCombat.Retired', function(_, p)
    npcCombat.retired(p)
end)
