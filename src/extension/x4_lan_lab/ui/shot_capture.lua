-- SHOT capture and undo state (md/x4lan_shot_capture.xml), dev scope, INERT: shot_capture=false,
-- and it needs shot_claims (shot_game.lua) and pilot_health. MD does the per-callback work; this
-- module only tells MD, under the HEALTH readback window, which co-op ship I pilot, which ones the
-- other player flies here (their shots are replays), whether the diagnostic undo is on, and a
-- conservative in-flight quarantine: the ship's NATIVE runtime laser range
-- (maxcombatrange.lasers.all, reported by MD) over the admitted weapon's library bulletspeed,
-- times a margin; nothing is assumed, a missing value refuses capture. MD's native cumulative
-- counts feed shot_game.lua as totals, never as increments.
local API = X4LAN_BUBBLE_API
local cfg = X4LAN_CONFIG
if
    not cfg
    or not cfg.coop
    or not cfg.shot_capture
    or not cfg.shot_claims
    or not cfg.pilot_health
    or not API
    or not API.shot
    or not API.health
    or not API.coop
then
    return
end
local coop = API.coop
local state = API.state
local health = API.health
local shot = API.shot
local capture = { revision = 0, range = {} }
API.shotCapture = capture
-- NPC capture needs room for a State rejected at a wall-second rollover. With
-- .25s refresh a missed request leaves up to .5s between accepted states, beyond
-- MD's .3s permission window. Keep that window strict and retry more frequently.
local INTERVAL = cfg.npc_combat and 0.1 or 0.25
local READING = 0.5
local MAXLIFE = 30
local MARGIN = 1.5
local function log(v)
    DebugError('[X4LAN] ' .. v)
end

local function paused()
    return API.paused and API.paused()
end

local function writable()
    return health.mdStamp and (health.now or 0) - health.mdAt <= READING and not paused()
end

-- MD 'entity|range' (metres, native runtime value); anything else clears it.
function capture.ranged(payload)
    local entity, range = tostring(payload):match('^(%d)|(.*)$')
    entity, range = tonumber(entity), tonumber(range)
    if entity == 1 or entity == 2 then
        capture.range[entity] = range and range > 0 and range or nil
    end
end

-- Conservative seconds a bullet from the entity's admitted weapon may remain in flight; nil refuses.
function capture.lifetime(entity)
    local support = shot.support[entity]
    local range = capture.range[entity]
    if not support or not range then
        return nil
    end
    local ok, entry = pcall(function()
        return GetLibraryEntry(GetMacroData(support.macro, 'infolibrary'), support.macro)
    end)
    local speed = ok and type(entry) == 'table' and tonumber(entry.bulletspeed)
    if not speed or speed <= 0 or MARGIN * range / speed > MAXLIFE then
        return nil
    end
    return MARGIN * range / speed
end

local function piloted()
    for entity = 1, 2 do
        if coop.owners[entity] == cfg.slot and coop.pilots[entity] == cfg.slot then
            return entity
        end
    end
    return 0
end

-- A co-op ship the OTHER player owns and flies: its shots are FIRE2 replays in my world.
local function remote(entity)
    return coop.owners[entity] ~= nil
        and coop.owners[entity] ~= cfg.slot
        and coop.pilots[entity] == coop.owners[entity]
end

function capture.frame(now)
    if shot.failed then
        capture.fail('shot module stopped')
        return
    end
    if now < (capture.next or 0) or not writable() then
        return
    end
    capture.next = now + INTERVAL
    local me = piloted()
    local life = me > 0 and capture.lifetime(me)
    if not life then
        me, life = 0, 0
    end
    capture.revision = capture.revision + 1
    AddUITriggeredEvent('X4LAN_ShotCapture', 'State', {
        revision = capture.revision,
        me = me,
        r1 = remote(1),
        r2 = remote(2),
        lifetime = life,
        undo = cfg.shot_undo == true,
        stamp = health.mdStamp,
        from = health.mdAge,
        deadline = health.mdAge + 1,
    })
end

-- MD 'victim|shooter|shooterGen|victimGen|count': the native cumulative count of my own ship's
-- hits under one admitted key; the shooter identity travels with it so a delayed count can never
-- be credited to another shooter.
function capture.count(payload)
    local victim, shooter, shooterGeneration, victimGeneration, count =
        tostring(payload):match('^(%d)|(%d)|(%d+)|(%d+)|(%d+)$')
    if capture.failed or not victim then
        return
    end
    shot.cumulative(
        tonumber(victim),
        tonumber(shooterGeneration),
        tonumber(victimGeneration),
        tonumber(count),
        tonumber(shooter)
    )
end

-- A failure stops capture and undo natively for the session.
function capture.fail(err)
    if capture.failed then
        return
    end
    capture.failed = true
    log('SHOT_CAPTURE STOPPED ' .. tostring(err))
    pcall(AddUITriggeredEvent, 'X4LAN_ShotCapture', 'Stop', {})
end

local frame = coop.frame
function coop.frame(now)
    frame(now)
    if shot.ready and not capture.setup then
        capture.setup = true
        AddUITriggeredEvent('X4LAN_ShotCapture', 'Setup')
    end
    if capture.ready and not capture.failed then
        local ok, err = pcall(capture.frame, now)
        if not ok then
            capture.fail(err)
        end
    end
end
RegisterEvent('X4LAN_ShotCapture.Ready', function()
    capture.ready = true
    log('SHOT_CAPTURE_READY slot=' .. cfg.slot)
end)
RegisterEvent('X4LAN_ShotCapture.Count', function(_, payload)
    if capture.ready then
        capture.count(payload)
    end
end)
RegisterEvent('X4LAN_ShotCapture.Range', function(_, payload)
    if capture.ready then
        capture.ranged(payload)
    end
end)
