-- Player life state is independent of the original shared ship and its cargo.
local cfg = X4LAN_CONFIG
local A = X4LAN_BUBBLE_API
if not cfg or not cfg.enabled or not cfg.bubble_actions or not A or not A.state.active then
    return
end
local ffi = require('ffi')
local C = ffi.C
local s = A.state
ffi.cdef([[void RemoveComponent(UniverseID);]])
-- remotes: one body record and spacesuit replica per remote client lease. remote,
-- remoteSuit and remoteKilled mirror the most recently updated remote (two players: the
-- only one). MD tracks one remote suit for kill detection (x4lan_life.xml).
local life = { mode = 'ship', dead = 0, generation = 0, sequence = 0, remotes = {} }
A.life = life
local MAX_REMOTES = 7
local function log(v)
    DebugError('[X4LAN] ' .. v)
end
local function send(value)
    if s.client and s.client.lease then
        s.transport:send(value)
        return true
    end
end
local function number(v, bound)
    local n = tonumber(v)
    if not n or n ~= n or math.abs(n) > bound then
        return nil
    end
    return n
end
function life.receive(client, f, now)
    if A.walk and A.walk.receive(f, now) then
        return true
    end
    local remoteSector
    if f[1] == 'BODY2' and cfg.coop then
        if #f ~= 15 or not f[9]:match('^[%w_.-]+_macro$') or #f[9] > 96 then
            return true
        end
        remoteSector = f[9]
        table.remove(f, 9)
        f[1] = 'BODY'
    elseif f[1] == 'BODY' and cfg.coop then
        return true
    end
    if f[1] == 'BODY' then
        if #f ~= 14 or not f[4]:match('^[a-f0-9]+$') or #f[4] ~= 32 then
            return true
        end
        local seq = number(f[5], 2147483647)
        local dead = number(f[7], 1)
        local gen = number(f[8], 2147483647)
        if
            not seq
            or seq < 0
            or seq % 1 ~= 0
            or not dead
            or dead < 0
            or dead % 1 ~= 0
            or not gen
            or gen < 0
            or gen % 1 ~= 0
        then
            return true
        end
        if f[6] ~= 'ship' and f[6] ~= 'foot' and f[6] ~= 'suit' then
            return true
        end
        if f[6] == 'suit' and gen == 0 then
            return true
        end
        local pose = {}
        for i = 1, 6 do
            pose[i] = number(f[i + 8], i <= 3 and (cfg.coop and 50000000 or 50000) or 2 * math.pi)
            if not pose[i] then
                return true
            end
        end
        local e = life.remotes[f[4]]
        if not e then
            local count = 0
            for _ in pairs(life.remotes) do
                count = count + 1
            end
            if count >= MAX_REMOTES then
                return true
            end
            e = { sequence = -1, lease = f[4] }
            life.remotes[f[4]] = e
        end
        if seq <= e.sequence then
            return true
        end
        e.sequence = seq
        e.remote =
            { mode = f[6], dead = dead, generation = gen, pose = pose, at = now, sector = remoteSector }
        life.remote = e.remote
        if dead == 1 and cfg.slot == 'p1' then
            -- BODY.dead names the sender's original ship. Two players: always ship 2. More
            -- players: only a fixture ship whose current pilot stream comes from this lease.
            local R = A.roster or { players = 2 }
            if R.players == 2 then
                AddUITriggeredEvent('X4LAN_Life', 'ShipDeath', 2)
            else
                for i = 1, 2 do
                    local r = A.coop and A.coop.remote[i]
                    if r and r.sourceLease == f[4] then
                        AddUITriggeredEvent('X4LAN_Life', 'ShipDeath', i)
                    end
                end
            end
        end
        return true
    elseif f[1] == 'SUITKILL' then
        local gen = #f == 5 and number(f[5], 2147483647) or nil
        if cfg.slot ~= 'p1' and gen and gen == life.generation and life.mode == 'suit' then
            AddUITriggeredEvent('X4LAN_Life', 'SuitDeath', gen)
        end
        return true
    end
    return false
