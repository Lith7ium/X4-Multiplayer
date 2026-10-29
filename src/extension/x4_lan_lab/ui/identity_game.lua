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
local s = api.state
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
local function log(v)
    DebugError('[X4LAN] IDENTITY ' .. v)
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
    local k = key(object)
    local old = identity.bound[k]
    if old and old.text == code and old.name == name then
        return true
    end
    identity.bound[k] =
        { object = object, code = ffi.new('char[?]', #code + 1, code), text = code, name = name }
    AddUITriggeredEvent('X4LAN_Universe', 'Describe', { object = api.id(object), name = name })
    return true
end
function identity.unbind(object)
    local k = key(object)
    local old = identity.bound[k]
    if old and old.forced and engine.IsComponentOperational(object) then
        engine.SetObjectForcedRadarVisible(object, false)
    end
    identity.bound[k] = nil
end
local function encode(text)
    return (text:gsub('.', function(c)
        return string.format('%02x', c:byte())
    end))
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
    return (text:gsub('..', function(c)
        return string.char(tonumber(c, 16))
    end))
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
    local b = identity.bound[key(object)]
    b.at = row.at
    if row.known and not b.known then
        AddUITriggeredEvent('X4LAN_Universe', 'Known', api.id(object))
        b.known = true
    end
    if b.forced ~= row.contact then
        engine.SetObjectForcedRadarVisible(object, row.contact)
        b.forced = row.contact
    end
end
local receive = universe.receive
function universe.receive(f, now)
    if f[1] ~= 'IDENT' then
        return receive(f, now)
    end
    if cfg.slot == 'p1' or #f ~= 11 or not universe.latest or f[4] ~= universe.latest.epoch then
        return true
    end
    local seq, id = protocol.integer(f[5]), protocol.integer(f[7])
    local scope = f[6]
    if
        not seq
        or not id
        or id == 0
        or (scope ~= 'P' and scope ~= 'N' and scope ~= 'F')
        or (scope == 'P' and id > (api.coop and api.coop.count or 2))
        or (scope == 'F' and id > 4)
        or not f[8]:match('^[A-Z0-9-]+$')
        or #f[8] > 32
        or (f[10] ~= '0' and f[10] ~= '1')
        or (f[11] ~= '0' and f[11] ~= '1')
    then
        return true
    end
    local name = decode(f[9])
    if not name or name:find('[%z\1-\31\127]') then
        return true
    end
    local k = scope .. id
    local old = identity.rows[k]
    if old and old.epoch == f[4] and seq <= old.seq then
        return true
    end
    identity.rows[k] = {
        epoch = f[4],
        scope = scope,
        id = id,
        seq = seq,
        code = f[8],
        name = name,
        known = f[10] == '1',
        contact = f[11] == '1',
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
                local p = engine.GetObjectPositionInSector(ship)
                list[#list + 1] = {
                    sector = engine.GetContextByClass(ship, 'sector', false),
                    p = p,
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
    local p = engine.GetObjectPositionInSector(object)
    if visible then
        for _, r in ipairs(radars) do
            if
                r.sector == sector
                and (p.x - r.p.x) ^ 2 + (p.y - r.p.y) ^ 2 + (p.z - r.p.z) ^ 2 <= r.range ^ 2
            then
                contact = true
                break
            end
        end
    end
    local k = entry.scope .. entry.id
    local last = identity.sent[k]
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
    identity.sent[k] = { code = code, name = name, flags = flags, at = now }
    identity.seq = identity.seq + 1
    s.transport:send(table.concat({
        'IDENT',
        '1',
        s.client.lease,
        s.client.lease,
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
        or not s.client
        or not s.client.lease
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
            for k, v in pairs(identity.sent) do
                if now - v.at > 10 then
                    identity.sent[k] = nil
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
        for _, e in ipairs(universe.publish) do
            list[#list + 1] = { scope = 'N', id = e.id, object = e.object }
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
        for k, row in pairs(identity.rows) do
            local r = row.scope == 'N' and universe.replicas[row.id] or nil
            local object = row.scope == 'P' and api.coop.ships[row.id]
                or row.scope == 'F' and fixture(row.id)
                or r and r.object
            if
                universe.latest
                and row.epoch == universe.latest.epoch
                and object
                and engine.IsComponentOperational(object)
                and now - row.at < identity.FRESH
            then
                observe(object, row)
            elseif now - row.at > 15 then
                identity.rows[k] = nil
            end
        end
        for k, b in pairs(identity.bound) do
            if not engine.IsComponentOperational(b.object) then
                identity.bound[k] = nil
            elseif b.forced and now - (b.at or 0) > identity.FRESH then
                engine.SetObjectForcedRadarVisible(b.object, false)
                b.forced = false
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
