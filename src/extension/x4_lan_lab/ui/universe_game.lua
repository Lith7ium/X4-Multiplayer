-- Original INTERNAL W01 NPC replication adapter. No third-party framework.
-- Native cargo uses a separate authority adapter; NPC combat remains separate work.
local cfg = X4LAN_CONFIG
local A = X4LAN_BUBBLE_API
local P = X4LAN_UNIVERSE_PROTOCOL
if not cfg or not cfg.world_sync or not A or not A.state.active then
    return
end
local ffi = require('ffi')
local C = ffi.C
local s = A.state
local M = assert(X4LAN_MOTION, 'motion buffer missing')
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
        return C[name]
    end)
    return ok and f or nil
end
local getTarget = api('GetPlayerTargetID')
local u = {
    receiver = P.receiver(),
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
    clock = M.drClock(),
    focus = {},
    focusSeq = 0,
    gaps = {},
}
A.universe = u
local function log(v)
    DebugError('[X4LAN] UNIVERSE ' .. v)
end
-- NPC replica motion. Replicas are pose-only (AI off): the engine holds a replica still
-- between two SetObjectSectorPos calls, so moving replicas are written every frame.
-- Server (send on divergence): every NPC is read every motion tick; its velocity is the
-- displacement over that one tick. A sample is sent only when the pose has diverged from
-- what clients extrapolate from the last sent sample (pose + velocity * elapsed), or as a
-- keepalive. Thresholds and rates depend on relevance: hot (within hotRange of a player
-- ship, or a client's target via UFOCUS), warm (within warmRange), cold. At most `budget`
-- samples per tick, most diverged first.
-- Client (dead reckoning, motion.lua M.dr*): the latest sample is extrapolated on the
-- server's estimated clock (rate and offset), errors blend out over M.dr.tau, and only an
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
u.npcMotion = NPC
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
local function count(name, n)
    if diagEvery then
        diag[name] = (diag[name] or 0) + (n or 1)
    end
end
local function peak(name, v)
    if diagEvery and v > (diag[name] or 0) then
        diag[name] = v
    end
end
local function sample(list, v)
    if diagEvery and #list < 4096 then
        list[#list + 1] = v
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
    if not u.diagAt then
        u.diagAt = now
        diag = {}
        errors = {}
        delays = {}
        return
    end
    if now - u.diagAt < diagEvery then
        return
    end
    local dt = now - u.diagAt
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
            u.tierCount and u.tierCount.hot or 0, u.tierCount and u.tierCount.cold or 0
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
        if u.clock.base then
            out[#out + 1] = string.format('clock_rate=%.3f', u.clock.rate)
        end
        -- gap_err=<bucket>:<samples>:<largest correction m>; buckets by source seconds since the
        -- replica's previous sample (base: first sample after a baseline pose).
        local parts = {}
        for _, b in ipairs({ 'lt0.5', 'lt2', 'lt5', 'ge5', 'base' }) do
            local g = u.gaps[b]
            if g then
                parts[#parts + 1] = string.format('%s:%d:%.0f', b, g.n, g.max)
            end
        end
        if #parts > 0 then
            out[#out + 1] = 'gap_err=' .. table.concat(parts, ',')
        end
        -- Engine write budget: configured ms per frame, mean and peak ms used by budgeted visits,
        -- mean ms of the unbudgeted near writes; clock=estimated when no timed clock exists.
        local stats = u.writeStats
        if stats and stats.frames > 0 then
            out[#out + 1] = string.format(
                'budget_ms=%.1f used_ms=%.2f used_ms_max=%.2f near_ms=%.2f budget_clock=%s',
                u.replicaWrite.budgetMs,
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
    u.diagAt = now
    diag = {}
    errors = {}
    delays = {}
    u.gaps = {}
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
local function send(p)
    s.transport:send(p)
end
local function protected(object)
    local k = key(object)
    return u.excluded[k]
        or u.replicaKeys[k]
        or A.coop.entity(object) > 0
        or object == C.GetPlayerControlledShipID()
        or object == C.GetPlayerOccupiedShipID()
        or object == C.GetPlayerContainerID()
end
local function currentSector()
    local object = C.GetPlayerContainerID()
    if object == 0 then
        return nil
    end
    local sector = C.GetContextByClass(object, 'sector', false)
    if sector == 0 then
        return nil
    end
    local name = GetComponentData(A.id(sector), 'macro')
    if not P.atom(name, true) then
        return nil
    end
    A.coop.sectors[name] = sector
    return name, sector
end
local function beginScan(name, sector)
    local factions = ffi.new('const char*[256]')
    local count = tonumber(C.GetAllFactions(factions, 256, true))
    assert(
        count > 0 and count < 256,
        'faction enumeration unavailable or exceeded limit; refusing an incomplete baseline'
    )
    local names = {}
    for i = 0, count - 1 do
        names[#names + 1] = ffi.string(factions[i])
    end
    return { name = name, sector = sector, factions = names, faction = 1, index = 0, seen = {}, objects = {}, total = 0 }
end
-- Capture filter, shared by the faction enumeration and the sector sweep: an operational
-- ship in the scanned sector that is not protected (replica, shared co-op ship, player
-- occupant, excluded fixture) and not owned by the player faction. There is no class,
-- size, purpose or docking filter; a baseline holds at most P.limit (4096) objects.
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
        not C.IsComponentOperational(object)
        or protected(object)
        or not C.IsComponentClass(object, 'ship')
    then
        return
    end
    if C.GetContextByClass(object, 'sector', false) ~= scan.sector then
        return
    end
    local macro, owner = GetComponentData(A.id(object), 'macro', 'owner')
    assert(
        P.atom(macro, true) and P.atom(owner),
        'NPC descriptor unavailable; refusing incomplete baseline'
    )
    -- player_factions: a host ship owned only by the host's human replicates as its pool faction.
    if owner == 'player' and cfg.slot == 'p1' and A.players then
        owner = A.players.personalOwner(object) or owner
    end
    if owner == 'player' then
        return
    end
    scan.objects[#scan.objects + 1] =
        { object = object, key = objectKey, macro = macro, owner = owner }
    assert(#scan.objects <= P.limit, 'sector object limit exceeded; refusing a truncated baseline')
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
            local count = tonumber(C.GetNumAllFactionShips(faction))
            assert(count <= MAX_SCANNED_OBJECTS, 'faction fleet exceeds internal scan limit')
            -- Headroom plus post-read count catches growth/truncation during enumeration.
            local capacity = count + 64
            scan.buffer = ffi.new('UniverseID[?]', capacity)
            scan.count = tonumber(C.GetAllFactionShips(scan.buffer, capacity, faction))
            scan.index = 0
            if
                scan.count >= capacity
                or tonumber(C.GetNumAllFactionShips(faction)) > scan.count
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
    AddUITriggeredEvent('X4LAN_Universe', 'Sweep', { sector = A.id(sectorId), token = sweep.token })
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
        u.sweepUnavailable = true
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
        if scan.retry or u.sweepUnavailable then
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
    local p = C.GetObjectPositionInSector(object)
    return { p.x, p.y, p.z, p.yaw, p.pitch, p.roll }
end
local function poseFields(p)
    local out = {}
    for _, v in ipairs(p) do
        out[#out + 1] = string.format('%.9g', v)
    end
    return table.concat(out, ',')
end
local function captured(scan)
    local rows = {}
    local objects = {}
    for _, entry in ipairs(scan.objects) do
        if
            C.IsComponentOperational(entry.object)
            and C.GetContextByClass(entry.object, 'sector', false) == scan.sector
        then
            local identity = u.identities[entry.key]
            local code = A.npcCombat and A.npcCombat.code(entry.object)
            if
                not identity
                or identity.retired
                or identity.macro ~= entry.macro
                or A.npcCombat
                    and (identity.code ~= code or identity.owner ~= entry.owner or identity.sector ~= scan.name)
            then
                u.nextId = u.nextId + 1
                assert(u.nextId <= 2147483647, 'world ID space exhausted')
                identity = {
                    id = u.nextId,
                    macro = entry.macro,
                    code = code,
                    owner = entry.owner,
                    sector = scan.name,
                }
                u.identities[entry.key] = identity
            end
            local row = identity.id
                .. ',ship,'
                .. entry.macro
                .. ','
                .. entry.owner
                .. ','
                .. poseFields(pose(entry.object))
                .. ',none,0'
            assert(P.record(row), 'invalid captured NPC state')
            objects[#objects + 1] =
                { id = identity.id, object = entry.object, sector = scan.name, identity = identity }
            rows[#rows + 1] = { id = identity.id, row = row }
        end
    end
    if A.cargo then
        for _, row in ipairs(A.cargo.rows(scan.sector)) do
            rows[#rows + 1] = row
        end
    end
    assert(#rows <= P.limit, 'combined sector object limit exceeded')
    u.lastScan = scan
    u.lastScans[scan.name] = scan
    u.cargoDirty = false
    table.sort(rows, function(a, b)
        return a.id < b.id
    end)
    local ordered = {}
    for i, r in ipairs(rows) do
        ordered[i] = r.row
    end
    u.revision = u.revision + 1
    u.publishes[scan.name] = objects
    u.publish = {}
    u.motionCursor = 1
    for _, entries in pairs(u.publishes) do
        for _, entry in ipairs(entries) do
            u.publish[#u.publish + 1] = entry
        end
    end
    u.sender = P.sender(P.baseline(s.client.lease, u.revision, scan.name, ordered))
    log('CAPTURE revision=' .. u.revision .. ' sector=' .. scan.name .. ' objects=' .. #ordered)
end
local function removeReplica(r)
    local object = r.object
    if object then
        count('removes')
    end
    if object and C.IsComponentOperational(object) then
        -- Recheck live player IDs; a player may have boarded since creation.
        if
            A.coop.entity(object) > 0
            or object == C.GetPlayerControlledShipID()
            or object == C.GetPlayerOccupiedShipID()
            or object == C.GetPlayerContainerID()
        then
            error('player entered an NPC replica; end this internal scene test')
        end
        C.RemoveComponent(object)
    end
    if object then
        u.replicaKeys[key(object)] = nil
    end
end
local function accept(b, now)
    local name = currentSector()
    if b.sector ~= name then
        return
    end
    u.acceptedAt = now
    u.latest = b
    if cfg.world_mode == 'observe' then
        log('OBSERVED revision=' .. b.revision .. ' sector=' .. b.sector .. ' objects=' .. #b.rows)
        return
    end
    local wanted = {}
    u.renderIds = {}
    u.renderCursor = 1
    for _, row in ipairs(b.rows) do
        local entity = assert(P.record(row))
        assert(entity.kind == 'ship' or A.cargo, 'cargo adapter missing')
        wanted[entity.id] = entity
        u.renderIds[#u.renderIds + 1] = entity.id
    end
    for id, r in pairs(u.replicas) do
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
            u.replicas[id] = nil
        end
    end
    for id, entity in pairs(wanted) do
        local r = u.replicas[id]
        if not r then
            r = { entity = entity, sector = b.sector, seq = -1 }
            u.replicas[id] = r
        else
            r.entity = entity
            -- A baseline pose is older than its capture plus transfer time. It replaces live motion
            -- only after the motion feed has been silent for NPC.hold seconds (packet loss/recovery);
            -- otherwise a moving replica would jump back to the capture position on every baseline.
            if not r.at or now - r.at >= (r.modern and NPC.hold or M.stale) then
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
function u.receive(f, now)
    if f[1] == 'UINTEREST' then
        if cfg.slot == 'p1' and (#f == 5 or #f == 6) and P.integer(f[4]) and P.atom(f[5], true) then
            u.wanted[f[5]] = now
        end
        return true
    elseif f[1] == 'UACK' then
        local rev = P.integer(f[5])
        local index = f[6] == '-1' and -1 or P.integer(f[6])
        if cfg.slot == 'p1' and #f == 6 and rev and index and u.sender then
            u.sender:ack(f[4], rev, index)
        end
        return true
    elseif f[1] == 'UPOS' or f[1] == 'UMOVE' then
        local modern = f[1] == 'UMOVE'
        if
            cfg.slot == 'p1'
            or #f ~= (modern and 20 or 13)
            or not u.latest
            or f[4] ~= u.latest.epoch
        then
            return true
        end
        local id, seq = P.integer(f[5]), P.integer(f[6])
        local r = id and u.replicas[id]
        if not r or not seq or seq <= r.seq or f[7] ~= r.sector then
            return true
        end
        local start = modern and 9 or 8
        local row = id
            .. ',ship,'
            .. r.entity.macro
            .. ','
            .. r.entity.owner
            .. ','
            .. table.concat(f, ',', start, start + 5)
            .. ',none,0'
        local parsed = P.record(row)
        if not parsed then
            return true
        end
        local velocity = { 0, 0, 0, 0, 0, 0 }
        if modern then
            -- Dead reckoning on the server's clock (source time in the sample, rate estimated).
            local time = tonumber(f[8])
            for i = 1, 6 do
                velocity[i] = tonumber(f[14 + i])
            end
            local track = r.modern and r.track or {}
            local last = track.time
            -- A replica without a live track shows its baseline pose: the first sample blends from it.
            local ok, err, snapped = M.drPush(
                track,
                u.clock,
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
            u.motionAt = now
            sample(delays, M.drExcess(u.clock, time, now))
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
                    local g = u.gaps[b] or { n = 0, max = 0 }
                    u.gaps[b] = g
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
            M.push(track, now - 0.1, r.entity.pose, velocity, now - 0.1)
        end
        local resets = track.resets or 0
        if not M.push(track, now, parsed.pose, velocity, now) then
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
    elseif f[1] == 'UFOCUS' then
        -- A client's selected target (network ID, 0 = none); hot on the server for focusHold s.
        -- The dedicated relay appends the client's lease; the two-player relay does not.
        if cfg.slot == 'p1' and (#f == 5 or #f == 6) then
            local seq, id = P.integer(f[4]), P.integer(f[5])
            local who = f[6] or 'guest'
            local old = u.focus[who]
            if seq and id and (not old or seq > old.seq) then
                u.focus[who] = { seq = seq, id = id, at = now }
            end
        end
        return true
    elseif f[1] == 'UBEGIN' or f[1] == 'UOBJECT' or f[1] == 'UCOMMIT' then
        if cfg.slot == 'p1' or u.failed then
            return true
        end
        local ack, b = u.receiver:receive(f, now)
        if b then
            u.incoming = b
        end -- Engine changes belong to an unpaused frame.
        if ack then
            send('UACK|1|' .. s.client.lease .. '|' .. f[4] .. '|' .. f[5] .. '|' .. ack)
        end
        return true
    end
    return false
end
-- Engine pose writes. SetObjectSectorPos runs on the client's main thread and is the
-- expensive part of a replica: live clients drew about 1,700 replica writes per second and
-- fell from about 40 fps (before the replicas loaded) to 13-16 fps, about 0.3-0.4 ms per
-- write. The prediction (motion.lua M.drRender) is continuous and costs microseconds; only
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
u.replicaWrite = REPLICA_WRITE
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
u.writeStats = writeStats
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
    return C.GetContextByClass(object, 'zone', false)
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
        written = ffi.new(
            'UIPosRot',
            {
                x = enginePose.x - offset[1],
                y = enginePose.y - offset[2],
                z = enginePose.z - offset[3],
                yaw = enginePose.yaw,
                pitch = enginePose.pitch,
                roll = enginePose.roll,
            }
        )
    end
    C.SetObjectSectorPos(object, sectorId, written)
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
        C.SetObjectSectorPos(object, sectorId, enginePose)
        action = 'rewrite'
    elseif slip > 0 and offset then
        action = 'zone'
    elseif slip > 0 then
        C.SetObjectSectorPos(object, sectorId, enginePose)
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
        local predicted, velocity = M.drRender(replica.track, u.renderT, now, u.clock.rate)
        if predicted then
            pose = predicted
            speed = M.speed(velocity)
        end
    elseif replica.track then
        pose = M.render(replica.track, now, 0.1) or pose
    end
    return pose, speed
end
-- Write class and the time until the next visit.
local function writeClass(replica, pose, speed)
    if replica.track and not replica.modern then
        return 'near', 0 -- legacy UPOS replicas have no velocity to classify by
    end
    local distance2 = u.playerPos and distanceSquared(pose, u.playerPos) or math.huge
    local focused = u.focusId == replica.entity.id
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
        if math.abs(M.angle(pose[i] - applied[i])) > REPLICA_WRITE.minTurn then
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
    local engine = C.GetObjectPositionInSector(replica.object)
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
    if replica.object and C.IsComponentOperational(replica.object) then
        return
    end
    count(replica.object and 'respawns' or 'creates')
    replica.object =
        C.SpawnObjectAtPos2(replica.entity.macro, sectorId, enginePose, replica.entity.owner)
    assert(replica.object ~= 0, 'NPC replica spawn failed')
    u.replicaKeys[key(replica.object)] = replica.entity.id
    C.ActivateObject(replica.object, false)
    replica.applied = nil
end
-- One visit: predict, classify, and write the engine pose when the class asks for it.
function u.writeReplica(replica, now, sectorId)
    if replica.entity.kind == 'drop' then
        A.cargo.ensure(replica, sectorId)
        replica.nextWrite = now + REPLICA_WRITE.viewInterval
        return
    end
    if A.npcCombat and A.npcCombat.hidden(replica) then
        if not replica.hidden then
            replica.hidden = true
            count('hides')
        end
        A.npcCombat.hide(replica, now)
        replica.nextWrite = now + REPLICA_WRITE.viewInterval
        return
    end
    if replica.hidden then
        replica.hidden = nil
        count('shows')
    end
    local pose, speed = predictedPose(replica, now)
    local enginePose = ffi.new(
        'UIPosRot',
        {
            x = pose[1],
            y = pose[2],
            z = pose[3],
            yaw = pose[4] * RADIANS_TO_DEGREES,
            pitch = pose[5] * RADIANS_TO_DEGREES,
            roll = pose[6] * RADIANS_TO_DEGREES,
        }
    )
    ensureObject(replica, sectorId, enginePose)
    assert(
        replica.object ~= C.GetPlayerContainerID()
            and replica.object ~= C.GetPlayerControlledShipID(),
        'player in NPC replica'
    )
    local class, interval = writeClass(replica, pose, speed)
    replica.writeClass = class
    replica.tier = (class == 'near' or u.focusId == replica.entity.id) and 'hot' or 'cold'
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
    local own = C.GetPlayerContainerID()
    if own == 0 or C.GetContextByClass(own, 'sector', false) ~= sectorId then
        return nil
    end
    local position = C.GetObjectPositionInSector(own)
    return { position.x, position.y, position.z }
end
local function isNear(replica)
    return replica.object and (replica.writeClass == 'near' or u.focusId == replica.entity.id)
end
local SECONDS_TO_MS = 1000
local BUDGET_EPSILON_MS = 1e-6 -- summed clock and estimate steps must not overshoot by rounding
-- Per frame: every due near replica, then the other due visits within the time budget.
function u.renderReplicas(now, sectorId)
    local ids = u.renderIds or {}
    if #ids == 0 then
        return
    end
    u.frameNo = u.frameNo + 1
    u.renderT = u.clock.base and M.drTime(u.clock, now)
    u.playerPos = playerPosition(sectorId)
    local near = {}
    local others = {}
    local nearCount = 0
    for _, id in ipairs(ids) do
        local replica = u.replicas[id]
        if replica and replica.sector == u.interest then
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
    u.nearCount = nearCount
    local clock = writeClock()
    local nearStart = clock and clock()
    for _, replica in ipairs(near) do
        u.writeReplica(replica, now, sectorId)
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
        local wrote = u.writeReplica(replica, now, sectorId)
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
        if object and object ~= 0 and C.IsComponentOperational(object) then
            local sector = C.GetContextByClass(object, 'sector', false)
            if sector ~= 0 then
                local p = C.GetObjectPositionInSector(object)
                local k = key(sector)
                local list = out[k] or {}
                out[k] = list
                list[#list + 1] = { p.x, p.y, p.z }
                n = n + 1
            end
        end
    end
    local owners = A.coop.owners or {}
    for i = 1, A.coop.count or 0 do
        if cfg.dedicated ~= true or (owners[i] and owners[i] ~= cfg.slot) then
            add(A.coop.ships[i])
        end
    end
    if cfg.dedicated ~= true then
        add(C.GetPlayerContainerID())
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
local function headingBranch(pose, v)
    if math.sqrt(v[1] ^ 2 + v[3] ^ 2) < 1 then
        return 0
    end
    local heading = math.atan2(v[1], v[3])
    local a, b = math.abs(M.angle(heading - pose[4])), math.abs(M.angle(heading + pose[4]))
    if a < M.dr.headingMatch and a <= b + 0.05 then
        return 1
    elseif b < M.dr.headingMatch then
        return -1
    end
    return 0
end
local function velocityOf(identity, p, now)
    local h = identity.hist or {}
    identity.hist = h
    local last = h[#h]
    if last and (now - last.time > 1 or now <= last.time) then
        h = {}
        identity.hist = h
        last = nil
    end
    if last then
        local d2 = 0
        for i = 1, 3 do
            d2 = d2 + (p[i] - last.pose[i]) ^ 2
        end
        if math.sqrt(d2) / (now - last.time) > 100000 then
            identity.hist = { { time = now, pose = p } }
            return { 0, 0, 0, 0, 0, 0 }
        end
    end
    h[#h + 1] = { time = now, pose = p }
    while #h > 2 and now - h[2].time >= NPC.velWindow do
        table.remove(h, 1)
    end
    local o = h[1]
    local dt = now - o.time
    if #h < 2 or dt <= 0.001 then
        return nil
    end
    local v = {}
    for i = 1, 6 do
        local d = p[i] - o.pose[i]
        if i > 3 then
            d = M.angle(d)
        end
        v[i] = d / dt
        if i > 3 and math.abs(v[i]) > NPC.maxRate then
            v[i] = 0
        end
    end
    -- Heading rate of the velocity: the course over the older and the newer half of the window.
    local m = h[math.max(1, math.floor((#h + 1) / 2))]
    local turn = 0
    if m ~= o and m ~= h[#h] then
        local h1 = math.atan2(m.pose[1] - o.pose[1], m.pose[3] - o.pose[3])
        local h2 = math.atan2(p[1] - m.pose[1], p[3] - m.pose[3])
        local d1, d2 =
            (m.pose[1] - o.pose[1]) ^ 2 + (m.pose[3] - o.pose[3]) ^ 2,
            (p[1] - m.pose[1]) ^ 2 + (p[3] - m.pose[3]) ^ 2
        if d1 > 1 and d2 > 1 then
            turn = M.angle(h2 - h1) / ((now - o.time) / 2)
        end
    end
    turn = math.max(-NPC.maxTurn, math.min(NPC.maxTurn, turn))
    -- The window's chord lags a turning course by half the window: rotate it to the present.
    if turn ~= 0 then
        local a = turn * dt / 2
        local c, s = math.cos(a), math.sin(a)
        v[1], v[3] = v[1] * c + v[3] * s, v[3] * c - v[1] * s
    end
    local branch = headingBranch(p, v)
    if branch ~= 0 then
        v[4] = branch * turn
    end
    return v
end
-- Why and how urgently an NPC is due: never sent, diverged from the shared prediction of its
-- last sent sample (position, heading or velocity beyond the tier threshold), or keepalive.
local function due(identity, p, v, tier, now)
    local s = identity.sent
    if not s then
        return 'first', 1e9, 0
    end
    local age = now - s.time
    local q, qv = M.drPredict(s.pose, s.vel, math.min(age, M.dr.coast))
    if age >= M.dr.coast then
        qv = { 0, 0, 0 }
    end
    local d2, turn, dv = 0, 0, 0
    for i = 1, 3 do
        d2 = d2 + (p[i] - q[i]) ^ 2
        dv = dv + (v[i] - qv[i]) ^ 2
    end
    for i = 4, 6 do
        turn = math.max(turn, math.abs(M.angle(p[i] - q[i])))
    end
    local err = math.sqrt(d2)
    local score = math.max(err / tier.pos, turn / tier.ang, math.sqrt(dv) * tier.look / tier.pos)
    -- Any tier: an error above max(urgentMin m, urgentTime s of speed) is sent at once, so a
    -- fast ship's error stays below the client's snap limit (min intervals only pace small ones).
    if err > math.max(NPC.urgentMin, NPC.urgentTime * M.speed(v)) then
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
function u.publishMotion(now)
    local players, shipCount = playerShips()
    local focus = {}
    peak('players', shipCount)
    for who, f in pairs(u.focus) do
        if now - f.at > NPC.focusHold then
            u.focus[who] = nil
        elseif f.id > 0 then
            focus[f.id] = true
        end
    end
    local total = #u.publish
    local list = {}
    for _ = 1, math.min(total, NPC.evalBudget) do
        local entry = u.publish[u.motionCursor]
        u.motionCursor = u.motionCursor % total + 1
        local identity = entry.identity
        local target = A.coop.sectors[entry.sector]
        if
            target
            and C.IsComponentOperational(entry.object)
            and C.GetContextByClass(entry.object, 'sector', false) == target
        then
            local p = pose(entry.object)
            local v = velocityOf(identity, p, now)
            if v then
                local tier = relevance(entry.id, p, players[key(target)], focus)
                local why, score, err = due(identity, p, v, tier, now)
                if why then
                    list[#list + 1] =
                        { entry = entry, p = p, v = v, tier = tier, why = why, score = score, err = err }
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
        u.motionSeq = u.motionSeq + 1
        entry.identity.sent = { time = now, pose = c.p, vel = c.v }
        count('sent')
        count('sent_' .. c.tier.name)
        count(c.why)
        if c.why ~= 'first' and c.why ~= 'keepalive' then
            sample(errors, c.err)
        end
        send(
            'UMOVE|1|'
                .. s.client.lease
                .. '|'
                .. s.client.lease
                .. '|'
                .. entry.id
                .. '|'
                .. u.motionSeq
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
    if not u.scan and now >= (u.nextScan or 0) then
        if u.sweepUnavailable then
            u.scan = beginScan(name, sectorId)
        else
            u.scan = beginSweep(name, sectorId, now)
        end
    end
    if not u.scan then
        return
    end
    local finished
    if u.scan.sweep then
        finished = sweepStep(u.scan, now)
    else
        finished = scanStep(u.scan)
    end
    if not finished then
        return
    end
    local scan = u.scan
    u.scan = nil
    if scan.retry then
        u.nextScan = now + 1
        return
    end
    local removed = 0
    for _, entry in ipairs(scan.objects) do
        local object = entry.object
        if
            C.IsComponentOperational(object)
            and not protected(object)
            and C.GetContextByClass(object, 'sector', false) == sectorId
        then
            C.RemoveComponent(object)
            removed = removed + 1
        end
    end
    count('suppressed', removed)
    if removed > 0 or now >= (u.nextSuppressLog or 0) then
        u.nextSuppressLog = now + SUPPRESS_LOG_INTERVAL
        log(
            'LOCAL_NPC_SUPPRESSED count='
                .. removed
                .. ' sector='
                .. name
                .. ' source='
                .. (scan.sweep and 'sweep' or 'factions')
        )
    end
    u.nextScan = now + (scan.sweep and NPC.sweepEvery or NPC.suppressEvery)
end
local function frame(now)
    if u.failed or not s.client or not s.client.lease or not A.coop.ready then
        return
    end
    if not u.setup then
        u.setup = true
        AddUITriggeredEvent('X4LAN_Universe', 'Setup')
        return
    end
    if not u.ready or A.paused() then
        return
    end
    local name, sector = currentSector()
    if not name then
        return
    end
    if cfg.slot ~= 'p1' then
        if u.interest ~= name then
            u.interest = name
            u.scan = nil
            u.receiver.pending = nil
            u.latest = nil
            u.nextScan = 0
            for _, r in pairs(u.replicas) do
                removeReplica(r)
            end
            u.replicas = {}
        end
        if now >= (u.nextInterest or 0) then
            u.nextInterest = now + 1
            u.interestSeq = u.interestSeq + 1
            send('UINTEREST|1|' .. s.client.lease .. '|' .. u.interestSeq .. '|' .. name)
        end
        if u.incoming then
            local incoming = u.incoming
            u.incoming = nil
            accept(incoming, now)
        end
        -- Replicas pause only when the server is silent: a stale baseline (membership) alone no
        -- longer freezes replicas that live motion still drives. Live captures took 35-40 s,
        -- so the former 15 s baseline pause froze every replica for about 20 s per cycle.
        local stale = u.latest
            and now - u.acceptedAt >= 15
            and (not u.motionAt or now - u.motionAt >= NPC.motionLive)
        if cfg.world_mode == 'replica' and stale then
            count('paused')
            if not u.renderPaused then
                u.renderPaused = true
                feature(
                    'npc_replicas',
                    'baseline_stale age=' .. string.format('%.1f', now - u.acceptedAt)
                )
            end
        elseif u.renderPaused and u.latest then
            u.renderPaused = nil
            log('FEATURE_RESUMED npc_replicas')
        end
        if cfg.world_mode == 'replica' and u.latest and not stale then
            -- UFOCUS: the replica the local player targets, refreshed every focusEvery s; 0 once
            -- when the target is cleared or is not a replica.
            if getTarget and now >= (u.focusCheck or 0) then
                u.focusCheck = now + 0.25
                local ok, target = pcall(getTarget)
                local id = ok and target and target ~= 0 and u.replicaKeys[key(target)] or nil
                if type(id) ~= 'number' then
                    id = nil
                end
                if id ~= u.focusId or (id and now >= (u.focusNext or 0)) then
                    u.focusSeq = u.focusSeq + 1
                    u.focusId = id
                    u.focusNext = now + NPC.focusEvery
                    send('UFOCUS|1|' .. s.client.lease .. '|' .. u.focusSeq .. '|' .. (id or 0))
                end
            end
            u.renderReplicas(now, sector)
            suppressLocalNpcs(name, sector, now)
        end
        if diagEvery then
            local live = 0
            u.tierCount = { hot = 0, cold = 0 }
            for _, r in pairs(u.replicas) do
                live = live + 1
                local t = r.tier == 'hot' and 'hot' or 'cold'
                u.tierCount[t] = u.tierCount[t] + 1
            end
            diagFlush(
                now,
                'guest',
                'sector='
                    .. name
                    .. ' replicas='
                    .. live
                    .. ' hot='
                    .. (u.nearCount or 0)
                    .. ' baseline_age='
                    .. (u.acceptedAt and string.format('%.1f', now - u.acceptedAt) or '-')
            )
        end
        return
    end
    for name, at in pairs(u.wanted) do
        if now - at > 5 then
            u.wanted[name] = nil
        end
    end
    if u.cargoDirty and (not u.sender or u.sender.done) then
        for name, scan in pairs(u.lastScans) do
            if u.wanted[name] then
                captured(scan)
                break
            end
        end
    end
    if u.sender and not u.sender.done then
        for _, p in ipairs(u.sender:packets(s.client.lease, now)) do
            send(p)
        end
    elseif not u.scan and now >= (u.nextScan or 0) then
        local names = {}
        for name in pairs(u.wanted) do
            names[#names + 1] = name
        end
        table.sort(names)
        if #names > 0 then
            u.scanChoice = (u.scanChoice or 0) % #names + 1
            local name = names[u.scanChoice]
            local target = A.coop.sectorId(name)
            if target then
                u.scan = beginScan(name, target)
            end
        end
    end
    if u.scan and captureStep(u.scan, now) then
        if not u.scan.retry then
            captured(u.scan)
        end
        u.scan = nil
        u.nextScan = now + 2
    end
    if #u.publish > 0 and now >= (u.nextMotion or 0) then
        u.nextMotion = now + 0.045 -- every 0.05 s server frame (float steps must not skip one)
        u.publishMotion(now)
    end
    local names = {}
    for name in pairs(u.wanted) do
        names[#names + 1] = name
    end
    table.sort(names)
    diagFlush(
        now,
        'host',
        'sectors='
            .. (#names > 0 and table.concat(names, ',') or '-')
            .. ' published='
            .. #u.publish
    )
end
function u.frame(now)
    local ok, err = pcall(frame, now)
    if not ok then
        u.failed = true
        log('STOPPED ' .. tostring(err))
        feature('world_sync', 'adapter_stopped')
    end
end
RegisterEvent('X4LAN_Universe.Exclude', function(_, object)
    u.excluded[key(ConvertIDTo64Bit(object))] = true
end)
RegisterEvent('X4LAN_Universe.Ready', function()
    u.ready = true
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
