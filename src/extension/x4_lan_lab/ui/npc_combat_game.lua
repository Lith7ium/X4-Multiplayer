-- INTERNAL opt-in NPC combat. Native MD owns capture, health and deduplication.
-- Initially free-flying S/M NPC hulls only, with the existing admitted stock guns.
local A = X4LAN_BUBBLE_API
local cfg = X4LAN_CONFIG
if not cfg or not cfg.npc_combat or not A or not A.universe or not A.shotCapture then
    return
end
local ffi = require('ffi')
local C = ffi.C
pcall(ffi.cdef, 'const char* GetObjectIDCode(UniverseID);')
local nativeCode = C.GetObjectIDCode -- Before identity_game's display-only override.
local U, P, s, c, h, d = A.universe, X4LAN_UNIVERSE_PROTOCOL, A.state, A.coop, A.health, A.shot
local n = { records = {}, states = {}, claims = {}, pending = {}, seq = 0, serial = 0, cursor = 0 }
A.npcCombat = n
local function log(v)
    DebugError('[X4LAN] NPC_COMBAT ' .. v)
end
-- Host NHP cadence. A record is read every poll, but NHP is sent only when its health or
-- death changes, and otherwise repeated with a new sequence every KEEPALIVE seconds. The
-- relay forwards new sequences only; a guest binds health while its newest sequence is
-- under 5 s old, so 1.5 s survives two consecutive lost datagrams. Formerly every read
-- was sent: up to 128 NHP per 0.1 s.
n.KEEPALIVE = 1.5
local function integer(v, max)
    local i = P.integer(tostring(v))
    return i and i <= (max or 2147483647) and i
end
local function percent(v)
    local x = tonumber(v)
    return x and x == x and x >= 0 and x <= 100 and x
end
local function key(id, gen, e, sg)
    return table.concat({ id, gen, e, sg }, '|')
end
local function fresh(now)
    return not A.paused() and h.mdStamp and now - h.mdAt <= 0.5
end
local function validGen(e)
    local g = h.valid[e]
    return g and g > (h.closed[e] or 0) and g
end
local function pilot(e, sg, slot)
    return c.owners[e] == slot
        and c.pilots[e] == slot
        and validGen(e) == sg
        and (
            slot == 'p1'
            or cfg.slot ~= 'p1' and h.live and h.live.entity == e and h.live.gen == sg
            or cfg.slot == 'p1' and h.remote[e] and h.remote[e].gen == sg
        )
end
local function send(fields)
    s.transport:send(table.concat(fields, '|'))
end
local function call(action, args)
    args.stamp, args.from, args.deadline = h.mdStamp, h.mdAge, h.mdAge + 1
    AddUITriggeredEvent('X4LAN_NpcCombat', action, args)
end
function n.code(object)
    return ffi.string(nativeCode(object))
end
function n.fail(reason)
    if n.failed then
        return
    end
    n.failed = true
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
        or c.entity(r.object) > 0
    then
        return false
    end
    if cfg.slot == 'p1' then
        local identity = U.identities[tostring(r.object):gsub('ULL$', '')]
        if not identity or identity.id ~= r.id then
            return false
        end
    end
    local macro, owner = GetComponentData(A.id(r.object), 'macro', 'owner')
    return n.code(r.object) == r.code
        and macro == r.macro
        and owner == r.owner
        and C.GetContextByClass(r.object, 'sector', false) == c.sectorId(r.sector)
end
local function make(id, object, sector, macro, owner, gen)
    if n.serial >= 32768 then
        n.fail('binding limit; reconnect required')
        return
    end
    if not (C.IsComponentClass(object, 'ship_s') or C.IsComponentClass(object, 'ship_m')) then
        return
    end
    n.serial = n.serial + 1
    local r = {
        id = id,
        object = object,
        sector = sector,
        macro = macro,
        owner = owner,
        code = n.code(object),
        gen = gen,
        serial = n.serial,
    }
    if not r.code:match('^[A-Z0-9-]+$') or #r.code > 32 then
        n.fail('native identity unavailable')
        return
    end
    n.records[id] = r
    return r
