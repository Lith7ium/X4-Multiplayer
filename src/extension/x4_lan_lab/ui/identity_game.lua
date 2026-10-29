-- Host display codes/names and shared sensor contacts. Native engine IDs stay local.
local api = X4LAN_BUBBLE_API
local cfg = X4LAN_CONFIG
if not api or not api.universe or not cfg.world_sync then
    return
end
local ffi = require('ffi')
local engine = ffi.C
local universe = api.universe
local protocol = X4LAN_UNIVERSE_PROTOCOL
local state = api.state
ffi.cdef([[const char* GetObjectIDCode(UniverseID);const char* GetComponentName(UniverseID);
void SetObjectForcedRadarVisible(UniverseID,bool);]])
local identity = { bound = {}, rows = {}, fixtures = {}, seq = 0, cursor = 1, sent = {} }
api.identity = identity
-- Host cadence: every entity is still read every 0.4 s (a bounded slice per 0.1 s), but
-- IDENT is sent only when its code, name, known or contact flag changes, and otherwise
-- repeated with a new sequence every KEEPALIVE seconds; the relay forwards new sequences
-- only. Guests keep an identity for FRESH seconds after its newest sequence.
identity.KEEPALIVE = 1.2
identity.FRESH = 4
local function key(object)
    return tostring(object):gsub('ULL$', '')
end

local function log(text)
    DebugError('[X4LAN] IDENTITY ' .. text)
end
local nativeCode = engine.GetObjectIDCode
function identity.display(object)
    local entry = identity.bound[key(object)]
    return entry and entry.code or nativeCode(object)
end

-- X4 menus retain ffi.C itself, not individual GetObjectIDCode functions. A narrow
-- lookup override reaches those callers while preserving all unrelated C lookups.
-- Each char array remains owned by I.bound for its full native component lifetime.
if cfg.slot ~= 'p1' then
    local mt = getmetatable(engine)
    local lookup = type(mt) == 'table' and mt.__index
    if type(lookup) == 'function' then
        mt.__index = function(lib, name)
            if lib == engine and name == 'GetObjectIDCode' then
                return identity.display
            end
            return lookup(lib, name)
        end
        identity.hooked = true
    else
        log('DISPLAY_HOOK_UNAVAILABLE; native codes remain visible')
    end
end

