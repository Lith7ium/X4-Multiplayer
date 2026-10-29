-- Inert, opt-in projectile macro proof. Native preparation ACK precedes switching.
-- Legacy replay is stopped; observed mode excludes scripted replay before Prepare.
-- Original installed guns, equipment mods and weapon groups are never replaced.
local A, cfg = X4LAN_BUBBLE_API, X4LAN_CONFIG
if
    not cfg
    or not cfg.shot_visual
    or not cfg.fire2
    or not cfg.shot_claims
    or not cfg.pilot_health
    or not A
    or not A.shot
    or not A.health
    or not A.coop
then
    return
end
local ffi = require('ffi')
local C = ffi.C
pcall(
    ffi.cdef,
    [[const char* GetWeaponLaunchedMacro(UniverseID);
void SetWeaponLaunchedMacro(UniverseID,const char*);
size_t GetNumUpgradeSlots(UniverseID,const char*,const char*);
UniverseID GetUpgradeSlotCurrentComponent(UniverseID,const char*,size_t);]]
)
local c, h, d = A.coop, A.health, A.shot
local v = { rows = {}, serial = 0, now = 0 }
A.shotVisual = v
local BULLETS = {
    weapon_ter_s_plasma_01_mk1_macro = {
        'bullet_ter_s_plasma_01_mk1_macro',
        'x4lan_visual_plasma_bullet_macro',
    },
    weapon_gen_s_laser_01_mk1_macro = {
        'bullet_gen_s_laser_01_mk1_macro',
        'x4lan_visual_pulse_mk1_bullet_macro',
    },
    weapon_gen_s_laser_01_mk2_macro = {
        'bullet_gen_s_laser_01_mk2_macro',
        'x4lan_visual_pulse_mk2_bullet_macro',
    },
}
local function log(s)
    DebugError('[X4LAN] SHOT_VISUAL ' .. s)
end
local function fresh()
    return h.mdStamp and v.now - (h.mdAt or -100) <= 0.3 and not A.paused()
end
local function serial()
    v.serial = v.serial + 1
    assert(v.serial < 2147483647, 'serial exhausted')
    return v.serial
end
local function wanted(e)
    local g = h.valid[e]
    if
        not g
        or g <= (h.closed[e] or 0)
        or not c.owners[e]
        or not c.ships[e]
        or c.ships[e] == 0
        or not C.IsComponentOperational(c.ships[e])
    then
        return nil
    end
    return g, c.owners[e] == cfg.slot and 1 or 2, c.ships[e]
end
local function current(e, r)
    local g, mode, ship = wanted(e)
    return not v.failed
        and not d.failed
        and not h.failed
        and r
        and r.gen == g
        and r.mode == mode
        and r.ship == ship
end
local function ready(e, g, mode, now)
    local r = v.rows[e]
    return current(e, r)
        and r.gen == g
        and r.mode == mode
        and r.phase == 'ready'
        and (now or h.now or v.now) - (r.ackAt or -100) <= 0.3
        and not A.paused()
end
function v.readyOriginal(e, g, now)
    return ready(e, g, 1, now)
end
function v.readyReplay(e, g, now)
    return ready(e, g, 2, now)
end
local function event(control, args)
    args.stamp, args.from, args.deadline = h.mdStamp, h.mdAge, h.mdAge + 1
    AddUITriggeredEvent('X4LAN_ShotVisual', control, args)