end
local function args(r)
    return {
        id = r.id,
        gen = r.gen,
        serial = r.serial,
        object = A.id(r.object),
        code = r.code,
        sector = r.sector,
        macro = r.macro,
        owner = r.owner,
    }
end
-- Called before a replica may respawn. A native local death is not host permission.
function n.hidden(r)
    local state = n.states[r.entity.id]
    if state and not state.alive then
        return true
    end
    if r.npcBound and (not r.object or not C.IsComponentOperational(r.object)) then
        n.fail('replica died without a host destruction state')
        return true
    end
    return false
end
function n.hide(replica, now)
    local state = n.states[replica.entity.id]
    local r = n.records[replica.entity.id]
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
function n.receive(f, now)
    if f[1] ~= 'NHP' and f[1] ~= 'NCLAIM' and f[1] ~= 'NACK' then
        return false
    end
    if n.failed or not s.client or f[3] ~= s.client.lease then
        return true
    end
    if f[1] == 'NHP' and cfg.slot ~= 'p1' and #f == 14 and U.latest and f[4] == U.latest.epoch then
        local id, gen, seq = integer(f[5]), integer(f[6]), integer(f[7])
        local hu, sh = percent(f[12]), percent(f[13])
        local r = id and U.replicas[id]
        local old = id and n.states[id]
        if
            not id
            or not gen
            or gen == 0
            or not seq
            or not hu
            or not sh
            or not r
            or r.entity.kind ~= 'ship'
            or f[8] ~= r.sector
            or f[10] ~= r.entity.macro
            or f[11] ~= r.entity.owner
            or f[11] == 'player'
            or not f[9]:match('^[A-Z0-9-]+$')
            or #f[9] > 32
            or (f[14] ~= '0' and f[14] ~= '1')
            or (f[14] == '0' and hu ~= 0)
            or (f[14] == '1' and hu <= 0)
        then
            return true
        end
        if old and (seq <= old.seq or gen ~= old.gen or f[9] ~= old.code or not old.alive) then
            return true
        end
        n.states[id] =
            { id = id, gen = gen, seq = seq, code = f[9], hull = hu, shield = sh, alive = f[14]
                == '1', at = now, epoch = f[4] }
    elseif f[1] == 'NCLAIM' and cfg.slot == 'p1' and #f == 12 and f[5] == s.client.lease then
        local id, gen, e, sg, seq, hits =
            integer(f[6]),
            integer(f[7]),
            integer(f[8], 2),
            integer(f[9]),
            integer(f[10]),
            integer(f[11], 1000000)
        local r = id and n.records[id]
        local sup = e and d.support[e]
        if
            not r
            or r.dead
            or r.gen ~= gen
            or not e
            or e < 1
            or not sg
            or not pilot(e, sg, c.owners[e])
            or not seq
            or not hits
            or not sup
            or f[12] ~= sup.digest
            or not safe(r)
            or not f[4]:match('^[a-f0-9]+$')
            or #f[4] ~= 32
        then
            return true
        end
        local k = key(id, gen, e, sg)
        local p = n.pending[k]
        if p and (seq <= p.seq or hits < p.hits or p.source ~= f[4]) then
            return true
        end
        n.pending[k] = {
            id = id,
            gen = gen,
            e = e,
            sg = sg,
            seq = seq,
            hits = hits,
            source = f[4],
            digest = f[12],
            strength = sup.strength,
            confirmed = p and p.confirmed or 0,
            next = p and p.next or 0,
        }
    elseif
        f[1] == 'NACK'
        and cfg.slot ~= 'p1'
        and #f == 10
        and U.latest
        and f[4] == U.latest.epoch
        and f[5] == s.client.lease
    then
        local id, gen, e, sg, count =
            integer(f[6]), integer(f[7]), integer(f[8], 2), integer(f[9]), integer(f[10], 1000000)
        if id and gen and e and sg and count then
            local p = n.claims[key(id, gen, e, sg)]
            if p and count <= p.hits then
                p.ack = math.max(p.ack, count)
            end
        end
    end
    return true
