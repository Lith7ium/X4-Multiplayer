-- The existing X4 trade sliders build the basket; only the host executes it.
local cfg = X4LAN_CONFIG
if not cfg or not cfg.enabled or not cfg.trading or not X4LAN_BUBBLE_API then
    return
end
local api = X4LAN_BUBBLE_API
local state = api.state
if not state.active then
    return
end
local protocol = X4LAN_TRADE_PROTOCOL
local trade = {
    markets = {},
    executed = {},
    assembler = protocol.assembler(),
    dock = 'none',
    dockseq = 0,
    revision = 0,
}
local function log(text)
    DebugError('[X4LAN] ' .. text)
end

local function notice(text)
    AddUITriggeredEvent('X4LAN_Trade', 'Notice', text)
    log('TRADE_NOTICE ' .. text)
end

local function key(value)
    if value == nil or value == 0 then
        return ''
    end
    if type(value) == 'cdata' or type(value) == 'string' or type(value) == 'number' then
        return tostring(value):gsub('ULL$', '')
    end
    local converted = ConvertIDTo64Bit(value)
    return converted and tostring(converted):gsub('ULL$', '') or ''
end
local statuses = {
    OK = 'Trade completed.',
    REJECTED = 'Trade could not be completed.',
    STOCK = 'Insufficient stock.',
    FUNDS = 'Insufficient funds.',
    CAPACITY = 'Insufficient cargo space.',
    PRICE = 'The price changed. Reopen the offer.',
    DOCK = 'Dock at the selected trading station first.',
    BUSY = 'A trade is already running. Try again later.',
    PARTIAL = 'Trade only partially completed. Check stock; do not submit it again.',
    UNCERTAIN = 'Trade status uncertain. Restart both test worlds.',
    UNSUPPORTED = 'This ware is not supported in the LAN bubble.',
}
local function send(packet)
    if state.client and state.client.lease then
        state.transport:send(packet)
        return true
    end
end

local function result(request, status)
    trade.executed[request] = status
    AddUITriggeredEvent('X4LAN_Trade', 'Finished')
    if state.client and state.client.lease then
        send('ORDER_DONE|1|' .. state.client.lease .. '|' .. request .. '|' .. status)
    end
    log('ORDER_DONE ' .. request .. ' ' .. status)
end

local function submit(market, lines)
    if cfg.coop then
        local i = api.coop and api.coop.controlled or 0
        if i == 0 or api.coop.pilots[i] ~= cfg.slot or api.coop.owners[i] ~= cfg.slot then
            notice('Take the controls of an available shared ship first.')
            return false
        end
        trade.dock = api.coop.docks[i] > 0 and ('m' .. api.coop.docks[i]) or 'none'
    end
    if trade.pending or trade.locked then
        notice('Wait for the pending trade to finish.')
        return false
    end
    if
        not trade.ready
        or not trade.owner
        or not state.client
        or not state.client.lease
        or not trade.lastSnapshot
        or GetCurRealTime() - trade.lastSnapshot > 5
    then
        notice('Trading is waiting for the host connection.')
        return false
    end
    if not protocol.lines(lines) then
        notice(statuses.UNSUPPORTED)
        return false
    end
    if trade.dock ~= market then
        notice(statuses.DOCK)
        return false
    end
    trade.requestCounter = (trade.requestCounter or 0) + 1
    local request = state.client.lease:sub(1, 24) .. string.format('%08x', trade.requestCounter)
    trade.pending = {
        request = request,
        market = market,
        lines = lines,
        owner = trade.owner,
        at = GetCurRealTime(),
    }
    if cfg.coop then
        trade.pending.ship = api.coop.controlled
        trade.pending.epoch = api.coop.epochs[trade.pending.ship]
    end
    notice('Trade order submitted.')
    return true
