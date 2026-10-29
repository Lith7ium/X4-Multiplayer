-- Original W01 atomic sector baselines; mirrors x4lan/universe.py's wire contract.
X4LAN_UNIVERSE_PROTOCOL = {}
local protocol = X4LAN_UNIVERSE_PROTOCOL
protocol.limit = 4096
protocol.window = 12
protocol.retry = 0.25
function protocol.split(s, separator)
    local out = {}
    for v in (s .. separator):gmatch('(.-)' .. separator) do
        out[#out + 1] = v
    end
    return out
end
function protocol.integer(s, limit)
    if type(s) ~= 'string' or not s:match('^%d+$') then
        return nil
    end
    local n = tonumber(s)
    if not n or n > (limit or 2147483647) then
        return nil
    end
    return n
end
function protocol.atom(s, macro)
    return type(s) == 'string'
        and #s > 0
        and #s <= 96
        and not s:find('[^a-zA-Z0-9_.-]')
        and (not macro or s:sub(-6) == '_macro')
end
function protocol.hex(s)
    return type(s) == 'string' and #s == 32 and not s:find('[^a-f0-9]')
end
function protocol.record(row)
    if type(row) ~= 'string' or #row > 512 or row:find('[^%g]') then
        return nil
    end
    local f = protocol.split(row, ',')
    if #f ~= 12 then
        return nil
    end
    local id = protocol.integer(f[1])
    if not id or id == 0 or (f[2] ~= 'ship' and f[2] ~= 'drop') then
        return nil
    end
    if not protocol.atom(f[3], true) or not protocol.atom(f[4]) or not protocol.atom(f[11]) then
        return nil
    end
    local pose = {}
    for i = 1, 6 do
        if f[i + 4]:find('[^0-9eE+.-]') then
            return nil
        end
        local n = tonumber(f[i + 4])
        if not n or n ~= n or math.abs(n) > (i <= 3 and 50000000 or 2 * math.pi) then
            return nil
        end
        pose[i] = n
    end
    local amount = protocol.integer(f[12], 1000000)
    if
        not amount
        or (f[2] == 'ship' and (f[11] ~= 'none' or amount ~= 0))
        or (f[2] == 'drop' and (f[11] == 'none' or amount == 0))
    then
        return nil
    end
    return {
        id = id,
        kind = f[2],
        macro = f[3],
        owner = f[4],
        pose = pose,
        ware = f[11],
        amount = amount,
    }
end
function protocol.checksum(rows)
    local a, b = 1, 0
    for i, row in ipairs(rows) do
        if i > 1 then
            a = (a + 10) % 65521
            b = (b + a) % 65521
        end
        for n = 1, #row do
            a = (a + row:byte(n)) % 65521
            b = (b + a) % 65521
        end
    end
    return b * 65536 + a
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
        local r = assert(protocol.record(row), 'bad world record')
        assert(r.id > previous, 'unordered/duplicate world ID')
        previous = r.id
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
function protocol.packet(b, lease, index)
    local header = '|1|' .. lease .. '|' .. b.epoch .. '|' .. b.revision
    if index == 0 then
        return 'UBEGIN'
            .. header
            .. '|'
            .. b.sector
            .. '|'
            .. #b.rows
            .. '|'
            .. string.format('%.0f', b.digest)
    end
    if index == #b.rows + 1 then
        return 'UCOMMIT' .. header
    end
    return 'UOBJECT' .. header .. '|' .. index .. '|' .. b.rows[index]
end
function protocol.receiver()
    local self = {}
    function self:receive(f, now)
        local rev = protocol.integer(f[5])
        if #f < 5 or not protocol.hex(f[4]) or not rev or rev == 0 then
            return nil
        end
        local epoch = f[4]
        local old = self.latest
        if old then
            if epoch ~= old.epoch or rev < old.revision then
                return nil
            end
            if rev == old.revision then
                return #old.rows + 1
            end
        end
        local p = self.pending
        if p and now - p.at > 15 then
            self.pending = nil
            p = nil
        end
        if f[1] == 'UBEGIN' and #f == 8 then
            local count = protocol.integer(f[7], protocol.limit)
            local digest = protocol.integer(f[8], 4294967295)
            if not protocol.atom(f[6], true) or not count or not digest then
                return nil
            end
            if p then
                if
                    p.epoch ~= epoch
                    or p.revision ~= rev
                    or p.sector ~= f[6]
                    or p.count ~= count
                    or p.digest ~= digest
                then
                    return nil
                end
            else
                self.pending = {
                    epoch = epoch,
                    revision = rev,
                    sector = f[6],
                    count = count,
                    digest = digest,
                    rows = {},
                    got = 0,
                    at = now,
                }
            end
            return 0
        end
        if not p or p.epoch ~= epoch or p.revision ~= rev then
            return -1
        end
        if f[1] == 'UOBJECT' and #f == 7 then
            local index = protocol.integer(f[6])
            if not index or index < 1 or index > p.count or not protocol.record(f[7]) then
                return nil
            end
            if p.rows[index] and p.rows[index] ~= f[7] then
                self.pending = nil
                return -1
            end
            if not p.rows[index] then
                p.rows[index] = f[7]
                p.got = p.got + 1
                p.at = now
            end
            return index
        end
        if f[1] == 'UCOMMIT' and #f == 5 and p.got == p.count then
            local ok, b = pcall(protocol.baseline, epoch, rev, p.sector, p.rows)
            if not ok or b.digest ~= p.digest then
                self.pending = nil
                return -1
            end
            self.latest = b
            self.pending = nil
            return p.count + 1, b
        end
        return nil
    end
    return self
end
function protocol.sender(b)
    local self = { baseline = b, acked = {}, sent = {}, done = false }
    function self:ack(epoch, rev, index)
        if epoch ~= b.epoch or rev ~= b.revision then
            return
        end
        local last = #b.rows + 1
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
            for i = 1, #b.rows do
                if not self.acked[i] then
                    indices[#indices + 1] = i
                    if #indices >= protocol.window then
                        break
                    end
                end
            end
            if #indices == 0 then
                indices = { #b.rows + 1 }
            end
        end
        local out = {}
        for _, index in ipairs(indices) do
            if now - (self.sent[index] or -math.huge) >= protocol.retry then
                self.sent[index] = now
                out[#out + 1] = protocol.packet(b, lease, index)
            end
        end
        return out
    end
    return self
end
