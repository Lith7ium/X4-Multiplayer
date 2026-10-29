-- Visible on-foot avatars, one per remote client slot (x4lan_walk.xml draws them).
-- WALK2 carries a pose relative to a context every client resolves identically: a shared
-- ship by index (Arena.$ships) or a station by sector macro + ID code + macro. Room
-- placement is resolved locally from that container-relative position.
-- Sender:    WALK2|1|lease|seq|state|kind|ship|sector|code|macro|room|x|y|z|yaw|speed|stime|transfers
-- Forwarded: WALK2|1|lease|senderLease|senderSlot|seq|state|kind|...|transfers
-- Opt-in (walking_avatars); every action the engine might refuse is logged, never retried blindly.
local cfg = X4LAN_CONFIG
local API = X4LAN_BUBBLE_API
if
    not cfg
    or not cfg.enabled
    or not cfg.bubble_actions
    or cfg.walking_avatars ~= true
    or not API
    or not API.state.active
then
    return
end
local sessionState = API.state
local roster = API.roster
    or { players = 2, slot = tonumber(tostring(cfg.slot):match('^p(%d)$')) or 1 }
local function log(value)
    DebugError('[X4LAN] ' .. value)
end
if roster.dedicated and roster.slot == 1 then
    log('WALK_DISABLED server: the dedicated server neither walks nor draws avatars')
    return