end
function n.count(payload)
    local f = P.split(tostring(payload), '|')
    if #f ~= 6 or n.failed or cfg.slot == 'p1' then
        return
    end
    local id, gen, serial, e, sg, hits =
        integer(f[1]),
        integer(f[2]),
        integer(f[3]),
        integer(f[4], 2),
        integer(f[5]),
        integer(f[6], 1000000)
    local r = id and n.records[id]
    local state = id and n.states[id]
    if
        not r
        or r.gen ~= gen
        or r.serial ~= serial
        or not state
        or not state.alive
        or not e
        or e < 1
        or not sg
        or not hits
        or not pilot(e, sg, c.owners[e])
        or not d.support[e]
        or not safe(r)
        or (h.now or 0) - state.at >= 5
    then
        return
    end
    local k = key(id, gen, e, sg)
    local p = n.claims[k]
    if not p then
        p = { id = id, gen = gen, e = e, sg = sg, hits = 0, ack = 0 }
        n.claims[k] = p
    end
    p.hits = math.max(p.hits, hits)
end
function n.read(payload)
    local f = P.split(tostring(payload), '|')
    if #f ~= 6 or n.failed or cfg.slot ~= 'p1' then
        return
    end
    local id, gen, serial = integer(f[1]), integer(f[2]), integer(f[3])
    local r = id and n.records[id]
    local hull, shield = percent(f[4]), percent(f[5])
    if
        not r
        or r.gen ~= gen
        or r.serial ~= serial
        or not hull
        or not shield
        or (f[6] ~= '0' and f[6] ~= '1')
    then
        return
    end
    if f[6] == '1' and (r.dead or not safe(r)) then
        return
    end
    r.hull, r.shield, r.dead, r.readAt = hull, shield, f[6] == '0', h.now
    local now = h.now or 0
    local sent = r.sent
    if
        sent
        and sent.hull == hull
        and sent.shield == shield
        and sent.dead == r.dead
        and now - sent.at < n.KEEPALIVE
    then
        return
    end
    r.sent = { hull = hull, shield = shield, dead = r.dead, at = now }
    n.seq = n.seq + 1
    send({
        'NHP',
        1,
        s.client.lease,
        s.client.lease,
        id,
        gen,
        n.seq,
        r.sector,
        r.code,
        r.macro,
        r.owner,
        hull,
        shield,
        r.dead and 0 or 1,
    })
end
function n.applied(payload)
    local f = P.split(tostring(payload), '|')
    if #f ~= 6 or n.failed or cfg.slot ~= 'p1' then
        return
    end
    local id, gen, serial, e, sg, count =
        integer(f[1]),
        integer(f[2]),
        integer(f[3]),
        integer(f[4], 2),
        integer(f[5]),
        integer(f[6], 1000000)
    if not id or not gen or not serial or not e or not sg or not count then
        return
    end
    local p = n.pending[key(id, gen, e, sg)]
    local r = n.records[id]
    if not p or not r or serial ~= r.serial or count > p.hits or not pilot(e, sg, c.owners[e]) then
        return
    end
    p.confirmed = math.max(p.confirmed, count)
    send({ 'NACK', 1, s.client.lease, s.client.lease, p.source, id, gen, e, sg, p.confirmed })
end
function n.retired(payload)
    local f = P.split(tostring(payload), '|')
    if #f ~= 3 or cfg.slot ~= 'p1' then
        return
    end
    local r = n.records[tonumber(f[1])]
    if not r or r.gen ~= tonumber(f[2]) or r.serial ~= tonumber(f[3]) then
        return
    end
    r.retired = true
    local identity = U.identities[tostring(r.object):gsub('ULL$', '')]
    if identity and identity.id == r.id then
        identity.retired = true
    end
