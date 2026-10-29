-- Native host missions, mirrored in the guest's ordinary mission interface.
local cfg = X4LAN_CONFIG
local api = X4LAN_BUBBLE_API
if not cfg or not cfg.faction_missions or not api or not api.coop or not api.health then
    return
end
local universeProtocol = X4LAN_UNIVERSE_PROTOCOL
local missionProtocol = X4LAN_MISSION_PROTOCOL
local state = api.state
local health = api.health
local missions = {
    assembly = missionProtocol.assembly(),
    records = {},
    building = {},
    requests = {},
    pending = {},
    changed = {},
    cursor = 0,
}
api.missions = missions
local function log(text)
    DebugError('[X4LAN] MISSIONS ' .. text)
end
local function fresh(now)
    return not api.paused() and health.mdStamp and now - (health.mdAt or 0) <= 0.5
end
local function call(action, request)
    request.stamp, request.from, request.deadline = health.mdStamp, health.mdAge, health.mdAge + 1
    request.epoch = missions.epoch
    AddUITriggeredEvent('X4LAN_Missions', action, request)
end
function missions.receive(fields, now)
    if fields[1] ~= 'MSTATE' and fields[1] ~= 'MREQ' then
        return false
    end
    if cfg.slot ~= 'p1' and fields[1] == 'MSTATE' then
        if missions.epoch and missions.epoch ~= fields[4] then
            return true
        end
        local record = missions.assembly:add(fields, now)
        if record then
            missions.epoch = record.epoch
            missions.records[record.id] = record
            missions.pending[record.id] = record
            missions.requests[record.id] = nil
        end
    elseif
        cfg.slot == 'p1'
        and fields[1] == 'MREQ'
        and #fields == 7
        and fields[4] == state.client.lease
    then
        local id, revision =
            universeProtocol.integer(fields[5], missionProtocol.limit),
            universeProtocol.integer(fields[6])
        local record = id and missions.records[id]
        if
            record
            and record.rev == revision
            and (
                (record.row.state == 'O' and fields[7] == 'ACCEPT')
                or (record.row.state == 'A' and fields[7] == 'ABORT')
            )
        then
            missions.requests[id] = { id = id, revision = revision, action = fields[7] }
        end
    end
    return true
end
function missions.frame(now)
    if
        missions.failed
        or health.failed
        or not state.client
        or not state.client.lease
        or not fresh(now)
    then
        return
    end
    if cfg.slot == 'p1' then
        missions.epoch = state.client.lease
    end
    if not missions.epoch then
        return
    end
    if not missions.setup then
        missions.setup = true
        call('Setup', {})
        return
    end
    if not missions.ready then
        return
    end
    if now >= (missions.nextPoll or 0) then
        missions.nextPoll = now + 0.25
        for id, record in pairs(missions.pending) do
            local mirror = {}
            for field, value in pairs(record.row) do
                mirror[field] = value
            end
            mirror.id, mirror.revision = record.id, record.rev
            call('Mirror', mirror)
            -- MD confirms execution. A paused/expired queued request is retried.
        end
        for id, record in pairs(missions.requests) do
            if cfg.slot == 'p1' then
                call('Command', { id = id, revision = record.revision, action = record.action })
            else
                state.transport:send(table.concat({
                    'MREQ',
                    1,
                    state.client.lease,
                    missions.epoch,
                    id,
                    record.revision,
                    record.action,
                }, '|'))
            end
        end
    end
    if cfg.slot == 'p1' and now >= (missions.nextSend or 0) then
        missions.nextSend = now + 0.25
        local ids = {}
        for id in pairs(missions.records) do
            ids[#ids + 1] = id
        end
        table.sort(ids)
        if #ids > 0 then
            missions.cursor = missions.cursor % #ids + 1
            local id = table.remove(missions.changed, 1) or ids[missions.cursor]
            local record = missions.records[id]
            for _, packet in
                ipairs(missionProtocol.packets(state.client.lease, missions.epoch, record))
            do
                state.transport:send(packet)
            end
        end
    end
end
RegisterEvent('X4LAN_Missions.Ready', function(_, epoch)
    if epoch == missions.epoch then
        missions.ready = true
    end
end)
RegisterEvent('X4LAN_Missions.Header', function(_, text)
    if cfg.slot ~= 'p1' then
        return
    end
    local fields = universeProtocol.split(text, '|')
    local id, revision =
        universeProtocol.integer(fields[1], missionProtocol.limit),
        universeProtocol.integer(fields[2])
    if #fields ~= 7 or not id or id < 1 or not revision or revision < 1 then
        return
    end
    missions.building[id] = {
        id = id,
        rev = revision,
        header = table.concat(
            { 'quota', fields[3], fields[4], fields[5], fields[6], fields[7] },
            ','
        ),
    }
end)
for _, field in ipairs({ 'Title', 'Description', 'Objective' }) do
    RegisterEvent('X4LAN_Missions.' .. field, function(_, text)
        if cfg.slot ~= 'p1' then
            return
        end
        local id, value = text:match('^(%d+)|(.*)$')
        local record = missions.building[tonumber(id)]
        if record then
            record[field] = value
        end
    end)
end
RegisterEvent('X4LAN_Missions.Commit', function(_, value)
    local id = tonumber(value)
    local record = missions.building[id]
    if
        cfg.slot ~= 'p1'
        or not record
        or not record.Title
        or not record.Description
        or not record.Objective
    then
        return
    end
    missions.building[id] = nil
    record.payload = record.header
        .. ','
        .. missionProtocol.encode(record.Title)
        .. ','
        .. missionProtocol.encode(record.Description)
        .. ','
        .. missionProtocol.encode(record.Objective)
    record.row = missionProtocol.parse(record.payload)
    local old = missions.records[id]
    if
        not record.row
        or old and (record.rev <= old.rev or not missionProtocol.advances(old.row, record.row))
    then
        log('rejected native record id=' .. id)
        return
    end
    record.digest = universeProtocol.checksum({ record.payload })
    missions.records[id] = record
    missions.requests[id] = nil
    local queued = false
    for _, mirror in ipairs(missions.changed) do
        if mirror == id then
            queued = true
        end
    end
    if not queued then
        missions.changed[#missions.changed + 1] = id
    end
    log(
        'native id='
            .. id
            .. ' revision='
            .. record.rev
            .. ' state='
            .. record.row.state
            .. ' progress='
            .. record.row.done
            .. '/'
            .. record.row.total
    )
end)
RegisterEvent('X4LAN_Missions.Mirrored', function(_, text)
    local id, revision = text:match('^(%d+)|(%d+)$')
    id, revision = tonumber(id), tonumber(revision)
    if missions.pending[id] and missions.pending[id].rev == revision then
        missions.pending[id] = nil
    end
end)
RegisterEvent('X4LAN_Missions.Request', function(_, text)
    if cfg.slot == 'p1' then
        return
    end
    local id, revision, action = text:match('^(%d+)|(%d+)|([A-Z]+)$')
    id, revision = tonumber(id), tonumber(revision)
    local record = missions.records[id]
    if
        record
        and record.rev == revision
        and (
            (action == 'ACCEPT' and record.row.state == 'O')
            or (action == 'ABORT' and record.row.state == 'A')
        )
    then
        missions.requests[id] = { id = id, revision = revision, action = action }
    end
end)
