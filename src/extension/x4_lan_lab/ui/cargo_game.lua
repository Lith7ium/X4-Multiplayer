-- Original native jettison/magnet bridge. Inventory and drop lifetime belong to host X4.
local API = X4LAN_BUBBLE_API
local cfg = X4LAN_CONFIG
if not API or not API.universe or not cfg.world_sync then
    return
end
local ffi = require('ffi')
local C = ffi.C
local universe = API.universe
local protocol = X4LAN_UNIVERSE_PROTOCOL
local state = API.state
local cargo = {
    drops = {},
    queue = {},
    held = {},
    executed = {},
    inventory = {},
    inventorySeq = 0,
    counter = 0,
    creationSeq = 0,
}
API.cargo = cargo
local function log(v)
    DebugError('[X4LAN] CARGO ' .. v)
end
local function key(v)
    return tostring(v):gsub('ULL$', '')
end
local function send(v)
    if state.client and state.client.lease then
        state.transport:send(v)
    end
end
local function event(name, args)
    AddUITriggeredEvent('X4LAN_Cargo', name, args)
end
local notices = {
    BUSY = 'Cargo transfer was not applied because the ship is busy. Try again when it is free.',
    CAPACITY = 'Cargo pickup was not applied: the host ship has no room for this amount.',
    STALE = 'Cargo transfer was not applied because control of the ship changed.',
    REJECTED = 'Cargo transfer was rejected by the host.',
    UNCERTAIN = 'Cargo outcome is uncertain. End this internal test and collect diagnostics; do not repeat the transfer.',
}
-- MD scalar object strings are hexadecimal. Do not round 64-bit IDs through doubles.
function cargo.object(text)
    if type(text) ~= 'string' or not text:match('^0x[%da-fA-F]+$') or #text > 18 then
        return nil
    end
    local value = ffi.new('uint64_t', 0)
    for digit in text:sub(3):gmatch('.') do
        value = value * 16 + tonumber(digit, 16)
    end
    return value
end
local function finish(request, status, fence)
    cargo.executed[request] = { status = status, fence = fence }
    send('CARGODONE|1|' .. state.client.lease .. '|' .. request .. '|' .. status .. '|' .. fence)
end
local function enqueue(ship, verb, id, ware, amount)
    local b = universe.latest
    if
        not b
        or not protocol.atom(ware)
        or not amount
        or amount <= 0
        or #cargo.queue >= 64
        or API.coop.owners[ship] ~= cfg.slot
        or not API.coop.epochs[ship]
    then
        log('REQUEST_REJECTED missing authority or queue full')
        return false
    end
    cargo.counter = cargo.counter + 1
    local req = state.client.lease:sub(1, 20) .. 'ca00' .. string.format('%08x', cargo.counter)
    cargo.queue[#cargo.queue + 1] = {
        request = req,
        host = b.epoch,
        ship = ship,
        epoch = API.coop.epochs[ship],
        verb = verb,
        drop = id,
        ware = ware,
        amount = amount,
    }
    if id > 0 then
        cargo.held[id] = { request = req }
    end
    return true
