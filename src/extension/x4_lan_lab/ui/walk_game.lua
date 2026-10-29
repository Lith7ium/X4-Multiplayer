-- Visible on-foot avatars, one per remote client slot (x4lan_walk.xml draws them).
-- WALK2 carries a pose relative to a context every client resolves identically: a shared
-- ship by index (Arena.$ships) or a station by sector macro + ID code + macro. Room
-- placement is resolved locally from that container-relative position.
-- Sender:    WALK2|1|lease|seq|state|kind|ship|sector|code|macro|room|x|y|z|yaw|speed|stime|transfers
-- Forwarded: WALK2|1|lease|senderLease|senderSlot|seq|state|kind|...|transfers
-- Opt-in (walking_avatars); every action the engine might refuse is logged, never retried blindly.
local cfg = X4LAN_CONFIG
local A = X4LAN_BUBBLE_API
if
    not cfg
    or not cfg.enabled
    or not cfg.bubble_actions
    or cfg.walking_avatars ~= true
    or not A
    or not A.state.active
then
    return
end
local s = A.state
local R = A.roster or { players = 2, slot = tonumber(tostring(cfg.slot):match('^p(%d)$')) or 1 }
local function log(v)
    DebugError('[X4LAN] ' .. v)
end
if R.dedicated and R.slot == 1 then
    log('WALK_DISABLED server: the dedicated server neither walks nor draws avatars')
    return