end
function life.frame(now)
    if A.walk then
        A.walk.frame(now)
    end
    if not life.setup then
        life.setup = true
        AddUITriggeredEvent('X4LAN_Life', 'Setup')
    end
    if A.paused() then
        return
    end
    if now >= (life.nextSend or 0) and s.client and s.client.lease then
        life.nextSend = now + 0.1
        life.sequence = life.sequence + 1
        local pose = { 0, 0, 0, 0, 0, 0 }
        local mode = life.mode
        if mode == 'suit' then
            local current = C.GetPlayerControlledShipID()
            if current ~= 0 and current ~= s.ship and current ~= s.proxy then
                life.suit = current
            end
        end
        if mode == 'suit' and life.suit and C.IsComponentOperational(life.suit) then
            local p = C.GetObjectPositionInSector(life.suit)
            pose = { p.x, p.y - (cfg.coop and 0 or 10000), p.z, p.yaw, p.pitch, p.roll }
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
            s.client.lease,
            tostring(life.sequence),
            mode,
            tostring(life.dead),
            tostring(life.generation),
        }
        if cfg.coop then
            packet[1] = 'BODY2'
            local object = (mode == 'suit' and life.suit) or C.GetPlayerContainerID()
            local sector = object and object ~= 0 and C.GetContextByClass(object, 'sector', false)
                or s.sector
            if sector == 0 then
                sector = s.sector
            end
            packet[#packet + 1] = GetComponentData(A.id(sector), 'macro')
        end
        for i = 1, 6 do
            packet[#packet + 1] = string.format('%.9g', pose[i])
        end
        send(table.concat(packet, '|'))
        if cfg.slot == 'p1' then
            for _, e in pairs(life.remotes) do
                if e.killed then
                    send('SUITKILL|1|' .. s.client.lease .. '|' .. e.killed)
                end
            end
        end
    end
    local live = {}
    for lease, e in pairs(life.remotes) do
        if s.client and s.client.lease and e.remote and now - e.remote.at >= 30 then
            -- A departed client: remove its replica and forget the record.
            if
                e.suit
                and C.IsComponentOperational(e.suit)
                and C.GetPlayerOccupiedShipID() ~= e.suit
            then
                C.RemoveComponent(e.suit)
            end
            life.remotes[lease] = nil
        else
            live[#live + 1] = e
        end
    end
    for _, e in ipairs(live) do
        life.frameRemote(e, now)
    end
    local latest = nil
    for _, e in ipairs(live) do
        if e.remote and (not latest or e.remote.at >= latest.remote.at) then
            latest = e
        end
    end
    life.remoteSuit, life.remoteKilled =
        latest and latest.suit or nil, latest and latest.killed or nil
end
function life.frameRemote(e, now)
    local remote = e.remote
    if not remote then
        return
    end
    if e.suit and (remote.mode ~= 'suit' or e.generation ~= remote.generation) then
        if C.IsComponentOperational(e.suit) and C.GetPlayerOccupiedShipID() ~= e.suit then
            C.RemoveComponent(e.suit)
        end
        e.suit = nil
        e.killed = nil
    end
    if remote.mode == 'suit' and now - remote.at < 1 and not e.killed then
        local sector = cfg.coop and A.coop.sectorId(remote.sector) or s.sector
        if cfg.coop and not A.coop.sectors[remote.sector] then
            return
        end
        local origin = cfg.coop and 0 or 10000
        if not e.suit then
            local p = remote.pose
            e.suit = C.SpawnObjectAtPos2(
                'ship_arg_xs_spacesuit_01_a_macro',
                sector,
                ffi.new('UIPosRot', { x = p[1], y = p[2] + origin, z = p[3] }),
                'player'
            )
            if e.suit == 0 then
                e.suit = nil
                return
            end
            e.generation = remote.generation
            AddUITriggeredEvent(
                'X4LAN_Life',
                'RemoteSuit',
                { object = A.id(e.suit), generation = remote.generation }
            )
            log('remote spacesuit created; generation=' .. remote.generation)
        end
        if C.IsComponentOperational(e.suit) then
            local p = remote.pose
            local deg = 180 / math.pi
            C.SetObjectSectorPos(
                e.suit,
                sector,
                ffi.new(
                    'UIPosRot',
                    { x = p[1], y = p[2] + origin, z = p[3], yaw = p[4] * deg, pitch = p[5] * deg, roll = p[6]
                        * deg }
                )
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
    for _, e in pairs(life.remotes) do
        if e.suit and e.generation == gen then
            e.killed = gen
        end
    end
    life.remoteKilled = gen
end)
