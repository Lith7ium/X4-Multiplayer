-- Cooperative navigation: stable ship identities, sector-aware poses, one pilot per ship.
-- NAV2 appends source time (seconds) and velocity (m/s, rad/s) to NAV fields.
-- Source clocks need no wall-clock synchronization. Authority/sector transitions reset
-- tracks; motion.lua also resets after pauses or discontinuities and bounds prediction.
-- FIRE2 (entity|epoch|seq|sourceTime|primaryGroup|secondaryGroup|primaryShots|secondaryShots)
-- mirrors the active weapon groups (C API numbering 1-4) and per-trigger shot counters,
-- played back on the NAV2 source clock.
-- Mode contract (one fire protocol per session, chosen by the matched package config):
--  fire2 absent/false: legacy NAV/NAV2 shot counter only; FIRE2 is neither sent nor accepted.
--  fire2=true: FIRE2 only, from startup; the NAV counter never triggers fire. Without the
--   weapon-group API remote fire is disabled with a FIRE2_UNAVAILABLE diagnostic (no fallback).
-- The first FIRE2 of an owner epoch is a baseline and never replays earlier shots.
-- NMODE (entity|epoch|navSeq|sourceTime|mode) accompanies every NAV2 with the same seq and
-- source time; mode is normal|boost|travel|highway. It is a separate optional message:
-- relays/clients without it drop it, and receivers without it infer a 'fast' mode from the
-- sampled speed. Every mode uses a mode-specific interpolation delay reached through a
-- bounded-rate render clock; the per-frame pose write places the replica and the native
-- velocity (all modes) gives it the speed the HUD, target lead and NPC aim read.
-- A local pause stops fire and drops queued edges. On resume, every FIRE2 frame whose source
-- time is not newer than the resume cutoff (NAV2 source clock) is adopted without playing.
local cfg = X4LAN_CONFIG
local bubbleApi = X4LAN_BUBBLE_API
if not cfg or not cfg.coop or not bubbleApi or not bubbleApi.state.active then
    return
end
local ffi = require('ffi')
local engineApi = ffi.C
local bubbleState = bubbleApi.state
pcall(
    ffi.cdef,
    [[uint32_t GetDefensibleActiveWeaponGroup(UniverseID,bool);
void SetDefensibleActiveWeaponGroup(UniverseID,bool,uint32_t);]]
)
local function api(name)
    local ok, f = pcall(function()
        return engineApi[name]
    end)
    return ok and f or nil
end
local getGroup, setGroup =
    api('GetDefensibleActiveWeaponGroup'), api('SetDefensibleActiveWeaponGroup')
local fire2Mode = cfg.fire2 == true
local fire2Ready = fire2Mode and getGroup ~= nil and setGroup ~= nil
if fire2Mode and not fire2Ready then
    DebugError(
        '[X4LAN] FIRE2_UNAVAILABLE: weapon-group API missing; remote fire disabled (no NAV fallback in fire2 mode)'
    )
end
local motionBuffer = assert(X4LAN_MOTION, 'motion buffer missing')
-- One shared ship per client (bubbleApi.roster): 1/2 in a two-player session, 1..N-1 (at
-- least 2) in a dedicated N-player session. Every remote ship has its own motion
-- track, FIRE2 queue and velocity fence; dock attachment stays on ships 1/2.
local shipRoster = bubbleApi.roster or { players = 2, ships = 2, fixture = 2 }
local coopState = {
    ships = {},
    sectors = {},
    resolving = {},
    owners = {},
    epochs = {},
    pilots = {},
    remote = {},
    seq = {},
    shots = {},
    docks = {},
    revision = -1,
    crewSeq = 0,
    metricSeq = 0,
    metrics = { n = 0, sum = 0, max = 0 },
    fire2 = {},
    fire2Seq = {},
    triggerShots = {},
    velocity = {},
    velocityGeneration = 0,
    modes = {},
    fences = {},
    count = shipRoster.ships,
}
for i = 1, coopState.count do
    coopState.seq[i], coopState.shots[i], coopState.docks[i], coopState.fire2Seq[i], coopState.triggerShots[i], coopState.fences[i] =
        0, 0, 0, 0, { 0, 0 }, 0
end
bubbleApi.coop = coopState
-- Correction blend time constant (s) and the velocity hold over refresh gaps (s).
local BLEND, VELOCITY_HOLD = 0.1, 0.25
-- Frame slip: at the next frame the engine pose differs from the last written sector pose
-- by more than SLIP_MIN m (or SLIP_SPEED s of rendered speed). SetObjectSectorPos converts
-- the pose into the ship's current zone, re-parents the ship to the zone at the target and
-- queues the zone-local pose; applied in the new zone it lands offset by the difference of
-- the two zone origins (51-67 km in native logs), and the engine then files the misplaced
-- ship back into the old zone, so the next write repeats it. A second write in the same
-- frame does not help: native 7.6 logs showed 121-164 slips per 5 s with it. The
-- slip vector of a re-parenting write is that zone offset, so it is kept per source zone
-- and subtracted from later writes that start in that zone: the queued local pose is then
-- correct for the target zone and the first write lands. A wrong prediction (another
-- target zone) falls back to an uncompensated rewrite; a slip that survives SLIP_WARP s is
-- moved with the MD warp, at most once per second.
local SLIP_MIN, SLIP_SPEED, SLIP_WARP = 5000, 2, 1
local function admittedSlot(slot)
    local slotNumber = tonumber(tostring(slot):match('^p([1-8])$'))
    return slotNumber and slotNumber <= shipRoster.players
end

local function log(text)
    DebugError('[X4LAN] ' .. text)
end

-- Rate-limited drop diagnostics for remote weapon visuals: one REMOTE_FIRE_DROP row per
-- (kind, reason) at most every 5 s, with the number of drops since the last row.
coopState.drops = {}
function coopState.dropped(kind, entity, reason, now)
    local key = kind .. '|' .. reason
    local d = coopState.drops[key] or { n = 0, next = 0 }
    coopState.drops[key] = d
    d.n = d.n + 1
    now = now or GetCurRealTime()
    if now >= d.next then
        log(
            string.format(
                'REMOTE_FIRE_DROP kind=%s ship=%s reason=%s count=%d',
                kind,
                tostring(entity),
                reason,
                d.n
            )
        )
        d.n = 0
        d.next = now + 5
    end
end

local function integer(value, limit)
    local number = tonumber(value)
    if
        not number
        or number ~= number
        or number < 0
        or number % 1 ~= 0
        or number > (limit or 2147483647)
    then
        return nil
    end
    return number
end

