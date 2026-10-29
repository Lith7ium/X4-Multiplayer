-- Original INTERNAL W01 NPC replication adapter. No third-party framework.
-- Native cargo uses a separate authority adapter; NPC combat remains separate work.
local cfg = X4LAN_CONFIG
local bubbleApi = X4LAN_BUBBLE_API
local worldProtocol = X4LAN_UNIVERSE_PROTOCOL
if not cfg or not cfg.world_sync or not bubbleApi or not bubbleApi.state.active then
    return
end
local ffi = require('ffi')
local engineApi = ffi.C
local bubbleState = bubbleApi.state
local motionBuffer = assert(X4LAN_MOTION, 'motion buffer missing')
assert(
    type(cfg.test_profile) == 'string'
        and cfg.test_profile:match('^4584[1-8]$')
        and cfg.build:sub(1, 7) == 'world1-',
    'world adapter requires isolated W01 build'
)
assert(
    cfg.world_mode == 'observe' or cfg.world_mode == 'replica',
    'select an explicit world test mode'
)
ffi.cdef([[
uint32_t GetAllFactions(const char**,uint32_t,bool);
uint32_t GetNumAllFactionShips(const char*);
uint32_t GetAllFactionShips(UniverseID*,uint32_t,const char*);
bool IsComponentClass(UniverseID,const char*);
void ActivateObject(UniverseID,bool);
void RemoveComponent(UniverseID);
]])
pcall(ffi.cdef, 'UniverseID GetPlayerTargetID(void);')
local function api(name)
    local ok, f = pcall(function()
        return engineApi[name]
    end)
    return ok and f or nil
end
local getTarget = api('GetPlayerTargetID')
local universeState = {
    receiver = worldProtocol.receiver(),
    excluded = {},
    identities = {},
    nextId = 0,
    revision = 0,
    interestSeq = 0,
    replicas = {},
    replicaKeys = {},
    motionSeq = 0,
    publish = {},
    publishes = {},
    lastScans = {},
    wanted = {},
    motionCursor = 1,
    frameNo = 0,
    clock = motionBuffer.drClock(),
    focus = {},
    focusSeq = 0,
    gaps = {},
}
bubbleApi.universe = universeState
local function log(text)
    DebugError('[X4LAN] UNIVERSE ' .. text)
