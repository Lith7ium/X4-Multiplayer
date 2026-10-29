-- Shared mission display; the host MD script owns progression and the reward.
local cfg = X4LAN_CONFIG
local api = X4LAN_BUBBLE_API
if not cfg or not cfg.coop or not api or not api.state.active then
    return
end
local state = api.state
local quest = { revision = -1 }
api.quest = quest
local function integer(value, max)
    local number = tonumber(value)
    if not number or number ~= number or number < 0 or number % 1 ~= 0 or number > max then
        return nil
    end
    return number
end
function quest.receive(fields)
    if fields[1] ~= 'QUEST' then
        return false
    end
    if #fields ~= 9 or #fields[4] ~= 32 or not fields[4]:match('^[a-f0-9]+$') then
        return true
    end
    local values = {}
    local limits = { 2147483647, 3, 20, 2, 1 }
    for i = 1, 5 do
        values[i] = integer(fields[i + 4], limits[i])
        if not values[i] then
            return true
        end
    end
    local revision, stage, delivered, destroyed, paid = unpack(values)
    if
        stage < 1
        or (stage == 1 and (delivered == 20 or paid ~= 0))
        or (stage == 2 and (delivered ~= 20 or destroyed == 2 or paid ~= 0))
        or (stage == 3 and (delivered ~= 20 or destroyed ~= 2 or paid ~= 1))
    then
        return true
    end
    if quest.owner and quest.owner ~= fields[4] then
        return true
    end -- A new game requires a fresh shared session.
    if revision <= quest.revision then
        return true
    end
    if quest.progress then
        for i = 2, 5 do
            if values[i] < quest.progress[i] then
                return true
            end
        end
    end
    quest.owner, quest.revision, quest.progress = fields[4], revision, values
    if cfg.slot ~= 'p1' then
        quest.pending = {
            revision = revision,
            stage = stage,
            delivered = delivered,
            destroyed = destroyed,
            paid = paid,
        }
    end
    return true
end
function quest.frame(now)
    if not api.trade or not api.trade.state.ready then
        return
    end
    if not quest.setup then
        quest.setup = true
        AddUITriggeredEvent('X4LAN_Quest', 'Setup')
    end
    if not quest.ready then
        return
    end
    if quest.pending and not api.paused() then
        AddUITriggeredEvent('X4LAN_Quest', 'Apply', quest.pending)
        quest.pending = nil
    end
    if
        cfg.slot == 'p1'
        and quest.payload
        and state.client
        and state.client.lease
        and now >= (quest.nextSend or 0)
    then
        quest.nextSend = now + 0.5
        state.transport:send('QUEST|1|' .. state.client.lease .. '|' .. quest.payload)
    end
end
RegisterEvent('X4LAN_Quest.Ready', function()
    quest.ready = true
end)
RegisterEvent('X4LAN_Quest.Snapshot', function(_, payload)
    if cfg.slot == 'p1' then
        quest.payload = payload
    end
end)

RegisterEvent('X4LAN_Quest.Delivery', function(_, payload)
    if cfg.slot ~= 'p1' or not quest.ready then
        return
    end
    local request, amount = payload:match('^([a-f0-9]+)|(%d+)$')
    amount = integer(amount, 1000000)
    if request and #request == 32 and amount and amount > 0 then
        AddUITriggeredEvent('X4LAN_Quest', 'Delivery', { request = request, amount = amount })
    end
end)