end
local w = { sequence = 0, remotes = {}, transfers = 0, sent = 0, dropped = {}, logAt = {} }
A.walk = w
w.DELAY = 0.2 -- render this far behind the newest pose (seconds, sender clock)
w.APPLY = 0.2 -- MD walk/placement update interval per avatar
w.STALE = 1 -- hide an avatar without a fresh pose
w.FORGET = 30 -- forget a silent client (lease timeout)
w.REFRESH = 2 -- re-send an unchanged placement (a refused placement retries)
w.MOVING = 0.1 -- local send interval while walking
w.IDLE = 0.5 -- local send interval while standing (still inside the relay's 1 s freshness)
w.MAX_SAMPLES = 10
local KINDS = { none = true, ship = true, station = true }
local function atom(v)
    return type(v) == 'string' and #v >= 1 and #v <= 96 and v:match('^[%w_.-]+$') ~= nil
end
-- Rate-limited diagnostics: one line per key every 5 s, with the suppressed count.
function w.diag(key, text, now)
    now = now or GetCurRealTime()
    local e = w.logAt[key]
    if e and now - e.at < 5 then
        e.suppressed = e.suppressed + 1
        return false
    end
    log(text .. ((e and e.suppressed > 0) and (' suppressed=' .. e.suppressed) or ''))
    w.logAt[key] = { at = now, suppressed = 0 }
    return true
end
local function drop(reason, now, detail)
    w.dropped[reason] = (w.dropped[reason] or 0) + 1
    w.diag(
        'drop:' .. reason,
        'WALK_DROP reason='
            .. reason
            .. (detail and (' ' .. detail) or '')
            .. ' total='
            .. w.dropped[reason],
        now
    )
end
-- Context validation shared by local and remote poses. Returns nil, reason when invalid.
local function context(p)
    if not KINDS[p.kind] then
        return nil, 'kind'
    end
    for _, k in ipairs({ 'sector', 'code', 'macro', 'room' }) do
        if not atom(p[k]) then
            return nil, k
        end
    end
    if p.state ~= 0 and p.state ~= 1 then
        return nil, 'state'
    end
    if (p.state == 1) == (p.kind == 'none') then
        return nil, 'state'
    end
    if (p.kind == 'ship') ~= (p.ship > 0) or p.ship > (R.ships or 2) then
        return nil, 'ship'
    end
    if p.kind == 'station' and (p.sector == '-' or p.code == '-' or p.macro == '-') then
        return nil, 'station'
    end
    for _, k in ipairs({ 'x', 'y', 'z' }) do
        if math.abs(p[k]) > 100000 then
            return nil, 'range'
        end
    end
    if math.abs(p.yaw) > 360 then
        return nil, 'range'
    end
    return p
end
local function number(v)
    local n = tonumber(v)
    if not n or n ~= n or n == math.huge or n == -math.huge then
        return nil
    end
    return n
end
local function integer(v, max)
    local n = number(v)
    if not n or n < 0 or n % 1 ~= 0 or n > max then
        return nil
    end
    return n
end
-- Identity of the place a pose is relative to; interpolation never crosses it.
function w.contextKey(p)
    return p.kind .. '|' .. p.ship .. '|' .. p.sector .. '|' .. p.code .. '|' .. p.macro
end
-- Forwarded WALK2 fields (1-based) -> sample, or nil, reason.
function w.parseRemote(f)
    if #f ~= 20 then
        return nil, 'format'
    end
    if not f[4]:match('^[a-f0-9]+$') or #f[4] ~= 32 then
        return nil, 'lease'
    end
    local slot = tonumber(f[5]:match('^p([1-8])$') or '')
    if not slot or slot > R.players then
        return nil, 'slot'
    end
    if slot == R.slot then
        return nil, 'own'
    end
    local p = {
        slot = slot,
        lease = f[4],
        seq = integer(f[6], 2147483647),
        state = integer(f[7], 1),
        kind = f[8],
        ship = integer(f[9], 8),
        sector = f[10],
        code = f[11],
        macro = f[12],
        room = f[13],
        x = number(f[14]),
        y = number(f[15]),
        z = number(f[16]),
        yaw = number(f[17]),
        speed = number(f[18]),
        stime = number(f[19]),
        transfers = integer(f[20], 2147483647),
    }
    for _, k in ipairs({ 'seq', 'state', 'ship', 'x', 'y', 'z', 'yaw', 'speed', 'stime', 'transfers' }) do
        if not p[k] then
            return nil, 'number'
        end
    end
    if p.speed < 0 or p.speed > 100 or p.stime < 0 or p.stime > 1e9 then
        return nil, 'range'
    end
    return context(p)
end
-- MD's local payload: state|kind|ship|sector|code|macro|room|x|y|z|yaw|transfers|reason.
function w.parseLocal(payload)
    local f = {}
    for v in (tostring(payload) .. '|'):gmatch('([^|]*)|') do
        f[#f + 1] = v
    end
    if #f ~= 13 then
        return nil, 'format'
    end
    local p = {
        state = integer(f[1], 1),
        kind = f[2],
        ship = integer(f[3], 8),
        sector = f[4],
        code = f[5],
        macro = f[6],
        room = f[7],
        x = number(f[8]),
        y = number(f[9]),
        z = number(f[10]),
        yaw = number(f[11]),
        transfers = integer(f[12], 2147483647),
        reason = f[13],
    }
    for _, k in ipairs({ 'state', 'ship', 'x', 'y', 'z', 'yaw', 'transfers' }) do
        if not p[k] then
            return nil, 'number'
        end
    end
    if p.code == '' then
        p.code = '-'
    end
    if p.room == '' then
        p.room = '-'
    end
    if p.kind == 'station' and p.code == '-' then
        p.state, p.kind, p.reason = '0', 'none', 'nocode'
    end -- fail closed: no identity
    p.state = tonumber(p.state)
    if p.state == 0 then
        p.kind, p.ship, p.sector, p.code, p.macro, p.room, p.x, p.y, p.z, p.yaw =
            'none', 0, '-', '-', '-', '-', 0, 0, 0, 0
    end
    if not atom(p.reason) then
        p.reason = '-'
    end
    return context(p)
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
function w.sample(r, now)
    local list = r.samples
    local n = #list
    if n == 0 then
        return nil
    end
    local t = now - (r.offset or 0) - w.DELAY
    if t >= list[n].stime then
        return list[n]
    end
    if t <= list[1].stime then
        return list[1]
    end
    for i = n - 1, 1, -1 do
        local a, b = list[i], list[i + 1]
        if a.stime <= t then
            local k = (b.stime > a.stime) and (t - a.stime) / (b.stime - a.stime) or 1
            local p = {}
            for key, v in pairs(b) do
                p[key] = v
            end
            p.x = a.x + (b.x - a.x) * k
            p.y = a.y + (b.y - a.y) * k
            p.z = a.z + (b.z - a.z) * k
            p.yaw = a.yaw + yawDelta(a.yaw, b.yaw) * k
            if p.yaw > 180 then
                p.yaw = p.yaw - 360
            elseif p.yaw < -180 then
                p.yaw = p.yaw + 360
            end
            return p
        end
    end
    return list[n]
end
function w.receive(f, now)
    if f[1] == 'WALK' then
        drop('legacy_walk', now)
        return true
    end
    if f[1] ~= 'WALK2' then
        return false
    end
    local p, reason = w.parseRemote(f)
    if not p then
        drop(reason, now, 'slot=' .. tostring(f[5]))
        return true
    end
    local r = w.remotes[p.slot]
    if r and r.lease ~= p.lease then
        log('WALK_REMOTE slot=p' .. p.slot .. ' new connection; previous pose history discarded')
        r = nil
    end
    if not r then
        r = { slot = p.slot, seq = -1, samples = {}, visible = false, transfers = p.transfers }
        w.remotes[p.slot] = r
    end
    r.lease = p.lease
    if p.seq <= r.seq then
        drop('order', now, 'slot=p' .. p.slot)
        return true
    end
    r.seq, r.at = p.seq, now
    -- Sender clock -> local clock: the smallest observed delay, creeping up slowly for drift.
    local o = now - p.stime
    if not r.offset or o < r.offset then
        r.offset = o
    else
        r.offset = r.offset + (o - r.offset) * 0.01
    end
    local last = r.samples[#r.samples]
    if
        last
        and (
            w.contextKey(last) ~= w.contextKey(p)
            or last.transfers ~= p.transfers
            or last.room ~= p.room
            or p.stime < last.stime
        )
    then
        r.samples = {}
    end
    r.samples[#r.samples + 1] = p
    while #r.samples > w.MAX_SAMPLES do
        table.remove(r.samples, 1)
    end
    return true
end
local function hide(r, reason, now)
    if r.visible or reason == 'forget' then
        AddUITriggeredEvent('X4LAN_Walk', 'Hide', { slot = r.slot, reason = reason })
        w.diag(
            'hide:' .. r.slot .. ':' .. reason,
            'WALK_HIDE slot=p' .. r.slot .. ' reason=' .. reason,
            now
        )
    end
    r.visible = false
    r.applied = nil
end
local function apply(r, now)
    local p = w.sample(r, now)
    if not p then
        return
    end
    if p.state == 0 then
        hide(r, 'not_on_foot', now)
        return
    end
    local a = r.applied
    local place = a and w.contextKey(a) == w.contextKey(p)
    local same = place and a.room == p.room
    local mode = 'move'
    -- A transporter ride usually changes the room; it stays inside one context.
    if place and p.transfers ~= a.transfers then
        mode = 'transport'
    end
    local moved = not a
        or not same
        or mode == 'transport'
        or distance(a, p) > 0.05
        or math.abs(yawDelta(a.yaw, p.yaw)) > 5
    if not moved and now - (r.appliedAt or 0) < w.REFRESH then
        return
    end
    local speed = same and distance(a, p) / w.APPLY or 0
    speed = math.max(0.3, math.min(7, speed))
    AddUITriggeredEvent(
        'X4LAN_Walk',
        'Move',
        {
            slot = r.slot,
            kind = p.kind,
            ship = p.ship,
            sector = p.sector,
            code = p.code,
            macro = p.macro,
            room = p.room,
            x = p.x,
            y = p.y,
            z = p.z,
            yaw = p.yaw,
            speed = speed,
            mode = mode,
        }
    )
    if not r.visible then
        w.diag(
            'show:' .. r.slot,
            string.format(
                'WALK_SHOW slot=p%d kind=%s ship=%d code=%s room=%s',
                r.slot,
                p.kind,
                p.ship,
                p.code,
                p.room
            ),
            now
        )
    end
    if mode == 'transport' then
        w.diag(
            'transport:' .. r.slot,
            'WALK_TRANSPORT slot=p' .. r.slot .. ' transfers=' .. p.transfers .. ' room=' .. p.room,
            now
        )
    end
    r.applied = p
    r.appliedAt = now
    r.visible = true
end
function w.frame(now)
    if not w.setup and s.ready then
        w.setup = true
        w.setupAt = now
        AddUITriggeredEvent('X4LAN_Walk', 'Setup', { players = R.players, slot = R.slot })
    end
    if w.setup and not w.ready and not w.failed and now - w.setupAt > 10 then
        w.failed = true
        log('WALK_STOPPED: MD avatar session did not confirm setup; avatars stay hidden')
    end
    -- Local pose. MD refreshes it every 0.1 s; a stale local pose is never re-published.
    local p = w.localPose
    if p and s.client and s.client.lease and not A.paused() and now - p.at < 1.5 then
        local interval = (p.state == 1 and (p.speed or 0) > 0.3) and w.MOVING or w.IDLE
        if p.changed or now >= (w.lastSend or -math.huge) + interval then
            w.sequence = w.sequence + 1
            w.lastSend = now
            p.changed = false
            w.sent = w.sent + 1
            s.transport:send(
                string.format(
                    'WALK2|1|%s|%d|%d|%s|%d|%s|%s|%s|%s|%.3f|%.3f|%.3f|%.2f|%.2f|%.3f|%d',
                    s.client.lease,
                    w.sequence,
                    p.state,
                    p.kind,
                    p.ship,
                    p.sector,
                    p.code,
                    p.macro,
                    p.room,
                    p.x,
                    p.y,
                    p.z,
                    p.yaw,
                    math.min(100, p.speed or 0),
                    math.max(0, math.min(1e9, now)),
                    p.transfers
                )
            )
            w.diag(
                'send',
                'WALK_SEND sent='
                    .. w.sent
                    .. ' state='
                    .. p.state
                    .. ' kind='
                    .. p.kind
                    .. ' reason='
                    .. p.reason,
                now
            )
        end
    end
    for slot, r in pairs(w.remotes) do
        if now - (r.at or now) > w.FORGET then
            hide(r, 'forget', now)
            w.remotes[slot] = nil
        elseif now - (r.at or now) > w.STALE then
            hide(r, 'stale', now)
        elseif w.ready and not w.failed and now >= (r.nextApply or 0) then
            r.nextApply = now + w.APPLY
            apply(r, now)
        end
    end
end
RegisterEvent('X4LAN_Walk.Ready', function()
    if not w.ready then
        w.ready = true
        log('WALK_READY players=' .. R.players .. ' slot=p' .. R.slot)
    end
end)
RegisterEvent('X4LAN_Walk.Pose2', function(_, payload)
    local now = GetCurRealTime()
    local p, reason = w.parseLocal(payload)
    if not p then
        w.diag('local:' .. reason, 'WALK_LOCAL_INVALID reason=' .. reason)
        return
    end
    local old = w.localPose
    p.at = now
    p.speed = 0
    if
        old
        and old.state == 1
        and p.state == 1
        and w.contextKey(old) == w.contextKey(p)
        and now > old.at
    then
        p.speed = (old.speed or 0) * 0.5 + distance(old, p) / (now - old.at) * 0.5
    end
    p.changed = not old
        or old.state ~= p.state
        or w.contextKey(old) ~= w.contextKey(p)
        or old.room ~= p.room
        or old.transfers ~= p.transfers
    if old and old.changed then
        p.changed = true
    end -- an unsent change stays pending
    w.localPose = p
end)
