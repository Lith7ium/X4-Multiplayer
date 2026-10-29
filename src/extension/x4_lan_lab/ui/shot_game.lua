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
local A = X4LAN_BUBBLE_API
local cfg = X4LAN_CONFIG
if
    not cfg
    or not cfg.coop
    or not cfg.shot_claims
    or not cfg.pilot_health
    or not A
    or not A.coop
    or not A.health
    or not A.state.active
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
local c, s, h = A.coop, A.state, A.health
local d =
    { seq = 0, totals = {}, sentAt = {}, support = {}, refusals = {}, confirmed = {}, pending = {}, inflight = {} }
A.shot = d
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
    if s.client and s.client.lease then
        s.transport:send(v)
    end
end
local function paused()
    return A.paused and A.paused()
end
local function key(e, gen, v, vg)
    return e .. '|' .. gen .. '|' .. v .. '|' .. vg
end
local function validGen(e)
    local g = h.valid[e]
    return g and g > (h.closed[e] or 0) and g or nil
end
local function victimGen(v)
    if v <= 2 then
        return validGen(v)
    end
    return 0
end
-- Only from a fresh MD readback of health_game.lua, never while paused (HEALTH's window).
local function writable()
    return h.mdStamp and (h.now or 0) - h.mdAt <= READING and not paused()
end

-- MD loadout report 'entity|turrets|weapon,macro;...': one macro, no mods, no turrets, and a library
-- entry whose only per-shot channel is hullshield; anything else refuses that shooter.
function d.loadout(payload)
    local e, turrets, list = tostring(payload):match('^(%d)|([^|]*)|(.*)$')
    e = tonumber(e)
    if not e or e < 1 or e > 2 then
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
    local v = ok and { macro = macro, strength = strength, digest = string.format(
        '%s:%.3f',
        macro,
        strength
    ) } or false
    local old = d.support[e]
    if old and (not v or old.digest ~= v.digest) then
        d.fail('admitted loadout changed; reconnect required; ' .. (reason or 'weapon-changed'))
        return
    end
    local changed = old == nil or (old and old.digest) ~= (v and v.digest)
    if changed then
        d.support[e] = v
        AddUITriggeredEvent(
            'X4LAN_Shot',
            'Support',
            { entity = e, strength = v and v.strength or 0 }
        )
    end
    -- Report a reason once, and again only if it changes. Never expose exception
    -- contents or repeat native writes merely to refresh an unchanged refusal.
    if changed or d.refusals[e] ~= reason then
        log(
            string.format(
                'SHOT_SUPPORT entity=%d %s',
                e,
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
    d.refusals[e] = reason
end

-- Authority: am I the machine whose world decides victim v of generation vg?
local function authority(v, vg)
    if v >= 3 then
        return cfg.slot == 'p1' and vg == 0
    end
    local live = h.live
    -- The host decides a co-op ship unless its remote pilot holds a live health generation (as the relay).
    if cfg.slot == 'p1' then
        return not (h.remote[v] and h.remote[v].gen == vg)
    end
    return live ~= nil and live.entity == v and live.gen == vg
end

-- Shooter: my piloted co-op ship with its valid generation.
local function shooter()
    for e = 1, 2 do
        if
            not h.failed
            and c.owners[e] == cfg.slot
            and c.pilots[e] == cfg.slot
            and validGen(e)
            and (cfg.slot == 'p1' or h.live and h.live.entity == e and h.live.gen == validGen(e))
        then
            return e, validGen(e)
        end
    end
end
-- Incremental capture entry: n native hits of my own ship on victim v.
function d.hit(v, n)
    local e, gen = shooter()
    local vg = v and v >= 1 and v <= 4 and victimGen(v)
    n = integer(n or 1, 1000000)
    if cfg.shot_visual and (not A.shotVisual or not A.shotVisual.readyOriginal(e, gen)) then
        return false
    end
    if
        d.failed
        or not e
        or not d.support[e]
        or v == e
        or not vg
        or not n
        or n < 1
        or authority(v, vg)
    then
        return false
    end
    local k = key(e, gen, v, vg)
    d.totals[k] = math.min(1000000, (d.totals[k] or 0) + n)
    d.dirty = true
    return true
end

-- Capture entry (shot_capture.lua): the NATIVE cumulative count of my own ship's hits on victim v
-- by shooter entity `from` under (sgen, vgen); it only raises the total of the current key, never
-- reinterprets it, and refuses a count admitted for another shooter.
function d.cumulative(v, sgen, vgen, total, from)
    local e, gen = shooter()
    local vg = v and v >= 1 and v <= 4 and victimGen(v)
    total = integer(total, 1000000)
    if
        d.failed
        or not e
        or e ~= from
        or not d.support[e]
        or v == e
        or not vg
        or gen ~= sgen
        or vg ~= vgen
        or not total
        or authority(v, vg)
    then
        return false
    end
    local k = key(e, gen, v, vg)
    if total > (d.totals[k] or 0) then
        d.totals[k] = total
        d.dirty = true
    end
    return true
end

-- SHOT|1|myLease|sourceLease|shooter|sgen|victim|vgen|seq|hits|digest
function d.receive(f)
    if f[1] ~= 'SHOT' then
        return false
    end
    if d.failed or #f ~= 11 or not s.client or f[3] ~= s.client.lease or not hex(f[4]) then
        return true
    end
    local e, gen, v, vg =
        integer(f[5], 2), integer(f[6], 2147483647), integer(f[7], 4), integer(f[8], 2147483647)
    local seq, hits = integer(f[9], 2147483647), integer(f[10], 1000000)
    if not e or e < 1 or not v or v < 1 or v == e or not gen or not vg or not seq or not hits then
        return true
    end
    if
        c.owners[e] == cfg.slot
        or c.pilots[e] ~= c.owners[e]
        or validGen(e) ~= gen
        or victimGen(v) ~= vg
        or not authority(v, vg)
    then
        return true
    end
    local sup = d.support[e]
    if not sup or f[11] ~= sup.digest then
        return true
    end -- unsupported or different loadout
    local k = key(e, gen, v, vg)
    local p = d.pending[k]
    if p and (seq <= p.seq or hits < p.hits) then
        return true
    end
    d.pending[k] = {
        seq = seq,
        hits = hits,
        shooter = e,
        sgen = gen,
        victim = v,
        vgen = vg,
        digest = sup.digest,
        strength = sup.strength,
    }
    return true
end

-- MD 'slot|sgen|vgen|count|state' after a compare-and-set apply: only now does the accepted count move.
function d.applied(payload)
    local slot, gen, vg, count, state = tostring(payload):match('^(%d)|(%d+)|(%d+)|(%d+)|(%d+)$')
    slot, gen, vg, count, state =
        tonumber(slot), tonumber(gen), tonumber(vg), tonumber(count), tonumber(state)
    if d.failed or not slot or slot < 1 or slot > 8 or state ~= d.stateRevision then
        return
    end
    local e, v = math.floor((slot - 1) / 4) + 1, (slot - 1) % 4 + 1
    local k = key(e, gen, v, vg)
    local p = d.pending[k]
    if
        not p
        or count > p.hits
        or validGen(e) ~= gen
        or victimGen(v) ~= vg
        or not authority(v, vg)
    then
        return
    end
    if count > (d.confirmed[k] or 0) then
        d.confirmed[k] = count
    end
    local fl = d.inflight[k]
    if fl and fl.count <= count then
        d.inflight[k] = nil
    end
end

-- A changed native permission state revokes queued writes before the next state is admitted.
-- The MD ledger checks this revision AND HEALTH closure/authority at execution.
local function state(now)
    local g1, g2 = validGen(1) or 0, validGen(2) or 0
    local a1, a2 = authority(1, g1), authority(2, g2)
    local sig = table.concat({ g1, g2, tostring(a1), tostring(a2) }, '|')
    if sig ~= d.stateKey then
        d.stateKey, d.stateRevision, d.stateAck = sig, (d.stateRevision or 0) + 1, nil
    end
    if writable() and (not d.nextState or now >= d.nextState) then
        d.nextState = now + 0.1
        AddUITriggeredEvent(
            'X4LAN_Shot',
            'State',
            {
                revision = d.stateRevision,
                g1 = g1,
                g2 = g2,
                a1 = a1,
                a2 = a2,
                stamp = h.mdStamp,
                from = h.mdAge,
                deadline = h.mdAge + 1,
            }
        )
    end
end
function d.stateReady(payload)
    local revision = integer(tostring(payload), 2147483647)
    if not d.failed and revision == d.stateRevision then
        d.stateAck = revision
    end
end

function d.frame(now)
    if h.failed then
        d.fail('health authority stopped')
        return
    end
    state(now)
    -- Shooter: cumulative counts of current generations only, at once on change, else every RESEND.
    local cur, curGen = shooter()
    local lease = s.client and s.client.lease
    for k, total in pairs(d.totals) do
        local e, gen, v, vg = k:match('^(%d)|(%d+)|(%d)|(%d+)$')
        e, gen, v, vg = tonumber(e), tonumber(gen), tonumber(v), tonumber(vg)
        if
            cur ~= e
            or curGen ~= gen
            or victimGen(v) ~= vg
            or not d.support[e]
            or authority(v, vg)
        then
            d.totals[k], d.sentAt[k] = nil, nil
        elseif lease and (d.dirty or now >= (d.sentAt[k] or 0)) then
            d.seq, d.sentAt[k] = d.seq + 1, now + RESEND
            send(
                string.format(
                    'SHOT|1|%s|%d|%d|%d|%d|%d|%d|%s',
                    lease,
                    e,
                    gen,
                    v,
                    vg,
                    d.seq,
                    total,
                    d.support[e].digest
                )
            )
        end
    end
    d.dirty = false
    -- Authority: issue the next increase (at most BATCH hits), retried until MD confirms it.
    for k, p in pairs(d.pending) do
        if
            validGen(p.shooter) ~= p.sgen
            or victimGen(p.victim) ~= p.vgen
            or not authority(p.victim, p.vgen)
        then
            d.pending[k], d.confirmed[k], d.inflight[k] = nil, nil, nil
        else
            local base = d.confirmed[k] or 0
            local fl = d.inflight[k]
            local sup = d.support[p.shooter]
            if
                p.hits > base
                and sup
                and sup.digest == p.digest
                and sup.strength == p.strength
                and (not fl or now - fl.at >= RETRY)
                and writable()
                and d.stateAck == d.stateRevision
            then
                local count = math.min(p.hits, base + BATCH)
                d.inflight[k] = { at = now, count = count }
                AddUITriggeredEvent(
                    'X4LAN_Shot',
                    'Apply',
                    {
                        shooter = p.shooter,
                        sgen = p.sgen,
                        victim = p.victim,
                        vgen = p.vgen,
                        base = base,
                        count = count,
                        strength = p.strength,
                        state = d.stateAck,
                        stamp = h.mdStamp,
                        from = h.mdAge,
                        deadline = h.mdAge + 1,
                    }
                )
            end
        end
    end
end

-- A failure ends this module for the session: nothing is sent or applied again; strengths go to
-- 0 in MD so no queued Apply can pass either.
function d.fail(err)
    if d.failed then
        return
    end
    d.failed = true
    log('SHOT STOPPED ' .. tostring(err))
    d.totals, d.pending, d.inflight = {}, {}, {}
    pcall(AddUITriggeredEvent, 'X4LAN_Shot', 'Stop')
    for e = 1, 2 do
        pcall(AddUITriggeredEvent, 'X4LAN_Shot', 'Support', { entity = e, strength = 0 })
    end
end

local receive, frame = c.receive, c.frame
function c.receive(f, now)
    local ok, handled = pcall(d.receive, f)
    if not ok then
        d.fail(handled)
        handled = f[1] == 'SHOT'
    end
    if handled then
        return true
    end
    return receive(f, now)
end
function c.frame(now)
    frame(now)
    if c.ready and h.ready and not d.setup then
        d.setup = true
        AddUITriggeredEvent('X4LAN_Shot', 'Setup')
    end
    if d.ready and not d.failed then
        local ok, err = pcall(d.frame, now)
        if not ok then
            d.fail(err)
        end
    end
end
RegisterEvent('X4LAN_Shot.Ready', function()
    d.ready = true
    log('SHOT_READY slot=' .. cfg.slot)
end)
RegisterEvent('X4LAN_Shot.Loadout', function(_, payload)
    if d.ready and not d.failed then
        d.loadout(payload)
    end
end)
RegisterEvent('X4LAN_Shot.StateReady', function(_, payload)
    d.stateReady(payload)
end)
RegisterEvent('X4LAN_Shot.Applied', function(_, payload)
    if d.ready then
        d.applied(payload)
    end
end)