end
function cargo.rows(sector)
    local rows = {}
    for k, d in pairs(cargo.drops) do
        if not C.IsComponentOperational(d.object) then
            cargo.drops[k] = nil
        elseif C.GetContextByClass(d.object, 'sector', false) == sector and d.amount > 0 then
            local p = C.GetObjectPositionInSector(d.object)
            local row = string.format(
                '%d,drop,%s,ownerless,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%s,%d',
                d.id,
                d.macro,
                p.x,
                p.y,
                p.z,
                p.yaw,
                p.pitch,
                p.roll,
                d.ware,
                d.amount
            )
            assert(protocol.record(row), 'invalid native drop')
            rows[#rows + 1] = { id = d.id, row = row }
        end
    end
    return rows
end
function cargo.ensure(r, sector)
    local id = r.entity.id
    local h = cargo.held[id]
    if h then
        if not h.fence or not universe.latest or universe.latest.revision < h.fence then
            return
        end
        cargo.held[id] = nil
    end
    if r.object and C.IsComponentOperational(r.object) then
        return
    end
    if r.creating then
        return
    end
    cargo.creationSeq = cargo.creationSeq + 1
    assert(cargo.creationSeq <= 2147483647, 'cargo creation generation exhausted')
    r.creating = cargo.creationSeq
    local e = r.entity
    local p = e.pose
    event('Create', {
        id = id,
        generation = r.creating,
        macro = e.macro,
        ware = e.ware,
        amount = e.amount,
        sector = API.id(sector),
        x = p[1],
        y = p[2],
        z = p[3],
        yaw = p[4],
        pitch = p[5],
        roll = p[6],
    })
end
local receive = universe.receive
function universe.receive(f, now)
    if f[1] == 'CARGOEXEC' then
        if cfg.slot ~= 'p1' or #f ~= 11 or not protocol.hex(f[4]) then
            return true
        end
        local req = f[4]
        local previous = cargo.executed[req]
        if previous then
            if previous.fence then
                finish(req, previous.status, previous.fence)
            end
            return true
        end
        local ship, epoch, id, amount =
            protocol.integer(f[5], 2),
            protocol.integer(f[6]),
            protocol.integer(f[8]),
            protocol.integer(f[10], 1000000)
        if
            not ship
            or ship < 1
            or not epoch
            or not id
            or not amount
            or amount < 1
            or not protocol.atom(f[9])
            or (f[7] ~= 'DROP' and f[7] ~= 'PICKUP')
        then
            return true
        end
        -- No engine mutation occurred, so the existing committed baseline suffices.
        if API.coop.epochs[ship] ~= epoch or API.coop.owners[ship] ~= f[11] then
            finish(req, 'STALE', universe.revision)
            return true
        end
        cargo.executed[req] = { status = 'PENDING' }
        event(
            'Order',
            { request = req, ship = ship, verb = f[7], drop = id, ware = f[9], amount = amount }
        )
        return true
    elseif f[1] == 'CARGOREPLY' then
        if cfg.slot == 'p1' or #f ~= 6 then
            return true
        end
        local q = cargo.queue[1]
        if not q or f[4] ~= q.request or f[5] == 'PENDING' then
            return true
        end
        if f[5] ~= 'OK' and not notices[f[5]] then
            return true
        end
        local fence = protocol.integer(f[6])
        if not fence then
            return true
        end
        -- A baseline may overtake its completion. Its committed revision already
        -- satisfies that fence; waiting for an invented next revision strands a
        -- partially collected drop until unrelated world state changes.
        if q.drop > 0 and cargo.held[q.drop] then
            cargo.held[q.drop].fence = fence
        end
        table.remove(cargo.queue, 1)
        log('RESULT ' .. q.verb .. ' ' .. f[5] .. ' fence=' .. fence)
        if notices[f[5]] then
            cargo.lastNotice = cargo.lastNotice or {}
            if now >= (cargo.lastNotice[f[5]] or -math.huge) + 5 then
                cargo.lastNotice[f[5]] = now
                AddUITriggeredEvent('X4LAN_Trade', 'Notice', notices[f[5]])
            end
        end
        return true
    elseif f[1] == 'CINV' then
        if
            cfg.slot == 'p1'
            or #f ~= 8
            or not universe.latest
            or f[4] ~= universe.latest.epoch
            or not protocol.atom(f[6])
        then
            return true
        end
        local seq, a, b =
            protocol.integer(f[5]), protocol.integer(f[7], 1000000), protocol.integer(f[8], 1000000)
        if not seq or not a or not b or (cargo.inventory[f[6]] or -1) >= seq then
            return true
        end
        cargo.inventory[f[6]] = seq
        event('Inventory', { ware = f[6], counts = { a, b } })
        return true
    end
    return receive(f, now)
end
local function frame(now)
    if not state.client or not state.client.lease or not API.coop.ready or API.paused() then
        return
    end
    if not cargo.setup then
        cargo.setup = true
        event('Setup')
        return
    end
    if not cargo.ready then
        return
    end
    if cfg.slot == 'p1' then
        for k, d in pairs(cargo.drops) do
            if not C.IsComponentOperational(d.object) then
                cargo.drops[k] = nil
                universe.cargoDirty = true
            end
        end
        for request, r in pairs(cargo.executed) do
            if r.after and now >= r.after and not r.fence then
                if not r.revision then
                    r.revision = universe.revision
                    universe.cargoDirty = true
                elseif universe.revision > r.revision then
                    finish(request, r.status, universe.revision)
                end
            end
        end
    else
        if universe.latest then
            for id, h in pairs(cargo.held) do
                if
                    h.fence
                    and universe.latest.revision >= h.fence
                    and not universe.replicas[id]
                then
                    cargo.held[id] = nil
                end
            end
        end
        local q = cargo.queue[1]
        if q and now >= (q.nextSend or 0) then
            q.nextSend = now + 0.25
            send(table.concat({
                'CARGOREQ',
                '1',
                state.client.lease,
                q.request,
                q.host,
                q.ship,
                q.epoch,
                q.verb,
                q.drop,
                q.ware,
                q.amount,
            }, '|'))
        end
    end
end
local originalFrame = universe.frame
function universe.frame(now)
    originalFrame(now)
    if cargo.failed then
        return
    end
    local ok, err = pcall(frame, now)
    if not ok then
        cargo.failed = true
        log('STOPPED ' .. tostring(err))
    end
end
RegisterEvent('X4LAN_Cargo.Ready', function()
    cargo.ready = true
    log('READY slot=' .. cfg.slot)
end)
RegisterEvent('X4LAN_Cargo.HostDrop', function(_, payload)
    if cfg.slot ~= 'p1' or type(payload) ~= 'string' then
        return
    end
    local f = protocol.split(payload, '|')
    local object = cargo.object(f[1])
    local amount = protocol.integer(f[3], 1000000)
    if #f ~= 3 or not object or not amount or amount == 0 or not protocol.atom(f[2]) then
        return
    end
    local k = key(object)
    local d = cargo.drops[k]
    if not d then
        local macro = GetComponentData(API.id(object), 'macro')
        if not protocol.atom(macro, true) then
            return
        end
        universe.nextId = universe.nextId + 1
        assert(universe.nextId <= 2147483647, 'world ID space exhausted')
        d = { id = universe.nextId, object = object, macro = macro }
        cargo.drops[k] = d
        event('Register', { id = d.id, object = API.id(object) })
        log('TRACK id=' .. d.id .. ' object=' .. k .. ' ware=' .. f[2] .. ' amount=' .. amount)
    end
    if d.amount ~= amount or d.ware ~= f[2] then
        universe.cargoDirty = true
    end
    d.ware = f[2]
    d.amount = amount
end)
RegisterEvent('X4LAN_Cargo.Created', function(_, payload)
    if type(payload) ~= 'string' then
        return
    end
    local f = protocol.split(payload, '|')
    local id = protocol.integer(f[1])
    local generation = protocol.integer(f[2])
    local object = cargo.object(f[3])
    local r = id and universe.replicas[id]
    if #f ~= 3 or not generation or not object then
        return
    end
    -- A duplicate callback for the adopted handle must not delete that live replica.
    if r and r.creation == generation and r.object == object then
        return
    end
    if not r or r.entity.kind ~= 'drop' or r.creating ~= generation or cargo.held[id] then
        if C.IsComponentOperational(object) then
            C.RemoveComponent(object)
        end
        if r and r.creating == generation then
            r.creating = nil
        end
        return
    end
    r.object = object
    r.creation = generation
    r.creating = nil
    universe.replicaKeys[key(object)] = true
    log('CREATED id=' .. id .. ' generation=' .. generation .. ' object=' .. key(object))
end)
RegisterEvent('X4LAN_Cargo.GuestDrop', function(_, payload)
    if type(payload) ~= 'string' then
        return
    end
    local f = protocol.split(payload, '|')
    local ship = protocol.integer(f[1], 2)
    local amount = protocol.integer(f[3], 1000000)
    if #f == 4 and ship and ship > 0 and amount and f[3] == f[4] then
        enqueue(ship, 'DROP', 0, f[2], amount)
    else
        log('ROLLBACK_FAILED drop; end this isolated test')
    end
end)
RegisterEvent('X4LAN_Cargo.GuestPickup', function(_, payload)
    if type(payload) ~= 'string' then
        return
    end
    local f = protocol.split(payload, '|')
    local id = protocol.integer(f[1])
    local generation = protocol.integer(f[2])
    local object = cargo.object(f[3])
    local ship = protocol.integer(f[4], 2)
    local amount = protocol.integer(f[6], 1000000)
    if
        #f == 7
        and id
        and generation
        and object
        and ship
        and ship > 0
        and amount
        and f[6] == f[7]
    then
        local r = universe.replicas[id]
        -- Rollback can free room for another native partial collection in the same
        -- frame. Its old replica must not enqueue another intention, including after
        -- a newer baseline has recreated the remaining quantity under the same ID.
        if cargo.held[id] or not r or r.creation ~= generation or r.object ~= object then
            return
        end
        if enqueue(ship, 'PICKUP', id, f[5], amount) then
            if r and r.object then
                if C.IsComponentOperational(r.object) then
                    C.RemoveComponent(r.object)
                end
                universe.replicaKeys[key(r.object)] = nil
                r.object = nil
            end
        end
    else
        log('ROLLBACK_FAILED pickup; end this isolated test')
    end
end)
RegisterEvent('X4LAN_Cargo.Inventory', function(_, payload)
    if
        cfg.slot ~= 'p1'
        or not state.client
        or not state.client.lease
        or type(payload) ~= 'string'
    then
        return
    end
    local f = protocol.split(payload, '|')
    if
        #f ~= 3
        or not protocol.atom(f[1])
        or not protocol.integer(f[2], 1000000)
        or not protocol.integer(f[3], 1000000)
    then
        return
    end
    cargo.inventorySeq = cargo.inventorySeq + 1
    send(
        'CINV|1|'
            .. state.client.lease
            .. '|'
            .. state.client.lease
            .. '|'
            .. cargo.inventorySeq
            .. '|'
            .. payload
    )
end)
RegisterEvent('X4LAN_Cargo.Result', function(_, payload)
    if cfg.slot ~= 'p1' or type(payload) ~= 'string' then
        return
    end
    local f = protocol.split(payload, '|')
    local r = cargo.executed[f[1]]
    if #f == 2 and r and r.status == 'PENDING' then
        r.status = f[2]
        r.after = GetCurRealTime() + 0.25
    end
end)

RegisterEvent('X4LAN_Cargo.CreateFailed', function(_, payload)
    if type(payload) ~= 'string' then
        return
    end
    local f = protocol.split(payload, '|')
    local id = protocol.integer(f[1])
    local generation = protocol.integer(f[2])
    local r = id and universe.replicas[id]
    if #f ~= 4 or not r or not generation or r.creating ~= generation then
        return
    end
    r.creating = nil
    cargo.failed = true
    log('STOPPED native donor cannot create cargo: ' .. payload)
    AddUITriggeredEvent(
        'X4LAN_Trade',
        'Notice',
        'Cargo replication stopped: the native container could not be created. Please end this internal test and collect diagnostics.'
    )
end)