local function numeric(value, bound)
    local number = tonumber(value)
    if not number or number ~= number or math.abs(number) > bound then
        return nil
    end
    return number
end

local function send(packet)
    if bubbleState.client and bubbleState.client.lease then
        bubbleState.transport:send(packet)
    end
end

-- Rate-limited: one COOP_FRAME_SLIP row at most every 5 s, with the repairs since the last.
local slipLog = { n = 0, next = 0 }
local function logSlip(i, slip, action, now)
    slipLog.n = slipLog.n + 1
    if now >= slipLog.next then
        log(
            string.format(
                'COOP_FRAME_SLIP ship=%d slip=%.0f action=%s count=%d',
                i,
                slip,
                action,
                slipLog.n
            )
        )
        slipLog.n = 0
        slipLog.next = now + 5
    end
end

local function clearVelocity(i)
    local v = coopState.velocity[i]
    if not v then
        return
    end
    coopState.velocityGeneration = coopState.velocityGeneration + 1
    coopState.fences[i] = coopState.fences[i] + 1
    AddUITriggeredEvent('X4LAN_Coop', 'VelocityFence', {
        ship = i,
        generation = coopState.velocityGeneration,
        session = v.session,
        epoch = v.epoch,
        enabled = false,
    })
    coopState.velocity[i] = nil
end

function coopState.stopVelocity(discardTracks)
    for i = 1, coopState.count do
        clearVelocity(i)
    end
    if discardTracks then
        coopState.remote = {}
    end
end

