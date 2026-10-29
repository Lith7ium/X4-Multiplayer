-- Original W01 atomic sector baselines; mirrors x4lan/universe.py's wire contract.
X4LAN_UNIVERSE_PROTOCOL = {}
local protocol = X4LAN_UNIVERSE_PROTOCOL
protocol.limit = 4096
protocol.window = 12
protocol.retry = 0.25
function protocol.split(text, separator)
    local fields = {}
    for value in (text .. separator):gmatch('(.-)' .. separator) do
        fields[#fields + 1] = value
    end
    return fields
end
function protocol.integer(text, limit)
    if type(text) ~= 'string' or not text:match('^%d+$') then
        return nil
    end
    local number = tonumber(text)
    if not number or number > (limit or 2147483647) then
        return nil
    end
    return number
end
function protocol.atom(text, macro)
    return type(text) == 'string'
        and #text > 0
        and #text <= 96
        and not text:find('[^a-zA-Z0-9_.-]')
        and (not macro or text:sub(-6) == '_macro')
end
function protocol.hex(text)
    return type(text) == 'string' and #text == 32 and not text:find('[^a-f0-9]')
end
function protocol.record(row)
    if type(row) ~= 'string' or #row > 512 or row:find('[^%g]') then
        return nil
    end
    local fields = protocol.split(row, ',')
    if #fields ~= 12 then
        return nil
    end
    local id = protocol.integer(fields[1])
    if not id or id == 0 or (fields[2] ~= 'ship' and fields[2] ~= 'drop') then
        return nil
    end
    if
        not protocol.atom(fields[3], true)
        or not protocol.atom(fields[4])
        or not protocol.atom(fields[11])
    then
        return nil
    end
    -- The first three values are sector positions; the last three are radians.
    local pose = {}
    for i = 1, 6 do
        if fields[i + 4]:find('[^0-9eE+.-]') then
            return nil
        end
        local coordinate = tonumber(fields[i + 4])
        if
            not coordinate
            or coordinate ~= coordinate
            or math.abs(coordinate) > (i <= 3 and 50000000 or 2 * math.pi)
        then
            return nil
        end
        pose[i] = coordinate
    end
    local amount = protocol.integer(fields[12], 1000000)
    if
        not amount
        or (fields[2] == 'ship' and (fields[11] ~= 'none' or amount ~= 0))
        or (fields[2] == 'drop' and (fields[11] == 'none' or amount == 0))
    then
        return nil
    end
    return {
        id = id,
        kind = fields[2],
        macro = fields[3],
        owner = fields[4],
        pose = pose,
        ware = fields[11],
        amount = amount,
    }
end
function protocol.checksum(rows)
    local low = 1
    local high = 0
    for i, row in ipairs(rows) do
        if i > 1 then
            low = (low + 10) % 65521
            high = (high + low) % 65521
        end
        for byteIndex = 1, #row do
            low = (low + row:byte(byteIndex)) % 65521
            high = (high + low) % 65521
        end
    end
    return high * 65536 + low
end
function protocol.baseline(epoch, revision, sector, rows)
    assert(
        protocol.hex(epoch) and revision > 0 and revision % 1 == 0 and revision <= 2147483647,
        'bad world identity'
    )
    assert(protocol.atom(sector, true) and #rows <= protocol.limit, 'bad world scope/size')
    local previous = 0
    local owned = {}
    for i, row in ipairs(rows) do
        local record = assert(protocol.record(row), 'bad world record')
        assert(record.id > previous, 'unordered/duplicate world ID')
        previous = record.id
        owned[i] = row
    end
    return {
        epoch = epoch,
        revision = revision,
        sector = sector,
        rows = owned,
        digest = protocol.checksum(owned),
    }
end
function protocol.packet(baseline, lease, index)
    local header = '|1|' .. lease .. '|' .. baseline.epoch .. '|' .. baseline.revision
    if index == 0 then
        return 'UBEGIN'
            .. header
            .. '|'
            .. baseline.sector
            .. '|'
            .. #baseline.rows
            .. '|'
            .. string.format('%.0f', baseline.digest)
    end
    if index == #baseline.rows + 1 then
        return 'UCOMMIT' .. header
    end
    return 'UOBJECT' .. header .. '|' .. index .. '|' .. baseline.rows[index]
end
function protocol.receiver()
    local self = {}
    function self:receive(fields, now)
        local revision = protocol.integer(fields[5])
        if #fields < 5 or not protocol.hex(fields[4]) or not revision or revision == 0 then
            return nil
        end
        local epoch = fields[4]
        local old = self.latest
        if old then
            if epoch ~= old.epoch or revision < old.revision then
                return nil
            end
            if revision == old.revision then
                return #old.rows + 1
            end
        end
        local pending = self.pending
        if pending and now - pending.at > 15 then
            self.pending = nil
            pending = nil
        end
        if fields[1] == 'UBEGIN' and #fields == 8 then
            local count = protocol.integer(fields[7], protocol.limit)
            local digest = protocol.integer(fields[8], 4294967295)
            if not protocol.atom(fields[6], true) or not count or not digest then
                return nil
            end
            if pending then
                if
                    pending.epoch ~= epoch
                    or pending.revision ~= revision
                    or pending.sector ~= fields[6]
                    or pending.count ~= count
                    or pending.digest ~= digest
                then
                    return nil
                end
            else
                self.pending = {
                    epoch = epoch,
                    revision = revision,
                    sector = fields[6],
                    count = count,
                    digest = digest,
                    rows = {},
                    got = 0,
                    at = now,
                }
            end
            return 0
        end
        if not pending or pending.epoch ~= epoch or pending.revision ~= revision then
            return -1
        end
        if fields[1] == 'UOBJECT' and #fields == 7 then
            local index = protocol.integer(fields[6])
            if
                not index
                or index < 1
                or index > pending.count
                or not protocol.record(fields[7])
            then
                return nil
            end
            if pending.rows[index] and pending.rows[index] ~= fields[7] then
                self.pending = nil
                return -1
            end
            if not pending.rows[index] then
                pending.rows[index] = fields[7]
                pending.got = pending.got + 1
                pending.at = now
            end
            return index
        end
        if fields[1] == 'UCOMMIT' and #fields == 5 and pending.got == pending.count then
            local ok, baseline =
                pcall(protocol.baseline, epoch, revision, pending.sector, pending.rows)
            if not ok or baseline.digest ~= pending.digest then
                self.pending = nil
                return -1
            end
            self.latest = baseline
            self.pending = nil
            return pending.count + 1, baseline
        end
        return nil
    end
    return self
end
function protocol.sender(baseline)
    local self = { baseline = baseline, acked = {}, sent = {}, done = false }
    function self:ack(epoch, revision, index)
        if epoch ~= baseline.epoch or revision ~= baseline.revision then
            return
        end
        local last = #baseline.rows + 1
        if index == -1 then
            self.acked = {}
            self.sent = {}
        elseif index == last and self.sent[last] then
            self.done = true
        elseif index >= 0 and index < last and self.sent[index] then
            self.acked[index] = true
        end
    end
    function self:packets(lease, now)
        if self.done then
            return {}
        end
        local indices = {}
        if not self.acked[0] then
            indices = { 0 }
        else
            for i = 1, #baseline.rows do
                if not self.acked[i] then
                    indices[#indices + 1] = i
                    if #indices >= protocol.window then
                        break
                    end
                end
            end
            if #indices == 0 then
                indices = { #baseline.rows + 1 }
            end
        end
        local packets = {}
        for _, index in ipairs(indices) do
            if now - (self.sent[index] or -math.huge) >= protocol.retry then
                self.sent[index] = now
                packets[#packets + 1] = protocol.packet(baseline, lease, index)
            end
        end
        return packets
    end
    return self
end
