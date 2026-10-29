-- The existing X4 trade sliders build the basket; only the host executes it.
local cfg = X4LAN_CONFIG
if not cfg or not cfg.enabled or not cfg.trading or not X4LAN_BUBBLE_API then
    return
end
local A = X4LAN_BUBBLE_API
local s = A.state
if not s.active then
    return
end
local P = X4LAN_TRADE_PROTOCOL
local t =
    { markets = {}, executed = {}, assembler = P.assembler(), dock = 'none', dockseq = 0, revision = 0 }
local function log(v)
    DebugError('[X4LAN] ' .. v)
end
local function notice(v)
    AddUITriggeredEvent('X4LAN_Trade', 'Notice', v)
    log('TRADE_NOTICE ' .. v)
end
local function key(v)
    if v == nil or v == 0 then
        return ''
    end
    if type(v) == 'cdata' or type(v) == 'string' or type(v) == 'number' then
        return tostring(v):gsub('ULL$', '')
    end
    local n = ConvertIDTo64Bit(v)
    return n and tostring(n):gsub('ULL$', '') or ''
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
    if s.client and s.client.lease then
        s.transport:send(packet)
        return true
    end
end
local function result(request, status)
    t.executed[request] = status
    AddUITriggeredEvent('X4LAN_Trade', 'Finished')
    if s.client and s.client.lease then
        send('ORDER_DONE|1|' .. s.client.lease .. '|' .. request .. '|' .. status)
    end
    log('ORDER_DONE ' .. request .. ' ' .. status)
end
local function submit(market, lines)
    if cfg.coop then
        local i = A.coop and A.coop.controlled or 0
        if i == 0 or A.coop.pilots[i] ~= cfg.slot or A.coop.owners[i] ~= cfg.slot then
            notice('Take the controls of an available shared ship first.')
            return false
        end
        t.dock = A.coop.docks[i] > 0 and ('m' .. A.coop.docks[i]) or 'none'
    end
    if t.pending or t.locked then
        notice('Wait for the pending trade to finish.')
        return false
    end
    if
        not t.ready
        or not t.owner
        or not s.client
        or not s.client.lease
        or not t.lastSnapshot
        or GetCurRealTime() - t.lastSnapshot > 5
    then
        notice('Trading is waiting for the host connection.')
        return false
    end
    if not P.lines(lines) then
        notice(statuses.UNSUPPORTED)
        return false
    end
    if t.dock ~= market then
        notice(statuses.DOCK)
        return false
    end
    t.requestCounter = (t.requestCounter or 0) + 1
    local request = s.client.lease:sub(1, 24) .. string.format('%08x', t.requestCounter)
    t.pending =
        { request = request, market = market, lines = lines, owner = t.owner, at = GetCurRealTime() }
    if cfg.coop then
        t.pending.ship = A.coop.controlled
        t.pending.epoch = A.coop.epochs[t.pending.ship]
    end
    notice('Trade order submitted.')
    return true
