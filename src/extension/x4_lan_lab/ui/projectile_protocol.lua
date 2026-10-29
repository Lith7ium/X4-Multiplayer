-- Strict BOLT/BEND codec for observed projectile visuals (reports/PROJECTILE-VISUAL-PROTOCOL.txt),
-- independent of X4. INERT and unused: nothing registers or loads it yet. Decoding checks syntax
-- and bounds only; it never establishes a hit, damage, pilot ownership, authority epoch or a live
-- HEALTH generation. Every message has an explicit direction: 'client' (client -> relay) or
-- 'forwarded' (relay -> other client, sourceLease right after the recipient lease, as in FIRE2).
-- Numbers serialize as '%.17g' (the same canonical text as x4lan/projectile_protocol.py).
X4LAN_PROJECTILE_PROTOCOL = {}
local protocol = X4LAN_PROJECTILE_PROTOCOL
protocol.MAX_PACKET, protocol.MAX_SEQ, protocol.MAX_TIME, protocol.MAX_POSITION, protocol.MAX_VELOCITY, protocol.MAX_ANGLE, protocol.MAX_TTL =
    768, 2147483647, 1e12, 50000000, 100000, 2 * math.pi, 3
protocol.KINDS = {
    'weapon_ter_s_plasma_01_mk1_macro',
    'weapon_gen_s_laser_01_mk1_macro',
    'weapon_gen_s_laser_01_mk2_macro',
}
local NUMBERS = {
    { 'x', protocol.MAX_POSITION },
    { 'y', protocol.MAX_POSITION },
    { 'z', protocol.MAX_POSITION },
    { 'vx', protocol.MAX_VELOCITY },
    { 'vy', protocol.MAX_VELOCITY },
    { 'vz', protocol.MAX_VELOCITY },
    { 'yaw', protocol.MAX_ANGLE },
    { 'pitch', protocol.MAX_ANGLE },
    { 'roll', protocol.MAX_ANGLE },
}
local function split(packet)
    local fields = {}
    for field in (packet .. '|'):gmatch('([^|]*)|') do
        fields[#fields + 1] = field
    end
    return fields
end
local function integer(text)
    if #text > 10 or not text:match('^[1-9]%d*$') then
        return nil
    end
    local value = tonumber(text)
    return value <= protocol.MAX_SEQ and value or nil
end
local function finite(value)
    return type(value) == 'number' and value == value and value ~= math.huge and value ~= -math.huge
end
local function number(text, limit)
    if
        not (
            text:match('^%-?%d+$')
            or text:match('^%-?%d+%.%d+$')
            or text:match('^%-?%d+[eE][%-+]?%d+$')
            or text:match('^%-?%d+%.%d+[eE][%-+]?%d+$')
        )
    then
        return nil
    end
    local value = tonumber(text)
    if not finite(value) or math.abs(value) > limit then
        return nil
    end
    return value
end
local function lease(value)
    return type(value) == 'string' and #value == 32 and value:match('^[a-f0-9]+$') ~= nil
end
local function whole(value)
    return finite(value) and value == math.floor(value) and value >= 1 and value <= protocol.MAX_SEQ
end

-- The same checks back decoding and serialization; returns the message or nil, reason.
local function check(message)
    if message.direction ~= 'client' and message.direction ~= 'forwarded' then
        return nil, 'invalid direction'
    end
    if message.type ~= 'BOLT' and message.type ~= 'BEND' then
        return nil, 'unknown message'
    end
    if not lease(message.lease) then
        return nil, 'invalid lease'
    end
    if message.direction == 'forwarded' and not lease(message.source_lease) then
        return nil, 'invalid lease'
    end
    if message.direction == 'client' and message.source_lease ~= nil then
        return nil, 'client form has no source lease'
    end
    for _, field in ipairs({ 'entity', 'epoch', 'gen', 'id' }) do
        if not whole(message[field]) then
            return nil, 'invalid ' .. field
        end
    end
    if message.entity ~= 1 and message.entity ~= 2 then
        return nil, 'invalid entity'
    end
    if not finite(message.time) or message.time < 0 or message.time > protocol.MAX_TIME then
        return nil, 'time out of bounds'
    end
    if message.type == 'BOLT' then
        if not whole(message.kind) or not protocol.KINDS[message.kind] then
            return nil, 'invalid kind'
        end
        if
            type(message.sector) ~= 'string'
            or #message.sector > 96
            or not message.sector:match('^[A-Za-z0-9_.%-]+$')
        then
            return nil, 'invalid sector'
        end
        for _, spec in ipairs(NUMBERS) do
            if not finite(message[spec[1]]) or math.abs(message[spec[1]]) > spec[2] then
                return nil, spec[1] .. ' out of bounds'
            end
        end
        if not finite(message.ttl) or message.ttl <= 0 or message.ttl > protocol.MAX_TTL then
            return nil, 'ttl out of bounds'
        end
    end
    return message
end

-- Decodes one BOLT/BEND packet of the stated direction: a named-field table, or nil, reason.
function protocol.parse(raw, direction)
    if direction ~= 'client' and direction ~= 'forwarded' then
        return nil, 'invalid direction'
    end
    if type(raw) ~= 'string' or #raw > protocol.MAX_PACKET or raw:find('[^\33-\126]') then
        return nil, 'invalid packet'
    end
    local fields = split(raw)
    local forwarded = direction == 'forwarded'
    local headerEnd = forwarded and 5 or 4
    if #fields < 2 or fields[2] ~= '1' or (fields[1] ~= 'BOLT' and fields[1] ~= 'BEND') then
        return nil, 'unknown message'
    end
    if #fields ~= (fields[1] == 'BOLT' and headerEnd + 16 or headerEnd + 4) then
        return nil, 'field count'
    end
    local message = {
        type = fields[1],
        direction = direction,
        lease = fields[3],
        source_lease = forwarded and fields[4] or nil,
        entity = integer(fields[headerEnd]),
        epoch = integer(fields[headerEnd + 1]),
        gen = integer(fields[headerEnd + 2]),
        id = integer(fields[headerEnd + 3]),
        time = number(fields[headerEnd + 4], protocol.MAX_TIME),
    }
    if message.type == 'BOLT' then
        message.sector, message.kind = fields[headerEnd + 5], integer(fields[headerEnd + 6])
        for i, spec in ipairs(NUMBERS) do
            message[spec[1]] = number(fields[headerEnd + 6 + i], spec[2])
        end
        message.ttl = number(fields[headerEnd + 16], protocol.MAX_TTL)
    end
    return check(message)
end

-- Encodes a message in its own direction: the packet, or nil, reason.
function protocol.serialize(message)
    if type(message) ~= 'table' then
        return nil, 'not a projectile message'
    end
    local ok, why = check(message)
    if not ok then
        return nil, why
    end
    local fields = { message.type, '1', message.lease }
    if message.direction == 'forwarded' then
        fields[#fields + 1] = message.source_lease
    end
    for _, field in ipairs({ 'entity', 'epoch', 'gen', 'id' }) do
        fields[#fields + 1] = string.format('%d', message[field])
    end
    fields[#fields + 1] = string.format('%.17g', message.time)
    if message.type == 'BOLT' then
        fields[#fields + 1] = message.sector
        fields[#fields + 1] = string.format('%d', message.kind)
        for _, spec in ipairs(NUMBERS) do
            fields[#fields + 1] = string.format('%.17g', message[spec[1]])
        end
        fields[#fields + 1] = string.format('%.17g', message.ttl)
    end
    local raw = table.concat(fields, '|')
    if #raw > protocol.MAX_PACKET then
        return nil, 'packet too long'
    end
    return raw
end