end
local nativeTradeList = GetTradeList
if nativeTradeList then
    GetTradeList = function(component, ...)
        local shared
        for i = 1, 2 do
            if trade.markets[i] and key(component) == trade.markets[i] then
                shared = i
            end
        end
        local offers = nativeTradeList(component, ...)
        if not shared then
            return offers
        end
        local result = {}
        for _, offer in ipairs(offers) do
            if offer.ismissionoffer and protocol.index[offer.ware] then
                -- These are ordinary market rows. The mission marker only identifies our native offers.
                offer.ismissionoffer = false
                if trade.snapshot then
                    for _, row in ipairs(trade.snapshot.rows) do
                        if row.index == protocol.index[offer.ware] then
                            local stock = row.cargo[shared + 2]
                            local amount = offer.isselloffer and stock
                                or math.min(
                                    math.max(0, row.limits[shared] - stock),
                                    math.floor(
                                        trade.snapshot.balances[shared + 2]
                                            / row.prices[shared * 2 - 1]
                                    )
                                )
                            offer.amount = math.min(offer.amount, amount)
                        end
                    end
                end
                if offer.amount == nil or offer.amount > 0 then
                    result[#result + 1] = offer
                end
            end
        end
        return result
    end
end

local function hook()
    if trade.menu then
        return
    end
    for _, menu in ipairs(Menus or {}) do
        if menu.name == 'MapMenu' then
            trade.menu = menu
            trade.originalConfirm = menu.buttonConfirmTrade
            menu.buttonConfirmTrade = function(...)
                if not state.ready then
                    return trade.originalConfirm(...)
                end
                local data = menu.contextMenuData or {}
                local market
                for i = 1, 2 do
                    if
                        data.component
                        and trade.markets[i]
                        and key(data.component) == trade.markets[i]
                    then
                        market = 'm' .. i
                    end
                end
                if not market then
                    notice('Shared trading is available at LAN Trading Station Alpha and Beta.')
                    return
                end
                local tradeShip = cfg.coop and api.coop and api.coop.ships[api.coop.controlled or 0]
                    or state.ship
                if
                    not data.currentShip
                    or key(data.currentShip) ~= key(tradeShip)
                    or data.wareexchange
                then
                    notice('Trade using the ship you are currently piloting.')
                    return
                end
                local basket = {}
                local seen = {}
                for tradeID, amount in pairs(data.orders or {}) do
                    if amount ~= 0 then
                        local offer =
                            menu.getTradeOfferByID(ConvertStringToLuaID(tostring(tradeID)))
                        if not offer or not protocol.index[offer.ware] or seen[offer.ware] then
                            notice(statuses.UNSUPPORTED)
                            return
                        end
                        seen[offer.ware] = true
                        basket[#basket + 1] = offer.ware
                            .. ','
                            .. string.format('%.0f', -amount)
                            .. ','
                            .. string.format('%.0f', offer.price * 100)
                    end
                end
                table.sort(basket)
                if #basket > 0 and submit(market, table.concat(basket, ';')) then
                    menu.closeContextMenu('back')
                end
            end
            log('native MapMenu trade confirmation connected')
            return
        end
    end
end
api.trade = {}
function api.trade.receive(client, fields, now)
    if fields[1] == 'ECON' then
        if #fields ~= 8 or not fields[4]:match('^[a-f0-9]+$') or #fields[4] ~= 32 then
            return true
        end
        if trade.owner ~= fields[4] then
            if trade.owner then
                trade.locked = true
                trade.worldChanged = true
                notice(statuses.UNCERTAIN)
            end
            trade.owner = fields[4]
            trade.assembler = protocol.assembler()
        end
        local parsed = trade.assembler:add(
            protocol.integer(fields[5]),
            protocol.integer(fields[6], 8),
            protocol.integer(fields[7], 8),
            fields[8],
            now
        )
        if parsed then
            trade.snapshot = parsed
            trade.lastSnapshot = now
            if cfg.slot ~= 'p1' and trade.ready and not trade.worldChanged then
                parsed.money = parsed.balances[cfg.coop and 1 or 2]
                local key = table.concat(parsed.balances, ',')
                for _, row in ipairs(parsed.rows) do
                    key = key .. '|' .. table.concat(row.cargo, ',')
                end
                if
                    not cfg.coop
                    or key ~= trade.appliedEconomy
                    or now >= (trade.nextEconomyApply or 0)
                then
                    AddUITriggeredEvent('X4LAN_Trade', 'Apply', parsed)
                    trade.appliedEconomy = key
                    trade.nextEconomyApply = now + 2
                end
            end
        end
        return true
    elseif fields[1] == 'DOCKSTATE' then
        if #fields ~= 6 or (fields[6] ~= 'none' and fields[6] ~= 'm1' and fields[6] ~= 'm2') then
            return true
        end
        local seq = protocol.integer(fields[5])
        if not seq then
            return true
        end
        if trade.dockOwner ~= fields[4] then
            trade.dockOwner = fields[4]
            trade.remoteSeq = -1
        end
        if seq > trade.remoteSeq then
            trade.remoteSeq = seq
            trade.remoteDock = fields[6]
            state.remoteDockLock = fields[6] ~= 'none'
            AddUITriggeredEvent('X4LAN_Trade', 'RemoteDock', tonumber(fields[6]:sub(2)) or 0)
        end
        return true
    elseif fields[1] == 'ORDER_EXEC' then
        if
            cfg.slot ~= 'p1'
            or #fields ~= 7
            or not fields[4]:match('^[a-f0-9]+$')
            or #fields[4] ~= 32
            or not (fields[5] == 'p1' or fields[5] == 'p2')
            or not (fields[6] == 'm1' or fields[6] == 'm2')
        then
            return true
        end
        if not trade.ready or api.paused() then
            return true
        end
        local old = trade.executed[fields[4]]
        if old and old ~= 'PENDING' then
            send('ORDER_DONE|1|' .. client.lease .. '|' .. fields[4] .. '|' .. old)
        elseif not old then
            local lines = protocol.lines(fields[7])
            if not lines then
                result(fields[4], 'UNSUPPORTED')
                return true
            end
            trade.executed[fields[4]] = 'PENDING'
            AddUITriggeredEvent('X4LAN_Trade', 'Order', {
                request = fields[4],
                slot = fields[5] == 'p1' and 1 or 2,
                market = tonumber(fields[6]:sub(2)),
                lines = lines,
            })
        end
        return true
    elseif fields[1] == 'ORDER_RESULT' then
        if #fields ~= 5 or not trade.pending or fields[4] ~= trade.pending.request then
            return true
        end
        if fields[5] ~= 'PENDING' and statuses[fields[5]] then
            log('ORDER_RESULT ' .. fields[4] .. ' ' .. fields[5])
            notice(statuses[fields[5]])
            if fields[5] == 'PARTIAL' or fields[5] == 'UNCERTAIN' then
                trade.locked = true
            end
            trade.pending = nil
        end
        return true
    end
    return false
end

function api.trade.frame(now)
    hook()
    if cfg.coop and (not api.coop or not api.coop.ready) then
        return
    end
    if not trade.setup then
        trade.setup = true
        AddUITriggeredEvent('X4LAN_Trade', 'Setup')
    end
    if not trade.ready or not state.client or not state.client.lease then
        return
    end
    local lease = state.client.lease
    if now >= (trade.nextSend or 0) then
        trade.nextSend = now + 0.5
        trade.dockseq = trade.dockseq + 1
        if not cfg.coop then
            send('DOCKSTATE|1|' .. lease .. '|' .. trade.dockseq .. '|' .. trade.dock)
        end
        if cfg.slot == 'p1' and trade.payload then
            local total = math.ceil(#trade.payload / 500)
            for i = 1, total do
                send(
                    'ECON|1|'
                        .. lease
                        .. '|'
                        .. trade.revision
                        .. '|'
                        .. i
                        .. '|'
                        .. total
                        .. '|'
                        .. trade.payload:sub((i - 1) * 500 + 1, i * 500)
                )
            end
        end
        if trade.pending then
            local pending = trade.pending
            if cfg.coop then
                send(
                    'COOPORDER|1|'
                        .. lease
                        .. '|'
                        .. pending.request
                        .. '|'
                        .. pending.owner
                        .. '|'
                        .. pending.ship
                        .. '|'
                        .. pending.epoch
                        .. '|'
                        .. pending.market
                        .. '|'
                        .. pending.lines
                )
            else
                send(
                    'ORDER|1|'
                        .. lease
                        .. '|'
                        .. pending.request
                        .. '|'
                        .. pending.owner
                        .. '|'
                        .. pending.market
                        .. '|'
                        .. pending.lines
                )
            end
            if now - pending.at > 30 and not pending.warned then
                pending.warned = true
                notice('Trade is still pending. Stay connected and docked.')
            end
        end
    end
end
RegisterEvent('X4LAN_Trade.Market1', function(_, object)
    trade.markets[1] = key(object)
end)
RegisterEvent('X4LAN_Trade.Market2', function(_, object)
    trade.markets[2] = key(object)
end)
RegisterEvent('X4LAN_Trade.Ready', function()
    trade.ready = true
    log('market adapter ready')
    if cfg.slot ~= 'p1' and trade.snapshot and not trade.worldChanged then
        trade.snapshot.money = trade.snapshot.balances[cfg.coop and 1 or 2]
        AddUITriggeredEvent('X4LAN_Trade', 'Apply', trade.snapshot)
    end
end)
RegisterEvent('X4LAN_Trade.LocalDock', function(_, market)
    if not cfg.coop then
        trade.dock = market
    end
end)
RegisterEvent('X4LAN_Trade.Snapshot', function(_, payload)
    if cfg.slot == 'p1' then
        if protocol.snapshot(payload) then
            trade.payload = payload
            trade.revision = trade.revision + 1
        else
            log('INVALID native economy snapshot ' .. tostring(payload))
        end
    end
end)
RegisterEvent('X4LAN_Trade.Result', function(_, payload)
    local request, status = payload:match('^([a-f0-9]+)|([A-Z]+)$')
    if request and statuses[status] and trade.executed[request] == 'PENDING' then
        result(request, status)
    end
end)
-- Exposed only for the development harness, which is never shipped in installers.
api.trade.state = trade
api.trade.submit = submit
hook()
