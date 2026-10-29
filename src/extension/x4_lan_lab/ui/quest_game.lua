-- Shared mission display; the host MD script owns progression and the reward.
local cfg = X4LAN_CONFIG
local api = X4LAN_BUBBLE_API
if not cfg or not cfg.coop or not api or not api.state.active then
    return
end
local s = api.state
local q = { revision = -1 }
api.quest = q
local function integer(v, max)
    local n = tonumber(v)
    if not n or n ~= n or n < 0 or n % 1 ~= 0 or n > max then
        return nil
    end
    return n
end
function q.receive(f)
    if f[1] ~= 'QUEST' then
        return false
    end
    if #f ~= 9 or #f[4] ~= 32 or not f[4]:match('^[a-f0-9]+$') then
        return true
    end
    local v = {}
    local limits = { 2147483647, 3, 20, 2, 1 }
    for i = 1, 5 do
        v[i] = integer(f[i + 4], limits[i])
        if not v[i] then
            return true
        end
    end
    local rev, stage, delivered, destroyed, paid = unpack(v)
    if
        stage < 1
        or (stage == 1 and (delivered == 20 or paid ~= 0))
        or (stage == 2 and (delivered ~= 20 or destroyed == 2 or paid ~= 0))
        or (stage == 3 and (delivered ~= 20 or destroyed ~= 2 or paid ~= 1))
    then
        return true
    end
    if q.owner and q.owner ~= f[4] then
        return true
    end -- A new game requires a fresh shared session.
    if rev <= q.revision then
        return true
    end
    if q.progress then
        for i = 2, 5 do
            if v[i] < q.progress[i] then
                return true
            end
        end
    end
    q.owner, q.revision, q.progress = f[4], rev, v
    if cfg.slot ~= 'p1' then
        q.pending = {
            revision = rev,
            stage = stage,
            delivered = delivered,
            destroyed = destroyed,
            paid = paid,
        }
    end
    return true
end
function q.frame(now)
    if not api.trade or not api.trade.state.ready then
        return
    end
    if not q.setup then
        q.setup = true
        AddUITriggeredEvent('X4LAN_Quest', 'Setup')
    end
    if not q.ready then
        return
    end
    if q.pending and not api.paused() then
        AddUITriggeredEvent('X4LAN_Quest', 'Apply', q.pending)
        q.pending = nil
    end
    if
        cfg.slot == 'p1'
        and q.payload
        and s.client
        and s.client.lease
        and now >= (q.nextSend or 0)
    then
        q.nextSend = now + 0.5
        s.transport:send('QUEST|1|' .. s.client.lease .. '|' .. q.payload)
    end
end
RegisterEvent('X4LAN_Quest.Ready', function()
    q.ready = true
end)
RegisterEvent('X4LAN_Quest.Snapshot', function(_, payload)
    if cfg.slot == 'p1' then
        q.payload = payload
    end
end)

RegisterEvent('X4LAN_Quest.Delivery', function(_, payload)
    if cfg.slot ~= 'p1' or not q.ready then
        return
    end
    local request, amount = payload:match('^([a-f0-9]+)|(%d+)$')
    amount = integer(amount, 1000000)
    if request and #request == 32 and amount and amount > 0 then
        AddUITriggeredEvent('X4LAN_Quest', 'Delivery', { request = request, amount = amount })
    end
end)