end
-- Read every actual installed slot (empty slots are retained, not compacted into fake IDs).
local function guns(e, ship)
    local sup = assert(d.support[e], 'unadmitted loadout')
    local pair = assert(BULLETS[sup.macro], 'unsupported loadout')
    assert(ship ~= 0 and C.IsComponentOperational(ship), 'ship unavailable')
    local n = tonumber(C.GetNumUpgradeSlots(ship, '', 'weapon'))
    assert(n and n >= 1 and n <= 32 and n % 1 == 0, 'slot bounds')
    local out = {}
    for slot = 1, n do
        local id = C.GetUpgradeSlotCurrentComponent(ship, 'weapon', slot)
        if id ~= 0 then
            assert(C.IsComponentOperational(id), 'weapon unavailable')
            assert(GetComponentData(A.id(id), 'macro') == sup.macro, 'installed loadout changed')
            assert(not C.GetInstalledWeaponMod(id, ffi.new('UIWeaponMod', {})), 'modified weapon')
            local macro = ffi.string(C.GetWeaponLaunchedMacro(id))
            assert(macro == pair[1] or macro == pair[2], 'foreign launched macro')
            out[#out + 1] = { slot = slot, id = id, macro = macro }
        end
    end
    assert(#out > 0, 'no installed weapons')
    return out, pair
end
local function verify(e, r)
    local list, pair = guns(e, r.ship)
    assert(#list == #r.guns, 'weapon count changed')
    for i, w in ipairs(list) do
        local previous = r.guns[i]
        assert(
            w.slot == previous.slot and w.id == previous.id and w.macro == pair[r.mode],
            'weapon proof changed'
        )
    end
end
local function proof(e, r)
    verify(e, r)
    local seq = serial()
    r.pendingProof = seq
    r.nextProof = v.now + 0.1
    event(
        'Proof',
        { entity = e, gen = r.gen, ticket = r.ticket, mode = r.mode, sequence = seq, revision = d.stateRevision }
    )
end
function v.prepared(payload)
    local e, g, ticket, mode, id = tostring(payload):match('^(%d)|(%d+)|(%d+)|(%d)|([^|]+)$')
    e, g, ticket, mode = tonumber(e), tonumber(g), tonumber(ticket), tonumber(mode)
    local r = e and v.rows[e]
    if
        not current(e, r)
        or r.phase ~= 'preparing'
        or ticket ~= r.ticket
        or g ~= r.gen
        or mode ~= r.mode
        or not fresh()
        or v.now - r.started > 0.3
        or ConvertIDTo64Bit(id) ~= r.ship
    then
        return
    end
    local list, pair = guns(e, r.ship)
    -- Native scripted replay is stopped or excluded, budgets cleared; verify each write.
    for _, w in ipairs(list) do
        if w.macro ~= pair[mode] then
            C.SetWeaponLaunchedMacro(w.id, pair[mode])
        end
        assert(
            ffi.string(C.GetWeaponLaunchedMacro(w.id)) == pair[mode],
            'projectile switch refused'
        )
    end
    r.guns = list
    r.phase = 'proving'
    proof(e, r)
    log('switched entity=' .. e .. ' gen=' .. g .. ' mode=' .. mode .. ' guns=' .. #list)
end
function v.proven(payload)
    local e, g, ticket, mode, seq = tostring(payload):match('^(%d)|(%d+)|(%d+)|(%d)|(%d+)$')
    e, g, ticket, mode, seq =
        tonumber(e), tonumber(g), tonumber(ticket), tonumber(mode), tonumber(seq)
    local r = e and v.rows[e]
    if
        current(e, r)
        and fresh()
        and g == r.gen
        and mode == r.mode
        and ticket == r.ticket
        and seq == r.pendingProof
    then
        r.phase = 'ready'
        r.ackAt = v.now
    end
end
function v.fail(err)
    if v.failed then
        return
    end
    v.failed = true
    log('STOPPED ' .. tostring(err))
    pcall(AddUITriggeredEvent, 'X4LAN_ShotVisual', 'Stop', {})
    d.fail('visual proof stopped')
end
function v.frame(now)
    v.now = now
    if d.failed or h.failed then
        v.fail('authority stopped')
        return
    end
    if not fresh() then
        return
    end
    for e = 1, 2 do
        local g, mode, ship = wanted(e)
        local r = v.rows[e]
        if not g or not d.support[e] then
            v.rows[e] = nil
        elseif not current(e, r) or r.phase == 'preparing' and now - r.started > 0.3 then
            local ticket = serial()
            r =
                { gen = g, mode = mode, ship = ship, ticket = ticket, started = now, phase = 'preparing' }
            v.rows[e] = r
            event(
                'Prepare',
                { entity = e, gen = g, ticket = ticket, mode = mode, revision = d.stateRevision }
            )
        elseif r.phase ~= 'preparing' and now >= (r.nextProof or 0) then
            proof(e, r)
        end
    end
end
local frame = c.frame
function c.frame(now)
    frame(now)
    v.now = now
    if d.ready and not v.setup then
        v.setup = true
        AddUITriggeredEvent(
            'X4LAN_ShotVisual',
            'Setup',
            { observed = cfg.projectile_visuals == true }
        )
    end
    if v.ready and not v.failed then
        local ok, err = pcall(v.frame, now)
        if not ok then
            v.fail(err)
        end
    end
end
local function guard(fn)
    return function(_, payload)
        local ok, err = pcall(fn, payload)
        if not ok then
            v.fail(err)
        end
    end
end
RegisterEvent('X4LAN_ShotVisual.Ready', function()
    v.ready = true
    log('READY')
end)
RegisterEvent('X4LAN_ShotVisual.Prepared', guard(v.prepared))
RegisterEvent('X4LAN_ShotVisual.Proven', guard(v.proven))
