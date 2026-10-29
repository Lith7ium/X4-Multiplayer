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
for i, ware in ipairs(protocol.wares) do
    protocol.index[ware] = i
end
function protocol.split(text, separator)
    local fields = {}
    for value in (text .. separator):gmatch('(.-)' .. separator) do
        fields[#fields + 1] = value
    end
    return fields
end
function protocol.integer(text, limit, signed)
    if type(text) ~= 'string' or not text:match(signed and '^%-?%d+$' or '^%d+$') then
        return nil
    end
    local number = tonumber(text)
    if not number or math.abs(number) > (limit or 1000000000000) then
        return nil
    end
    return number
end
function protocol.snapshot(payload)
    if #payload > 4000 then
        return nil
    end
    local rows = protocol.split(payload, ';')
    if #rows ~= #protocol.wares + 1 then
        return nil
    end
    local balanceFields = protocol.split(rows[1], ',')
    if #balanceFields ~= 5 or balanceFields[1] ~= 'M' then
        return nil
    end
    local result = { balances = {}, rows = {} }
    local seen = {}
    for i = 1, 4 do
        result.balances[i] = protocol.integer(balanceFields[i + 1])
        if not result.balances[i] then
            return nil
        end
    end
    for i = 2, #rows do
        local wareFields = protocol.split(rows[i], ',')
        local index = protocol.index[wareFields[2]]
        if #wareFields ~= 13 or wareFields[1] ~= 'W' or not index or seen[index] then
            return nil
        end
        seen[index] = true
        local values = {}
        for j = 3, 13 do
            values[j - 2] = protocol.integer(wareFields[j])
            if not values[j - 2] then
                return nil
            end
        end
        if values[1] < 1 or values[1] > 100000 then
            return nil
        end
        for _, j in ipairs({ 2, 3, 4, 5, 8, 11 }) do
            if values[j] > 1000000 then
                return nil
            end
        end
        for _, j in ipairs({ 6, 7, 9, 10 }) do
            if values[j] < 1 then
                return nil
            end
        end
        result.rows[#result.rows + 1] = {
            index = index,
            cargo = { values[2], values[3], values[4], values[5] },
            prices = { values[6], values[7], values[9], values[10] },
            limits = { values[8], values[11] },
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
        local fields = protocol.split(row, ',')
        local index = protocol.index[fields[1]]
        local quantity = protocol.integer(fields[2], 1000000, true)
        local price = protocol.integer(fields[3])
        if
            #fields ~= 3
            or not index
            or seen[index]
            or not quantity
            or quantity == 0
            or not price
            or price == 0
            or math.abs(quantity) * price > 1000000000000
        then
            return nil
        end
        seen[index] = true
        result[#result + 1] = { index = index, quantity = quantity, price = price }
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
            for revisionKey, item in pairs(self.pending) do
                if now - item.at > 3 or revisionKey < revision - 4 then
                    self.pending[revisionKey] = nil
                end
            end
            local item = self.pending[revision]
            if not item then
                local count, oldest = 0, nil
                for revisionKey in pairs(self.pending) do
                    count = count + 1
                    if not oldest or revisionKey < oldest then
                        oldest = revisionKey
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
            for revisionKey in pairs(self.pending) do
                if revisionKey <= revision then
                    self.pending[revisionKey] = nil
                end
            end
            return parsed
        end,
    }
end