end
local nativeTradeList = GetTradeList
if nativeTradeList then
    GetTradeList = function(component, ...)
        local shared
        for i = 1, 2 do
            if t.markets[i] and key(component) == t.markets[i] then
                shared = i
            end
        end
        local offers = nativeTradeList(component, ...)
        if not shared then
            return offers
        end
        local result = {}
        for _, offer in ipairs(offers) do
            if offer.ismissionoffer and P.index[offer.ware] then
                -- These are ordinary market rows. The mission marker only identifies our native offers.
                offer.ismissionoffer = false
                if t.snapshot then
                    for _, row in ipairs(t.snapshot.rows) do
                        if row.index == P.index[offer.ware] then
                            local stock = row.cargo[shared + 2]
                            local amount = offer.isselloffer and stock
                                or math.min(
                                    math.max(0, row.limits[shared] - stock),
                                    math.floor(
                                        t.snapshot.balances[shared + 2] / row.prices[shared * 2 - 1]
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
    if t.menu then
        return
    end
    for _, menu in ipairs(Menus or {}) do
        if menu.name == 'MapMenu' then
            t.menu = menu
            t.originalConfirm = menu.buttonConfirmTrade
            menu.buttonConfirmTrade = function(...)
                if not s.ready then
                    return t.originalConfirm(...)
                end
                local data = menu.contextMenuData or {}
                local market
                for i = 1, 2 do
                    if data.component and t.markets[i] and key(data.component) == t.markets[i] then
                        market = 'm' .. i
                    end
                end
                if not market then
                    notice('Shared trading is available at LAN Trading Station Alpha and Beta.')
                    return
                end
                local tradeShip = cfg.coop and A.coop and A.coop.ships[A.coop.controlled or 0]
                    or s.ship
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
                        if not offer or not P.index[offer.ware] or seen[offer.ware] then
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
A.trade = {}
function A.trade.receive(client, f, now)
    if f[1] == 'ECON' then
        if #f ~= 8 or not f[4]:match('^[a-f0-9]+$') or #f[4] ~= 32 then
            return true
        end
        if t.owner ~= f[4] then
            if t.owner then
                t.locked = true
                t.worldChanged = true
                notice(statuses.UNCERTAIN)
            end
            t.owner = f[4]
            t.assembler = P.assembler()
        end
        local parsed =
            t.assembler:add(P.integer(f[5]), P.integer(f[6], 8), P.integer(f[7], 8), f[8], now)
        if parsed then
            t.snapshot = parsed
            t.lastSnapshot = now
            if cfg.slot ~= 'p1' and t.ready and not t.worldChanged then
                parsed.money = parsed.balances[cfg.coop and 1 or 2]
                local key = table.concat(parsed.balances, ',')
                for _, r in ipairs(parsed.rows) do
                    key = key .. '|' .. table.concat(r.cargo, ',')
                end
                if not cfg.coop or key ~= t.appliedEconomy or now >= (t.nextEconomyApply or 0) then
                    AddUITriggeredEvent('X4LAN_Trade', 'Apply', parsed)
                    t.appliedEconomy = key
                    t.nextEconomyApply = now + 2
                end
            end
        end
        return true
    elseif f[1] == 'DOCKSTATE' then
        if #f ~= 6 or (f[6] ~= 'none' and f[6] ~= 'm1' and f[6] ~= 'm2') then
            return true
        end
        local seq = P.integer(f[5])
        if not seq then
            return true
        end
        if t.dockOwner ~= f[4] then
            t.dockOwner = f[4]
            t.remoteSeq = -1
        end
        if seq > t.remoteSeq then
            t.remoteSeq = seq
            t.remoteDock = f[6]
            s.remoteDockLock = f[6] ~= 'none'
            AddUITriggeredEvent('X4LAN_Trade', 'RemoteDock', tonumber(f[6]:sub(2)) or 0)
        end
        return true
    elseif f[1] == 'ORDER_EXEC' then
        if
            cfg.slot ~= 'p1'
            or #f ~= 7
            or not f[4]:match('^[a-f0-9]+$')
            or #f[4] ~= 32
            or not (f[5] == 'p1' or f[5] == 'p2')
            or not (f[6] == 'm1' or f[6] == 'm2')
        then
            return true
        end
        if not t.ready or A.paused() then
            return true
        end
        local old = t.executed[f[4]]
        if old and old ~= 'PENDING' then
            send('ORDER_DONE|1|' .. client.lease .. '|' .. f[4] .. '|' .. old)
        elseif not old then
            local lines = P.lines(f[7])
            if not lines then
                result(f[4], 'UNSUPPORTED')
                return true
            end
            t.executed[f[4]] = 'PENDING'
            AddUITriggeredEvent(
                'X4LAN_Trade',
                'Order',
                { request = f[4], slot = f[5] == 'p1' and 1 or 2, market = tonumber(f[6]:sub(2)), lines = lines }
            )
        end
        return true
    elseif f[1] == 'ORDER_RESULT' then
        if #f ~= 5 or not t.pending or f[4] ~= t.pending.request then
            return true
        end
        if f[5] ~= 'PENDING' and statuses[f[5]] then
            log('ORDER_RESULT ' .. f[4] .. ' ' .. f[5])
            notice(statuses[f[5]])
            if f[5] == 'PARTIAL' or f[5] == 'UNCERTAIN' then
                t.locked = true
            end
            t.pending = nil
        end
        return true
    end
    return false
end
function A.trade.frame(now)
    hook()
    if cfg.coop and (not A.coop or not A.coop.ready) then
        return
    end
    if not t.setup then
        t.setup = true
        AddUITriggeredEvent('X4LAN_Trade', 'Setup')
    end
    if not t.ready or not s.client or not s.client.lease then
        return
    end
    local lease = s.client.lease
    if now >= (t.nextSend or 0) then
        t.nextSend = now + 0.5
        t.dockseq = t.dockseq + 1
        if not cfg.coop then
            send('DOCKSTATE|1|' .. lease .. '|' .. t.dockseq .. '|' .. t.dock)
        end
        if cfg.slot == 'p1' and t.payload then
            local total = math.ceil(#t.payload / 500)
            for i = 1, total do
                send(
                    'ECON|1|'
                        .. lease
                        .. '|'
                        .. t.revision
                        .. '|'
                        .. i
                        .. '|'
                        .. total
                        .. '|'
                        .. t.payload:sub((i - 1) * 500 + 1, i * 500)
                )
            end
        end
        if t.pending then
            local p = t.pending
            if cfg.coop then
                send(
                    'COOPORDER|1|'
                        .. lease
                        .. '|'
                        .. p.request
                        .. '|'
                        .. p.owner
                        .. '|'
                        .. p.ship
                        .. '|'
                        .. p.epoch
                        .. '|'
                        .. p.market
                        .. '|'
                        .. p.lines
                )
            else
                send(
                    'ORDER|1|'
                        .. lease
                        .. '|'
                        .. p.request
                        .. '|'
                        .. p.owner
                        .. '|'
                        .. p.market
                        .. '|'
                        .. p.lines
                )
            end
            if now - p.at > 30 and not p.warned then
                p.warned = true
                notice('Trade is still pending. Stay connected and docked.')
            end
        end
    end
end
RegisterEvent('X4LAN_Trade.Market1', function(_, object)
    t.markets[1] = key(object)
end)
RegisterEvent('X4LAN_Trade.Market2', function(_, object)
    t.markets[2] = key(object)
end)
RegisterEvent('X4LAN_Trade.Ready', function()
    t.ready = true
    log('market adapter ready')
    if cfg.slot ~= 'p1' and t.snapshot and not t.worldChanged then
        t.snapshot.money = t.snapshot.balances[cfg.coop and 1 or 2]
        AddUITriggeredEvent('X4LAN_Trade', 'Apply', t.snapshot)
    end
end)
RegisterEvent('X4LAN_Trade.LocalDock', function(_, market)
    if not cfg.coop then
        t.dock = market
    end
end)
RegisterEvent('X4LAN_Trade.Snapshot', function(_, payload)
    if cfg.slot == 'p1' then
        if P.snapshot(payload) then
            t.payload = payload
            t.revision = t.revision + 1
        else
            log('INVALID native economy snapshot ' .. tostring(payload))
        end
    end
end)
RegisterEvent('X4LAN_Trade.Result', function(_, payload)
    local request, status = payload:match('^([a-f0-9]+)|([A-Z]+)$')
    if request and statuses[status] and t.executed[request] == 'PENDING' then
        result(request, status)
    end
end)
-- Exposed only for the development harness, which is never shipped in installers.
A.trade.state = t
A.trade.submit = submit
hook()