local function applyVelocity(i, sector, linear, now)
    if not linear then
        clearVelocity(i)
        return
    end
    for n = 1, 3 do
        if not numeric(linear[n], 100000) then
            clearVelocity(i)
            return
        end
    end
    local parent = engineApi.GetContextByClass(coopState.ships[i], 'zone', false)
    if parent == 0 then
        clearVelocity(i)
        return
    end
    local v = coopState.velocity[i]
    local session = bubbleState.client.lease
    if v and (v.session ~= session or v.epoch ~= coopState.epochs[i] or v.sector ~= sector) then
        clearVelocity(i)
        v = nil
    end
    -- A zone change inside the sector is caused by the pose write itself (temp zones every
    -- few frames at highway speed). It is no boundary: rebind the running vector to the new
    -- zone at once instead of zeroing it (MD revalidates the zone's alignment).
    local rebound = v and v.parent ~= parent
    if rebound then
        v.parent = parent
    end
    if not v then
        coopState.velocityGeneration = coopState.velocityGeneration + 1
        v = {
            generation = coopState.velocityGeneration,
            session = session,
            epoch = coopState.epochs[i],
            sector = sector,
            parent = parent,
            sequence = 0,
        }
        coopState.velocity[i] = v
        coopState.fences[i] = coopState.fences[i] + 1
        AddUITriggeredEvent('X4LAN_Coop', 'VelocityFence', {
            ship = i,
            generation = v.generation,
            session = session,
            epoch = v.epoch,
            enabled = true,
        })
    end
    local changed = not v.linear
    if v.linear then
        for n = 1, 3 do
            if math.abs(linear[n] - v.linear[n]) > 0.0001 then
                changed = true
            end
        end
    end
    -- Changed vectors, especially the packet-loss freeze, never wait for a timer.
    if changed or rebound or now >= (v.next or 0) then
        v.next = now + 0.1
        v.sequence = v.sequence + 1
        v.linear = { linear[1], linear[2], linear[3] }
        local dockStamp, dockAge
        local attached = cfg.coop_docks and i <= 2
        if attached and coopState.attachmentWindow then
            dockStamp, dockAge = coopState.attachmentWindow(i, now)
        end
        AddUITriggeredEvent('X4LAN_Coop', 'Velocity', {
            ship = i,
            generation = v.generation,
            session = v.session,
            epoch = v.epoch,
            sequence = v.sequence,
            sector = bubbleApi.id(sector),
            parent = bubbleApi.id(parent),
            x = linear[1],
            y = linear[2],
            z = linear[3],
            expires = GetCurTime() + 0.3,
            dockGeneration = attached
                    and coopState.attachmentGeneration
                    and coopState.attachmentGeneration(i)
                or nil,
            dockStamp = dockStamp,
            dockAge = dockAge,
        })
    end
end

-- A replica the engine has attached to a highway (its highway context differs from its
-- sector) is moved by the highway itself. No pose write, warp or native vector may touch
-- it then: the mod never puts a replica on a highway, so this only guards an engine capture
-- (for example a pose written into a highway entry) and leaves the ship to the engine.
local function replicaOnHighway(ship)
    local highway = engineApi.GetContextByClass(ship, 'highway', false)
    return highway ~= 0 and highway ~= engineApi.GetContextByClass(ship, 'sector', false)
end

-- Local motion mode of an owned ship. Only the piloted ship exposes boost/travel state.
-- A highway context equal to the sector is not a highway (engine doubles return one id).
local function localMode(ship, sector)
    local highway = engineApi.GetContextByClass(ship, 'highway', false)
    if highway ~= 0 and highway ~= sector then
        return 'highway'
    end
    if engineApi.GetPlayerControlledShipID() ~= ship then
        return 'normal'
    end
    local boosting, travel
    if GetPlayerSpeed then
        local ok, _, _, _, b, t = pcall(GetPlayerSpeed)
        if ok then
            boosting, travel = b, t
        end
    end
    if
        travel == true or (travel == nil and GetPlayerActivity and GetPlayerActivity() == 'travel')
    then
        return 'travel'
    end
    if boosting == true then
        return 'boost'
    end
    return 'normal'
end
coopState.localMode = localMode
local function stopFire2(i)
    local q = coopState.fire2[i]
    if q and (q.on[1] or q.on[2]) then
        AddUITriggeredEvent('X4LAN_Coop', 'Fire2', {
            ship = i,
            epoch = q.epoch,
            primary = false,
            secondary = false,
            primaryShots = 0,
            secondaryShots = 0,
            primaryGroup = q.groups[1],
            secondaryGroup = q.groups[2],
        })
    end
    if q then
        q.on = { false, false }
        q.deadline = { 0, 0 }
        q.budgetUntil = { 0, 0 }
        q.events = {}
    end
end

-- Freeze source-clock boundaries before NAV2 can reset its motion offset. They
-- belong to an authority epoch, independently of FIRE2 baselines and motion tracks.
local function pauseBoundary(now)
    if bubbleApi.paused() then
        coopState.wasPaused = true
        return true
    end
    if coopState.wasPaused then
        coopState.wasPaused = false
        coopState.resumeAt = now
        coopState.resumeCutoffs = coopState.resumeCutoffs or {}
        for i = 1, coopState.count do
            local r = coopState.remote[i]
            local old = coopState.resumeCutoffs[i]
            local floor = old and old.epoch == coopState.epochs[i] and old.cutoff or nil
            local cutoff = r and r.track and r.track.offset and now - r.track.offset or nil
            coopState.resumeCutoffs[i] = {
                epoch = coopState.epochs[i],
                floor = floor,
                cutoff = cutoff and math.max(cutoff, floor or -math.huge) or nil,
            }
        end
    end
    return false
end

local function resumeCutoff(i)
    if not coopState.resumeAt then
        return nil
    end
    local b = coopState.resumeCutoffs[i]
    if not b or b.epoch ~= coopState.epochs[i] then
        b = { epoch = coopState.epochs[i] }
        coopState.resumeCutoffs[i] = b
    end
    if not b.cutoff then
        -- Until a source mapping exists, consume counters without playback. The
        -- first mapped NAV2 sample is also a baseline, never a historical fire edge.
        local r = coopState.remote[i]
        local track = r and r.track
        local last = track and track.samples[#track.samples]
        if track and track.offset and last then
            b.cutoff = math.max(b.floor or -math.huge, coopState.resumeAt - track.offset, last.time)
        end
    end
    return b.cutoff or math.huge
end

function coopState.stopFire()
    for i, r in pairs(coopState.remote) do
        if r.firing then
            AddUITriggeredEvent('X4LAN_Coop', 'Fire', { ship = i, on = false })
        end
        r.firing = false
        r.fireDeadline = 0
    end
    for i in pairs(coopState.fire2) do
        stopFire2(i)
    end
end

function coopState.entity(object)
    for i = 1, coopState.count do
        if coopState.ships[i] and object == coopState.ships[i] then
            return i
        end
    end
    return 0
end

function coopState.sectorId(name)
    if coopState.sectors[name] then
        return coopState.sectors[name]
    end
    if not coopState.resolving[name] then
        coopState.resolving[name] = true
        RegisterEvent('X4LAN_Coop.Sector.' .. name, function(_, object)
            coopState.sectors[name] = ConvertIDTo64Bit(object)
            log('sector mapping ready: ' .. name)
        end)
        AddUITriggeredEvent('X4LAN_Coop', 'ResolveSector', name)
    end
    return nil
end

function coopState.prepare(now)
    if not coopState.setup then
        if bubbleState.ships then
            for i = 1, coopState.count do
                coopState.ships[i] = bubbleState.ships[i]
            end
        else
            local own = shipRoster.own or (cfg.slot == 'p1' and 1 or 2)
            assert(coopState.count == 2 and (own == 1 or own == 2), 'invalid local ship mapping')
            coopState.ships[own] = bubbleState.ship
            coopState.ships[3 - own] = bubbleState.proxy
        end
        for i = 1, coopState.count do
            assert(coopState.ships[i], 'shared ship ' .. i .. ' missing')
        end
        coopState.setup = true
        AddUITriggeredEvent('X4LAN_Coop', 'Setup')
    end
    local m = coopState.metrics
    if coopState.lastFrame then
        local delta = now - coopState.lastFrame
        if delta > 0 then
            m.n = m.n + 1
            m.sum = m.sum + delta
            m.max = math.max(m.max, delta)
        end
    end
    coopState.lastFrame = now
end

function coopState.receive(fields, now)
    pauseBoundary(now) -- Client packets arrive before coop.frame, including NAV2.
    if fields[1] == 'AUTH' then
        if #fields ~= 4 + 3 * coopState.count then
            return true
        end
        local revision = integer(fields[4])
        if not revision or revision < coopState.revision then
            return true
        end
        local owners, epochs, pilots = {}, {}, {}
        for i = 1, coopState.count do
            local index = 2 + i * 3
            owners[i], epochs[i], pilots[i] =
                fields[index], integer(fields[index + 1]), fields[index + 2]
            if
                not admittedSlot(owners[i])
                or not epochs[i]
                or epochs[i] < 1
                or (pilots[i] ~= '0' and pilots[i] ~= owners[i])
            then
                return true
            end
        end
        for i = 1, coopState.count do
            if coopState.epochs[i] ~= epochs[i] or coopState.owners[i] ~= owners[i] then
                clearVelocity(i)
                if coopState.remote[i] and coopState.remote[i].firing then
                    AddUITriggeredEvent('X4LAN_Coop', 'Fire', { ship = i, on = false })
                end
                stopFire2(i)
                coopState.fire2[i] = nil
                coopState.fire2Seq[i] = 0
                coopState.triggerShots[i] = { 0, 0 }
                coopState.remote[i] = nil
                coopState.seq[i] = 0
                coopState.shots[i] = 0
                coopState.motion = coopState.motion or {}
                coopState.motion[i] = nil
                log('ship ' .. i .. ' movement authority=' .. owners[i] .. '; epoch=' .. epochs[i])
            end
        end
        coopState.owners, coopState.epochs, coopState.pilots, coopState.revision =
            owners, epochs, pilots, revision
        return true
    elseif fields[1] == 'NAV' or fields[1] == 'NAV2' then
        local modern = fields[1] == 'NAV2'
        if #fields ~= (modern and 23 or 16) then
            return true
        end
        local entity, epoch, seq =
            integer(fields[5], coopState.count), integer(fields[6]), integer(fields[7])
        local name = fields[8]
        if
            not entity
            or entity < 1
            or not epoch
            or not seq
            or not name:match('^[%w_.-]+_macro$')
            or #name > 96
        then
            return true
        end
        if coopState.owners[entity] == cfg.slot or coopState.epochs[entity] ~= epoch then
            return true
        end
        local pose = {}
        for i = 1, 6 do
            pose[i] = numeric(fields[i + 8], i <= 3 and 50000000 or 2 * math.pi)
            if not pose[i] then
                return true
            end
        end
        local dock, fire = integer(fields[15], 2), integer(fields[16])
        if not dock or not fire then
            return true
        end
        local remote = coopState.remote[entity]
        if remote and seq <= remote.seq then
            return true
        end
        if not remote or remote.sector ~= name then
            clearVelocity(entity)
            -- Same source process, same real-time clock: a sector change keeps its offset.
            local clock = remote
                    and remote.track
                    and remote.sourceLease == fields[4]
                    and remote.track.offset
                or nil
            remote = {
                samples = {},
                fire = fire,
                sector = name,
                clock = clock,
                motion = remote and remote.sourceLease == fields[4] and remote.motion or {},
            }
            coopState.remote[entity] = remote
        end
        if modern then
            local velocity = {}
            for i = 1, 6 do
                velocity[i] = tonumber(fields[17 + i])
            end
            local track = remote.track
                or {
                    retainClock = true,
                    leak = motionBuffer.leak,
                    offset = remote.clock,
                    adaptive = true,
                }
            local declared = coopState.modes[entity]
            declared = declared
                    and declared.epoch == epoch
                    and declared.lease == fields[4]
                    and now - declared.at <= motionBuffer.stale
                    and declared.mode
                or nil
            track.mode = declared
            if not motionBuffer.push(track, tonumber(fields[17]), pose, velocity, now) then
                return true
            end
            remote.track = track
            resumeCutoff(entity) -- Latch an unknown mapping at its first valid NAV2 baseline.
        else
            remote.track = nil
        end
        remote.seq, remote.at, remote.dock, remote.sourceLease = seq, now, dock, fields[4]
        -- In fire2 mode the NAV counter never triggers fire; there is no protocol transition.
        if fire > remote.fire and not fire2Mode then
            remote.fireDeadline = now + 0.35
        end
        remote.fire = fire
        remote.samples[#remote.samples + 1] = { time = now, pose = pose }
        while #remote.samples > 5 do
            table.remove(remote.samples, 1)
        end
        return true
    elseif fields[1] == 'NMODE' then
        if #fields ~= 9 then
            return true
        end
        local entity, epoch, seq, time, mode =
            integer(fields[5], coopState.count),
            integer(fields[6]),
            integer(fields[7]),
            numeric(fields[8], 1e12),
            fields[9]
        if
            not entity
            or entity < 1
            or not epoch
            or not seq
            or not time
            or time < 0
            or not motionBuffer.modes[mode]
        then
            return true
        end
        if coopState.owners[entity] == cfg.slot or coopState.epochs[entity] ~= epoch then
            return true
        end
        local old = coopState.modes[entity]
        if old and old.epoch == epoch and old.lease == fields[4] and seq <= old.seq then
            return true
        end
        coopState.modes[entity] =
            { epoch = epoch, lease = fields[4], seq = seq, time = time, mode = mode, at = now }
        return true
    elseif fields[1] == 'FIRE2' then
        if #fields ~= 12 then
            return true
        end
        local entity, epoch, seq =
            integer(fields[5], coopState.count), integer(fields[6]), integer(fields[7])
        local time = numeric(fields[8], 1e12)
        local groups = { integer(fields[9], 4), integer(fields[10], 4) }
        local shots = { integer(fields[11]), integer(fields[12]) }
        if not entity or entity < 1 or not epoch or not seq or not time or time < 0 then
            coopState.dropped('FIRE2', fields[5], 'malformed', now)
            return true
        end
        if
            not groups[1]
            or groups[1] < 1
            or not groups[2]
            or groups[2] < 1
            or not shots[1]
            or not shots[2]
        then
            coopState.dropped('FIRE2', entity, 'malformed', now)
            return true
        end
        if not fire2Ready then
            coopState.dropped('FIRE2', entity, 'weapon-group-api-missing', now)
            return true
        end
        if coopState.owners[entity] == cfg.slot then
            coopState.dropped('FIRE2', entity, 'own-ship', now)
            return true
        end
        if coopState.epochs[entity] ~= epoch then
            coopState.dropped(
                'FIRE2',
                entity,
                'epoch ' .. tostring(coopState.epochs[entity]) .. '~=' .. epoch,
                now
            )
            return true
        end
        local fireQueue = coopState.fire2[entity]
        if not fireQueue or fireQueue.epoch ~= epoch then
            -- Baseline: adopt the counters without replaying them.
            coopState.fire2[entity] = {
                epoch = epoch,
                seq = seq,
                time = time,
                shots = shots,
                groups = groups,
                at = now,
                events = {},
                on = { false, false },
                deadline = { 0, 0 },
                budgetUntil = { 0, 0 },
                group = { groups[1], groups[2] },
            }
            return true
        end
        if
            seq <= fireQueue.seq
            or time <= fireQueue.time
            or shots[1] < fireQueue.shots[1]
            or shots[2] < fireQueue.shots[2]
        then
            return true
        end
        -- Paused or pre-resume history (source time at or before the cutoff) is adopted, never played.
        local paused = pauseBoundary(now)
        local cutoff = resumeCutoff(entity)
        if not paused and not (cutoff and time <= cutoff) then
            for k = 1, 2 do
                if shots[k] > fireQueue.shots[k] or groups[k] ~= fireQueue.groups[k] then
                    fireQueue.events[#fireQueue.events + 1] = {
                        time = time,
                        trigger = k,
                        group = groups[k],
                        count = math.min(256, shots[k] - fireQueue.shots[k]),
                    }
                end
            end
        end
        while #fireQueue.events > 32 do
            table.remove(fireQueue.events, 1)
        end
        fireQueue.seq, fireQueue.time, fireQueue.shots, fireQueue.groups, fireQueue.at =
            seq, time, shots, groups, now
        return true
    elseif fields[1] == 'PILOT_DENIED' then
        local seq = #fields == 5 and integer(fields[4]) or nil
        if seq and seq <= (coopState.crewSeq or 0) and seq > (coopState.deniedSeq or -1) then
            coopState.deniedSeq = seq
            local entity = integer(fields[5], coopState.count)
            if
                entity
                and entity > 0
                and coopState.entity(engineApi.GetPlayerControlledShipID()) == entity
                and coopState.pilots[entity] ~= cfg.slot
            then
                AddUITriggeredEvent('X4LAN_Coop', 'LeaveControls', entity)
                log('pilot request denied: the ship is occupied, trading or destroyed')
            end
        end
        return true
    elseif fields[1] == 'COOP_EXEC' then
        if #fields ~= 7 or not integer(fields[5], 2) or tonumber(fields[5]) < 1 then
            return true
        end
        fields[1] = 'ORDER_EXEC'
        fields[5] = 'p' .. fields[5]
        if bubbleApi.trade then
            return bubbleApi.trade.receive(bubbleState.client, fields, now)
        end
        return true
    end
    return false
end

local function declaredMode(i, remote)
    local modeState = coopState.modes[i]
    if
        modeState
        and modeState.epoch == coopState.epochs[i]
        and modeState.lease == remote.sourceLease
        and remote.at
        and math.abs(remote.at - modeState.at) <= motionBuffer.stale
    then
        return modeState.mode
    end
    return nil
end

local function render(remote, now, i)
    if not remote or #remote.samples == 0 then
        return nil
    end
    if remote.track then
        local track = remote.track
        local last = track.samples and track.samples[#track.samples]
        local mode, profile = motionBuffer.classify(
            remote.motion,
            declaredMode(i, remote),
            last and motionBuffer.speed(last.velocity) or 0,
            now
        )
        track.adaptive = true
        remote.mode = mode
        remote.delay = motionBuffer.delayFor(track, profile.delay)
        remote.target = motionBuffer.clock(track, now, remote.delay)
        -- A widened delay also widens the prediction horizon (bounded at 1 s).
        return motionBuffer.renderAt(
            track,
            remote.target,
            math.max(profile.horizon, math.min(1, remote.delay))
        )
    end
    if now - remote.at > 0.75 then
        return nil
    end
    local target = now - 0.1
    local earlier, later = remote.samples[1], remote.samples[#remote.samples]
    for i = 2, #remote.samples do
        if remote.samples[i].time >= target then
            later = remote.samples[i]
            break
        end
        earlier = remote.samples[i]
    end
    local fraction = later.time > earlier.time
            and math.max(0, math.min(1, (target - earlier.time) / (later.time - earlier.time)))
        or 0
    local pose = {}
    for i = 1, 6 do
        if i <= 3 then
            pose[i] = earlier.pose[i] + (later.pose[i] - earlier.pose[i]) * fraction
        else
            pose[i] = earlier.pose[i]
                + ((later.pose[i] - earlier.pose[i] + math.pi) % (2 * math.pi) - math.pi)
                    * fraction
        end
    end
    return pose
end

-- Play FIRE2 trigger evidence back at the same source time as the rendered NAV2 pose.
local function fire2Frame(i, remote, ship, now)
    -- Observed-projectile mode never aims/fires a native replay gun.
    if cfg.projectile_visuals or (cfg.shot_visual and i > 2) then
        -- Projectile and harmless-shot visuals exist for ships 1/2 only: remote fire of ships
        -- 3.. is not drawn in those modes (fail closed, logged once per ship).
        if i > 2 and coopState.fire2[i] and not (coopState.fireLimitLogged or {})[i] then
            coopState.fireLimitLogged = coopState.fireLimitLogged or {}
            coopState.fireLimitLogged[i] = true
            log(
                'FIRE_VISUAL_LIMIT ship='
                    .. i
                    .. ': remote fire is not drawn for ships beyond 2 in projectile/shot visual mode'
            )
        end
        stopFire2(i)
        return
    end
    local fireQueue = coopState.fire2[i]
    if not fireQueue then
        return
    end
    local visualGen = bubbleApi.health and bubbleApi.health.valid[i]
    local visualReady = not cfg.shot_visual
        or bubbleApi.shotVisual and bubbleApi.shotVisual.readyReplay(i, visualGen, now)
    if
        not visualReady
        or now - fireQueue.at > 0.35
        or coopState.owners[i] == cfg.slot
        or not engineApi.IsComponentOperational(ship)
    then
        -- Freshness: stop even if motion keeps rendering a frozen prediction.
        stopFire2(i)
        return
    end
    local target = (remote and remote.track and remote.track.offset)
            and (remote.target or now - remote.track.offset - motionBuffer.delay)
        or math.huge
    local cutoff = resumeCutoff(i)
    local keep = {}
    local grant = { 0, 0 }
    local reset = { false, false }
    local changed = false
    fireQueue.budgetUntil = fireQueue.budgetUntil or { 0, 0 }
    fireQueue.group = fireQueue.group or { fireQueue.groups[1], fireQueue.groups[2] }
    for trigger = 1, 2 do
        if fireQueue.budgetUntil[trigger] > 0 and now >= fireQueue.budgetUntil[trigger] then
            reset[trigger] = true
            fireQueue.budgetUntil[trigger] = 0
            fireQueue.deadline[trigger] = 0
            changed = true
        end
    end
    for _, event in ipairs(fireQueue.events) do
        if cutoff and event.time <= cutoff then
        -- Pre-resume history is never played, even if it was queued before the cutoff existed.
        elseif event.time <= target then
            if target - event.time <= motionBuffer.stale then
                local trigger = event.trigger
                if fireQueue.group[trigger] ~= event.group then
                    -- Never spend earlier-group credit on a later group, even within one frame.
                    grant[trigger] = 0
                    reset[trigger] = true
                    fireQueue.deadline[trigger] = 0
                    fireQueue.budgetUntil[trigger] = 0
                    fireQueue.group[trigger] = event.group
                    changed = true
                end
                if event.count > 0 then
                    if fireQueue.budgetUntil[trigger] == 0 then
                        fireQueue.budgetUntil[trigger] = now + 0.35
                    end
                    fireQueue.deadline[trigger] = fireQueue.budgetUntil[trigger]
                    grant[trigger] = math.min(256, grant[trigger] + event.count)
                end
            end
        else
            keep[#keep + 1] = event
        end
    end
    fireQueue.events = keep
    local on = { fireQueue.deadline[1] > now, fireQueue.deadline[2] > now }
    if
        changed
        or on[1] ~= fireQueue.on[1]
        or on[2] ~= fireQueue.on[2]
        or grant[1] > 0
        or grant[2] > 0
    then
        -- MD stops the old group before activating a new one, in the same action block.
        fireQueue.on = on
        AddUITriggeredEvent('X4LAN_Coop', 'Fire2', {
            ship = i,
            epoch = fireQueue.epoch,
            primary = on[1],
            secondary = on[2],
            primaryShots = grant[1],
            secondaryShots = grant[2],
            primaryGroup = fireQueue.group[1],
            secondaryGroup = fireQueue.group[2],
            resetPrimary = reset[1],
            resetSecondary = reset[2],
            visual = cfg.shot_visual == true,
            visualGen = visualGen or 0,
        })
    end
end

local function publishMetrics(now)
    if now >= (coopState.nextMetric or 0) then
        coopState.nextMetric = now + 1
        coopState.metricSeq = coopState.metricSeq + 1
        local m = coopState.metrics
        local object = engineApi.GetPlayerContainerID()
        local sector = object ~= 0 and engineApi.GetContextByClass(object, 'sector', false) or 0
        local name = sector ~= 0 and GetComponentData(bubbleApi.id(sector), 'macro') or 'unknown'
        local mode = bubbleApi.paused() and 'paused'
            or (bubbleApi.life and bubbleApi.life.mode or 'waiting')
        send(
            string.format(
                'PERF|1|%s|%d|%.2f|%.2f|%.2f|%s|%s',
                bubbleState.client.lease,
                coopState.metricSeq,
                m.sum > 0 and m.n / m.sum or 0,
                m.n > 0 and 1000 * m.sum / m.n or 0,
                m.max * 1000,
                mode,
                name
            )
        )
        coopState.metrics = { n = 0, sum = 0, max = 0 }
    end
end

-- Opt-in (cfg.motion_diagnostics = seconds between rows, or true for 0.5 s): requested
-- vs actual replica motion. Inert and unallocated by default; no session data is logged.
local diagnose
if cfg.motion_diagnostics then
    local interval = tonumber(cfg.motion_diagnostics) or 0.5
    local d = {}
    local function dist(a, b)
        return math.sqrt((a[1] - b[1]) ^ 2 + (a[2] - b[2]) ^ 2 + (a[3] - b[3]) ^ 2)
    end
    diagnose = function(i, r, ship, p, linear, changed, now)
        local a = engineApi.GetObjectPositionInSector(ship)
        local actual = { a.x, a.y, a.z }
        local q = d[i] or {}
        d[i] = q
        -- drift: how far the engine moved the replica away from the last requested pose.
        local drift = r.applied and dist(actual, r.applied) or 0
        q.maxDrift = math.max(q.maxDrift or 0, drift)
        q.frames = (q.frames or 0) + 1
        q.writes = (q.writes or 0) + (changed and 1 or 0)
        q.maxError = math.max(q.maxError or 0, dist(p, actual))
        if now < (q.next or 0) then
            return
        end
        q.next = now + interval
        local own = engineApi.GetPlayerContainerID()
        local range = -1
        if
            own ~= 0
            and engineApi.GetContextByClass(own, 'sector', false)
                == engineApi.GetContextByClass(ship, 'sector', false)
        then
            local o = engineApi.GetObjectPositionInSector(own)
            range = dist({ o.x, o.y, o.z }, actual)
        end
        local m = coopState.modes[i]
        local track = r.track or {}
        -- request/drift are per-frame travel (speed * frame time) when native velocity runs;
        -- error is the real target error before this frame's write.
        log(
            string.format(
                'MOTION_DIAG ship=%d mode=%s declared=%s speed=%.1f request=%.2f drift=%.2f maxDrift=%.2f error=%.2f maxError=%.2f range=%.0f writes=%d/%d fences=%d resets=%d lag=%.3f delay=%.3f excess=%.3f drainMax=%d velocity=%s',
                i,
                r.mode or 'legacy',
                m and m.mode or 'none',
                linear and motionBuffer.speed(linear) or 0,
                r.applied and dist(p, r.applied) or 0,
                drift,
                q.maxDrift,
                dist(p, actual),
                q.maxError or 0,
                range,
                q.writes,
                q.frames,
                coopState.fences[i],
                track.resets or 0,
                (track.lag or 0) - (track.offset or 0),
                r.delay or 0,
                track.excess or 0,
                bubbleState.client and bubbleState.client.drainedMax or 0,
                coopState.velocity[i] and 'native' or 'off'
            )
        )
        q.maxDrift, q.frames, q.writes, q.maxError = 0, 0, 0, 0
        if bubbleState.client then
            bubbleState.client.drainedMax = 0
        end
    end
end

-- MD warp of a remote replica into the received sector pose, at most once per second.
local function warp(i, remote, sector, value, now)
    if now < (remote.nextWarp or 0) then
        return false
    end
    remote.nextWarp = now + 1
    value.ship = i
    value.sector = bubbleApi.id(sector)
    if cfg.coop_docks and i <= 2 then
        value.dockGeneration = coopState.attachmentGeneration and coopState.attachmentGeneration(i)
        if coopState.attachmentWindow then
            value.dockStamp, value.dockAge = coopState.attachmentWindow(i, now)
        end
    end
    AddUITriggeredEvent('X4LAN_Coop', 'SectorMove', value)
    return true
end

local function zoneOf(ship)
    return engineApi.GetContextByClass(ship, 'zone', false)
end

-- One pose write. slip/dx,dy,dz: slip found before this frame's write (0 if none).
local function writePose(i, remote, ship, sector, value, written, slip, dx, dy, dz, now)
    if slip > 0 and remote.wFrom and remote.wFrom ~= remote.wTo then
        -- The last frame's first write re-parented the ship: its slip is that zone offset
        -- (plus the error of the compensation it already used).
        local k = remote.wK
        remote.zoneFix = remote.zoneFix or {}
        remote.zoneFixes = (remote.zoneFixes or 0) + 1
        if remote.zoneFixes > 16 then
            remote.zoneFix = {}
            remote.zoneFixes = 1
        end
        remote.zoneFix[tostring(remote.wFrom)] =
            { to = remote.wTo, k = { k[1] + dx, k[2] + dy, k[3] + dz } }
    end
    local from = zoneOf(ship)
    local fix = remote.zoneFix and remote.zoneFix[tostring(from)]
    local k = fix and fix.k
    local x, y, z = value.x, value.y, value.z
    if k then
        value.x, value.y, value.z = x - k[1], y - k[2], z - k[3]
    end
    engineApi.SetObjectSectorPos(ship, sector, ffi.new('UIPosRot', value))
    remote.applied = written
    local to = zoneOf(ship)
    remote.wFrom, remote.wTo, remote.wK = from, to, k or { 0, 0, 0 }
    value.x, value.y, value.z = x, y, z
    local action
    if k and to ~= fix.to then
        -- Predicted target zone missed: drop the offset and write uncompensated.
        remote.zoneFix[tostring(from)] = nil
        engineApi.SetObjectSectorPos(ship, sector, ffi.new('UIPosRot', value))
        action = 'rewrite'
    elseif slip > 0 then
        action = k and 'zone' or 'rewrite'
    end
    if slip > 0 then
        if now - remote.slipSince >= SLIP_WARP and warp(i, remote, sector, value, now) then
            remote.slipSince = nil
            action = 'warp'
        elseif action == 'rewrite' and not k then
            engineApi.SetObjectSectorPos(ship, sector, ffi.new('UIPosRot', value))
        end
    end
    if action then
        logSlip(i, slip, action, now)
    end
end

function coopState.frame(now)
    if not coopState.ready or not bubbleState.client or not bubbleState.client.lease then
        coopState.stopVelocity(false)
        return
    end
    local controlled = coopState.entity(engineApi.GetPlayerControlledShipID())
    local aboard = coopState.entity(engineApi.GetPlayerContainerID())
    coopState.controlled = controlled
    if now >= (coopState.nextCrew or 0) then
        coopState.nextCrew = now + 0.1
        coopState.crewSeq = coopState.crewSeq + 1
        send(
            'CREW|1|'
                .. bubbleState.client.lease
                .. '|'
                .. coopState.crewSeq
                .. '|'
                .. controlled
                .. '|'
                .. aboard
        )
    end
    publishMetrics(now)
    if pauseBoundary(now) then
        coopState.stopVelocity(false)
        -- FIRE2 never resumes from pre-pause history: stop active fire and drop queued edges.
        for i in pairs(coopState.fire2) do
            stopFire2(i)
        end
        return
    end
    if now >= (coopState.nextNav or 0) then
        coopState.nextNav = now + 0.05
        for i = 1, coopState.count do
            local ship = coopState.ships[i]
            if coopState.owners[i] == cfg.slot and engineApi.IsComponentOperational(ship) then
                local sector = engineApi.GetContextByClass(ship, 'sector', false)
                if sector ~= 0 then
                    local name = GetComponentData(bubbleApi.id(sector), 'macro')
                    coopState.sectors[name] = sector
                    local p = engineApi.GetObjectPositionInSector(ship)
                    coopState.seq[i] = coopState.seq[i] + 1
                    if cfg.coop_docks and i <= 2 and coopState.publishDock then
                        coopState.publishDock(i, coopState.seq[i], now)
                    end
                    local fields = {
                        'NAV2',
                        '1',
                        bubbleState.client.lease,
                        tostring(i),
                        tostring(coopState.epochs[i]),
                        tostring(coopState.seq[i]),
                        name,
                    }
                    for _, value in ipairs({ p.x, p.y, p.z, p.yaw, p.pitch, p.roll }) do
                        fields[#fields + 1] = string.format('%.9g', value)
                    end
                    fields[#fields + 1] = tostring(coopState.docks[i])
                    fields[#fields + 1] = tostring(coopState.shots[i])
                    local values = { p.x, p.y, p.z, p.yaw, p.pitch, p.roll }
                    coopState.motion = coopState.motion or {}
                    local velocity = motionBuffer.velocity(coopState.motion[i], values, now)
                    coopState.motion[i] = { time = now, pose = values }
                    fields[#fields + 1] = string.format('%.6f', now)
                    for _, v in ipairs(velocity) do
                        fields[#fields + 1] = string.format('%.9g', v)
                    end
                    send(table.concat(fields, '|'))
                    send(
                        string.format(
                            'NMODE|1|%s|%d|%d|%d|%.6f|%s',
                            bubbleState.client.lease,
                            i,
                            coopState.epochs[i],
                            coopState.seq[i],
                            now,
                            localMode(ship, sector)
                        )
                    )
                    -- FIRE2 shares NAV2's source time; a group outside 1-4 is never encoded.
                    local primary, secondary =
                        fire2Ready and getGroup(ship, true) or 0,
                        fire2Ready and getGroup(ship, false) or 0
                    if
                        fire2Ready
                        and primary >= 1
                        and primary <= 4
                        and secondary >= 1
                        and secondary <= 4
                    then
                        coopState.fire2Seq[i] = coopState.fire2Seq[i] + 1
                        local shots = coopState.triggerShots[i]
                        send(
                            string.format(
                                'FIRE2|1|%s|%d|%d|%d|%.6f|%d|%d|%d|%d',
                                bubbleState.client.lease,
                                i,
                                coopState.epochs[i],
                                coopState.fire2Seq[i],
                                now,
                                primary,
                                secondary,
                                shots[1],
                                shots[2]
                            )
                        )
                    end
                end
            end
        end
    end
    for i = 1, coopState.count do
        local r = coopState.remote[i]
        local ship = coopState.ships[i]
        local p, linear = render(r, now, i)
        local velocityApplied = false
        if r and r.firing and (not p or now > (r.fireDeadline or 0)) then
            r.firing = false
            AddUITriggeredEvent('X4LAN_Coop', 'Fire', { ship = i, on = false })
        end
        if
            p
            and coopState.owners[i] ~= cfg.slot
            and engineApi.IsComponentOperational(ship)
            and engineApi.GetPlayerControlledShipID() ~= ship
        then
            if (r.fireDeadline or 0) > now and not r.firing then
                r.firing = true
                AddUITriggeredEvent('X4LAN_Coop', 'Fire', { ship = i, on = true })
            end
            local sector = coopState.sectorId(r.sector)
            if
                not cfg.coop_docks
                and coopState.docks[i] ~= r.dock
                and sector
                and now >= (r.nextDock or 0)
            then
                r.nextDock = now + 1
                AddUITriggeredEvent('X4LAN_Coop', 'Dock', {
                    ship = i,
                    market = r.dock,
                    sector = bubbleApi.id(sector),
                    x = p[1],
                    y = p[2],
                    z = p[3],
                    yaw = p[4] * 180 / math.pi,
                    pitch = p[5] * 180 / math.pi,
                    roll = p[6] * 180 / math.pi,
                })
            end
            -- The legacy market index is zero at ordinary stations too. Recheck the
            -- actual native attachment before a synchronous pose write; a stale/free
            -- network sample must not pull a locally docked ship out of its parent.
            -- Dock attachment covers ships 1/2; ships 3.. follow the received pose only.
            local dockAllowed = not cfg.coop_docks
                or i > 2
                or (coopState.dockPoseAllowed and coopState.dockPoseAllowed(i, r, now))
            local attached = replicaOnHighway(ship)
            if attached ~= (r.highwayAttached == true) then
                r.highwayAttached = attached or nil
                log(
                    string.format(
                        'REPLICA_HIGHWAY ship=%d state=%s mode=%s',
                        i,
                        attached and 'attached' or 'released',
                        r.mode or 'legacy'
                    )
                )
            end
            if attached then
                -- The last written pose and zone offsets do not survive the engine's highway move.
                r.applied = nil
                r.slipSince = nil
                r.wFrom = nil
                r.zoneFix = nil
            elseif
                dockAllowed
                and sector
                and r.dock == 0
                and coopState.docks[i] == 0
                and GetComponentData(bubbleApi.id(ship), 'isdocked') ~= true
            then
                local value = {
                    x = p[1],
                    y = p[2],
                    z = p[3],
                    yaw = p[4] * 180 / math.pi,
                    pitch = p[5] * 180 / math.pi,
                    roll = p[6] * 180 / math.pi,
                }
                if engineApi.GetContextByClass(ship, 'sector', false) ~= sector then
                    -- The last written pose belongs to the old sector frame; it is no slip reference.
                    if warp(i, r, sector, value, now) then
                        r.applied = nil
                        r.slipSince = nil
                        r.wFrom = nil
                        r.zoneFix = nil
                    end
                elseif now >= (r.nextApply or 0) then
                    -- One shared ship per client (at most seven). Correct their native controller
                    -- drift on every render frame, including frame rates above 60 FPS.
                    r.nextApply = now
                    local changed = not r.applied
                    if r.applied then
                        for n = 1, 6 do
                            if math.abs(p[n] - r.applied[n]) > (n <= 3 and 0.01 or 0.0001) then
                                changed = true
                            end
                        end
                    end
                    local slip, dx, dy, dz = 0, 0, 0, 0
                    if r.applied then
                        local a = engineApi.GetObjectPositionInSector(ship)
                        dx, dy, dz = a.x - r.applied[1], a.y - r.applied[2], a.z - r.applied[3]
                        slip = math.sqrt(dx * dx + dy * dy + dz * dz)
                    end
                    if
                        slip
                        > math.max(
                            SLIP_MIN,
                            SLIP_SPEED * (linear and motionBuffer.speed(linear) or 0)
                        )
                    then
                        changed = true
                        r.slipSince = r.slipSince or now
                    else
                        slip = 0
                        r.slipSince = nil
                    end
                    if not changed then
                        -- The native controller/physics can move or turn a ship even when its
                        -- received pose stays constant. Compare the actual engine pose before
                        -- skipping a correction; the cached requested pose is not a readback.
                        local actual = engineApi.GetObjectPositionInSector(ship)
                        local current =
                            { actual.x, actual.y, actual.z, actual.yaw, actual.pitch, actual.roll }
                        for n = 1, 6 do
                            local delta = current[n] - p[n]
                            if
                                math.abs(n <= 3 and delta or motionBuffer.angle(delta))
                                > (n <= 3 and 0.01 or 0.0001)
                            then
                                changed = true
                                break
                            end
                        end
                    end
                    -- A late sample or clock slew makes the rendered target hop against its own
                    -- extrapolation (previous target + velocity * frame time). In normal flight a small
                    -- hop is absorbed into an offset that decays over ~BLEND s instead of being written
                    -- in one frame; large hops, pose-only modes and engine-drift repairs stay exact.
                    local written = p
                    local dt = math.max(0, now - (r.frameAt or now))
                    local steady = linear
                        and r.lastLinear
                        and motionBuffer.speed(linear) > 1
                        and math.sqrt(
                                (linear[1] - r.lastLinear[1]) ^ 2
                                    + (linear[2] - r.lastLinear[2]) ^ 2
                                    + (linear[3] - r.lastLinear[3]) ^ 2
                            )
                            <= 0.1 * motionBuffer.speed(linear)
                    if
                        coopState.velocity[i]
                        and (r.mode or 'normal') == 'normal'
                        and r.lastTarget
                        and steady
                        and dt > 0
                        and dt <= 0.25
                    then
                        local hop = {}
                        for n = 1, 3 do
                            hop[n] = p[n] - (r.lastTarget[n] + r.lastLinear[n] * dt)
                        end
                        local size = math.sqrt(hop[1] ^ 2 + hop[2] ^ 2 + hop[3] ^ 2)
                        local speed = linear and motionBuffer.speed(linear) or 0
                        local off = r.blend or { 0, 0, 0 }
                        local decay = math.exp(-dt / BLEND)
                        if size > math.max(25, speed * 0.1) then
                            off = { 0, 0, 0 }
                        else
                            for n = 1, 3 do
                                off[n] = (off[n] - (size > 0.05 and hop[n] or 0)) * decay
                            end
                        end
                        if math.abs(off[1]) + math.abs(off[2]) + math.abs(off[3]) < 0.01 then
                            off = { 0, 0, 0 }
                        end
                        r.blend = off
                        if off[1] ~= 0 or off[2] ~= 0 or off[3] ~= 0 then
                            written =
                                { p[1] + off[1], p[2] + off[2], p[3] + off[3], p[4], p[5], p[6] }
                            value.x, value.y, value.z = written[1], written[2], written[3]
                            changed = true
                        end
                    else
                        r.blend = nil
                    end
                    r.lastTarget = { p[1], p[2], p[3] }
                    r.lastLinear = linear and { linear[1], linear[2], linear[3] } or nil
                    r.frameAt = now
                    if diagnose then
                        diagnose(i, r, ship, p, linear, changed, now)
                    end
                    if changed then
                        writePose(i, r, ship, sector, value, written, slip, dx, dy, dz, now)
                    end
                    -- Native velocity in every mode. The per-frame pose write stays authoritative
                    -- (pathing SetOffsetOrder, see reports/PROXY-MOTION-20261025.txt); the vector is
                    -- what GetComponentDetails().speed, the HUD speed and target lead report. Without
                    -- it a replica in boost, travel or a highway read 0 m/s while it moved at km/s.
                    if engineApi.GetPlayerControlledShipID() ~= ship and linear then
                        applyVelocity(i, sector, linear, now)
                        velocityApplied = true
                        r.velocityAt = now
                    end
                end
            end
        end
        -- A frame without a velocity refresh (dock pairing gap, skipped apply) keeps the
        -- running native vector for VELOCITY_HOLD s; MD's 0.3 s deadline still bounds it.
        -- Real boundaries (no pose, local pilot, dock) fence at once.
        if not velocityApplied then
            local hold = p
                and r
                and r.velocityAt
                and now - r.velocityAt <= VELOCITY_HOLD
                and not r.highwayAttached
                and r.dock == 0
                and coopState.docks[i] == 0
                and coopState.owners[i] ~= cfg.slot
                and engineApi.GetPlayerControlledShipID() ~= ship
                and engineApi.IsComponentOperational(ship)
                and not bubbleApi.paused()
            if not hold then
                clearVelocity(i)
            end
        end
        -- Queue physical motion before replaying fire for this rendered pose.
        fire2Frame(i, r, ship, now)
    end
end
RegisterEvent('X4LAN_Coop.Ready', function()
    coopState.ready = true
    log('cooperative navigation ready')
end)
RegisterEvent('X4LAN_Coop.Docks', function(_, payload)
    local a, b = payload:match('^(%d)|(%d)$')
    if a and tonumber(a) <= 2 and tonumber(b) <= 2 then
        coopState.docks[1], coopState.docks[2] = tonumber(a), tonumber(b)
        for i = 1, 2 do
            if coopState.docks[i] ~= 0 then
                clearVelocity(i)
            end
        end -- market docks: ships 1/2
    end
end)
RegisterEvent('X4LAN_Coop.Shot', function(_, payload)
    -- Payload 'n' (legacy) or 'n|p' / 'n|s': the shot came from the active primary or secondary group.
    local entity, trigger = tostring(payload):match('^(%d)|?([ps]?)$')
    local i = tonumber(entity)
    if i and i >= 1 and i <= coopState.count and coopState.owners[i] == cfg.slot then
        coopState.shots[i] = coopState.shots[i] + 1
        local k = trigger == 's' and 2 or 1
        coopState.triggerShots[i][k] = coopState.triggerShots[i][k] + 1
    end
end)
