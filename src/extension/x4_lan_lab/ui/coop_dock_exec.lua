-- Optional exact-bay execution. Prepared operations are local capabilities;
-- native handles are obtained locally and are never accepted from the network.
local API, cfg = X4LAN_BUBBLE_API, X4LAN_CONFIG
if not API or not API.coopDocks or not cfg.coop_docks then
    return
end
local dockAdapter, coopAdapter, sessionState = API.coopDocks, API.coop, API.state
local execution =
    { generation = { 0, 0 }, ack = { 0, 0 }, keys = {}, operation = 0, epoch = API.nonce() }
dockAdapter.execution = execution
local function log(inputValue)
    DebugError('[X4LAN] COOP_DOCK_EXEC ' .. inputValue)
end

local function event(control, value)
    AddUITriggeredEvent('X4LAN_CoopAttachment', control, value)
end

local function key(entity, now)
    -- Separate datagrams can temporarily be unpaired. This blocks pose/new
    -- preparation, but must not repeatedly cancel an already invoked undock.
    local intent = dockAdapter.intent(entity, now)
    if not intent then
        return table.concat({
            'blocked',
            tostring(coopAdapter.owners[entity]),
            tostring(coopAdapter.epochs[entity]),
            tostring(coopAdapter.pilots[entity]),
        }, '|'),
            nil
    end
    return table.concat(
        { coopAdapter.owners[entity], intent.epoch, intent.lease, intent.mode, intent.dock },
        '|'
    ),
        intent
end

local function fence(entity, authorityKey, intent)
    if execution.keys[entity] == authorityKey then
        return
    end
    execution.keys[entity] = authorityKey
    execution.generation[entity] = execution.generation[entity] + 1
    if execution.pending then
        execution.pending = nil
    end
    event('Fence', {
        seed = cfg.seed_id,
        epoch = execution.epoch,
        ship = entity,
        generation = execution.generation[entity],
        enabled = intent ~= nil,
    })
end

function dockAdapter.authorized(entity, now)
    local authorityKey, intent = key(entity, now)
    return execution.ready
        and intent ~= nil
        and execution.keys[entity] == authorityKey
        and execution.ack[entity] == execution.generation[entity]
end

function coopAdapter.attachmentGeneration(entity)
    return execution.generation[entity]
end

function coopAdapter.attachmentWindow(entity, now)
    local localRowState = dockAdapter.localRow(entity, now)
    if localRowState then
        return localRowState.stamp, localRowState.age
    end
end

local function current(pendingOperation, now)
    return pendingOperation
        and dockAdapter.authorized(pendingOperation.entity, now)
        and pendingOperation.generation == execution.generation[pendingOperation.entity]
end

local function outcome(reason)
    if execution.lastOutcome ~= reason then
        execution.lastOutcome = reason
        log(reason)
    end
    execution.pending = nil
    execution.nextAttempt = GetCurRealTime() + 0.5
end

local function choose(intent, localRowState)
    if localRowState.mode == 'UNKNOWN' then
        return nil
    end
    if intent.mode == localRowState.mode and intent.dock == localRowState.dock then
        return nil
    end
    if localRowState.mode == 'EXTERNAL' or localRowState.mode == 'STORED' then
        return 'UNDOCK', 'FREE', 0
    end
    if
        localRowState.mode == 'APPROACH'
        and (intent.mode == 'FREE' or intent.dock ~= localRowState.dock)
    then
        return 'CANCEL', 'FREE', 0
    end
    if intent.mode == 'APPROACH' and localRowState.mode == 'FREE' then
        return 'RESERVE', 'APPROACH', intent.dock
    end
    if
        (intent.mode == 'EXTERNAL' or intent.mode == 'STORED')
        and (
            localRowState.mode == 'FREE'
            or localRowState.mode == 'APPROACH' and localRowState.dock == intent.dock
        )
    then
        return 'ATTACH', intent.mode, intent.dock
    end
end

