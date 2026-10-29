-- Shooter-confirmed hits (SHOT), dev scope: reports/SHOOTER-CONFIRMED-HITS-DESIGN.txt.
-- The shooter counts the hits its OWN piloted co-op ship scored on a remote player's ship or a
-- shared LAN target and sends the cumulative count; the victim's authority machine applies the
-- increase through the native damage primitive (md/x4lan_shot.xml), so the engine resolves
-- shields, hull, floor, death and kill credit at the authority's real state.
-- Bounded: only a uniform, unmodified loadout of ordinary hullshield-only bullets without turrets
-- is supported; anything else refuses claims on that machine. shot_capture.lua supplies native
-- cumulative hit counts when explicitly enabled; this module never guesses hits from visuals.
--  SHOT|1|lease|shooter|shooterGen|victim|victimGen|seq|hits|macro:strength   shooter -> relay
--  forwarded to the victim's authority with the source lease after the recipient lease.
-- Generations are the relay's health generations (HGEN, health_game.lua); LAN targets use 0.
local API = X4LAN_BUBBLE_API
local cfg = X4LAN_CONFIG
if
    not cfg
    or not cfg.coop
    or not cfg.shot_claims
    or not cfg.pilot_health
    or not API
    or not API.coop
    or not API.health
    or not API.state.active
then
    return
end
local ffi = require('ffi')
local C = ffi.C
pcall(
    ffi.cdef,
    [[typedef struct {
 const char* Name; const char* RawName; const char* Ware; uint32_t Quality; const char* PropertyType;
 float DamageFactor; float CoolingFactor; float ReloadFactor; float SpeedFactor; float LifeTimeFactor;
 float MiningFactor; float StickTimeFactor; float ChargeTimeFactor; float BeamLengthFactor;
 uint32_t AddedAmount; float RotationSpeedFactor; float SurfaceElementFactor;
} UIWeaponMod;
bool GetInstalledWeaponMod(UniverseID weaponid, UIWeaponMod* weaponmod);]]
)
local coopAdapter, sessionState, healthAdapter = API.coop, API.state, API.health
local shotAdapter = {
    seq = 0,
    totals = {},
    sentAt = {},
    support = {},
    refusals = {},
    confirmed = {},
    pending = {},
    inflight = {},
}
API.shot = shotAdapter
local STOCK = {
    weapon_ter_s_plasma_01_mk1_macro = 40,
    weapon_gen_s_laser_01_mk1_macro = 36,
    weapon_gen_s_laser_01_mk2_macro = 46,
}
local OTHER = {
    'shieldonlydpshot',
    'hullonlydpshot',
    'hullnoshielddpshot',
    'hullshieldareadpshot',
    'shieldonlyareadpshot',
    'hullonlyareadpshot',
    'hullnoshieldareadpshot',
}
local RESEND, RETRY, BATCH, READING = 0.25, 0.5, 20, 0.5
local function log(v)
    DebugError('[X4LAN] ' .. v)
end
local function number(v, max)
    local n = tonumber(v)
    if not n or n ~= n or n < 0 or n > max then
        return nil
    end
    return n
end
local function integer(v, max)
    local n = number(v, max)
    if n and n % 1 == 0 then
        return n
    end
end
local function hex(v)
    return type(v) == 'string' and #v == 32 and v:match('^[a-f0-9]+$') ~= nil
end
local function send(v)
    if sessionState.client and sessionState.client.lease then
        sessionState.transport:send(v)
    end
end
local function paused()
    return API.paused and API.paused()
end
local function key(entity, generation, v, victimGeneration)
    return entity .. '|' .. generation .. '|' .. v .. '|' .. victimGeneration
end
local function validGen(entity)
    local g = healthAdapter.valid[entity]
    return g and g > (healthAdapter.closed[entity] or 0) and g or nil
end
local function victimGen(v)
    if v <= 2 then
        return validGen(v)
    end
    return 0