end
local walk = { sequence = 0, remotes = {}, transfers = 0, sent = 0, dropped = {}, logAt = {} }
API.walk = walk
walk.DELAY = 0.2 -- render this far behind the newest pose (seconds, sender clock)
walk.APPLY = 0.2 -- MD walk/placement update interval per avatar
walk.STALE = 1 -- hide an avatar without a fresh pose
walk.FORGET = 30 -- forget a silent client (lease timeout)
walk.REFRESH = 2 -- re-send an unchanged placement (a refused placement retries)
walk.MOVING = 0.1 -- local send interval while walking
walk.IDLE = 0.5 -- local send interval while standing (still inside the relay's 1 s freshness)
walk.MAX_SAMPLES = 10
local KINDS = { none = true, ship = true, station = true }
local function atom(value)
    return type(value) == 'string'
        and #value >= 1
        and #value <= 96
        and value:match('^[%w_.-]+$') ~= nil
end

-- Rate-limited diagnostics: one line per key every 5 s, with the suppressed count.
function walk.diag(key, text, now)
    now = now or GetCurRealTime()
    local e = walk.logAt[key]
    if e and now - e.at < 5 then
        e.suppressed = e.suppressed + 1
        return false
    end
    log(text .. ((e and e.suppressed > 0) and (' suppressed=' .. e.suppressed) or ''))
    walk.logAt[key] = { at = now, suppressed = 0 }
    return true
end

local function drop(reason, now, detail)
    walk.dropped[reason] = (walk.dropped[reason] or 0) + 1
    walk.diag(
        'drop:' .. reason,
        'WALK_DROP reason='
            .. reason
            .. (detail and (' ' .. detail) or '')
            .. ' total='
            .. walk.dropped[reason],
        now
    )
end

-- Context validation shared by local and remote poses. Returns nil, reason when invalid.
local function context(pose)
    if not KINDS[pose.kind] then
        return nil, 'kind'
    end
    for _, k in ipairs({ 'sector', 'code', 'macro', 'room' }) do
        if not atom(pose[k]) then
            return nil, k
        end
    end
    if pose.state ~= 0 and pose.state ~= 1 then
        return nil, 'state'
    end
    if (pose.state == 1) == (pose.kind == 'none') then
        return nil, 'state'
    end
    if (pose.kind == 'ship') ~= (pose.ship > 0) or pose.ship > (roster.ships or 2) then
        return nil, 'ship'
    end
    if pose.kind == 'station' and (pose.sector == '-' or pose.code == '-' or pose.macro == '-') then
        return nil, 'station'
    end
    for _, k in ipairs({ 'x', 'y', 'z' }) do
        if math.abs(pose[k]) > 100000 then
            return nil, 'range'
        end
    end
    if math.abs(pose.yaw) > 360 then
        return nil, 'range'
    end
    return pose
end

local function number(value)
    local numberValue = tonumber(value)
    if
        not numberValue
        or numberValue ~= numberValue
        or numberValue == math.huge
        or numberValue == -math.huge
    then
        return nil
    end
    return numberValue
end

local function integer(value, max)
    local numberValue = number(value)
    if not numberValue or numberValue < 0 or numberValue % 1 ~= 0 or numberValue > max then
        return nil
    end
    return numberValue
end

-- Identity of the place a pose is relative to; interpolation never crosses it.
function walk.contextKey(pose)
    return pose.kind
        .. '|'
        .. pose.ship
        .. '|'
        .. pose.sector
        .. '|'
        .. pose.code
        .. '|'
        .. pose.macro
end

-- Forwarded WALK2 fields (1-based) -> sample, or nil, reason.
function walk.parseRemote(fields)
    if #fields ~= 20 then
        return nil, 'format'
    end
    if not fields[4]:match('^[a-f0-9]+$') or #fields[4] ~= 32 then
        return nil, 'lease'
    end
    local slot = tonumber(fields[5]:match('^p([1-8])$') or '')
    if not slot or slot > roster.players then
        return nil, 'slot'
    end
    if slot == roster.slot then
        return nil, 'own'
    end
    local pose = {
        slot = slot,
        lease = fields[4],
        seq = integer(fields[6], 2147483647),
        state = integer(fields[7], 1),
        kind = fields[8],
        ship = integer(fields[9], 8),
        sector = fields[10],
        code = fields[11],
        macro = fields[12],
        room = fields[13],
        x = number(fields[14]),
        y = number(fields[15]),
        z = number(fields[16]),
        yaw = number(fields[17]),
        speed = number(fields[18]),
        stime = number(fields[19]),
        transfers = integer(fields[20], 2147483647),
    }
    for _, k in ipairs({
        'seq',
        'state',
        'ship',
        'x',
        'y',
        'z',
        'yaw',
        'speed',
        'stime',
        'transfers',
    }) do
        if not pose[k] then
            return nil, 'number'
        end
    end
    if pose.speed < 0 or pose.speed > 100 or pose.stime < 0 or pose.stime > 1e9 then
        return nil, 'range'
    end
    return context(pose)
end

-- MD's local payload: state|kind|ship|sector|code|macro|room|x|y|z|yaw|transfers|reason.
function walk.parseLocal(payload)
    local fields = {}
    for value in (tostring(payload) .. '|'):gmatch('([^|]*)|') do
        fields[#fields + 1] = value
    end
    if #fields ~= 13 then
        return nil, 'format'
    end
    local pose = {
        state = integer(fields[1], 1),
        kind = fields[2],
        ship = integer(fields[3], 8),
        sector = fields[4],
        code = fields[5],
        macro = fields[6],
        room = fields[7],
        x = number(fields[8]),
        y = number(fields[9]),
        z = number(fields[10]),
        yaw = number(fields[11]),
        transfers = integer(fields[12], 2147483647),
        reason = fields[13],
    }
    for _, k in ipairs({ 'state', 'ship', 'x', 'y', 'z', 'yaw', 'transfers' }) do
        if not pose[k] then
            return nil, 'number'
        end
    end
    if pose.code == '' then
        pose.code = '-'
    end
    if pose.room == '' then
        pose.room = '-'
    end
    if pose.kind == 'station' and pose.code == '-' then
        pose.state, pose.kind, pose.reason = '0', 'none', 'nocode'
    end -- fail closed: no identity
    pose.state = tonumber(pose.state)
    if pose.state == 0 then
        pose.kind, pose.ship, pose.sector, pose.code, pose.macro, pose.room, pose.x, pose.y, pose.z, pose.yaw =
            'none', 0, '-', '-', '-', '-', 0, 0, 0, 0
    end
    if not atom(pose.reason) then
        pose.reason = '-'
    end
    return context(pose)
end

local function yawDelta(a, b)
    local d = (b - a) % 360
    if d > 180 then
        d = d - 360
    end
    return d
end

local function distance(a, b)
    return math.sqrt((a.x - b.x) ^ 2 + (a.y - b.y) ^ 2 + (a.z - b.z) ^ 2)
end

-- Interpolated pose at the render time (sender clock), never extrapolated past the newest.
function walk.sample(remote, now)
    local list = remote.samples
    local numberValue = #list
    if numberValue == 0 then
        return nil
    end
    local t = now - (remote.offset or 0) - walk.DELAY
    if t >= list[numberValue].stime then
        return list[numberValue]
    end
    if t <= list[1].stime then
        return list[1]
    end
    for i = numberValue - 1, 1, -1 do
        local a, b = list[i], list[i + 1]
        if a.stime <= t then
            local k = (b.stime > a.stime) and (t - a.stime) / (b.stime - a.stime) or 1
            local pose = {}
            for key, value in pairs(b) do
                pose[key] = value
            end
            pose.x = a.x + (b.x - a.x) * k
            pose.y = a.y + (b.y - a.y) * k
            pose.z = a.z + (b.z - a.z) * k
            pose.yaw = a.yaw + yawDelta(a.yaw, b.yaw) * k
            if pose.yaw > 180 then
                pose.yaw = pose.yaw - 360
            elseif pose.yaw < -180 then
                pose.yaw = pose.yaw + 360
            end
            return pose
        end
    end
    return list[numberValue]
end

function walk.receive(fields, now)
    if fields[1] == 'WALK' then
        drop('legacy_walk', now)
        return true
    end
    if fields[1] ~= 'WALK2' then
        return false
    end
    local pose, reason = walk.parseRemote(fields)
    if not pose then
        drop(reason, now, 'slot=' .. tostring(fields[5]))
        return true
    end
    local remote = walk.remotes[pose.slot]
    if remote and remote.lease ~= pose.lease then
        log('WALK_REMOTE slot=p' .. pose.slot .. ' new connection; previous pose history discarded')
        remote = nil
    end
    if not remote then
        remote = {
            slot = pose.slot,
            seq = -1,
            samples = {},
            visible = false,
            transfers = pose.transfers,
        }
        walk.remotes[pose.slot] = remote
    end
    remote.lease = pose.lease
    if pose.seq <= remote.seq then
        drop('order', now, 'slot=p' .. pose.slot)
        return true
    end
    remote.seq, remote.at = pose.seq, now
    -- Sender clock -> local clock: the smallest observed delay, creeping up slowly for drift.
    local o = now - pose.stime
    if not remote.offset or o < remote.offset then
        remote.offset = o
    else
        remote.offset = remote.offset + (o - remote.offset) * 0.01
    end
    local last = remote.samples[#remote.samples]
    if
        last
        and (
            walk.contextKey(last) ~= walk.contextKey(pose)
            or last.transfers ~= pose.transfers
            or last.room ~= pose.room
            or pose.stime < last.stime
        )
    then
        remote.samples = {}
    end
    remote.samples[#remote.samples + 1] = pose
    while #remote.samples > walk.MAX_SAMPLES do
        table.remove(remote.samples, 1)
    end
    return true
end

local function hide(remote, reason, now)
    if remote.visible or reason == 'forget' then
        AddUITriggeredEvent('X4LAN_Walk', 'Hide', { slot = remote.slot, reason = reason })
        walk.diag(
            'hide:' .. remote.slot .. ':' .. reason,
            'WALK_HIDE slot=p' .. remote.slot .. ' reason=' .. reason,
            now
        )
    end
    remote.visible = false
    remote.applied = nil
end

local function apply(remote, now)
    local pose = walk.sample(remote, now)
    if not pose then
        return
    end
    if pose.state == 0 then
        hide(remote, 'not_on_foot', now)
        return
    end
    local a = remote.applied
    local place = a and walk.contextKey(a) == walk.contextKey(pose)
    local same = place and a.room == pose.room
    local mode = 'move'
    -- A transporter ride usually changes the room; it stays inside one context.
    if place and pose.transfers ~= a.transfers then
        mode = 'transport'
    end
    local moved = not a
        or not same
        or mode == 'transport'
        or distance(a, pose) > 0.05
        or math.abs(yawDelta(a.yaw, pose.yaw)) > 5
    if not moved and now - (remote.appliedAt or 0) < walk.REFRESH then
        return
    end
    local speed = same and distance(a, pose) / walk.APPLY or 0
    speed = math.max(0.3, math.min(7, speed))
    AddUITriggeredEvent('X4LAN_Walk', 'Move', {
        slot = remote.slot,
        kind = pose.kind,
        ship = pose.ship,
        sector = pose.sector,
        code = pose.code,
        macro = pose.macro,
        room = pose.room,
        x = pose.x,
        y = pose.y,
        z = pose.z,
        yaw = pose.yaw,
        speed = speed,
        mode = mode,
    })
    if not remote.visible then
        walk.diag(
            'show:' .. remote.slot,
            string.format(
                'WALK_SHOW slot=p%d kind=%s ship=%d code=%s room=%s',
                remote.slot,
                pose.kind,
                pose.ship,
                pose.code,
                pose.room
            ),
            now
        )
    end
    if mode == 'transport' then
        walk.diag(
            'transport:' .. remote.slot,
            'WALK_TRANSPORT slot=p'
                .. remote.slot
                .. ' transfers='
                .. pose.transfers
                .. ' room='
                .. pose.room,
            now
        )
    end
    remote.applied = pose
    remote.appliedAt = now
    remote.visible = true
end

function walk.frame(now)
    if not walk.setup and sessionState.ready then
        walk.setup = true
        walk.setupAt = now
        AddUITriggeredEvent('X4LAN_Walk', 'Setup', { players = roster.players, slot = roster.slot })
    end
    if walk.setup and not walk.ready and not walk.failed and now - walk.setupAt > 10 then
        walk.failed = true
        log('WALK_STOPPED: MD avatar session did not confirm setup; avatars stay hidden')
    end
    -- Local pose. MD refreshes it every 0.1 s; a stale local pose is never re-published.
    local pose = walk.localPose
    if
        pose
        and sessionState.client
        and sessionState.client.lease
        and not API.paused()
        and now - pose.at < 1.5
    then
        local interval = (pose.state == 1 and (pose.speed or 0) > 0.3) and walk.MOVING or walk.IDLE
        if pose.changed or now >= (walk.lastSend or -math.huge) + interval then
            walk.sequence = walk.sequence + 1
            walk.lastSend = now
            pose.changed = false
            walk.sent = walk.sent + 1
            sessionState.transport:send(
                string.format(
                    'WALK2|1|%s|%d|%d|%s|%d|%s|%s|%s|%s|%.3f|%.3f|%.3f|%.2f|%.2f|%.3f|%d',
                    sessionState.client.lease,
                    walk.sequence,
                    pose.state,
                    pose.kind,
                    pose.ship,
                    pose.sector,
                    pose.code,
                    pose.macro,
                    pose.room,
                    pose.x,
                    pose.y,
                    pose.z,
                    pose.yaw,
                    math.min(100, pose.speed or 0),
                    math.max(0, math.min(1e9, now)),
                    pose.transfers
                )
            )
            walk.diag(
                'send',
                'WALK_SEND sent='
                    .. walk.sent
                    .. ' state='
                    .. pose.state
                    .. ' kind='
                    .. pose.kind
                    .. ' reason='
                    .. pose.reason,
                now
            )
        end
    end
    for slot, remote in pairs(walk.remotes) do
        if now - (remote.at or now) > walk.FORGET then
            hide(remote, 'forget', now)
            walk.remotes[slot] = nil
        elseif now - (remote.at or now) > walk.STALE then
            hide(remote, 'stale', now)
        elseif walk.ready and not walk.failed and now >= (remote.nextApply or 0) then
            remote.nextApply = now + walk.APPLY
            apply(remote, now)
        end
    end
end
RegisterEvent('X4LAN_Walk.Ready', function()
    if not walk.ready then
        walk.ready = true
        log('WALK_READY players=' .. roster.players .. ' slot=p' .. roster.slot)
    end
end)
RegisterEvent('X4LAN_Walk.Pose2', function(_, payload)
    local now = GetCurRealTime()
    local pose, reason = walk.parseLocal(payload)
    if not pose then
        walk.diag('local:' .. reason, 'WALK_LOCAL_INVALID reason=' .. reason)
        return
    end
    local old = walk.localPose
    pose.at = now
    pose.speed = 0
    if
        old
        and old.state == 1
        and pose.state == 1
        and walk.contextKey(old) == walk.contextKey(pose)
        and now > old.at
    then
        pose.speed = (old.speed or 0) * 0.5 + distance(old, pose) / (now - old.at) * 0.5
    end
    pose.changed = not old
        or old.state ~= pose.state
        or walk.contextKey(old) ~= walk.contextKey(pose)
        or old.room ~= pose.room
        or old.transfers ~= pose.transfers
    if old and old.changed then
        pose.changed = true
    end -- an unsent change stays pending
    walk.localPose = pose
end)
