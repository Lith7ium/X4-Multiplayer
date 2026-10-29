-- Player life state is independent of the original shared ship and its cargo.
local cfg = X4LAN_CONFIG
local api = X4LAN_BUBBLE_API
if not cfg or not cfg.enabled or not cfg.bubble_actions or not api or not api.state.active then
    return
end
local ffi = require('ffi')
local engine = ffi.C
local state = api.state
ffi.cdef([[void RemoveComponent(UniverseID);]])
-- remotes: one body record and spacesuit replica per remote client lease. remote,
-- remoteSuit and remoteKilled mirror the most recently updated remote (two players: the
-- only one). MD tracks one remote suit for kill detection (x4lan_life.xml).
local life = { mode = 'ship', dead = 0, generation = 0, sequence = 0, remotes = {} }
api.life = life
local MAX_REMOTES = 7
local function log(text)
    DebugError('[X4LAN] ' .. text)
end

local function send(value)
    if state.client and state.client.lease then
        state.transport:send(value)
        return true
    end
end

local function number(value, bound)
    local number = tonumber(value)
    if not number or number ~= number or math.abs(number) > bound then
        return nil
    end
    return number
end

function life.receive(client, fields, now)
    if api.walk and api.walk.receive(fields, now) then
        return true
    end
    local remoteSector
    if fields[1] == 'BODY2' and cfg.coop then
        if #fields ~= 15 or not fields[9]:match('^[%w_.-]+_macro$') or #fields[9] > 96 then
            return true
        end
        remoteSector = fields[9]
        table.remove(fields, 9)
        fields[1] = 'BODY'
    elseif fields[1] == 'BODY' and cfg.coop then
        return true
    end
    if fields[1] == 'BODY' then
        if #fields ~= 14 or not fields[4]:match('^[a-f0-9]+$') or #fields[4] ~= 32 then
            return true
        end
        local sequence = number(fields[5], 2147483647)
        local dead = number(fields[7], 1)
        local generation = number(fields[8], 2147483647)
        if
            not sequence
            or sequence < 0
            or sequence % 1 ~= 0
            or not dead
            or dead < 0
            or dead % 1 ~= 0
            or not generation
            or generation < 0
            or generation % 1 ~= 0
        then
            return true
        end
        if fields[6] ~= 'ship' and fields[6] ~= 'foot' and fields[6] ~= 'suit' then
            return true
        end
        if fields[6] == 'suit' and generation == 0 then
            return true
        end
        local pose = {}
        for i = 1, 6 do
            pose[i] =
                number(fields[i + 8], i <= 3 and (cfg.coop and 50000000 or 50000) or 2 * math.pi)
            if not pose[i] then
                return true
            end
        end
        local entry = life.remotes[fields[4]]
        if not entry then
            local count = 0
            for _ in pairs(life.remotes) do
                count = count + 1
            end
            if count >= MAX_REMOTES then
                return true
            end
            entry = { sequence = -1, lease = fields[4] }
            life.remotes[fields[4]] = entry
        end
        if sequence <= entry.sequence then
            return true
        end
        entry.sequence = sequence
        entry.remote = {
            mode = fields[6],
            dead = dead,
            generation = generation,
            pose = pose,
            at = now,
            sector = remoteSector,
        }
        life.remote = entry.remote
        if dead == 1 and cfg.slot == 'p1' then
            -- BODY.dead names the sender's original ship. Two players: always ship 2. More
            -- players: only a fixture ship whose current pilot stream comes from this lease.
            local roster = api.roster or { players = 2 }
            if roster.players == 2 then
                AddUITriggeredEvent('X4LAN_Life', 'ShipDeath', 2)
            else
                for i = 1, 2 do
                    local remotePilot = api.coop and api.coop.remote[i]
                    if remotePilot and remotePilot.sourceLease == fields[4] then
                        AddUITriggeredEvent('X4LAN_Life', 'ShipDeath', i)
                    end
                end
            end
        end
        return true
    elseif fields[1] == 'SUITKILL' then
        local generation = #fields == 5 and number(fields[5], 2147483647) or nil
        if
            cfg.slot ~= 'p1'
            and generation
            and generation == life.generation
            and life.mode == 'suit'
        then
            AddUITriggeredEvent('X4LAN_Life', 'SuitDeath', generation)
        end
        return true
    end
    return false
end