end
-- NPC replica motion. Replicas are pose-only (AI off): the engine holds a replica still
-- between two SetObjectSectorPos calls, so moving replicas are written every frame.
-- Server (send on divergence): every NPC is read every motion tick; its velocity is the
-- displacement over that one tick. A sample is sent only when the pose has diverged from
-- what clients extrapolate from the last sent sample (pose + velocity * elapsed), or as a
-- keepalive. Thresholds and rates depend on relevance: hot (within hotRange of a player
-- ship, or a client's target via UFOCUS), warm (within warmRange), cold. At most `budget`
-- samples per tick, most diverged first.
-- Client (dead reckoning, motion.lua motionBuffer.dr*): the latest sample is extrapolated on the
-- server's estimated clock (rate and offset), errors blend out over motionBuffer.dr.tau, and only an
-- error above the snap limit snaps (logged, at most once per second per replica).
-- A baseline pose replaces a live track only after `hold` s without motion.
local NPC = {
    stale = 2.5,
    cap = 50000,
    hold = 8,
    jump = 100,
    suppressEvery = 2,
    budget = 32,
    evalBudget = 768,
    hotRange = 6000,
    warmRange = 25000,
    focusHold = 3,
    focusEvery = 1,
    motionLive = 5,
    scanHost = 4096,
    scanGuest = 1024,
    slipMin = 5000,
    slipSpeed = 2,
    slipRespawn = 1,
    slipTrack = 10,
    velWindow = 0.2,
    maxRate = 3,
    maxTurn = 2,
    urgentMin = 300,
    urgentTime = 0.1,
    sweepEvery = 1,
    sweepTimeout = 3,
}
NPC.tiers = {
    hot = { name = 'hot', pos = 3, ang = 0.02, look = 0.25, min = 0.05, keep = 1, weight = 4 },
    warm = { name = 'warm', pos = 25, ang = 0.06, look = 0.5, min = 0.2, keep = 2, weight = 2 },
    cold = { name = 'cold', pos = 150, ang = 0.2, look = 1, min = 0.5, keep = 4, weight = 1 },
}
universeState.npcMotion = NPC
-- Diagnostic (config npc_motion_diagnostics: seconds, or true = 1 s; on in internal packages,
-- false in the inert source config). Counts only; nothing is computed when off.
local diagEvery = cfg.npc_motion_diagnostics == true and 1 or tonumber(cfg.npc_motion_diagnostics)
if diagEvery and diagEvery <= 0 then
    diagEvery = nil
end
local function feature(name, reason)
    DebugError('[X4LAN] FEATURE ' .. name .. ' DISABLED reason=' .. reason)
end
if not diagEvery and cfg.npc_motion_diagnostics then
    feature('npc_motion_diagnostics', 'invalid_value')
end
local diag, errors, delays = {}, {}, {}
local function count(name, amount)
    if diagEvery then
        diag[name] = (diag[name] or 0) + (amount or 1)
    end
end
local function peak(name, value)
    if diagEvery and value > (diag[name] or 0) then
        diag[name] = value
    end
end
local function sample(list, value)
    if diagEvery and #list < 4096 then
        list[#list + 1] = value
    end
end
local DIAG_FIELDS = {
    'creates',
    'respawns',
    'removes',
    'hides',
    'shows',
    'suppressed',
    'commits',
    'samples',
    'reset_gap',
    'reset_jump',
    'baseline_snap',
    'writes',
    'hot_writes',
    'jumps',
    'max_jump',
    'max_gap',
    'max_delay',
    'near_writes',
    'write_skips',
    'write_deferred',
    'snaps',
    'slips',
    'sent',
    'sent_hot',
    'sent_warm',
    'sent_cold',
    'first',
    'urgent',
    'diverge',
    'keepalive',
    'deferred',
    'focus',
    'players',
    'paused',
}
local function percentile(list, q)
    if #list == 0 then
        return nil
    end
    table.sort(list)
    return list[math.max(1, math.ceil(q * #list))]
end
-- Guest rows add correction error percentiles (m, at sample arrival), snaps/s, sample rate per
-- hot/cold replica (Hz), arrival delay beyond the least-delayed sample (p95, s) and the
-- estimated server clock rate (server seconds per local second).
local function diagFlush(now, role, extra)
    if not diagEvery then
        return
    end
    if not universeState.diagAt then
        universeState.diagAt = now
        diag = {}
        errors = {}
        delays = {}
        return
    end
    if now - universeState.diagAt < diagEvery then
        return
    end
    local dt = now - universeState.diagAt
    local out = { 'NPC_DIAG role=' .. role, string.format('dt=%.2f', dt), extra }
    for _, name in ipairs(DIAG_FIELDS) do
        local v = diag[name]
        if v then
            out[#out + 1] = name .. '=' .. (v % 1 == 0 and tostring(v) or string.format('%.3f', v))
        end
    end
    if role == 'guest' then
        local p50, p95 = percentile(errors, 0.5), percentile(errors, 0.95)
        if p50 then
            out[#out + 1] = string.format('err_p50=%.1f err_p95=%.1f', p50, p95)
        end
        out[#out + 1] = string.format('snaps_s=%.2f', (diag.snaps or 0) / dt)
        local hot, cold =
            universeState.tierCount and universeState.tierCount.hot or 0,
            universeState.tierCount and universeState.tierCount.cold or 0
        if hot > 0 then
            out[#out + 1] = string.format('hz_hot=%.1f', (diag.samples_hot or 0) / (hot * dt))
        end
        if cold > 0 then
            out[#out + 1] = string.format('hz_cold=%.1f', (diag.samples_cold or 0) / (cold * dt))
        end
        local late = percentile(delays, 0.95)
        if late then
            out[#out + 1] = string.format('late_p95=%.3f', late)
        end
        if universeState.clock.base then
            out[#out + 1] = string.format('clock_rate=%.3f', universeState.clock.rate)
        end
        -- gap_err=<bucket>:<samples>:<largest correction m>; buckets by source seconds since the
        -- replica's previous sample (base: first sample after a baseline pose).
        local parts = {}
        for _, b in ipairs({ 'lt0.5', 'lt2', 'lt5', 'ge5', 'base' }) do
            local g = universeState.gaps[b]
            if g then
                parts[#parts + 1] = string.format('%s:%d:%.0f', b, g.n, g.max)
            end
        end
        if #parts > 0 then
            out[#out + 1] = 'gap_err=' .. table.concat(parts, ',')
        end
        -- Engine write budget: configured ms per frame, mean and peak ms used by budgeted visits,
        -- mean ms of the unbudgeted near writes; clock=estimated when no timed clock exists.
        local stats = universeState.writeStats
        if stats and stats.frames > 0 then
            out[#out + 1] = string.format(
                'budget_ms=%.1f used_ms=%.2f used_ms_max=%.2f near_ms=%.2f budget_clock=%s',
                universeState.replicaWrite.budgetMs,
                stats.usedMs / stats.frames,
                stats.usedMaxMs,
                stats.nearMs / stats.frames,
                stats.measured and 'measured' or 'estimated'
            )
            stats.frames = 0
            stats.usedMs = 0
            stats.usedMaxMs = 0
            stats.nearMs = 0
        end
    elseif #errors > 0 then
        -- Host: divergence (m) between the real pose and the shared prediction at send time.
        out[#out + 1] = string.format(
            'pred_err_p50=%.1f pred_err_p95=%.1f pred_err_max=%.1f',
            percentile(errors, 0.5),
            percentile(errors, 0.95),
            errors[#errors]
        )
    end
    log(table.concat(out, ' '))
    universeState.diagAt = now
    diag = {}
    errors = {}
    delays = {}
    universeState.gaps = {}
end
-- One NPC_SNAP row at most every 2 s, with the snaps since the last row.
local snapLog = { n = 0, next = 0 }
local function logSnap(id, err, reason, now)
    snapLog.n = snapLog.n + 1
    if now >= snapLog.next then
        log(
            string.format(
                'NPC_SNAP id=%d error=%.0f reason=%s count=%d',
                id,
                err,
                reason,
                snapLog.n
            )
        )
        snapLog.n = 0
        snapLog.next = now + 2
    end
end
local function key(object)
    return tostring(object):gsub('ULL$', '')
end
local function send(packet)
    bubbleState.transport:send(packet)
end
local function protected(object)
    local objectKey = key(object)
    return universeState.excluded[objectKey]
        or universeState.replicaKeys[objectKey]
        or bubbleApi.coop.entity(object) > 0
        or object == engineApi.GetPlayerControlledShipID()
        or object == engineApi.GetPlayerOccupiedShipID()
        or object == engineApi.GetPlayerContainerID()
end
local function currentSector()
    local object = engineApi.GetPlayerContainerID()
    if object == 0 then
        return nil
    end
    local sector = engineApi.GetContextByClass(object, 'sector', false)
    if sector == 0 then
        return nil
    end
    local name = GetComponentData(bubbleApi.id(sector), 'macro')
    if not worldProtocol.atom(name, true) then
        return nil
    end
    bubbleApi.coop.sectors[name] = sector
    return name, sector
end
local function beginScan(name, sector)
    local factions = ffi.new('const char*[256]')
    local count = tonumber(engineApi.GetAllFactions(factions, 256, true))
    assert(
        count > 0 and count < 256,
        'faction enumeration unavailable or exceeded limit; refusing an incomplete baseline'
    )
    local names = {}
    for i = 0, count - 1 do
        names[#names + 1] = ffi.string(factions[i])
    end
    return {
        name = name,
        sector = sector,
        factions = names,
        faction = 1,
        index = 0,
        seen = {},
        objects = {},
        total = 0,
    }
end
-- Capture filter, shared by the faction enumeration and the sector sweep: an operational
-- ship in the scanned sector that is not protected (replica, shared co-op ship, player
-- occupant, excluded fixture) and not owned by the player faction. There is no class,
-- size, purpose or docking filter; a baseline holds at most worldProtocol.limit (4096) objects.
local MAX_SCANNED_OBJECTS = 250000
local function considerObject(scan, object)
    local objectKey = key(object)
    if scan.seen[objectKey] then
        return
    end
    scan.seen[objectKey] = true
    scan.total = scan.total + 1
    assert(scan.total <= MAX_SCANNED_OBJECTS, 'universe fleet exceeds internal scan limit')
    if
        not engineApi.IsComponentOperational(object)
        or protected(object)
        or not engineApi.IsComponentClass(object, 'ship')
    then
        return
    end
    if engineApi.GetContextByClass(object, 'sector', false) ~= scan.sector then
        return
    end
    local macro, owner = GetComponentData(bubbleApi.id(object), 'macro', 'owner')
    assert(
        worldProtocol.atom(macro, true) and worldProtocol.atom(owner),
        'NPC descriptor unavailable; refusing incomplete baseline'
    )
    -- player_factions: a host ship owned only by the host's human replicates as its pool faction.
    if owner == 'player' and cfg.slot == 'p1' and bubbleApi.players then
        owner = bubbleApi.players.personalOwner(object) or owner
    end
    if owner == 'player' then
        return
    end
    scan.objects[#scan.objects + 1] =
        { object = object, key = objectKey, macro = macro, owner = owner }
    assert(
        #scan.objects <= worldProtocol.limit,
        'sector object limit exceeded; refusing a truncated baseline'
    )
end
-- Per-frame enumeration budget (objects; a faction fleet read costs 16). The live server
-- enumerated 512 objects and at most one faction per 20 Hz frame: a capture took 25-30 s
-- of server time, so baselines were 35-40 s old on clients. The enumeration walks every
-- faction's fleet in the galaxy; a guest uses it only when the MD sector sweep is missing.
local FACTION_READ_COST = 16
local function scanStep(scan)
    -- Bound per-frame enumeration; metadata/positions are read only for this sector.
    local budget = cfg.slot == 'p1' and NPC.scanHost or NPC.scanGuest
    while budget > 0 do
        budget = budget - 1
        if not scan.buffer or scan.index >= scan.count then
            local faction = scan.factions[scan.faction]
            if not faction then
                return true
            end
            scan.faction = scan.faction + 1
            local count = tonumber(engineApi.GetNumAllFactionShips(faction))
            assert(count <= MAX_SCANNED_OBJECTS, 'faction fleet exceeds internal scan limit')
            -- Headroom plus post-read count catches growth/truncation during enumeration.
            local capacity = count + 64
            scan.buffer = ffi.new('UniverseID[?]', capacity)
            scan.count = tonumber(engineApi.GetAllFactionShips(scan.buffer, capacity, faction))
            scan.index = 0
            if
                scan.count >= capacity
                or tonumber(engineApi.GetNumAllFactionShips(faction)) > scan.count
            then
                -- A changing fleet invalidates this capture, not the entire world adapter.
                scan.retry = true
                return true
            end
            budget = budget - (FACTION_READ_COST - 1)
        else
            local object = scan.buffer[scan.index]
            scan.index = scan.index + 1
            considerObject(scan, object)
        end
    end
    return false
end
-- Guest local-NPC suppression: an MD sweep (find_ship in the player's sector, engine side)
-- instead of walking every faction's fleet in the galaxy. Live clients walked about 200,000
-- entries per pass at 1,024 per frame, so the client's own NPCs (freighters docking, ships
-- launched or arriving through gates, builders, laser towers) stayed visible and unsynced
-- for up to about 15 s, and the walk cost frame time. A sweep without a reply within
-- sweepTimeout s disables the sweep (FEATURE npc_sweep DISABLED) and the enumeration is used.
local sweep = { token = 0 }
local function beginSweep(name, sectorId, now)
    sweep.token = sweep.token + 1
    sweep.objects = {}
    sweep.done = false
    AddUITriggeredEvent(
        'X4LAN_Universe',
        'Sweep',
        { sector = bubbleApi.id(sectorId), token = sweep.token }
    )
    return {
        name = name,
        sector = sectorId,
        sweep = true,
        token = sweep.token,
        startedAt = now,
        seen = {},
        objects = {},
        total = 0,
    }
end
local function sweepStep(scan, now)
    if sweep.token ~= scan.token then
        scan.retry = true
        return true
    end
    if not sweep.done then
        if now - scan.startedAt < NPC.sweepTimeout then
            return false
        end
        universeState.sweepUnavailable = true
        feature('npc_sweep', 'no_reply')
        scan.retry = true
        return true
    end
    for _, object in ipairs(sweep.objects) do
        considerObject(scan, object)
    end
    return true
end
RegisterEvent('X4LAN_Universe.Swept', function(_, object)
    if sweep.objects and not sweep.done then
        sweep.objects[#sweep.objects + 1] = ConvertIDTo64Bit(object)
    end
end)
RegisterEvent('X4LAN_Universe.SweepDone', function(_, token)
    if tonumber(token) == sweep.token then
        sweep.done = true
    end
end)
-- Server capture: the faction walk, then the same MD sector sweep the clients use, so every
-- ship a client sweep removes is also one the server replicates (union, duplicates skipped).
local function captureStep(scan, now)
    if not scan.walked then
        if not scanStep(scan) then
            return false
        end
        scan.walked = true
        if scan.retry or universeState.sweepUnavailable then
            return true
        end
        local request = beginSweep(scan.name, scan.sector, now)
        scan.token = request.token
        scan.startedAt = now
        return false
    end
    if not sweepStep(scan, now) then
        return false
    end
    scan.retry = nil -- a silent sweep only disables the sweep; the walk's capture stands
    return true
end
local function pose(object)
    local position = engineApi.GetObjectPositionInSector(object)
    return { position.x, position.y, position.z, position.yaw, position.pitch, position.roll }
end
local function poseFields(pose)
    local out = {}
    for _, value in ipairs(pose) do
        out[#out + 1] = string.format('%.9g', value)
    end
    return table.concat(out, ',')
end
local function captured(scan)
    local rows = {}
    local objects = {}
    for _, entry in ipairs(scan.objects) do
        if
            engineApi.IsComponentOperational(entry.object)
            and engineApi.GetContextByClass(entry.object, 'sector', false) == scan.sector
        then
            local identity = universeState.identities[entry.key]
            local code = bubbleApi.npcCombat and bubbleApi.npcCombat.code(entry.object)
            if
                not identity
                or identity.retired
                or identity.macro ~= entry.macro
                or bubbleApi.npcCombat
                    and (identity.code ~= code or identity.owner ~= entry.owner or identity.sector ~= scan.name)
            then
                universeState.nextId = universeState.nextId + 1
                assert(universeState.nextId <= 2147483647, 'world ID space exhausted')
                identity = {
                    id = universeState.nextId,
                    macro = entry.macro,
                    code = code,
                    owner = entry.owner,
                    sector = scan.name,
                }
                universeState.identities[entry.key] = identity
            end
            local row = identity.id
                .. ',ship,'
                .. entry.macro
                .. ','
                .. entry.owner
                .. ','
                .. poseFields(pose(entry.object))
                .. ',none,0'
            assert(worldProtocol.record(row), 'invalid captured NPC state')
            objects[#objects + 1] =
                { id = identity.id, object = entry.object, sector = scan.name, identity = identity }
            rows[#rows + 1] = { id = identity.id, row = row }
        end
    end
    if bubbleApi.cargo then
        for _, row in ipairs(bubbleApi.cargo.rows(scan.sector)) do
            rows[#rows + 1] = row
        end
    end
    assert(#rows <= worldProtocol.limit, 'combined sector object limit exceeded')
    universeState.lastScan = scan
    universeState.lastScans[scan.name] = scan
    universeState.cargoDirty = false
    table.sort(rows, function(a, b)
        return a.id < b.id
    end)
    local ordered = {}
    for i, r in ipairs(rows) do
        ordered[i] = r.row
    end
    universeState.revision = universeState.revision + 1
    universeState.publishes[scan.name] = objects
    universeState.publish = {}
    universeState.motionCursor = 1
    for _, entries in pairs(universeState.publishes) do
        for _, entry in ipairs(entries) do
            universeState.publish[#universeState.publish + 1] = entry
        end
    end
    universeState.sender = worldProtocol.sender(
        worldProtocol.baseline(bubbleState.client.lease, universeState.revision, scan.name, ordered)
    )
    log(
        'CAPTURE revision='
            .. universeState.revision
            .. ' sector='
            .. scan.name
            .. ' objects='
            .. #ordered
    )
end
local function removeReplica(replica)
    local object = replica.object
    if object then
        count('removes')
    end
    if object and engineApi.IsComponentOperational(object) then
        -- Recheck live player IDs; a player may have boarded since creation.
        if
            bubbleApi.coop.entity(object) > 0
            or object == engineApi.GetPlayerControlledShipID()
            or object == engineApi.GetPlayerOccupiedShipID()
            or object == engineApi.GetPlayerContainerID()
        then
            error('player entered an NPC replica; end this internal scene test')
        end
        engineApi.RemoveComponent(object)
    end
    if object then
        universeState.replicaKeys[key(object)] = nil
    end
end
local function accept(b, now)
    local name = currentSector()
    if b.sector ~= name then
        return
    end
    universeState.acceptedAt = now
    universeState.latest = b
    if cfg.world_mode == 'observe' then
        log('OBSERVED revision=' .. b.revision .. ' sector=' .. b.sector .. ' objects=' .. #b.rows)
        return
    end
    local wanted = {}
    universeState.renderIds = {}
    universeState.renderCursor = 1
    for _, row in ipairs(b.rows) do
        local entity = assert(worldProtocol.record(row))
        assert(entity.kind == 'ship' or bubbleApi.cargo, 'cargo adapter missing')
        wanted[entity.id] = entity
        universeState.renderIds[#universeState.renderIds + 1] = entity.id
    end
    for id, r in pairs(universeState.replicas) do
        local next = wanted[id]
        if
            not next
            or next.macro ~= r.entity.macro
            or next.owner ~= r.entity.owner
            or next.kind ~= r.entity.kind
            or next.ware ~= r.entity.ware
            or next.amount ~= r.entity.amount
            or b.sector ~= r.sector
        then
            removeReplica(r)
            universeState.replicas[id] = nil
        end
    end
    for id, entity in pairs(wanted) do
        local r = universeState.replicas[id]
        if not r then
            r = { entity = entity, sector = b.sector, seq = -1 }
            universeState.replicas[id] = r
        else
            r.entity = entity
            -- A baseline pose is older than its capture plus transfer time. It replaces live motion
            -- only after the motion feed has been silent for NPC.hold seconds (packet loss/recovery);
            -- otherwise a moving replica would jump back to the capture position on every baseline.
            if not r.at or now - r.at >= (r.modern and NPC.hold or motionBuffer.stale) then
                if r.track then
                    count('baseline_snap')
                end
                r.track = nil
                r.target = nil
                r.nextWrite = nil -- show the baseline pose on the next frame
            end
        end
    end
    count('commits')
    log('COMMITTED revision=' .. b.revision .. ' sector=' .. b.sector .. ' objects=' .. #b.rows)
end
function universeState.receive(fields, now)
    if fields[1] == 'UINTEREST' then
        if
            cfg.slot == 'p1'
            and (#fields == 5 or #fields == 6)
            and worldProtocol.integer(fields[4])
            and worldProtocol.atom(fields[5], true)
        then
            universeState.wanted[fields[5]] = now
        end
        return true
    elseif fields[1] == 'UACK' then
        local rev = worldProtocol.integer(fields[5])
        local index = fields[6] == '-1' and -1 or worldProtocol.integer(fields[6])
        if cfg.slot == 'p1' and #fields == 6 and rev and index and universeState.sender then
            universeState.sender:ack(fields[4], rev, index)
        end
        return true
    elseif fields[1] == 'UPOS' or fields[1] == 'UMOVE' then
        local modern = fields[1] == 'UMOVE'
        if
            cfg.slot == 'p1'
            or #fields ~= (modern and 20 or 13)
            or not universeState.latest
            or fields[4] ~= universeState.latest.epoch
        then
            return true
        end
        local id, seq = worldProtocol.integer(fields[5]), worldProtocol.integer(fields[6])
        local r = id and universeState.replicas[id]
        if not r or not seq or seq <= r.seq or fields[7] ~= r.sector then
            return true
        end
        local start = modern and 9 or 8
        local row = id
            .. ',ship,'
            .. r.entity.macro
            .. ','
            .. r.entity.owner
            .. ','
            .. table.concat(fields, ',', start, start + 5)
            .. ',none,0'
        local parsed = worldProtocol.record(row)
        if not parsed then
            return true
        end
        local velocity = { 0, 0, 0, 0, 0, 0 }
        if modern then
            -- Dead reckoning on the server's clock (source time in the sample, rate estimated).
            local time = tonumber(fields[8])
            for i = 1, 6 do
                velocity[i] = tonumber(fields[14 + i])
            end
            local track = r.modern and r.track or {}
            local last = track.time
            -- A replica without a live track shows its baseline pose: the first sample blends from it.
            local ok, err, snapped = motionBuffer.drPush(
                track,
                universeState.clock,
                time,
                parsed.pose,
                velocity,
                now,
                (not last) and r.applied or nil
            )
            if not ok then
                return true
            end
            count('samples')
            count(r.tier == 'hot' and 'samples_hot' or 'samples_cold')
            universeState.motionAt = now
            sample(delays, motionBuffer.drExcess(universeState.clock, time, now))
            if last or r.applied then
                sample(errors, err)
                -- Gap (source seconds since the previous sample) versus correction size.
                local gap = last and time - last or -1
                peak('max_gap', math.max(0, gap))
                if diagEvery then
                    local b = gap < 0 and 'base'
                        or gap < 0.5 and 'lt0.5'
                        or gap < 2 and 'lt2'
                        or gap < 5 and 'lt5'
                        or 'ge5'
                    local g = universeState.gaps[b] or { n = 0, max = 0 }
                    universeState.gaps[b] = g
                    g.n = g.n + 1
                    g.max = math.max(g.max, err)
                end
            end
            if snapped then
                count('snaps')
                logSnap(id, err, last and 'diverged' or 'baseline', now)
                r.nextWrite = nil -- write the resynchronised pose on the next frame
            end
            r.track = track
            r.target = parsed.pose
            r.at = now
            r.seq = seq
            r.modern = true
            return true
        end
        -- Legacy UPOS (no source time): interpolated 0.1 s behind arrival, written every frame.
        local track = (not r.modern) and r.track or nil
        if not track then
            track = {}
            motionBuffer.push(track, now - 0.1, r.entity.pose, velocity, now - 0.1)
        end
        local resets = track.resets or 0
        if not motionBuffer.push(track, now, parsed.pose, velocity, now) then
            return true
        end
        count('samples')
        if (track.resets or 0) > resets then
            count('reset_' .. (track.lastReset or 'gap'))
        end
        r.track = track
        r.target = parsed.pose
        r.at = now
        r.seq = seq
        r.modern = false
        r.nextWrite = nil
        return true
    elseif fields[1] == 'UFOCUS' then
        -- A client's selected target (network ID, 0 = none); hot on the server for focusHold s.
        -- The dedicated relay appends the client's lease; the two-player relay does not.
        if cfg.slot == 'p1' and (#fields == 5 or #fields == 6) then
            local seq, id = worldProtocol.integer(fields[4]), worldProtocol.integer(fields[5])
            local who = fields[6] or 'guest'
            local old = universeState.focus[who]
            if seq and id and (not old or seq > old.seq) then
                universeState.focus[who] = { seq = seq, id = id, at = now }
            end
        end
        return true
    elseif fields[1] == 'UBEGIN' or fields[1] == 'UOBJECT' or fields[1] == 'UCOMMIT' then
        if cfg.slot == 'p1' or universeState.failed then
            return true
        end
        local ack, b = universeState.receiver:receive(fields, now)
        if b then
            universeState.incoming = b
        end -- Engine changes belong to an unpaused frame.
        if ack then
            send(
                'UACK|1|'
                    .. bubbleState.client.lease
                    .. '|'
                    .. fields[4]
                    .. '|'
                    .. fields[5]
                    .. '|'
                    .. ack
            )
        end
        return true
    end
    return false
end
-- Engine pose writes. SetObjectSectorPos runs on the client's main thread and is the
-- expensive part of a replica: live clients drew about 1,700 replica writes per second and
-- fell from about 40 fps (before the replicas loaded) to 13-16 fps, about 0.3-0.4 ms per
-- write. The prediction (motion.lua motionBuffer.drRender) is continuous and costs microseconds; only
-- the write rate depends on the replica's distance from the local player:
--  near  (within nearRange and moving):  every frame
--  view  (within viewRange):             every viewInterval s, fastViewInterval s when fast
--  far   (beyond viewRange):             checked every farInterval s, written only when the
--                                        pose moved more than farError m, or after farRefresh s
-- A write is skipped when the pose changed less than minMove m and minTurn rad.
-- Near replicas (moving within nearRange, or the player's target) are written every frame
-- without any limit: a battle must not starve the ships the player looks at. All other due
-- visits share a per-frame time budget (config npc_write_budget_ms, default budgetMs),
-- measured with the profiler clock (measure_clock.lua); without a timed clock each write
-- counts estimatedWriteMs (config npc_write_cost_ms) and each visit without a write
-- estimatedVisitMs. Order: never-written replicas first, then the most overdue (round robin).
local REPLICA_WRITE = {
    budgetMs = 4,
    estimatedWriteMs = 0.35,
    estimatedVisitMs = 0.02,
    nearRange = 2500,
    nearMinSpeed = 1,
    viewRange = 15000,
    viewInterval = 0.25,
    fastViewInterval = 0.125,
    fastSpeed = 150,
    farInterval = 0.5,
    farError = 100,
    farRefresh = 5,
    minMove = 0.01,
    minTurn = 0.01 * math.pi / 180,
}
universeState.replicaWrite = REPLICA_WRITE
local MAX_WRITE_BUDGET_MS = 50
-- A configured number in milliseconds within (0, MAX_WRITE_BUDGET_MS], else the default.
local function configuredMs(value, default)
    local number = tonumber(value)
    if number and number > 0 and number <= MAX_WRITE_BUDGET_MS then
        return number
    end
    return default
end
REPLICA_WRITE.budgetMs = configuredMs(cfg.npc_write_budget_ms, REPLICA_WRITE.budgetMs)
REPLICA_WRITE.estimatedWriteMs = configuredMs(cfg.npc_write_cost_ms, REPLICA_WRITE.estimatedWriteMs)
-- The profiler clock in seconds, or nil when no timed clock is available.
local function writeClock()
    local measure = rawget(_G, 'X4LAN_MEASURE')
    if measure and measure.timed and type(measure.clock) == 'function' then
        return measure.clock
    end
    return nil
end
-- Budget use per diagnostic window (NPC_DIAG budget_ms, used_ms, used_ms_max, near_ms).
local writeStats = { frames = 0, usedMs = 0, usedMaxMs = 0, nearMs = 0 }
universeState.writeStats = writeStats
local RADIANS_TO_DEGREES = 180 / math.pi
local ZONE_FIX_LIMIT = 16 -- learned zone offsets kept per replica
local RESPAWN_INTERVAL = 2 -- seconds between two slip respawns of one replica
local SLIP_LOG_INTERVAL = 5 -- seconds between two NPC_FRAME_SLIP rows

-- Zone-frame slip (coop_game.lua SLIP_*, 43dd063): SetObjectSectorPos converts the pose
-- into the replica's current zone, re-parents it to the zone at the target and queues the
-- zone-local pose, which then lands offset by the difference of the two zone origins; the
-- engine files the replica back into the old zone and the next write repeats it. Near the
-- player the target lies in a zone around the player's ship, so it repeats on every write.
-- For replicas near the player (or that slipped in the last slipTrack s) the slip of a
-- re-parenting write is kept per source zone and subtracted from later writes that start
-- there; elsewhere (and when the predicted target zone is missed) the pose is rewritten. A
-- slip that lasts slipRespawn s respawns the replica at the target pose, at most once per
-- RESPAWN_INTERVAL s. NPC_FRAME_SLIP rows count each action, one row per SLIP_LOG_INTERVAL s.
local slipLog = { rows = 0, next = 0, zone = 0, rewrite = 0, respawn = 0 }
local function zoneOf(object)
    return engineApi.GetContextByClass(object, 'zone', false)
end
local function logSlipAction(replica, slip, action, now)
    count('slips')
    slipLog.rows = slipLog.rows + 1
    slipLog[action] = slipLog[action] + 1
    if now < slipLog.next then
        return
    end
    log(
        string.format(
            'NPC_FRAME_SLIP id=%d slip=%.0f action=%s count=%d zone=%d rewrite=%d respawn=%d',
            replica.entity.id,
            slip,
            action,
            slipLog.rows,
            slipLog.zone,
            slipLog.rewrite,
            slipLog.respawn
        )
    )
    slipLog.rows = 0
    slipLog.zone = 0
    slipLog.rewrite = 0
    slipLog.respawn = 0
    slipLog.next = now + SLIP_LOG_INTERVAL
end
-- Remembers the zone offset of the last write if that write re-parented and slipped.
local function learnZoneOffset(replica, slipOffset)
    local previousOffset = replica.lastWriteOffset
    replica.zoneFix = replica.zoneFix or {}
    replica.zoneFixCount = (replica.zoneFixCount or 0) + 1
    if replica.zoneFixCount > ZONE_FIX_LIMIT then
        replica.zoneFix = {}
        replica.zoneFixCount = 1
    end
    replica.zoneFix[tostring(replica.lastWriteFrom)] = {
        to = replica.lastWriteTo,
        offset = {
            previousOffset[1] + slipOffset[1],
            previousOffset[2] + slipOffset[2],
            previousOffset[3] + slipOffset[3],
        },
    }
end
-- One engine write of `enginePose` (UIPosRot, degrees) for the predicted `pose` (radians).
-- slip/slipOffset: how far the engine moved the replica since the last write (0 if none).
local function writePose(replica, sectorId, enginePose, pose, slip, slipOffset, now, trackZones)
    local object = replica.object
    if slip > 0 and replica.lastWriteFrom and replica.lastWriteFrom ~= replica.lastWriteTo then
        learnZoneOffset(replica, slipOffset)
    end
    local fromZone = trackZones and zoneOf(object)
    local fix = fromZone and replica.zoneFix and replica.zoneFix[tostring(fromZone)]
    local offset = fix and fix.offset
    local written = enginePose
    if offset then
        written = ffi.new('UIPosRot', {
            x = enginePose.x - offset[1],
            y = enginePose.y - offset[2],
            z = enginePose.z - offset[3],
            yaw = enginePose.yaw,
            pitch = enginePose.pitch,
            roll = enginePose.roll,
        })
    end
    engineApi.SetObjectSectorPos(object, sectorId, written)
    replica.applied = pose
    if trackZones then
        replica.lastWriteFrom = fromZone
        replica.lastWriteTo = zoneOf(object)
        replica.lastWriteOffset = offset or { 0, 0, 0 }
    else
        replica.lastWriteFrom = nil
    end
    local action = nil
    if offset and replica.lastWriteTo ~= fix.to then
        -- The predicted target zone was missed: drop the offset and write uncompensated.
        replica.zoneFix[tostring(fromZone)] = nil
        engineApi.SetObjectSectorPos(object, sectorId, enginePose)
        action = 'rewrite'
    elseif slip > 0 and offset then
        action = 'zone'
    elseif slip > 0 then
        engineApi.SetObjectSectorPos(object, sectorId, enginePose)
        action = 'rewrite'
    end
    local slipLasted = replica.slipSince and now - replica.slipSince >= NPC.slipRespawn
    if slip > 0 and slipLasted and now >= (replica.nextRespawn or 0) then
        replica.nextRespawn = now + RESPAWN_INTERVAL
        replica.slipSince = nil
        removeReplica(replica)
        replica.object = nil
        replica.applied = nil
        replica.zoneFix = nil
        replica.nextWrite = now -- spawn again on the next frame
        action = 'respawn'
    end
    if action then
        logSlipAction(replica, slip, action, now)
    end
end
local function distanceSquared(a, b)
    return (a[1] - b[1]) ^ 2 + (a[2] - b[2]) ^ 2 + (a[3] - b[3]) ^ 2
end
-- The predicted pose (radians) and speed (m/s) of a replica now.
local function predictedPose(replica, now)
    local pose = replica.entity.pose
    local speed = 0
    if replica.track and replica.modern then
        local predicted, velocity = motionBuffer.drRender(
            replica.track,
            universeState.renderT,
            now,
            universeState.clock.rate
        )
        if predicted then
            pose = predicted
            speed = motionBuffer.speed(velocity)
        end
    elseif replica.track then
        pose = motionBuffer.render(replica.track, now, 0.1) or pose
    end
    return pose, speed
end
-- Write class and the time until the next visit.
local function writeClass(replica, pose, speed)
    if replica.track and not replica.modern then
        return 'near', 0 -- legacy UPOS replicas have no velocity to classify by
    end
    local distance2 = universeState.playerPos and distanceSquared(pose, universeState.playerPos)
        or math.huge
    local focused = universeState.focusId == replica.entity.id
    if
        (distance2 <= REPLICA_WRITE.nearRange ^ 2 or focused)
        and speed >= REPLICA_WRITE.nearMinSpeed
    then
        return 'near', 0
    end
    if distance2 <= REPLICA_WRITE.viewRange ^ 2 or focused then
        if speed >= REPLICA_WRITE.fastSpeed then
            return 'view', REPLICA_WRITE.fastViewInterval
        end
        return 'view', REPLICA_WRITE.viewInterval
    end
    return 'far', REPLICA_WRITE.farInterval
end
local function poseChanged(pose, applied)
    for i = 1, 3 do
        if math.abs(pose[i] - applied[i]) > REPLICA_WRITE.minMove then
            return true
        end
    end
    for i = 4, 6 do
        if math.abs(motionBuffer.angle(pose[i] - applied[i])) > REPLICA_WRITE.minTurn then
            return true
        end
    end
    return false
end
-- Far replicas are written only when the predicted pose left the written one by more than
-- farError m, or after farRefresh s.
local function needsWrite(replica, pose, class, now)
    if not replica.applied then
        return true
    end
    if not poseChanged(pose, replica.applied) then
        return false
    end
    if class ~= 'far' then
        return true
    end
    local moved2 = distanceSquared(pose, replica.applied)
    return moved2 > REPLICA_WRITE.farError ^ 2
        or now - (replica.appliedAt or 0) >= REPLICA_WRITE.farRefresh
end
-- Engine pose minus the last written pose (zone-frame slip), or 0 when within the limit.
local function measureSlip(replica, speed, now)
    if not replica.applied then
        return 0, { 0, 0, 0 }
    end
    local engine = engineApi.GetObjectPositionInSector(replica.object)
    local offset = {
        engine.x - replica.applied[1],
        engine.y - replica.applied[2],
        engine.z - replica.applied[3],
    }
    local slip = math.sqrt(offset[1] ^ 2 + offset[2] ^ 2 + offset[3] ^ 2)
    if slip <= math.max(NPC.slipMin, NPC.slipSpeed * speed) then
        replica.slipSince = nil
        return 0, offset
    end
    replica.slipSince = replica.slipSince or now
    replica.zoneTrackUntil = now + NPC.slipTrack
    return slip, offset
end
local function countJump(replica, pose, speed, now)
    if not diagEvery or not replica.applied or not replica.appliedAt then
        return
    end
    local moved = math.sqrt(distanceSquared(pose, replica.applied))
    local expected = math.max(speed, replica.speed or 0) * (now - replica.appliedAt)
    if moved > NPC.jump + 2 * expected then
        count('jumps')
        peak('max_jump', moved)
    end
end
local function ensureObject(replica, sectorId, enginePose)
    if replica.object and engineApi.IsComponentOperational(replica.object) then
        return
    end
    count(replica.object and 'respawns' or 'creates')
    replica.object = engineApi.SpawnObjectAtPos2(
        replica.entity.macro,
        sectorId,
        enginePose,
        replica.entity.owner
    )
    assert(replica.object ~= 0, 'NPC replica spawn failed')
    universeState.replicaKeys[key(replica.object)] = replica.entity.id
    engineApi.ActivateObject(replica.object, false)
    replica.applied = nil
end
-- One visit: predict, classify, and write the engine pose when the class asks for it.
function universeState.writeReplica(replica, now, sectorId)
    if replica.entity.kind == 'drop' then
        bubbleApi.cargo.ensure(replica, sectorId)
        replica.nextWrite = now + REPLICA_WRITE.viewInterval
        return
    end
    if bubbleApi.npcCombat and bubbleApi.npcCombat.hidden(replica) then
        if not replica.hidden then
            replica.hidden = true
            count('hides')
        end
        bubbleApi.npcCombat.hide(replica, now)
        replica.nextWrite = now + REPLICA_WRITE.viewInterval
        return
    end
    if replica.hidden then
        replica.hidden = nil
        count('shows')
    end
    local pose, speed = predictedPose(replica, now)
    local enginePose = ffi.new('UIPosRot', {
        x = pose[1],
        y = pose[2],
        z = pose[3],
        yaw = pose[4] * RADIANS_TO_DEGREES,
        pitch = pose[5] * RADIANS_TO_DEGREES,
        roll = pose[6] * RADIANS_TO_DEGREES,
    })
    ensureObject(replica, sectorId, enginePose)
    assert(
        replica.object ~= engineApi.GetPlayerContainerID()
            and replica.object ~= engineApi.GetPlayerControlledShipID(),
        'player in NPC replica'
    )
    local class, interval = writeClass(replica, pose, speed)
    replica.writeClass = class
    replica.tier = (class == 'near' or universeState.focusId == replica.entity.id) and 'hot'
        or 'cold'
    replica.nextWrite = now + interval
    replica.speed = speed
    local slip, slipOffset = measureSlip(replica, speed, now)
    if slip == 0 and not needsWrite(replica, pose, class, now) then
        count('write_skips')
        return false
    end
    countJump(replica, pose, speed, now)
    -- Zones are tracked near the player and for slipTrack s after any slip of this replica.
    local trackZones = class == 'near' or now < (replica.zoneTrackUntil or 0)
    writePose(replica, sectorId, enginePose, pose, slip, slipOffset, now, trackZones)
    replica.appliedAt = now
    count(class == 'near' and 'near_writes' or 'writes')
    return true
end
-- Visit order: never-written replicas first, then near ones, then the most overdue.
local function visitBefore(a, b)
    local rankA = a.object and (a.writeClass == 'near' and 1 or 2) or 0
    local rankB = b.object and (b.writeClass == 'near' and 1 or 2) or 0
    if rankA ~= rankB then
        return rankA < rankB
    end
    local dueA = a.nextWrite or 0
    local dueB = b.nextWrite or 0
    if dueA ~= dueB then
        return dueA < dueB
    end
    return a.entity.id < b.entity.id
end
local function playerPosition(sectorId)
    local own = engineApi.GetPlayerContainerID()
    if own == 0 or engineApi.GetContextByClass(own, 'sector', false) ~= sectorId then
        return nil
    end
    local position = engineApi.GetObjectPositionInSector(own)
    return { position.x, position.y, position.z }
end
local function isNear(replica)
    return replica.object
        and (replica.writeClass == 'near' or universeState.focusId == replica.entity.id)
end
local SECONDS_TO_MS = 1000
local BUDGET_EPSILON_MS = 1e-6 -- summed clock and estimate steps must not overshoot by rounding
-- Per frame: every due near replica, then the other due visits within the time budget.
function universeState.renderReplicas(now, sectorId)
    local ids = universeState.renderIds or {}
    if #ids == 0 then
        return
    end
    universeState.frameNo = universeState.frameNo + 1
    universeState.renderT = universeState.clock.base
        and motionBuffer.drTime(universeState.clock, now)
    universeState.playerPos = playerPosition(sectorId)
    local near = {}
    local others = {}
    local nearCount = 0
    for _, id in ipairs(ids) do
        local replica = universeState.replicas[id]
        if replica and replica.sector == universeState.interest then
            if isNear(replica) then
                nearCount = nearCount + 1
            end
            if now >= (replica.nextWrite or 0) then
                if isNear(replica) then
                    near[#near + 1] = replica
                else
                    others[#others + 1] = replica
                end
            end
        end
    end
    universeState.nearCount = nearCount
    local clock = writeClock()
    local nearStart = clock and clock()
    for _, replica in ipairs(near) do
        universeState.writeReplica(replica, now, sectorId)
    end
    if clock then
        writeStats.nearMs = writeStats.nearMs + (clock() - nearStart) * SECONDS_TO_MS
    end
    table.sort(others, visitBefore)
    local start = clock and clock()
    local usedMs = 0
    local visited = 0
    for _, replica in ipairs(others) do
        if usedMs >= REPLICA_WRITE.budgetMs - BUDGET_EPSILON_MS then
            break
        end
        local wrote = universeState.writeReplica(replica, now, sectorId)
        visited = visited + 1
        if clock then
            usedMs = (clock() - start) * SECONDS_TO_MS
        elseif wrote then
            usedMs = usedMs + REPLICA_WRITE.estimatedWriteMs
        else
            usedMs = usedMs + REPLICA_WRITE.estimatedVisitMs
        end
    end
    if #others > visited then
        count('write_deferred', #others - visited)
    end
    writeStats.frames = writeStats.frames + 1
    writeStats.usedMs = writeStats.usedMs + usedMs
    writeStats.usedMaxMs = math.max(writeStats.usedMaxMs, usedMs)
    writeStats.measured = clock ~= nil
end
-- Server: player ship positions per sector, keyed by key(sector): engine IDs are 64-bit
-- cdata, and two cdata values that compare equal are still different table keys (live:
-- every NPC was cold). The dedicated server counts only shared ships a client owns (its
-- own observer player and unclaimed ships are no players); a two-player host also counts
-- its local player.
local function playerShips()
    local out, n = {}, 0
    local function add(object)
        if object and object ~= 0 and engineApi.IsComponentOperational(object) then
            local sector = engineApi.GetContextByClass(object, 'sector', false)
            if sector ~= 0 then
                local p = engineApi.GetObjectPositionInSector(object)
                local k = key(sector)
                local list = out[k] or {}
                out[k] = list
                list[#list + 1] = { p.x, p.y, p.z }
                n = n + 1
            end
        end
    end
    local owners = bubbleApi.coop.owners or {}
    for i = 1, bubbleApi.coop.count or 0 do
        if cfg.dedicated ~= true or (owners[i] and owners[i] ~= cfg.slot) then
            add(bubbleApi.coop.ships[i])
        end
    end
    if cfg.dedicated ~= true then
        add(engineApi.GetPlayerContainerID())
    end
    return out, n
end
local function relevance(id, p, players, focus)
    if focus[id] then
        return NPC.tiers.hot
    end
    local best = math.huge
    for _, q in ipairs(players or {}) do
        local d = (p[1] - q[1]) ^ 2 + (p[2] - q[2]) ^ 2 + (p[3] - q[3]) ^ 2
        if d < best then
            best = d
        end
    end
    if best <= NPC.hotRange ^ 2 then
        return NPC.tiers.hot
    end
    if best <= NPC.warmRange ^ 2 then
        return NPC.tiers.warm
    end
    return NPC.tiers.cold
end
-- Velocity over the last velWindow s of motion ticks (a single tick amplifies an engine
-- position that updates in steps). A tick displacement above 100 km/s (gate, teleport)
-- restarts the history and carries no velocity. Euler rates above maxRate rad/s are angle
-- wraps (pitch through 90 degrees, roll flips), not rotation, and count as 0.
-- Turn rate: the shared prediction turns the velocity with the sample's yaw rate when the
-- velocity heading matches the yaw. The sent yaw rate is then the measured rate of the
-- velocity heading (in the matched convention), so a yaw that is not the course cannot
-- bend the predicted path; turns above maxTurn rad/s are clamped.
local function headingBranch(pose, velocity)
    if math.sqrt(velocity[1] ^ 2 + velocity[3] ^ 2) < 1 then
        return 0
    end
    local heading = math.atan2(velocity[1], velocity[3])
    local forwardError, oppositeError =
        math.abs(motionBuffer.angle(heading - pose[4])),
        math.abs(motionBuffer.angle(heading + pose[4]))
    if forwardError < motionBuffer.dr.headingMatch and forwardError <= oppositeError + 0.05 then
        return 1
    elseif oppositeError < motionBuffer.dr.headingMatch then
        return -1
    end
    return 0
end
local function velocityOf(identity, pose, now)
    local history = identity.hist or {}
    identity.hist = history
    local last = history[#history]
    if last and (now - last.time > 1 or now <= last.time) then
        history = {}
        identity.hist = history
        last = nil
    end
    if last then
        local d2 = 0
        for i = 1, 3 do
            d2 = d2 + (pose[i] - last.pose[i]) ^ 2
        end
        if math.sqrt(d2) / (now - last.time) > 100000 then
            identity.hist = { { time = now, pose = pose } }
            return { 0, 0, 0, 0, 0, 0 }
        end
    end
    history[#history + 1] = { time = now, pose = pose }
    while #history > 2 and now - history[2].time >= NPC.velWindow do
        table.remove(history, 1)
    end
    local oldest = history[1]
    local elapsed = now - oldest.time
    if #history < 2 or elapsed <= 0.001 then
        return nil
    end
    local velocity = {}
    for i = 1, 6 do
        local delta = pose[i] - oldest.pose[i]
        if i > 3 then
            delta = motionBuffer.angle(delta)
        end
        velocity[i] = delta / elapsed
        if i > 3 and math.abs(velocity[i]) > NPC.maxRate then
            velocity[i] = 0
        end
    end
    -- Heading rate of the velocity: the course over the older and the newer half of the window.
    local midpoint = history[math.max(1, math.floor((#history + 1) / 2))]
    local turn = 0
    if midpoint ~= oldest and midpoint ~= history[#history] then
        local h1 = math.atan2(midpoint.pose[1] - oldest.pose[1], midpoint.pose[3] - oldest.pose[3])
        local h2 = math.atan2(pose[1] - midpoint.pose[1], pose[3] - midpoint.pose[3])
        local d1, d2 =
            (midpoint.pose[1] - oldest.pose[1]) ^ 2 + (midpoint.pose[3] - oldest.pose[3]) ^ 2,
            (pose[1] - midpoint.pose[1]) ^ 2 + (pose[3] - midpoint.pose[3]) ^ 2
        if d1 > 1 and d2 > 1 then
            turn = motionBuffer.angle(h2 - h1) / ((now - oldest.time) / 2)
        end
    end
    turn = math.max(-NPC.maxTurn, math.min(NPC.maxTurn, turn))
    -- The window's chord lags a turning course by half the window: rotate it to the present.
    if turn ~= 0 then
        local angle = turn * elapsed / 2
        local cosine, sine = math.cos(angle), math.sin(angle)
        velocity[1], velocity[3] =
            velocity[1] * cosine + velocity[3] * sine, velocity[3] * cosine - velocity[1] * sine
    end
    local branch = headingBranch(pose, velocity)
    if branch ~= 0 then
        velocity[4] = branch * turn
    end
    return velocity
end
-- Why and how urgently an NPC is due: never sent, diverged from the shared prediction of its
-- last sent sample (position, heading or velocity beyond the tier threshold), or keepalive.
local function due(identity, pose, velocity, tier, now)
    local sent = identity.sent
    if not sent then
        return 'first', 1e9, 0
    end
    local age = now - sent.time
    local predictedPose, predictedVelocity =
        motionBuffer.drPredict(sent.pose, sent.vel, math.min(age, motionBuffer.dr.coast))
    if age >= motionBuffer.dr.coast then
        predictedVelocity = { 0, 0, 0 }
    end
    local distanceSquared, turn, velocityDifferenceSquared = 0, 0, 0
    for i = 1, 3 do
        distanceSquared = distanceSquared + (pose[i] - predictedPose[i]) ^ 2
        velocityDifferenceSquared = velocityDifferenceSquared
            + (velocity[i] - predictedVelocity[i]) ^ 2
    end
    for i = 4, 6 do
        turn = math.max(turn, math.abs(motionBuffer.angle(pose[i] - predictedPose[i])))
    end
    local err = math.sqrt(distanceSquared)
    local score = math.max(
        err / tier.pos,
        turn / tier.ang,
        math.sqrt(velocityDifferenceSquared) * tier.look / tier.pos
    )
    -- Any tier: an error above max(urgentMin m, urgentTime s of speed) is sent at once, so a
    -- fast ship's error stays below the client's snap limit (min intervals only pace small ones).
    if err > math.max(NPC.urgentMin, NPC.urgentTime * motionBuffer.speed(velocity)) then
        return 'urgent', err / tier.pos * tier.weight + 1e6, err
    end
    if score >= 1 and age >= tier.min then
        return 'diverge', score * tier.weight, err
    end
    if age >= tier.keep then
        return 'keepalive', age / tier.keep * tier.weight * 0.5, err
    end
    return nil
end
function universeState.publishMotion(now)
    local players, shipCount = playerShips()
    local focus = {}
    peak('players', shipCount)
    for who, f in pairs(universeState.focus) do
        if now - f.at > NPC.focusHold then
            universeState.focus[who] = nil
        elseif f.id > 0 then
            focus[f.id] = true
        end
    end
    local total = #universeState.publish
    local list = {}
    for _ = 1, math.min(total, NPC.evalBudget) do
        local entry = universeState.publish[universeState.motionCursor]
        universeState.motionCursor = universeState.motionCursor % total + 1
        local identity = entry.identity
        local target = bubbleApi.coop.sectors[entry.sector]
        if
            target
            and engineApi.IsComponentOperational(entry.object)
            and engineApi.GetContextByClass(entry.object, 'sector', false) == target
        then
            local p = pose(entry.object)
            local v = velocityOf(identity, p, now)
            if v then
                local tier = relevance(entry.id, p, players[key(target)], focus)
                local why, score, err = due(identity, p, v, tier, now)
                if why then
                    list[#list + 1] = {
                        entry = entry,
                        p = p,
                        v = v,
                        tier = tier,
                        why = why,
                        score = score,
                        err = err,
                    }
                end
            end
        else
            identity.hist = nil
        end
    end
    table.sort(list, function(a, b)
        if a.score ~= b.score then
            return a.score > b.score
        end
        return a.entry.id < b.entry.id
    end)
    for i = 1, math.min(NPC.budget, #list) do
        local c = list[i]
        local entry = c.entry
        universeState.motionSeq = universeState.motionSeq + 1
        entry.identity.sent = { time = now, pose = c.p, vel = c.v }
        count('sent')
        count('sent_' .. c.tier.name)
        count(c.why)
        if c.why ~= 'first' and c.why ~= 'keepalive' then
            sample(errors, c.err)
        end
        send(
            'UMOVE|1|'
                .. bubbleState.client.lease
                .. '|'
                .. bubbleState.client.lease
                .. '|'
                .. entry.id
                .. '|'
                .. universeState.motionSeq
                .. '|'
                .. entry.sector
                .. '|'
                .. string.format('%.6f', now)
                .. '|'
                .. poseFields(c.p):gsub(',', '|')
                .. '|'
                .. poseFields(c.v):gsub(',', '|')
        )
    end
    if #list > NPC.budget then
        count('deferred', #list - NPC.budget)
    end
    local n = 0
    for _ in pairs(focus) do
        n = n + 1
    end
    peak('focus', n)
end
-- Guest: removes the client's own NPCs in the replicated sector (every ship the server
-- would capture, which is not a replica). Sweeps every sweepEvery s; the faction
-- enumeration fallback every suppressEvery s.
local SUPPRESS_LOG_INTERVAL = 30
local function suppressLocalNpcs(name, sectorId, now)
    if not universeState.scan and now >= (universeState.nextScan or 0) then
        if universeState.sweepUnavailable then
            universeState.scan = beginScan(name, sectorId)
        else
            universeState.scan = beginSweep(name, sectorId, now)
        end
    end
    if not universeState.scan then
        return
    end
    local finished
    if universeState.scan.sweep then
        finished = sweepStep(universeState.scan, now)
    else
        finished = scanStep(universeState.scan)
    end
    if not finished then
        return
    end
    local scan = universeState.scan
    universeState.scan = nil
    if scan.retry then
        universeState.nextScan = now + 1
        return
    end
    local removed = 0
    for _, entry in ipairs(scan.objects) do
        local object = entry.object
        if
            engineApi.IsComponentOperational(object)
            and not protected(object)
            and engineApi.GetContextByClass(object, 'sector', false) == sectorId
        then
            engineApi.RemoveComponent(object)
            removed = removed + 1
        end
    end
    count('suppressed', removed)
    if removed > 0 or now >= (universeState.nextSuppressLog or 0) then
        universeState.nextSuppressLog = now + SUPPRESS_LOG_INTERVAL
        log(
            'LOCAL_NPC_SUPPRESSED count='
                .. removed
                .. ' sector='
                .. name
                .. ' source='
                .. (scan.sweep and 'sweep' or 'factions')
        )
    end
    universeState.nextScan = now + (scan.sweep and NPC.sweepEvery or NPC.suppressEvery)
end
local function frame(now)
    if
        universeState.failed
        or not bubbleState.client
        or not bubbleState.client.lease
        or not bubbleApi.coop.ready
    then
        return
    end
    if not universeState.setup then
        universeState.setup = true
        AddUITriggeredEvent('X4LAN_Universe', 'Setup')
        return
    end
    if not universeState.ready or bubbleApi.paused() then
        return
    end
    local name, sector = currentSector()
    if not name then
        return
    end
    if cfg.slot ~= 'p1' then
        if universeState.interest ~= name then
            universeState.interest = name
            universeState.scan = nil
            universeState.receiver.pending = nil
            universeState.latest = nil
            universeState.nextScan = 0
            for _, r in pairs(universeState.replicas) do
                removeReplica(r)
            end
            universeState.replicas = {}
        end
        if now >= (universeState.nextInterest or 0) then
            universeState.nextInterest = now + 1
            universeState.interestSeq = universeState.interestSeq + 1
            send(
                'UINTEREST|1|'
                    .. bubbleState.client.lease
                    .. '|'
                    .. universeState.interestSeq
                    .. '|'
                    .. name
            )
        end
        if universeState.incoming then
            local incoming = universeState.incoming
            universeState.incoming = nil
            accept(incoming, now)
        end
        -- Replicas pause only when the server is silent: a stale baseline (membership) alone no
        -- longer freezes replicas that live motion still drives. Live captures took 35-40 s,
        -- so the former 15 s baseline pause froze every replica for about 20 s per cycle.
        local stale = universeState.latest
            and now - universeState.acceptedAt >= 15
            and (not universeState.motionAt or now - universeState.motionAt >= NPC.motionLive)
        if cfg.world_mode == 'replica' and stale then
            count('paused')
            if not universeState.renderPaused then
                universeState.renderPaused = true
                feature(
                    'npc_replicas',
                    'baseline_stale age=' .. string.format('%.1f', now - universeState.acceptedAt)
                )
            end
        elseif universeState.renderPaused and universeState.latest then
            universeState.renderPaused = nil
            log('FEATURE_RESUMED npc_replicas')
        end
        if cfg.world_mode == 'replica' and universeState.latest and not stale then
            -- UFOCUS: the replica the local player targets, refreshed every focusEvery s; 0 once
            -- when the target is cleared or is not a replica.
            if getTarget and now >= (universeState.focusCheck or 0) then
                universeState.focusCheck = now + 0.25
                local ok, target = pcall(getTarget)
                local id = ok and target and target ~= 0 and universeState.replicaKeys[key(target)]
                    or nil
                if type(id) ~= 'number' then
                    id = nil
                end
                if
                    id ~= universeState.focusId or (id and now >= (universeState.focusNext or 0))
                then
                    universeState.focusSeq = universeState.focusSeq + 1
                    universeState.focusId = id
                    universeState.focusNext = now + NPC.focusEvery
                    send(
                        'UFOCUS|1|'
                            .. bubbleState.client.lease
                            .. '|'
                            .. universeState.focusSeq
                            .. '|'
                            .. (id or 0)
                    )
                end
            end
            universeState.renderReplicas(now, sector)
            suppressLocalNpcs(name, sector, now)
        end
        if diagEvery then
            local live = 0
            universeState.tierCount = { hot = 0, cold = 0 }
            for _, r in pairs(universeState.replicas) do
                live = live + 1
                local t = r.tier == 'hot' and 'hot' or 'cold'
                universeState.tierCount[t] = universeState.tierCount[t] + 1
            end
            diagFlush(
                now,
                'guest',
                'sector='
                    .. name
                    .. ' replicas='
                    .. live
                    .. ' hot='
                    .. (universeState.nearCount or 0)
                    .. ' baseline_age='
                    .. (
                        universeState.acceptedAt
                            and string.format('%.1f', now - universeState.acceptedAt)
                        or '-'
                    )
            )
        end
        return
    end
    for name, at in pairs(universeState.wanted) do
        if now - at > 5 then
            universeState.wanted[name] = nil
        end
    end
    if universeState.cargoDirty and (not universeState.sender or universeState.sender.done) then
        for name, scan in pairs(universeState.lastScans) do
            if universeState.wanted[name] then
                captured(scan)
                break
            end
        end
    end
    if universeState.sender and not universeState.sender.done then
        for _, p in ipairs(universeState.sender:packets(bubbleState.client.lease, now)) do
            send(p)
        end
    elseif not universeState.scan and now >= (universeState.nextScan or 0) then
        local names = {}
        for name in pairs(universeState.wanted) do
            names[#names + 1] = name
        end
        table.sort(names)
        if #names > 0 then
            universeState.scanChoice = (universeState.scanChoice or 0) % #names + 1
            local name = names[universeState.scanChoice]
            local target = bubbleApi.coop.sectorId(name)
            if target then
                universeState.scan = beginScan(name, target)
            end
        end
    end
    if universeState.scan and captureStep(universeState.scan, now) then
        if not universeState.scan.retry then
            captured(universeState.scan)
        end
        universeState.scan = nil
        universeState.nextScan = now + 2
    end
    if #universeState.publish > 0 and now >= (universeState.nextMotion or 0) then
        universeState.nextMotion = now + 0.045 -- every 0.05 s server frame (float steps must not skip one)
        universeState.publishMotion(now)
    end
    local names = {}
    for name in pairs(universeState.wanted) do
        names[#names + 1] = name
    end
    table.sort(names)
    diagFlush(
        now,
        'host',
        'sectors='
            .. (#names > 0 and table.concat(names, ',') or '-')
            .. ' published='
            .. #universeState.publish
    )
end
function universeState.frame(now)
    local ok, err = pcall(frame, now)
    if not ok then
        universeState.failed = true
        log('STOPPED ' .. tostring(err))
        feature('world_sync', 'adapter_stopped')
    end
end
RegisterEvent('X4LAN_Universe.Exclude', function(_, object)
    universeState.excluded[key(ConvertIDTo64Bit(object))] = true
end)
RegisterEvent('X4LAN_Universe.Ready', function()
    universeState.ready = true
    log(
        'READY mode='
            .. cfg.world_mode
            .. ' slot='
            .. cfg.slot
            .. ' npc_motion_diagnostics='
            .. tostring(diagEvery or false)
    )
end)
log(
    'ARMED internal world replication; native cargo and shared identity adapters load separately; NPC combat remains experimental'
)