end
function n.frame(now)
    if n.failed then
        return
    end
    if U.failed or h.failed or d.failed or A.shotCapture.failed then
        n.fail('dependency stopped')
        return
    end
    if not s.client or not s.client.lease or not fresh(now) then
        return
    end
    if not n.setup and A.shotCapture.ready then
        n.setup = true
        AddUITriggeredEvent('X4LAN_NpcCombat', 'Setup')
        return
    end
    if not n.ready then
        return
    end
    if now >= (n.nextPoll or 0) then
        n.nextPoll = now + 0.1
        local list = cfg.slot == 'p1' and U.publish or U.renderIds or {}
        local count = #list
        for _ = 1, math.min(count, 128) do
            n.cursor = n.cursor % count + 1
            local entry = list[n.cursor]
            local id = cfg.slot == 'p1' and entry.id or entry
            local replica = cfg.slot ~= 'p1' and U.replicas[id]
            local state = n.states[id]
            local r = n.records[id]
            -- A terminal host fact remains actionable when the motion/baseline feed has
            -- expired. Never wait for another pose packet to retire a confirmed death.
            if replica and state and not state.alive then
                n.hide(replica, now)
            end
            if cfg.slot == 'p1' then
                if not r and C.IsComponentOperational(entry.object) then
                    local macro, owner = GetComponentData(A.id(entry.object), 'macro', 'owner')
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
            for _, r in pairs(n.records) do
                if r.dead and U.wanted[r.sector] then
                    dead[#dead + 1] = r
                end
            end
            for i = 1, math.min(#dead, 128) do
                n.deadCursor = (n.deadCursor or 0) % #dead + 1
                local r = dead[n.deadCursor]
                -- The death itself was sent by n.read; repeat it at the keepalive cadence only.
                if not r.sent or not r.sent.dead or now - r.sent.at >= n.KEEPALIVE then
                    r.sent = { hull = 0, shield = 0, dead = true, at = now }
                    n.seq = n.seq + 1
                    send({
                        'NHP',
                        1,
                        s.client.lease,
                        s.client.lease,
                        r.id,
                        r.gen,
                        n.seq,
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
    for k, p in pairs(n.claims) do
        local state = n.states[p.id]
        if
            not state
            or not state.alive
            or state.gen ~= p.gen
            or not pilot(p.e, p.sg, c.owners[p.e])
        then
            n.claims[k] = nil
        elseif
            now - state.at < 5
            and p.hits > p.ack
            and now >= (p.next or 0)
            and d.support[p.e]
        then
            p.next = now + 0.25
            n.seq = n.seq + 1
            send({
                'NCLAIM',
                1,
                s.client.lease,
                state.epoch,
                p.id,
                p.gen,
                p.e,
                p.sg,
                n.seq,
                p.hits,
                d.support[p.e].digest,
            })
        end
    end
    for k, p in pairs(n.pending) do
        local r = n.records[p.id]
        local sup = d.support[p.e]
        if not r or not pilot(p.e, p.sg, c.owners[p.e]) then
            n.pending[k] = nil
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
            a.state = d.stateAck
            if a.state and a.state == d.stateRevision then
                call('Apply', a)
            end
        end
    end
end
local receive, frame = U.receive, U.frame
function U.receive(f, now)
    local ok, handled = pcall(n.receive, f, now)
    if not ok then
        n.fail(handled)
        handled = f[1] == 'NHP' or f[1] == 'NCLAIM' or f[1] == 'NACK'
    end
    if handled then
        return true
    end
    return receive(f, now)
end
function U.frame(now)
    frame(now)
    local ok, err = pcall(n.frame, now)
    if not ok then
        n.fail(err)
    end
end
RegisterEvent('X4LAN_NpcCombat.Ready', function()
    n.ready = true
    log('READY slot=' .. cfg.slot)
end)
RegisterEvent('X4LAN_NpcCombat.Read', function(_, p)
    n.read(p)
end)
RegisterEvent('X4LAN_NpcCombat.Count', function(_, p)
    n.count(p)
end)
RegisterEvent('X4LAN_NpcCombat.Applied', function(_, p)
    n.applied(p)
end)
RegisterEvent('X4LAN_NpcCombat.Retired', function(_, p)
    n.retired(p)
end)