function life.frame(now)
    if api.walk then
        api.walk.frame(now)
    end
    if not life.setup then
        life.setup = true
        AddUITriggeredEvent('X4LAN_Life', 'Setup')
    end
    if api.paused() then
        return
    end
    if now >= (life.nextSend or 0) and state.client and state.client.lease then
        life.nextSend = now + 0.1
        life.sequence = life.sequence + 1
        local pose = { 0, 0, 0, 0, 0, 0 }
        local mode = life.mode
        if mode == 'suit' then
            local current = engine.GetPlayerControlledShipID()
            if current ~= 0 and current ~= state.ship and current ~= state.proxy then
                life.suit = current
            end
        end
        if mode == 'suit' and life.suit and engine.IsComponentOperational(life.suit) then
            local position = engine.GetObjectPositionInSector(life.suit)
            pose = {
                position.x,
                position.y - (cfg.coop and 0 or 10000),
                position.z,
                position.yaw,
                position.pitch,
                position.roll,
            }
            if
                math.max(math.abs(pose[1]), math.abs(pose[2]), math.abs(pose[3]))
                > (cfg.coop and 50000000 or 50000)
            then
                mode = 'foot'
                pose = { 0, 0, 0, 0, 0, 0 }
            end
        elseif mode == 'suit' then
            mode = 'foot' -- Teleport/control change has not finished yet; never spawn a suit at a fabricated origin.
        end
        local packet = {
            'BODY',
            '1',
            state.client.lease,
            tostring(life.sequence),
            mode,
            tostring(life.dead),
            tostring(life.generation),
        }
        if cfg.coop then
            packet[1] = 'BODY2'
            local object = (mode == 'suit' and life.suit) or engine.GetPlayerContainerID()
            local sector = object
                    and object ~= 0
                    and engine.GetContextByClass(object, 'sector', false)
                or state.sector
            if sector == 0 then
                sector = state.sector
            end
            packet[#packet + 1] = GetComponentData(api.id(sector), 'macro')
        end
        for i = 1, 6 do
            packet[#packet + 1] = string.format('%.9g', pose[i])
        end
        send(table.concat(packet, '|'))
        if cfg.slot == 'p1' then
            for _, entry in pairs(life.remotes) do
                if entry.killed then
                    send('SUITKILL|1|' .. state.client.lease .. '|' .. entry.killed)
                end
            end
        end
    end
    local live = {}
    for lease, entry in pairs(life.remotes) do
        if state.client and state.client.lease and entry.remote and now - entry.remote.at >= 30 then
            -- A departed client: remove its replica and forget the record.
            if
                entry.suit
                and engine.IsComponentOperational(entry.suit)
                and engine.GetPlayerOccupiedShipID() ~= entry.suit
            then
                engine.RemoveComponent(entry.suit)
            end
            life.remotes[lease] = nil
        else
            live[#live + 1] = entry
        end
    end
    for _, entry in ipairs(live) do
        life.frameRemote(entry, now)
    end
    local latest = nil
    for _, entry in ipairs(live) do
        if entry.remote and (not latest or entry.remote.at >= latest.remote.at) then
            latest = entry
        end
    end
    life.remoteSuit, life.remoteKilled =
        latest and latest.suit or nil, latest and latest.killed or nil
end

function life.frameRemote(entry, now)
    local remote = entry.remote
    if not remote then
        return
    end
    if entry.suit and (remote.mode ~= 'suit' or entry.generation ~= remote.generation) then
        if
            engine.IsComponentOperational(entry.suit)
            and engine.GetPlayerOccupiedShipID() ~= entry.suit
        then
            engine.RemoveComponent(entry.suit)
        end
        entry.suit = nil
        entry.killed = nil
    end
    if remote.mode == 'suit' and now - remote.at < 1 and not entry.killed then
        local sector = cfg.coop and api.coop.sectorId(remote.sector) or state.sector
        if cfg.coop and not api.coop.sectors[remote.sector] then
            return
        end
        local origin = cfg.coop and 0 or 10000
        if not entry.suit then
            local pose = remote.pose
            entry.suit = engine.SpawnObjectAtPos2(
                'ship_arg_xs_spacesuit_01_a_macro',
                sector,
                ffi.new('UIPosRot', { x = pose[1], y = pose[2] + origin, z = pose[3] }),
                'player'
            )
            if entry.suit == 0 then
                entry.suit = nil
                return
            end
            entry.generation = remote.generation
            AddUITriggeredEvent(
                'X4LAN_Life',
                'RemoteSuit',
                { object = api.id(entry.suit), generation = remote.generation }
            )
            log('remote spacesuit created; generation=' .. remote.generation)
        end
        if engine.IsComponentOperational(entry.suit) then
            local pose = remote.pose
            local degreesPerRadian = 180 / math.pi
            engine.SetObjectSectorPos(
                entry.suit,
                sector,
                ffi.new('UIPosRot', {
                    x = pose[1],
                    y = pose[2] + origin,
                    z = pose[3],
                    yaw = pose[4] * degreesPerRadian,
                    pitch = pose[5] * degreesPerRadian,
                    roll = pose[6] * degreesPerRadian,
                })
            )
        end
    end
end
RegisterEvent('X4LAN_Life.State', function(_, payload)
    local mode, dead, gen = payload:match('^(%a+)|(%d+)|(%d+)$')
    if not mode then
        return
    end
    if mode ~= life.mode or tonumber(dead) ~= life.dead then
        log('player state=' .. mode .. '; original-ship-destroyed=' .. dead)
    end
    life.mode, life.dead, life.generation = mode, tonumber(dead), tonumber(gen)
    life.suit = nil -- The engine can finish changing control after this MD event.
end)
RegisterEvent('X4LAN_Life.RemoteSuitKilled', function(_, generation)
    -- MD names the suit by generation; it belongs to the remote whose replica has it.
    local gen = tonumber(generation)
    for _, entry in pairs(life.remotes) do
        if entry.suit and entry.generation == gen then
            entry.killed = gen
        end
    end
    life.remoteKilled = gen
end)
