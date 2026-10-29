-- Strict wire parsing, independent of X4. Money is always integral cents.
X4LAN_TRADE_PROTOCOL = {}
local protocol = X4LAN_TRADE_PROTOCOL
protocol.wares = {
    'energycells',
    'foodrations',
    'medicalsupplies',
    'hullparts',
    'refinedmetals',
    'siliconwafers',
}
protocol.index = {}
for i, w in ipairs(protocol.wares) do
    protocol.index[w] = i
end
function protocol.split(s, sep)
    local out = {}
    for v in (s .. sep):gmatch('(.-)' .. sep) do
        out[#out + 1] = v
    end
    return out
end
function protocol.integer(s, limit, signed)
    if type(s) ~= 'string' or not s:match(signed and '^%-?%d+$' or '^%d+$') then
        return nil
    end
    local n = tonumber(s)
    if not n or math.abs(n) > (limit or 1000000000000) then
        return nil
    end
    return n
end
function protocol.snapshot(payload)
    if #payload > 4000 then
        return nil
    end
    local rows = protocol.split(payload, ';')
    if #rows ~= #protocol.wares + 1 then
        return nil
    end
    local m = protocol.split(rows[1], ',')
    if #m ~= 5 or m[1] ~= 'M' then
        return nil
    end
    local result = { balances = {}, rows = {} }
    local seen = {}
    for i = 1, 4 do
        result.balances[i] = protocol.integer(m[i + 1])
        if not result.balances[i] then
            return nil
        end
    end
    for i = 2, #rows do
        local r = protocol.split(rows[i], ',')
        local index = protocol.index[r[2]]
        if #r ~= 13 or r[1] ~= 'W' or not index or seen[index] then
            return nil
        end
        seen[index] = true
        local v = {}
        for j = 3, 13 do
            v[j - 2] = protocol.integer(r[j])
            if not v[j - 2] then
                return nil
            end
        end
        if v[1] < 1 or v[1] > 100000 then
            return nil
        end
        for _, j in ipairs({ 2, 3, 4, 5, 8, 11 }) do
            if v[j] > 1000000 then
                return nil
            end
        end
        for _, j in ipairs({ 6, 7, 9, 10 }) do
            if v[j] < 1 then
                return nil
            end
        end
        result.rows[#result.rows + 1] = {
            index = index,
            cargo = { v[2], v[3], v[4], v[5] },
            prices = { v[6], v[7], v[9], v[10] },
            limits = { v[8], v[11] },
        }
    end
    return result
end
function protocol.lines(payload)
    local rows = protocol.split(payload, ';')
    if #rows < 1 or #rows > #protocol.wares then
        return nil
    end
    local result, seen = {}, {}
    for _, row in ipairs(rows) do
        local f = protocol.split(row, ',')
        local index = protocol.index[f[1]]
        local qty = protocol.integer(f[2], 1000000, true)
        local price = protocol.integer(f[3])
        if
            #f ~= 3
            or not index
            or seen[index]
            or not qty
            or qty == 0
            or not price
            or price == 0
            or math.abs(qty) * price > 1000000000000
        then
            return nil
        end
        seen[index] = true
        result[#result + 1] = { index = index, quantity = qty, price = price }
    end
    return result
end
function protocol.assembler()
    return {
        revision = -1,
        pending = {},
        add = function(self, revision, part, total, payload, now)
            if
                not revision
                or revision <= self.revision
                or not part
                or not total
                or total < 1
                or total > 8
                or part < 1
                or part > total
                or #payload > 500
            then
                return nil
            end
            for rev, item in pairs(self.pending) do
                if now - item.at > 3 or rev < revision - 4 then
                    self.pending[rev] = nil
                end
            end
            local item = self.pending[revision]
            if not item then
                local count, oldest = 0, nil
                for rev in pairs(self.pending) do
                    count = count + 1
                    if not oldest or rev < oldest then
                        oldest = rev
                    end
                end
                if count >= 4 then
                    if revision < oldest then
                        return nil
                    end
                    self.pending[oldest] = nil
                end
                item = { at = now, total = total, parts = {} }
                self.pending[revision] = item
            end
            if item.total ~= total or item.parts[part] and item.parts[part] ~= payload then
                return nil
            end
            item.parts[part] = payload
            for i = 1, total do
                if not item.parts[i] then
                    return nil
                end
            end
            local parsed = protocol.snapshot(table.concat(item.parts))
            if not parsed then
                return nil
            end
            self.revision = revision
            for rev in pairs(self.pending) do
                if rev <= revision then
                    self.pending[rev] = nil
                end
            end
            return parsed
        end,
    }
end
