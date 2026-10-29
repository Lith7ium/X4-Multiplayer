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
local function split(s)
    local out = {}
    for v in (s .. '|'):gmatch('([^|]*)|') do
        out[#out + 1] = v
    end
    return out
end
local function integer(s)
    if #s > 10 or not s:match('^[1-9]%d*$') then
        return nil
    end
    local n = tonumber(s)
    return n <= protocol.MAX_SEQ and n or nil
end
local function finite(n)
    return type(n) == 'number' and n == n and n ~= math.huge and n ~= -math.huge
end
local function number(s, limit)
    if
        not (
            s:match('^%-?%d+$')
            or s:match('^%-?%d+%.%d+$')
            or s:match('^%-?%d+[eE][%-+]?%d+$')
            or s:match('^%-?%d+%.%d+[eE][%-+]?%d+$')
        )
    then
        return nil
    end
    local n = tonumber(s)
    if not finite(n) or math.abs(n) > limit then
        return nil
    end
    return n
end
local function lease(s)
    return type(s) == 'string' and #s == 32 and s:match('^[a-f0-9]+$') ~= nil
end
local function whole(n)
    return finite(n) and n == math.floor(n) and n >= 1 and n <= protocol.MAX_SEQ
end

-- The same checks back decoding and serialization; returns the message or nil, reason.
local function check(m)
    if m.direction ~= 'client' and m.direction ~= 'forwarded' then
        return nil, 'invalid direction'
    end
    if m.type ~= 'BOLT' and m.type ~= 'BEND' then
        return nil, 'unknown message'
    end
    if not lease(m.lease) then
        return nil, 'invalid lease'
    end
    if m.direction == 'forwarded' and not lease(m.source_lease) then
        return nil, 'invalid lease'
    end
    if m.direction == 'client' and m.source_lease ~= nil then
        return nil, 'client form has no source lease'
    end
    for _, k in ipairs({ 'entity', 'epoch', 'gen', 'id' }) do
        if not whole(m[k]) then
            return nil, 'invalid ' .. k
        end
    end
    if m.entity ~= 1 and m.entity ~= 2 then
        return nil, 'invalid entity'
    end
    if not finite(m.time) or m.time < 0 or m.time > protocol.MAX_TIME then
        return nil, 'time out of bounds'
    end
    if m.type == 'BOLT' then
        if not whole(m.kind) or not protocol.KINDS[m.kind] then
            return nil, 'invalid kind'
        end
        if
            type(m.sector) ~= 'string'
            or #m.sector > 96
            or not m.sector:match('^[A-Za-z0-9_.%-]+$')
        then
            return nil, 'invalid sector'
        end
        for _, n in ipairs(NUMBERS) do
            if not finite(m[n[1]]) or math.abs(m[n[1]]) > n[2] then
                return nil, n[1] .. ' out of bounds'
            end
        end
        if not finite(m.ttl) or m.ttl <= 0 or m.ttl > protocol.MAX_TTL then
            return nil, 'ttl out of bounds'
        end
    end
    return m
end

-- Decodes one BOLT/BEND packet of the stated direction: a named-field table, or nil, reason.
function protocol.parse(raw, direction)
    if direction ~= 'client' and direction ~= 'forwarded' then
        return nil, 'invalid direction'
    end
    if type(raw) ~= 'string' or #raw > protocol.MAX_PACKET or raw:find('[^\33-\126]') then
        return nil, 'invalid packet'
    end
    local f = split(raw)
    local forwarded = direction == 'forwarded'
    local h = forwarded and 5 or 4
    if #f < 2 or f[2] ~= '1' or (f[1] ~= 'BOLT' and f[1] ~= 'BEND') then
        return nil, 'unknown message'
    end
    if #f ~= (f[1] == 'BOLT' and h + 16 or h + 4) then
        return nil, 'field count'
    end
    local m = {
        type = f[1],
        direction = direction,
        lease = f[3],
        source_lease = forwarded and f[4] or nil,
        entity = integer(f[h]),
        epoch = integer(f[h + 1]),
        gen = integer(f[h + 2]),
        id = integer(f[h + 3]),
        time = number(f[h + 4], protocol.MAX_TIME),
    }
    if m.type == 'BOLT' then
        m.sector, m.kind = f[h + 5], integer(f[h + 6])
        for i, n in ipairs(NUMBERS) do
            m[n[1]] = number(f[h + 6 + i], n[2])
        end
        m.ttl = number(f[h + 16], protocol.MAX_TTL)
    end
    return check(m)
end

-- Encodes a message in its own direction: the packet, or nil, reason.
function protocol.serialize(m)
    if type(m) ~= 'table' then
        return nil, 'not a projectile message'
    end
    local ok, why = check(m)
    if not ok then
        return nil, why
    end
    local f = { m.type, '1', m.lease }
    if m.direction == 'forwarded' then
        f[#f + 1] = m.source_lease
    end
    for _, k in ipairs({ 'entity', 'epoch', 'gen', 'id' }) do
        f[#f + 1] = string.format('%d', m[k])
    end
    f[#f + 1] = string.format('%.17g', m.time)
    if m.type == 'BOLT' then
        f[#f + 1] = m.sector
        f[#f + 1] = string.format('%d', m.kind)
        for _, n in ipairs(NUMBERS) do
            f[#f + 1] = string.format('%.17g', m[n[1]])
        end
        f[#f + 1] = string.format('%.17g', m.ttl)
    end
    local raw = table.concat(f, '|')
    if #raw > protocol.MAX_PACKET then
        return nil, 'packet too long'
    end
    return raw
end