end
-- Only from a fresh MD readback of health_game.lua, never while paused (HEALTH's window).
local function writable()
    return healthAdapter.mdStamp
        and (healthAdapter.now or 0) - healthAdapter.mdAt <= READING
        and not paused()
end

-- MD loadout report 'entity|turrets|weapon,macro;...': one macro, no mods, no turrets, and a library
-- entry whose only per-shot channel is hullshield; anything else refuses that shooter.
function shotAdapter.loadout(payload)
    local entity, turrets, list = tostring(payload):match('^(%d)|([^|]*)|(.*)$')
    entity = tonumber(entity)
    if not entity or entity < 1 or entity > 2 then
        return
    end
    local macro, ok, n = nil, turrets == '0', 0
    local reason = not ok and ('turrets=' .. turrets) or nil
    for id, m in list:gmatch('([^,;]+),([^;]+);') do
        n = n + 1
        if macro and m ~= macro then
            ok = false
            reason = reason or 'mixed-weapons'
        end
        macro = m
        local done, mod = pcall(function()
            local weapon = ConvertIDTo64Bit(id)
            assert(
                weapon and weapon ~= 0 and C.IsComponentOperational(weapon),
                'weapon identity unavailable'
            )
            return C.GetInstalledWeaponMod(weapon, ffi.new('UIWeaponMod', {}))
        end)
        if not done or mod then
            ok = false
            reason = reason
                or (
                    (
                        not done
                            and (tostring(mod):find('weapon identity unavailable', 1, true) and 'weapon-unavailable:' or 'weapon-query-failed:')
                        or 'weapon-modified:'
                    ) .. id
                )
        end
    end
    local strength
    if ok and n > 0 and STOCK[macro] then
        local done, entry = pcall(function()
            return GetLibraryEntry(GetMacroData(macro, 'infolibrary'), macro)
        end)
        ok = done and type(entry) == 'table'
        if not ok then
            reason = reason or 'library-unavailable'
        end
        strength = ok and number(entry.hullshielddpshot, 1e6)
        if not strength or strength ~= STOCK[macro] or entry.isbeamweapon ~= 0 then
            ok = false
            reason = reason or 'damage-profile'
        end
        for _, k in ipairs(OTHER) do
            if ok and tonumber(entry[k]) ~= 0 then
                ok = false
                reason = reason or ('damage-channel:' .. k)
            end
        end
    else
        ok = false
        reason = reason or (n == 0 and 'no-operational-weapons' or 'unsupported-weapon')
    end
    local v = ok
            and {
                macro = macro,
                strength = strength,
                digest = string.format('%s:%.3f', macro, strength),
            }
        or false
    local old = shotAdapter.support[entity]
    if old and (not v or old.digest ~= v.digest) then
        shotAdapter.fail(
            'admitted loadout changed; reconnect required; ' .. (reason or 'weapon-changed')
        )
        return
    end
    local changed = old == nil or (old and old.digest) ~= (v and v.digest)
    if changed then
        shotAdapter.support[entity] = v
        AddUITriggeredEvent(
            'X4LAN_Shot',
            'Support',
            { entity = entity, strength = v and v.strength or 0 }
        )
    end
    -- Report a reason once, and again only if it changes. Never expose exception
    -- contents or repeat native writes merely to refresh an unchanged refusal.
    if changed or shotAdapter.refusals[entity] ~= reason then
        log(
            string.format(
                'SHOT_SUPPORT entity=%d %s',
                entity,
                v and v.digest
                    or (
                        'refused reason='
                        .. reason
                        .. ' weapons='
                        .. n
                        .. ' macro='
                        .. tostring(macro)
                    )
            )
        )
    end
    shotAdapter.refusals[entity] = reason
end

-- Authority: am I the machine whose world decides victim v of generation vg?
local function authority(v, victimGeneration)
    if v >= 3 then
        return cfg.slot == 'p1' and victimGeneration == 0
    end
    local live = healthAdapter.live
    -- The host decides a co-op ship unless its remote pilot holds a live health generation (as the relay).
    if cfg.slot == 'p1' then
        return not (healthAdapter.remote[v] and healthAdapter.remote[v].gen == victimGeneration)
    end
    return live ~= nil and live.entity == v and live.gen == victimGeneration
end

-- Shooter: my piloted co-op ship with its valid generation.
local function shooter()
    for entity = 1, 2 do
        if
            not healthAdapter.failed
            and coopAdapter.owners[entity] == cfg.slot
            and coopAdapter.pilots[entity] == cfg.slot
            and validGen(entity)
            and (
                cfg.slot == 'p1'
                or healthAdapter.live
                    and healthAdapter.live.entity == entity
                    and healthAdapter.live.gen == validGen(entity)
            )
        then
            return entity, validGen(entity)
        end
    end
end
-- Incremental capture entry: n native hits of my own ship on victim v.
function shotAdapter.hit(v, n)
    local entity, generation = shooter()
    local victimGeneration = v and v >= 1 and v <= 4 and victimGen(v)
    n = integer(n or 1, 1000000)
    if
        cfg.shot_visual
        and (not API.shotVisual or not API.shotVisual.readyOriginal(entity, generation))
    then
        return false
    end
    if
        shotAdapter.failed
        or not entity
        or not shotAdapter.support[entity]
        or v == entity
        or not victimGeneration
        or not n
        or n < 1
        or authority(v, victimGeneration)
    then
        return false
    end
    local k = key(entity, generation, v, victimGeneration)
    shotAdapter.totals[k] = math.min(1000000, (shotAdapter.totals[k] or 0) + n)
    shotAdapter.dirty = true
    return true
end

-- Capture entry (shot_capture.lua): the NATIVE cumulative count of my own ship's hits on victim v
-- by shooter entity `from` under (sgen, vgen); it only raises the total of the current key, never
-- reinterprets it, and refuses a count admitted for another shooter.
function shotAdapter.cumulative(v, sgen, vgen, total, from)
    local entity, generation = shooter()
    local victimGeneration = v and v >= 1 and v <= 4 and victimGen(v)
    total = integer(total, 1000000)
    if
        shotAdapter.failed
        or not entity
        or entity ~= from
        or not shotAdapter.support[entity]
        or v == entity
        or not victimGeneration
        or generation ~= sgen
        or victimGeneration ~= vgen
        or not total
        or authority(v, victimGeneration)
    then
        return false
    end
    local k = key(entity, generation, v, victimGeneration)
    if total > (shotAdapter.totals[k] or 0) then
        shotAdapter.totals[k] = total
        shotAdapter.dirty = true
    end
    return true
end

-- SHOT|1|myLease|sourceLease|shooter|sgen|victim|vgen|seq|hits|digest
function shotAdapter.receive(fields)
    if fields[1] ~= 'SHOT' then
        return false
    end
    if
        shotAdapter.failed
        or #fields ~= 11
        or not sessionState.client
        or fields[3] ~= sessionState.client.lease
        or not hex(fields[4])
    then
        return true
    end
    local entity, generation, v, victimGeneration =
        integer(fields[5], 2),
        integer(fields[6], 2147483647),
        integer(fields[7], 4),
        integer(fields[8], 2147483647)
    local seq, hits = integer(fields[9], 2147483647), integer(fields[10], 1000000)
    if
        not entity
        or entity < 1
        or not v
        or v < 1
        or v == entity
        or not generation
        or not victimGeneration
        or not seq
        or not hits
    then
        return true
    end
    if
        coopAdapter.owners[entity] == cfg.slot
        or coopAdapter.pilots[entity] ~= coopAdapter.owners[entity]
        or validGen(entity) ~= generation
        or victimGen(v) ~= victimGeneration
        or not authority(v, victimGeneration)
    then
        return true
    end
    local support = shotAdapter.support[entity]
    if not support or fields[11] ~= support.digest then
        return true
    end -- unsupported or different loadout
    local k = key(entity, generation, v, victimGeneration)
    local p = shotAdapter.pending[k]
    if p and (seq <= p.seq or hits < p.hits) then
        return true
    end
    shotAdapter.pending[k] = {
        seq = seq,
        hits = hits,
        shooter = entity,
        sgen = generation,
        victim = v,
        vgen = victimGeneration,
        digest = support.digest,
        strength = support.strength,
    }
    return true
end

-- MD 'slot|sgen|vgen|count|state' after a compare-and-set apply: only now does the accepted count move.
function shotAdapter.applied(payload)
    local slot, generation, victimGeneration, count, state =
        tostring(payload):match('^(%d)|(%d+)|(%d+)|(%d+)|(%d+)$')
    slot, generation, victimGeneration, count, state =
        tonumber(slot),
        tonumber(generation),
        tonumber(victimGeneration),
        tonumber(count),
        tonumber(state)
    if
        shotAdapter.failed
        or not slot
        or slot < 1
        or slot > 8
        or state ~= shotAdapter.stateRevision
    then
        return
    end
    local entity, v = math.floor((slot - 1) / 4) + 1, (slot - 1) % 4 + 1
    local k = key(entity, generation, v, victimGeneration)
    local p = shotAdapter.pending[k]
    if
        not p
        or count > p.hits
        or validGen(entity) ~= generation
        or victimGen(v) ~= victimGeneration
        or not authority(v, victimGeneration)
    then
        return
    end
    if count > (shotAdapter.confirmed[k] or 0) then
        shotAdapter.confirmed[k] = count
    end
    local fl = shotAdapter.inflight[k]
    if fl and fl.count <= count then
        shotAdapter.inflight[k] = nil
    end
end

-- A changed native permission state revokes queued writes before the next state is admitted.
-- The MD ledger checks this revision AND HEALTH closure/authority at execution.
local function state(now)
    local g1, g2 = validGen(1) or 0, validGen(2) or 0
    local a1, a2 = authority(1, g1), authority(2, g2)
    local sig = table.concat({ g1, g2, tostring(a1), tostring(a2) }, '|')
    if sig ~= shotAdapter.stateKey then
        shotAdapter.stateKey, shotAdapter.stateRevision, shotAdapter.stateAck =
            sig, (shotAdapter.stateRevision or 0) + 1, nil
    end
    if writable() and (not shotAdapter.nextState or now >= shotAdapter.nextState) then
        shotAdapter.nextState = now + 0.1
        AddUITriggeredEvent('X4LAN_Shot', 'State', {
            revision = shotAdapter.stateRevision,
            g1 = g1,
            g2 = g2,
            a1 = a1,
            a2 = a2,
            stamp = healthAdapter.mdStamp,
            from = healthAdapter.mdAge,
            deadline = healthAdapter.mdAge + 1,
        })
    end
end
function shotAdapter.stateReady(payload)
    local revision = integer(tostring(payload), 2147483647)
    if not shotAdapter.failed and revision == shotAdapter.stateRevision then
        shotAdapter.stateAck = revision
    end
end

function shotAdapter.frame(now)
    if healthAdapter.failed then
        shotAdapter.fail('health authority stopped')
        return
    end
    state(now)
    -- Shooter: cumulative counts of current generations only, at once on change, else every RESEND.
    local cur, curGen = shooter()
    local lease = sessionState.client and sessionState.client.lease
    for k, total in pairs(shotAdapter.totals) do
        local entity, generation, v, victimGeneration = k:match('^(%d)|(%d+)|(%d)|(%d+)$')
        entity, generation, v, victimGeneration =
            tonumber(entity), tonumber(generation), tonumber(v), tonumber(victimGeneration)
        if
            cur ~= entity
            or curGen ~= generation
            or victimGen(v) ~= victimGeneration
            or not shotAdapter.support[entity]
            or authority(v, victimGeneration)
        then
            shotAdapter.totals[k], shotAdapter.sentAt[k] = nil, nil
        elseif lease and (shotAdapter.dirty or now >= (shotAdapter.sentAt[k] or 0)) then
            shotAdapter.seq, shotAdapter.sentAt[k] = shotAdapter.seq + 1, now + RESEND
            send(
                string.format(
                    'SHOT|1|%s|%d|%d|%d|%d|%d|%d|%s',
                    lease,
                    entity,
                    generation,
                    v,
                    victimGeneration,
                    shotAdapter.seq,
                    total,
                    shotAdapter.support[entity].digest
                )
            )
        end
    end
    shotAdapter.dirty = false
    -- Authority: issue the next increase (at most BATCH hits), retried until MD confirms it.
    for k, p in pairs(shotAdapter.pending) do
        if
            validGen(p.shooter) ~= p.sgen
            or victimGen(p.victim) ~= p.vgen
            or not authority(p.victim, p.vgen)
        then
            shotAdapter.pending[k], shotAdapter.confirmed[k], shotAdapter.inflight[k] =
                nil, nil, nil
        else
            local base = shotAdapter.confirmed[k] or 0
            local fl = shotAdapter.inflight[k]
            local support = shotAdapter.support[p.shooter]
            if
                p.hits > base
                and support
                and support.digest == p.digest
                and support.strength == p.strength
                and (not fl or now - fl.at >= RETRY)
                and writable()
                and shotAdapter.stateAck == shotAdapter.stateRevision
            then
                local count = math.min(p.hits, base + BATCH)
                shotAdapter.inflight[k] = { at = now, count = count }
                AddUITriggeredEvent('X4LAN_Shot', 'Apply', {
                    shooter = p.shooter,
                    sgen = p.sgen,
                    victim = p.victim,
                    vgen = p.vgen,
                    base = base,
                    count = count,
                    strength = p.strength,
                    state = shotAdapter.stateAck,
                    stamp = healthAdapter.mdStamp,
                    from = healthAdapter.mdAge,
                    deadline = healthAdapter.mdAge + 1,
                })
            end
        end
    end
end

-- A failure ends this module for the session: nothing is sent or applied again; strengths go to
-- 0 in MD so no queued Apply can pass either.
function shotAdapter.fail(err)
    if shotAdapter.failed then
        return
    end
    shotAdapter.failed = true
    log('SHOT STOPPED ' .. tostring(err))
    shotAdapter.totals, shotAdapter.pending, shotAdapter.inflight = {}, {}, {}
    pcall(AddUITriggeredEvent, 'X4LAN_Shot', 'Stop')
    for entity = 1, 2 do
        pcall(AddUITriggeredEvent, 'X4LAN_Shot', 'Support', { entity = entity, strength = 0 })
    end
end

local receive, frame = coopAdapter.receive, coopAdapter.frame
function coopAdapter.receive(fields, now)
    local ok, handled = pcall(shotAdapter.receive, fields)
    if not ok then
        shotAdapter.fail(handled)
        handled = fields[1] == 'SHOT'
    end
    if handled then
        return true
    end
    return receive(fields, now)
end
function coopAdapter.frame(now)
    frame(now)
    if coopAdapter.ready and healthAdapter.ready and not shotAdapter.setup then
        shotAdapter.setup = true
        AddUITriggeredEvent('X4LAN_Shot', 'Setup')
    end
    if shotAdapter.ready and not shotAdapter.failed then
        local ok, err = pcall(shotAdapter.frame, now)
        if not ok then
            shotAdapter.fail(err)
        end
    end
end
RegisterEvent('X4LAN_Shot.Ready', function()
    shotAdapter.ready = true
    log('SHOT_READY slot=' .. cfg.slot)
end)
RegisterEvent('X4LAN_Shot.Loadout', function(_, payload)
    if shotAdapter.ready and not shotAdapter.failed then
        shotAdapter.loadout(payload)
    end
end)
RegisterEvent('X4LAN_Shot.StateReady', function(_, payload)
    shotAdapter.stateReady(payload)
end)
RegisterEvent('X4LAN_Shot.Applied', function(_, payload)
    if shotAdapter.ready then
        shotAdapter.applied(payload)
    end
end)
