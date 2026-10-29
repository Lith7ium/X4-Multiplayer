-- Optional exact-bay execution. Prepared operations are local capabilities;
-- native handles are obtained locally and are never accepted from the network.
local A, cfg = X4LAN_BUBBLE_API, X4LAN_CONFIG
if not A or not A.coopDocks or not cfg.coop_docks then
    return
end
local d, c, s = A.coopDocks, A.coop, A.state
local x = { generation = { 0, 0 }, ack = { 0, 0 }, keys = {}, operation = 0, epoch = A.nonce() }
d.execution = x
local function log(v)
    DebugError('[X4LAN] COOP_DOCK_EXEC ' .. v)
end
local function event(control, value)
    AddUITriggeredEvent('X4LAN_CoopAttachment', control, value)
end
local function key(e, now)
    -- Separate datagrams can temporarily be unpaired. This blocks pose/new
    -- preparation, but must not repeatedly cancel an already invoked undock.
    local q = d.intent(e, now)
    if not q then
        return table.concat(
            { 'blocked', tostring(c.owners[e]), tostring(c.epochs[e]), tostring(c.pilots[e]) },
            '|'
        ),
            nil
    end
    return table.concat({ c.owners[e], q.epoch, q.lease, q.mode, q.dock }, '|'), q
end
local function fence(e, k, q)
    if x.keys[e] == k then
        return
    end
    x.keys[e] = k
    x.generation[e] = x.generation[e] + 1
    if x.pending then
        x.pending = nil
    end
    event(
        'Fence',
        { seed = cfg.seed_id, epoch = x.epoch, ship = e, generation = x.generation[e], enabled = q
            ~= nil }
    )
end
function d.authorized(e, now)
    local k, q = key(e, now)
    return x.ready and q ~= nil and x.keys[e] == k and x.ack[e] == x.generation[e]
end
function c.attachmentGeneration(e)
    return x.generation[e]
end
function c.attachmentWindow(e, now)
    local r = d.localRow(e, now)
    if r then
        return r.stamp, r.age
    end
end
local function current(p, now)
    return p and d.authorized(p.entity, now) and p.generation == x.generation[p.entity]
end
local function outcome(reason)
    if x.lastOutcome ~= reason then
        x.lastOutcome = reason
        log(reason)
    end
    x.pending = nil
    x.nextAttempt = GetCurRealTime() + 0.5
end
local function choose(q, r)
    if r.mode == 'UNKNOWN' then
        return nil
    end
    if q.mode == r.mode and q.dock == r.dock then
        return nil
    end
    if r.mode == 'EXTERNAL' or r.mode == 'STORED' then
        return 'UNDOCK', 'FREE', 0
    end
    if r.mode == 'APPROACH' and (q.mode == 'FREE' or q.dock ~= r.dock) then
        return 'CANCEL', 'FREE', 0
    end
    if q.mode == 'APPROACH' and r.mode == 'FREE' then
        return 'RESERVE', 'APPROACH', q.dock
    end
    if
        (q.mode == 'EXTERNAL' or q.mode == 'STORED')
        and (r.mode == 'FREE' or r.mode == 'APPROACH' and r.dock == q.dock)
    then
        return 'ATTACH', q.mode, q.dock
    end