function identity.bind(object, code, name)
    if
        not object
        or object == 0
        or type(code) ~= 'string'
        or #code > 32
        or not code:match('^[A-Z0-9-]+$')
        or type(name) ~= 'string'
        or #name == 0
        or #name > 160
        or name:find('[%z\1-\31\127]')
    then
        return false
    end
    local objectKey = key(object)
    local old = identity.bound[objectKey]
    if old and old.text == code and old.name == name then
        return true
    end
    identity.bound[objectKey] =
        { object = object, code = ffi.new('char[?]', #code + 1, code), text = code, name = name }
    AddUITriggeredEvent('X4LAN_Universe', 'Describe', { object = api.id(object), name = name })
    return true
end

function identity.unbind(object)
    local objectKey = key(object)
    local old = identity.bound[objectKey]
    if old and old.forced and engine.IsComponentOperational(object) then
        engine.SetObjectForcedRadarVisible(object, false)
    end
    identity.bound[objectKey] = nil
end

local function encode(text)
    return (
        text:gsub('.', function(character)
            return string.format('%02x', character:byte())
        end)
    )
end

local function decode(text)
    if
        type(text) ~= 'string'
        or #text == 0
        or #text > 320
        or #text % 2 ~= 0
        or text:find('[^0-9a-f]')
    then
        return nil
    end
    return (
        text:gsub('..', function(hexByte)
            return string.char(tonumber(hexByte, 16))
        end)
    )
end

local function fixture(id)
    if id <= 2 then
        return identity.fixtures[id]
    end
    local trade = api.trade and api.trade.state
    local value = trade and trade.markets and trade.markets[id - 2]
    return value and ConvertIDTo64Bit(api.id(value)) or nil
end
for i = 1, 2 do
    RegisterEvent('X4LAN_Universe.Target' .. i, function(_, object)
        identity.fixtures[i] = ConvertIDTo64Bit(object)
    end)
end

local function observe(object, row)
    if not identity.bind(object, row.code, row.name) then
        return
    end
    local binding = identity.bound[key(object)]
    binding.at = row.at
    if row.known and not binding.known then
        AddUITriggeredEvent('X4LAN_Universe', 'Known', api.id(object))
        binding.known = true
    end
    if binding.forced ~= row.contact then
        engine.SetObjectForcedRadarVisible(object, row.contact)
        binding.forced = row.contact
    end
end
local receive = universe.receive
function universe.receive(fields, now)
    if fields[1] ~= 'IDENT' then
        return receive(fields, now)
    end
    if
        cfg.slot == 'p1'
        or #fields ~= 11
        or not universe.latest
        or fields[4] ~= universe.latest.epoch
    then
        return true
    end
    local sequence, id = protocol.integer(fields[5]), protocol.integer(fields[7])
    local scope = fields[6]
    if
        not sequence
        or not id
        or id == 0
        or (scope ~= 'P' and scope ~= 'N' and scope ~= 'F')
        or (scope == 'P' and id > (api.coop and api.coop.count or 2))
        or (scope == 'F' and id > 4)
        or not fields[8]:match('^[A-Z0-9-]+$')
        or #fields[8] > 32
        or (fields[10] ~= '0' and fields[10] ~= '1')
        or (fields[11] ~= '0' and fields[11] ~= '1')
    then
        return true
    end
    local name = decode(fields[9])
    if not name or name:find('[%z\1-\31\127]') then
        return true
    end
    local rowKey = scope .. id
    local old = identity.rows[rowKey]
    if old and old.epoch == fields[4] and sequence <= old.seq then
        return true
    end
    identity.rows[rowKey] = {
        epoch = fields[4],
        scope = scope,
        id = id,
        seq = sequence,
        code = fields[8],
        name = name,
        known = fields[10] == '1',
        contact = fields[11] == '1',
        at = now,
    }
    return true
end

local function sensors()
    local list = {}
    for _, ship in ipairs(api.coop.ships) do
        if engine.IsComponentOperational(ship) then
            local range = GetComponentData(api.id(ship), 'maxradarrange')
            if type(range) == 'number' and range > 0 then
                local position = engine.GetObjectPositionInSector(ship)
                list[#list + 1] = {
                    sector = engine.GetContextByClass(ship, 'sector', false),
                    p = position,
                    range = range,
                }
            end
        end
    end
    return list
end

local function publish(entry, radars)
    local object = entry.object
    if not engine.IsComponentOperational(object) then
        return
    end
    local code = ffi.string(nativeCode(object))
    if #code == 0 or #code > 32 or not code:match('^[A-Z0-9-]+$') then
        return
    end
    local name = ffi.string(engine.GetComponentName(object)):gsub('[%z\1-\31\127]', '')
    if #name == 0 or #name > 160 then
        name = code
    end
    local known, visible = GetComponentData(api.id(object), 'isknown', 'isradarvisible')
    local contact = false
    local sector = engine.GetContextByClass(object, 'sector', false)
    local position = engine.GetObjectPositionInSector(object)
    if visible then
        for _, radar in ipairs(radars) do
            if
                radar.sector == sector
                and (position.x - radar.p.x) ^ 2 + (position.y - radar.p.y) ^ 2 + (position.z - radar.p.z) ^ 2
                    <= radar.range ^ 2
            then
                contact = true
                break
            end
        end
    end
    local rowKey = entry.scope .. entry.id
    local last = identity.sent[rowKey]
    local now = identity.now or 0
    local flags = ((known or contact) and '1' or '0') .. (contact and '1' or '0')
    if
        last
        and last.code == code
        and last.name == name
        and last.flags == flags
        and now - last.at < identity.KEEPALIVE
    then
        return
    end
    identity.sent[rowKey] = { code = code, name = name, flags = flags, at = now }
    identity.seq = identity.seq + 1
    state.transport:send(table.concat({
        'IDENT',
        '1',
        state.client.lease,
        state.client.lease,
        identity.seq,
        entry.scope,
        entry.id,
        code,
        encode(name),
        flags:sub(1, 1),
        flags:sub(2, 2),
    }, '|'))
end

local function frame(now)
    if
        not universe.ready
        or not api.coop.ready
        or not state.client
        or not state.client.lease
        or api.paused()
    then
        return
    end
    if cfg.slot == 'p1' then
        if now < (identity.nextSend or 0) then
            return
        end
        identity.nextSend = now + 0.1
        identity.now = now
        if now >= (identity.nextPrune or 0) then
            identity.nextPrune = now + 10
            for rowKey, sent in pairs(identity.sent) do
                if now - sent.at > 10 then
                    identity.sent[rowKey] = nil
                end
            end
        end
        local radars = sensors()
        local list = {}
        for i, object in ipairs(api.coop.ships) do
            list[#list + 1] = { scope = 'P', id = i, object = object }
        end
        for i = 1, 4 do
            local object = fixture(i)
            if object then
                list[#list + 1] = { scope = 'F', id = i, object = object }
            end
        end
        for _, entity in ipairs(universe.publish) do
            list[#list + 1] = { scope = 'N', id = entity.id, object = entity.object }
        end
        if #list == 0 then
            return
        end
        identity.cursor = (identity.cursor - 1) % #list + 1
        for _ = 1, math.min(#list, math.min(256, math.max(32, math.ceil(#list / 10)))) do
            publish(list[identity.cursor], radars)
            identity.cursor = identity.cursor % #list + 1
        end
    else
        for rowKey, row in pairs(identity.rows) do
            local replica = row.scope == 'N' and universe.replicas[row.id] or nil
            local object = row.scope == 'P' and api.coop.ships[row.id]
                or row.scope == 'F' and fixture(row.id)
                or replica and replica.object
            if
                universe.latest
                and row.epoch == universe.latest.epoch
                and object
                and engine.IsComponentOperational(object)
                and now - row.at < identity.FRESH
            then
                observe(object, row)
            elseif now - row.at > 15 then
                identity.rows[rowKey] = nil
            end
        end
        for rowKey, binding in pairs(identity.bound) do
            if not engine.IsComponentOperational(binding.object) then
                identity.bound[rowKey] = nil
            elseif binding.forced and now - (binding.at or 0) > identity.FRESH then
                engine.SetObjectForcedRadarVisible(binding.object, false)
                binding.forced = false
            end
        end
    end
end
local original = universe.frame
function universe.frame(now)
    original(now)
    if identity.failed then
        return
    end
    local ok, err = pcall(frame, now)
    if not ok then
        identity.failed = true
        log('STOPPED ' .. tostring(err))
    end
end