function dockAdapter.executeFrame(now)
    if dockAdapter.closed then
        if execution.started and not execution.stopped then
            execution.stopped = true
            event('Stop')
        end
        return
    end
    if
        dockAdapter.ready
        and not execution.started
        and sessionState.client
        and sessionState.client.lease
    then
        execution.started = true
        execution.lease = sessionState.client.lease
        execution.startAt = now
        event('Start', { seed = cfg.seed_id, epoch = execution.epoch, role = cfg.slot })
    end
    if not execution.started then
        return
    end
    if not sessionState.client or sessionState.client.lease ~= execution.lease then
        dockAdapter.stop('attachment session lease changed; reload required')
        return
    end
    if not execution.ready then
        if now - execution.startAt > 5 then
            dockAdapter.stop('native attachment setup timed out')
        end
        return
    end
    for entity = 1, 2 do
        local authorityKey, intent = key(entity, now)
        fence(entity, authorityKey, intent)
    end
    local pendingOperation = execution.pending
    if pendingOperation then
        if not current(pendingOperation, now) then
            execution.pending = nil
            return
        end
        if now - pendingOperation.at > (pendingOperation.applied and 30 or 2) then
            -- An invoked operation may still be moving natively. Keep pose fenced by
            -- native attachment, report it, and inspect before any further operation.
            outcome(
                'WAIT_EXPIRED entity='
                    .. pendingOperation.entity
                    .. ' action='
                    .. pendingOperation.action
            )
            return
        end
        if pendingOperation.applied then
            local localRowState = dockAdapter.localRow(pendingOperation.entity, now)
            if
                localRowState
                and localRowState.at >= pendingOperation.applied
                and localRowState.mode == pendingOperation.want
                and localRowState.dock == pendingOperation.dock
            then
                outcome(
                    'CONFIRMED entity='
                        .. pendingOperation.entity
                        .. ' action='
                        .. pendingOperation.action
                        .. ' mode='
                        .. localRowState.mode
                )
            elseif
                localRowState
                and localRowState.at >= pendingOperation.applied
                and pendingOperation.action == 'UNDOCK'
                and localRowState.mode == 'APPROACH'
            then
                -- X Shuttle can physically detach yet retain its assignment. localRow also
                -- checks native isdocked=false: only now may a separate guarded CANCEL
                -- remove that reservation. Pose remains blocked until actual FREE readback.
                outcome('DETACHED_WITH_RESERVATION entity=' .. pendingOperation.entity)
            end
        end
        return
    end
    if now < (execution.nextAttempt or 0) then
        return
    end
    for entity = 1, 2 do
        local intent, localRowState =
            dockAdapter.goal(entity, now), dockAdapter.localRow(entity, now)
        if intent and localRowState and dockAdapter.authorized(entity, now) then
            local action, want, dock = choose(intent, localRowState)
            if action then
                execution.operation = execution.operation + 1
                execution.pending = {
                    entity = entity,
                    generation = execution.generation[entity],
                    operation = execution.operation,
                    action = action,
                    want = want,
                    dock = dock,
                    at = now,
                }
                event('Prepare', {
                    seed = cfg.seed_id,
                    epoch = execution.epoch,
                    operation = execution.operation,
                    ship = entity,
                    native = API.id(coopAdapter.ships[entity]),
                    generation = execution.generation[entity],
                    action = action,
                    state = want,
                    dock = dock,
                    fromState = localRowState.mode,
                    fromDock = localRowState.dock,
                    stamp = localRowState.stamp,
                    age = localRowState.age,
                })
                return
            end
        end
    end
end
RegisterEvent('X4LAN_CoopAttachment.Ready', function(_, inputValue)
    if inputValue == cfg.seed_id .. '|' .. execution.epoch and not dockAdapter.closed then
        execution.ready = true
        log('READY')
    end
end)
RegisterEvent('X4LAN_CoopAttachment.Fenced', function(_, inputValue)
    local seed, epoch, entity, generation =
        tostring(inputValue):match('^([^|]+)|([^|]+)|(%d)|(%d+)$')
    entity, generation = tonumber(entity), tonumber(generation)
    if
        seed == cfg.seed_id
        and epoch == execution.epoch
        and entity
        and entity >= 1
        and entity <= 2
        and generation == execution.generation[entity]
    then
        execution.ack[entity] = generation
    end
end)
RegisterEvent('X4LAN_CoopAttachment.Error', function(_, inputValue)
    dockAdapter.stop(inputValue)
end)
RegisterEvent('X4LAN_CoopAttachment.Window', function(_, inputValue)
    local seed, epoch, op, accepted = tostring(inputValue):match('^([^|]+)|([^|]+)|(%d+)|([01])$')
    local pendingOperation = execution.pending
    local now = GetCurRealTime()
    if
        seed ~= cfg.seed_id
        or epoch ~= execution.epoch
        or not pendingOperation
        or tonumber(op) ~= pendingOperation.operation
        or not current(pendingOperation, now)
        or now - pendingOperation.at > 2
    then
        return
    end
    if accepted ~= '1' then
        outcome(
            'REFUSED entity=' .. pendingOperation.entity .. ' action=' .. pendingOperation.action
        )
        return
    end
    event(
        'Apply',
        { seed = cfg.seed_id, epoch = execution.epoch, operation = pendingOperation.operation }
    )
end)
RegisterEvent('X4LAN_CoopAttachment.Applied', function(_, inputValue)
    local seed, epoch, op, result = tostring(inputValue):match('^([^|]+)|([^|]+)|(%d+)|(%a+)$')
    local pendingOperation = execution.pending
    local now = GetCurRealTime()
    if
        seed ~= cfg.seed_id
        or epoch ~= execution.epoch
        or not pendingOperation
        or tonumber(op) ~= pendingOperation.operation
        or not current(pendingOperation, now)
    then
        return
    end
    if result == 'applied' then
        pendingOperation.applied = now
        pendingOperation.at = now
    else
        outcome(
            'NOT_APPLIED entity='
                .. pendingOperation.entity
                .. ' action='
                .. pendingOperation.action
                .. ' result='
                .. tostring(result)
        )
    end
end)