end
function d.executeFrame(now)
    if d.closed then
        if x.started and not x.stopped then
            x.stopped = true
            event('Stop')
        end
        return
    end
    if d.ready and not x.started and s.client and s.client.lease then
        x.started = true
        x.lease = s.client.lease
        x.startAt = now
        event('Start', { seed = cfg.seed_id, epoch = x.epoch, role = cfg.slot })
    end
    if not x.started then
        return
    end
    if not s.client or s.client.lease ~= x.lease then
        d.stop('attachment session lease changed; reload required')
        return
    end
    if not x.ready then
        if now - x.startAt > 5 then
            d.stop('native attachment setup timed out')
        end
        return
    end
    for e = 1, 2 do
        local k, q = key(e, now)
        fence(e, k, q)
    end
    local p = x.pending
    if p then
        if not current(p, now) then
            x.pending = nil
            return
        end
        if now - p.at > (p.applied and 30 or 2) then
            -- An invoked operation may still be moving natively. Keep pose fenced by
            -- native attachment, report it, and inspect before any further operation.
            outcome('WAIT_EXPIRED entity=' .. p.entity .. ' action=' .. p.action)
            return
        end
        if p.applied then
            local r = d.localRow(p.entity, now)
            if r and r.at >= p.applied and r.mode == p.want and r.dock == p.dock then
                outcome(
                    'CONFIRMED entity=' .. p.entity .. ' action=' .. p.action .. ' mode=' .. r.mode
                )
            elseif r and r.at >= p.applied and p.action == 'UNDOCK' and r.mode == 'APPROACH' then
                -- X Shuttle can physically detach yet retain its assignment. localRow also
                -- checks native isdocked=false: only now may a separate guarded CANCEL
                -- remove that reservation. Pose remains blocked until actual FREE readback.
                outcome('DETACHED_WITH_RESERVATION entity=' .. p.entity)
            end
        end
        return
    end
    if now < (x.nextAttempt or 0) then
        return
    end
    for e = 1, 2 do
        local q, r = d.goal(e, now), d.localRow(e, now)
        if q and r and d.authorized(e, now) then
            local action, want, dock = choose(q, r)
            if action then
                x.operation = x.operation + 1
                x.pending = {
                    entity = e,
                    generation = x.generation[e],
                    operation = x.operation,
                    action = action,
                    want = want,
                    dock = dock,
                    at = now,
                }
                event(
                    'Prepare',
                    {
                        seed = cfg.seed_id,
                        epoch = x.epoch,
                        operation = x.operation,
                        ship = e,
                        native = A.id(c.ships[e]),
                        generation = x.generation[e],
                        action = action,
                        state = want,
                        dock = dock,
                        fromState = r.mode,
                        fromDock = r.dock,
                        stamp = r.stamp,
                        age = r.age,
                    }
                )
                return
            end
        end
    end
end
RegisterEvent('X4LAN_CoopAttachment.Ready', function(_, v)
    if v == cfg.seed_id .. '|' .. x.epoch and not d.closed then
        x.ready = true
        log('READY')
    end
end)
RegisterEvent('X4LAN_CoopAttachment.Fenced', function(_, v)
    local seed, epoch, e, g = tostring(v):match('^([^|]+)|([^|]+)|(%d)|(%d+)$')
    e, g = tonumber(e), tonumber(g)
    if
        seed == cfg.seed_id
        and epoch == x.epoch
        and e
        and e >= 1
        and e <= 2
        and g == x.generation[e]
    then
        x.ack[e] = g
    end
end)
RegisterEvent('X4LAN_CoopAttachment.Error', function(_, v)
    d.stop(v)
end)
RegisterEvent('X4LAN_CoopAttachment.Window', function(_, v)
    local seed, epoch, op, accepted = tostring(v):match('^([^|]+)|([^|]+)|(%d+)|([01])$')
    local p = x.pending
    local now = GetCurRealTime()
    if
        seed ~= cfg.seed_id
        or epoch ~= x.epoch
        or not p
        or tonumber(op) ~= p.operation
        or not current(p, now)
        or now - p.at > 2
    then
        return
    end
    if accepted ~= '1' then
        outcome('REFUSED entity=' .. p.entity .. ' action=' .. p.action)
        return
    end
    event('Apply', { seed = cfg.seed_id, epoch = x.epoch, operation = p.operation })
end)
RegisterEvent('X4LAN_CoopAttachment.Applied', function(_, v)
    local seed, epoch, op, result = tostring(v):match('^([^|]+)|([^|]+)|(%d+)|(%a+)$')
    local p = x.pending
    local now = GetCurRealTime()
    if
        seed ~= cfg.seed_id
        or epoch ~= x.epoch
        or not p
        or tonumber(op) ~= p.operation
        or not current(p, now)
    then
        return
    end
    if result == 'applied' then
        p.applied = now
        p.at = now
    else
        outcome(
            'NOT_APPLIED entity='
                .. p.entity
                .. ' action='
                .. p.action
                .. ' result='
                .. tostring(result)
        )
    end
end)
