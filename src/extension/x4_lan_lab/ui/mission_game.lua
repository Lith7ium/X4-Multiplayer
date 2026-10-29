-- Native host missions, mirrored in the guest's ordinary mission interface.
local cfg, A = X4LAN_CONFIG, X4LAN_BUBBLE_API
if not cfg or not cfg.faction_missions or not A or not A.coop or not A.health then
    return
end
local P, M = X4LAN_UNIVERSE_PROTOCOL, X4LAN_MISSION_PROTOCOL
local s, h = A.state, A.health
local q =
    { assembly = M.assembly(), records = {}, building = {}, requests = {}, pending = {}, changed = {}, cursor = 0 }
A.missions = q
local function log(v)
    DebugError('[X4LAN] MISSIONS ' .. v)
end
local function fresh(now)
    return not A.paused() and h.mdStamp and now - (h.mdAt or 0) <= 0.5
end
local function call(action, r)
    r.stamp, r.from, r.deadline = h.mdStamp, h.mdAge, h.mdAge + 1
    r.epoch = q.epoch
    AddUITriggeredEvent('X4LAN_Missions', action, r)
end
function q.receive(f, now)
    if f[1] ~= 'MSTATE' and f[1] ~= 'MREQ' then
        return false
    end
    if cfg.slot ~= 'p1' and f[1] == 'MSTATE' then
        if q.epoch and q.epoch ~= f[4] then
            return true
        end
        local r = q.assembly:add(f, now)
        if r then
            q.epoch = r.epoch
            q.records[r.id] = r
            q.pending[r.id] = r
            q.requests[r.id] = nil
        end
    elseif cfg.slot == 'p1' and f[1] == 'MREQ' and #f == 7 and f[4] == s.client.lease then
        local id, rev = P.integer(f[5], M.limit), P.integer(f[6])
        local r = id and q.records[id]
        if
            r
            and r.rev == rev
            and (
                (r.row.state == 'O' and f[7] == 'ACCEPT') or (
                    r.row.state == 'A' and f[7] == 'ABORT'
                )
            )
        then
            q.requests[id] = { id = id, revision = rev, action = f[7] }
        end
    end
    return true
end
function q.frame(now)
    if q.failed or h.failed or not s.client or not s.client.lease or not fresh(now) then
        return
    end
    if cfg.slot == 'p1' then
        q.epoch = s.client.lease
    end
    if not q.epoch then
        return
    end
    if not q.setup then
        q.setup = true
        call('Setup', {})
        return
    end
    if not q.ready then
        return
    end
    if now >= (q.nextPoll or 0) then
        q.nextPoll = now + 0.25
        for id, r in pairs(q.pending) do
            local v = {}
            for k, value in pairs(r.row) do
                v[k] = value
            end
            v.id, v.revision = r.id, r.rev
            call('Mirror', v)
            -- MD confirms execution. A paused/expired queued request is retried.
        end
        for id, r in pairs(q.requests) do
            if cfg.slot == 'p1' then
                call('Command', { id = id, revision = r.revision, action = r.action })
            else
                s.transport:send(
                    table.concat(
                        { 'MREQ', 1, s.client.lease, q.epoch, id, r.revision, r.action },
                        '|'
                    )
                )
            end
        end
    end
    if cfg.slot == 'p1' and now >= (q.nextSend or 0) then
        q.nextSend = now + 0.25
        local ids = {}
        for id in pairs(q.records) do
            ids[#ids + 1] = id
        end
        table.sort(ids)
        if #ids > 0 then
            q.cursor = q.cursor % #ids + 1
            local id = table.remove(q.changed, 1) or ids[q.cursor]
            local r = q.records[id]
            for _, packet in ipairs(M.packets(s.client.lease, q.epoch, r)) do
                s.transport:send(packet)
            end
        end
    end
end
RegisterEvent('X4LAN_Missions.Ready', function(_, epoch)
    if epoch == q.epoch then
        q.ready = true
    end
end)
RegisterEvent('X4LAN_Missions.Header', function(_, text)
    if cfg.slot ~= 'p1' then
        return
    end
    local f = P.split(text, '|')
    local id, rev = P.integer(f[1], M.limit), P.integer(f[2])
    if #f ~= 7 or not id or id < 1 or not rev or rev < 1 then
        return
    end
    q.building[id] = { id = id, rev = rev, header = table.concat(
        { 'quota', f[3], f[4], f[5], f[6], f[7] },
        ','
    ) }
end)
for _, field in ipairs({ 'Title', 'Description', 'Objective' }) do
    RegisterEvent('X4LAN_Missions.' .. field, function(_, text)
        if cfg.slot ~= 'p1' then
            return
        end
        local id, value = text:match('^(%d+)|(.*)$')
        local r = q.building[tonumber(id)]
        if r then
            r[field] = value
        end
    end)
end
RegisterEvent('X4LAN_Missions.Commit', function(_, value)
    local id = tonumber(value)
    local r = q.building[id]
    if cfg.slot ~= 'p1' or not r or not r.Title or not r.Description or not r.Objective then
        return
    end
    q.building[id] = nil
    r.payload = r.header
        .. ','
        .. M.encode(r.Title)
        .. ','
        .. M.encode(r.Description)
        .. ','
        .. M.encode(r.Objective)
    r.row = M.parse(r.payload)
    local old = q.records[id]
    if not r.row or old and (r.rev <= old.rev or not M.advances(old.row, r.row)) then
        log('rejected native record id=' .. id)
        return
    end
    r.digest = P.checksum({ r.payload })
    q.records[id] = r
    q.requests[id] = nil
    local queued = false
    for _, v in ipairs(q.changed) do
        if v == id then
            queued = true
        end
    end
    if not queued then
        q.changed[#q.changed + 1] = id
    end
    log(
        'native id='
            .. id
            .. ' revision='
            .. r.rev
            .. ' state='
            .. r.row.state
            .. ' progress='
            .. r.row.done
            .. '/'
            .. r.row.total
    )
end)
RegisterEvent('X4LAN_Missions.Mirrored', function(_, text)
    local id, rev = text:match('^(%d+)|(%d+)$')
    id, rev = tonumber(id), tonumber(rev)
    if q.pending[id] and q.pending[id].rev == rev then
        q.pending[id] = nil
    end
end)
RegisterEvent('X4LAN_Missions.Request', function(_, text)
    if cfg.slot == 'p1' then
        return
    end
    local id, rev, action = text:match('^(%d+)|(%d+)|([A-Z]+)$')
    id, rev = tonumber(id), tonumber(rev)
    local r = q.records[id]
    if
        r
        and r.rev == rev
        and (
            (action == 'ACCEPT' and r.row.state == 'O') or (
                action == 'ABORT' and r.row.state == 'A'
            )
        )
    then
        q.requests[id] = { id = id, revision = rev, action = action }
    end
end)
