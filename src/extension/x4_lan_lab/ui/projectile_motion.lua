-- Unregistered pure observed-projectile buffer. No engine, transport or damage calls.
-- Caller admits authority/sector/clock first. All times use the NAV2 source clock;
-- sample receives its delayed render cursor, not the packet arrival clock.
X4LAN_PROJECTILE_MOTION = { capacity = 256, window = 2048 }
local motion = X4LAN_PROJECTILE_MOTION
local function finite(value, limit)
    return type(value) == 'number' and value == value and math.abs(value) <= limit
end

local function integer(value)
    return finite(value, 2147483647) and value >= 1 and value % 1 == 0
end

local function vector(values, length, limit)
    if type(values) ~= 'table' or #values ~= length then
        return false
    end
    for i = 1, length do
        if not finite(values[i], limit) then
            return false
        end
    end
    return true
end

local function copy(values)
    local result = {}
    for i = 1, #values do
        result[i] = values[i]
    end
    return result
end

function motion.new(key, cutoff)
    assert(type(key) == 'string' and #key > 0 and finite(cutoff or 0, 1e12))
    return { key = key, cutoff = cutoff or 0, latest = 0, seen = {}, active = {}, count = 0 }
end

local function remove(buffer, id)
    if buffer.active[id] then
        buffer.active[id] = nil
        buffer.count = buffer.count - 1
    end
end

-- Keep replay rejection bounded even after a long session. IDs below the window
-- remain rejected without retaining a tombstone for every historical projectile.
local function mark(buffer, id)
    if not integer(id) then
        return false
    end
    if buffer.latest > 0 and id > buffer.latest + motion.window then
        return false
    end
    if id <= buffer.latest - motion.window or buffer.seen[id] then
        return false
    end
    if id > buffer.latest then
        buffer.latest = id
        for old in pairs(buffer.seen) do
            if old <= id - motion.window then
                buffer.seen[old] = nil
                remove(buffer, old)
            end
        end
    end
    buffer.seen[id] = true
    return true
end

local function purge(buffer, renderNow)
    for id, projectile in pairs(buffer.active) do
        if
            renderNow >= math.min(projectile.time + projectile.ttl, projectile.ended or math.huge)
        then
            remove(buffer, id)
        end
    end
end

-- Admission uses the UNDELAYED NAV2 source estimate. renderNow may be delayed;
-- purge against that cursor so the last interpolation tail is not removed early.
function motion.birth(buffer, key, projectile, sourceNow, renderNow)
    if
        key ~= buffer.key
        or type(projectile) ~= 'table'
        or not finite(sourceNow, 1e12)
        or not finite(projectile.time, 1e12)
        or projectile.time < 0
        or projectile.time <= buffer.cutoff
        or not integer(projectile.kind)
        or projectile.kind > 3
        or not finite(projectile.ttl, 3)
        or projectile.ttl <= 0
        or not vector(projectile.pose, 6, 50000000)
        or not vector(projectile.velocity, 3, 100000)
    then
        return false
    end
    for i = 4, 6 do
        if math.abs(projectile.pose[i]) > 2 * math.pi then
            return false
        end
    end
    if renderNow == nil then
        renderNow = sourceNow
    end
    if
        not finite(renderNow, 1e12)
        or renderNow > sourceNow
        or projectile.time > sourceNow + 0.35
    then
        return false
    end
    purge(buffer, renderNow)
    if not mark(buffer, projectile.id) then
        return false
    end
    -- Remember refused/expired births too: a later duplicate cannot resurrect them.
    if sourceNow >= projectile.time + projectile.ttl or buffer.count >= motion.capacity then
        return false
    end
    buffer.active[projectile.id] = {
        id = projectile.id,
        time = projectile.time,
        ttl = projectile.ttl,
        pose = copy(projectile.pose),
        velocity = copy(projectile.velocity),
        kind = projectile.kind,
    }
    buffer.count = buffer.count + 1
    return true
end

function motion.finish(buffer, key, id, time)
    if key ~= buffer.key or not integer(id) or not finite(time, 1e12) or time < 0 then
        return false
    end
    local projectile = buffer.active[id]
    if projectile then
        if time < projectile.time then
            return false
        end
        -- An end is immutable too; no repeated end may extend its visible lifetime.
        projectile.ended = math.min(projectile.ended or math.huge, time)
        return true
    end
    return mark(buffer, id) -- End-before-birth tombstone, including after expiry.
end

function motion.clear(buffer, sourceNow)
    assert(finite(sourceNow, 1e12))
    buffer.active = {}
    buffer.count = 0
    buffer.cutoff = math.max(buffer.cutoff, sourceNow)
    -- Retain seen IDs and watermark until the caller binds a different authority key.
end

function motion.sample(buffer, sourceNow)
    assert(finite(sourceNow, 1e12))
    local samples = {}
    for id, projectile in pairs(buffer.active) do
        if
            sourceNow >= math.min(projectile.time + projectile.ttl, projectile.ended or math.huge)
        then
            remove(buffer, id)
        elseif sourceNow >= projectile.time then
            local pose = copy(projectile.pose)
            local valid = true
            for i = 1, 3 do
                pose[i] = pose[i] + projectile.velocity[i] * (sourceNow - projectile.time)
                if math.abs(pose[i]) > 50000000 then
                    valid = false
                end
            end
            if valid then
                samples[id] = { pose = pose, kind = projectile.kind }
            else
                remove(buffer, id)
            end
        end
    end
    return samples
end

-- Actual native position differences retain inherited ship motion, slew and spread.
-- Do not substitute ship forward direction or a selected target for this velocity.
function motion.velocity(earlier, later)
    if
        type(earlier) ~= 'table'
        or type(later) ~= 'table'
        or not finite(earlier.time, 1e12)
        or not finite(later.time, 1e12)
        or not vector(earlier.pose, 6, 50000000)
        or not vector(later.pose, 6, 50000000)
    then
        return nil
    end
    local elapsed = later.time - earlier.time
    if elapsed < 0.02 or elapsed > 0.2 then
        return nil
    end
    local velocity = {}
    for i = 1, 3 do
        velocity[i] = (later.pose[i] - earlier.pose[i]) / elapsed
        if not finite(velocity[i], 100000) then
            return nil
        end
    end
    return velocity
end
