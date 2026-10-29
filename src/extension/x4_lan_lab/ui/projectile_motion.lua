-- Unregistered pure observed-projectile buffer. No engine, transport or damage calls.
-- Caller admits authority/sector/clock first. All times use the NAV2 source clock;
-- sample receives its delayed render cursor, not the packet arrival clock.
X4LAN_PROJECTILE_MOTION = { capacity = 256, window = 2048 }
local motion = X4LAN_PROJECTILE_MOTION
local function finite(n, limit)
    return type(n) == 'number' and n == n and math.abs(n) <= limit
end
local function integer(n)
    return finite(n, 2147483647) and n >= 1 and n % 1 == 0
end
local function vector(v, n, limit)
    if type(v) ~= 'table' or #v ~= n then
        return false
    end
    for i = 1, n do
        if not finite(v[i], limit) then
            return false
        end
    end
    return true
end
local function copy(v)
    local out = {}
    for i = 1, #v do
        out[i] = v[i]
    end
    return out
end
function motion.new(key, cutoff)
    assert(type(key) == 'string' and #key > 0 and finite(cutoff or 0, 1e12))
    return { key = key, cutoff = cutoff or 0, latest = 0, seen = {}, active = {}, count = 0 }
end
local function remove(b, id)
    if b.active[id] then
        b.active[id] = nil
        b.count = b.count - 1
    end
end
-- Keep replay rejection bounded even after a long session. IDs below the window
-- remain rejected without retaining a tombstone for every historical projectile.
local function mark(b, id)
    if not integer(id) then
        return false
    end
    if b.latest > 0 and id > b.latest + motion.window then
        return false
    end
    if id <= b.latest - motion.window or b.seen[id] then
        return false
    end
    if id > b.latest then
        b.latest = id
        for old in pairs(b.seen) do
            if old <= id - motion.window then
                b.seen[old] = nil
                remove(b, old)
            end
        end
    end
    b.seen[id] = true
    return true
end
local function purge(b, renderNow)
    for id, r in pairs(b.active) do
        if renderNow >= math.min(r.time + r.ttl, r.ended or math.huge) then
            remove(b, id)
        end
    end
end
-- Admission uses the UNDELAYED NAV2 source estimate. renderNow may be delayed;
-- purge against that cursor so the last interpolation tail is not removed early.
function motion.birth(b, key, r, sourceNow, renderNow)
    if
        key ~= b.key
        or type(r) ~= 'table'
        or not finite(sourceNow, 1e12)
        or not finite(r.time, 1e12)
        or r.time < 0
        or r.time <= b.cutoff
        or not integer(r.kind)
        or r.kind > 3
        or not finite(r.ttl, 3)
        or r.ttl <= 0
        or not vector(r.pose, 6, 50000000)
        or not vector(r.velocity, 3, 100000)
    then
        return false
    end
    for i = 4, 6 do
        if math.abs(r.pose[i]) > 2 * math.pi then
            return false
        end
    end
    if renderNow == nil then
        renderNow = sourceNow
    end
    if not finite(renderNow, 1e12) or renderNow > sourceNow or r.time > sourceNow + 0.35 then
        return false
    end
    purge(b, renderNow)
    if not mark(b, r.id) then
        return false
    end
    -- Remember refused/expired births too: a later duplicate cannot resurrect them.
    if sourceNow >= r.time + r.ttl or b.count >= motion.capacity then
        return false
    end
    b.active[r.id] = {
        id = r.id,
        time = r.time,
        ttl = r.ttl,
        pose = copy(r.pose),
        velocity = copy(r.velocity),
        kind = r.kind,
    }
    b.count = b.count + 1
    return true
end
function motion.finish(b, key, id, time)
    if key ~= b.key or not integer(id) or not finite(time, 1e12) or time < 0 then
        return false
    end
    local r = b.active[id]
    if r then
        if time < r.time then
            return false
        end
        -- An end is immutable too; no repeated end may extend its visible lifetime.
        r.ended = math.min(r.ended or math.huge, time)
        return true
    end
    return mark(b, id) -- End-before-birth tombstone, including after expiry.
end
function motion.clear(b, sourceNow)
    assert(finite(sourceNow, 1e12))
    b.active = {}
    b.count = 0
    b.cutoff = math.max(b.cutoff, sourceNow)
    -- Retain seen IDs and watermark until the caller binds a different authority key.
end
function motion.sample(b, sourceNow)
    assert(finite(sourceNow, 1e12))
    local out = {}
    for id, r in pairs(b.active) do
        if sourceNow >= math.min(r.time + r.ttl, r.ended or math.huge) then
            remove(b, id)
        elseif sourceNow >= r.time then
            local p = copy(r.pose)
            local valid = true
            for i = 1, 3 do
                p[i] = p[i] + r.velocity[i] * (sourceNow - r.time)
                if math.abs(p[i]) > 50000000 then
                    valid = false
                end
            end
            if valid then
                out[id] = { pose = p, kind = r.kind }
            else
                remove(b, id)
            end
        end
    end
    return out
end
-- Actual native position differences retain inherited ship motion, slew and spread.
-- Do not substitute ship forward direction or a selected target for this velocity.
function motion.velocity(a, b)
    if
        type(a) ~= 'table'
        or type(b) ~= 'table'
        or not finite(a.time, 1e12)
        or not finite(b.time, 1e12)
        or not vector(a.pose, 6, 50000000)
        or not vector(b.pose, 6, 50000000)
    then
        return nil
    end
    local dt = b.time - a.time
    if dt < 0.02 or dt > 0.2 then
        return nil
    end
    local v = {}
    for i = 1, 3 do
        v[i] = (b.pose[i] - a.pose[i]) / dt
        if not finite(v[i], 100000) then
            return nil
        end
    end
    return v
end
