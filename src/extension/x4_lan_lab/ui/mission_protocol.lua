-- Bounded host-native mission records. No local objective/reward authority.
X4LAN_MISSION_PROTOCOL = {}
local missionProtocol = X4LAN_MISSION_PROTOCOL
local universeProtocol = X4LAN_UNIVERSE_PROTOCOL
missionProtocol.limit = 256
missionProtocol.partSize = 500
missionProtocol.maxParts = 24
missionProtocol.transitions = {
    O = { O = true, A = true, C = true, F = true, X = true, W = true },
    A = {
        A = true,
        C = true,
        F = true,
        X = true,
    },
}
function missionProtocol.encode(text)
    return (
        text:gsub('.', function(character)
            return string.format('%02x', character:byte())
        end)
    )
end

local function utf8(text)
    local i = 1
    while i <= #text do
        local firstByte = text:byte(i)
        local length = firstByte < 128 and 1
            or firstByte >= 194 and firstByte <= 223 and 2
            or firstByte >= 224 and firstByte <= 239 and 3
            or firstByte >= 240 and firstByte <= 244 and 4
        if not length or i + length - 1 > #text then
            return false
        end
        for j = 1, length - 1 do
            local continuation = text:byte(i + j)
            if continuation < 128 or continuation > 191 then
                return false
            end
        end
        local continuation = text:byte(i + 1)
        if
            length == 3
                and (firstByte == 224 and continuation < 160 or firstByte == 237 and continuation > 159)
            or length == 4
                and (firstByte == 240 and continuation < 144 or firstByte == 244 and continuation > 143)
        then
            return false
        end
        i = i + length
    end
    return true
end

function missionProtocol.text(encoded, max)
    if
        type(encoded) ~= 'string'
        or #encoded == 0
        or #encoded > max * 2
        or #encoded % 2 ~= 0
        or encoded:find('[^0-9a-f]')
    then
        return nil
    end
    local decoded = encoded:gsub('..', function(hexByte)
        return string.char(tonumber(hexByte, 16))
    end)
    if not utf8(decoded) or decoded:find('[%z\1-\8\11-\31\127]') then
        return nil
    end
    return decoded
end

function missionProtocol.parse(payload)
    local fields = universeProtocol.split(payload, ',')
    if
        #fields ~= 9
        or fields[1] ~= 'quota'
        or not ({ O = true, A = true, C = true, F = true, X = true, W = true })[fields[2]]
    then
        return nil
    end
    local done, total, reward =
        universeProtocol.integer(fields[3], 1000000),
        universeProtocol.integer(fields[4], 1000000),
        universeProtocol.integer(fields[5])
    if
        not done
        or not total
        or total < 1
        or done > total
        or not reward
        or #fields[6] > 64
        or not fields[6]:match('^[a-z][a-z0-9_]*$')
    then
        return nil
    end
    if fields[2] == 'O' and done ~= 0 or fields[2] == 'C' and done ~= total then
        return nil
    end
    local title, description, objective =
        missionProtocol.text(fields[7], 256),
        missionProtocol.text(fields[8], 4096),
        missionProtocol.text(fields[9], 512)
    if not title or not description or not objective then
        return nil
    end
    return {
        kind = fields[1],
        state = fields[2],
        done = done,
        total = total,
        reward = reward,
        faction = fields[6],
        title = title,
        description = description,
        objective = objective,
    }
end

function missionProtocol.advances(old, new)
    if not old then
        return true
    end
    if
        not missionProtocol.transitions[old.state]
        or not missionProtocol.transitions[old.state][new.state]
        or new.done < old.done
    then
        return false
    end
    for _, field in ipairs({
        'kind',
        'total',
        'reward',
        'faction',
        'title',
        'description',
        'objective',
    }) do
        if old[field] ~= new[field] then
            return false
        end
    end
    return true
end

function missionProtocol.assembly()
    local assembly = { records = {}, pending = {} }
    function assembly:add(fields, now)
        if
            #fields ~= 10
            or fields[1] ~= 'MSTATE'
            or not universeProtocol.hex(fields[4])
            or self.epoch and fields[4] ~= self.epoch
        then
            return nil
        end
        local id, revision, part, total, digest =
            universeProtocol.integer(fields[5], missionProtocol.limit),
            universeProtocol.integer(fields[6]),
            universeProtocol.integer(fields[7], missionProtocol.maxParts),
            universeProtocol.integer(fields[8], missionProtocol.maxParts),
            universeProtocol.integer(fields[9], 4294967295)
        if
            not id
            or id < 1
            or not revision
            or revision < 1
            or not part
            or not total
            or part < 1
            or part > total
            or not digest
            or #fields[10] < 1
            or #fields[10] > missionProtocol.partSize
            or fields[10]:find('[^a-zA-Z0-9_,]')
        then
            return nil
        end
        local old = self.records[id]
        if old and revision <= old.rev then
            return nil
        end
        local pending = self.pending[id]
        if pending and revision < pending.rev then
            return nil
        end
        if not pending or revision > pending.rev or now - pending.at >= 10 then
            pending = {
                epoch = fields[4],
                rev = revision,
                total = total,
                digest = digest,
                parts = {},
                at = now,
                got = 0,
            }
            self.pending[id] = pending
        end
        if
            pending.epoch ~= fields[4]
            or pending.total ~= total
            or pending.digest ~= digest
            or pending.parts[part] and pending.parts[part] ~= fields[10]
        then
            return nil
        end
        if not pending.parts[part] then
            pending.got = pending.got + 1
            pending.parts[part] = fields[10]
        end
        if pending.got ~= total then
            return nil
        end
        local payload = table.concat(pending.parts)
        if universeProtocol.checksum({ payload }) ~= digest then
            return nil
        end
        local row = missionProtocol.parse(payload)
        if not row or not missionProtocol.advances(old and old.row, row) then
            return nil
        end
        local record = {
            id = id,
            rev = revision,
            row = row,
            payload = payload,
            digest = digest,
            epoch = fields[4],
        }
        self.epoch = fields[4]
        self.records[id] = record
        self.pending[id] = nil
        return record
    end
    return assembly
end

function missionProtocol.packets(lease, epoch, record)
    local packets = {}
    local total = math.ceil(#record.payload / missionProtocol.partSize)
    for i = 1, total do
        packets[i] = table.concat({
            'MSTATE',
            1,
            lease,
            epoch,
            record.id,
            record.rev,
            i,
            total,
            string.format('%.0f', record.digest),
            record.payload:sub(
                (i - 1) * missionProtocol.partSize + 1,
                i * missionProtocol.partSize
            ),
        }, '|')
    end
    return packets
end
