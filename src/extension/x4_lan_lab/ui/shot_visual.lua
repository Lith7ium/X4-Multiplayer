-- Inert, opt-in projectile macro proof. Native preparation ACK precedes switching.
-- Legacy replay is stopped; observed mode excludes scripted replay before Prepare.
-- Original installed guns, equipment mods and weapon groups are never replaced.
local API, cfg = X4LAN_BUBBLE_API, X4LAN_CONFIG
if
    not cfg
    or not cfg.shot_visual
    or not cfg.fire2
    or not cfg.shot_claims
    or not cfg.pilot_health
    or not API
    or not API.shot
    or not API.health
    or not API.coop
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
local coopAdapter, healthAdapter, shotAdapter = API.coop, API.health, API.shot
local shotVisual = { rows = {}, serial = 0, now = 0 }
API.shotVisual = shotVisual
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
    return healthAdapter.mdStamp
        and shotVisual.now - (healthAdapter.mdAt or -100) <= 0.3
        and not API.paused()
end
local function serial()
    shotVisual.serial = shotVisual.serial + 1
    assert(shotVisual.serial < 2147483647, 'serial exhausted')
    return shotVisual.serial
end
local function wanted(entity)
    local generation = healthAdapter.valid[entity]
    if
        not generation
        or generation <= (healthAdapter.closed[entity] or 0)
        or not coopAdapter.owners[entity]
        or not coopAdapter.ships[entity]
        or coopAdapter.ships[entity] == 0
        or not C.IsComponentOperational(coopAdapter.ships[entity])
    then
        return nil
    end
    return generation, coopAdapter.owners[entity] == cfg.slot and 1 or 2, coopAdapter.ships[entity]
end
local function current(entity, row)
    local generation, mode, ship = wanted(entity)
    return not shotVisual.failed
        and not shotAdapter.failed
        and not healthAdapter.failed
        and row
        and row.gen == generation
        and row.mode == mode
        and row.ship == ship
end
local function ready(entity, generation, mode, now)
    local row = shotVisual.rows[entity]
    return current(entity, row)
        and row.gen == generation
        and row.mode == mode
        and row.phase == 'ready'
        and (now or healthAdapter.now or shotVisual.now) - (row.ackAt or -100) <= 0.3
        and not API.paused()
end
function shotVisual.readyOriginal(entity, generation, now)
    return ready(entity, generation, 1, now)
end
function shotVisual.readyReplay(entity, generation, now)
    return ready(entity, generation, 2, now)
end
local function event(control, args)
    args.stamp, args.from, args.deadline =
        healthAdapter.mdStamp, healthAdapter.mdAge, healthAdapter.mdAge + 1
    AddUITriggeredEvent('X4LAN_ShotVisual', control, args)
end
-- Read every actual installed slot (empty slots are retained, not compacted into fake IDs).
local function guns(entity, ship)
    local support = assert(shotAdapter.support[entity], 'unadmitted loadout')
    local pair = assert(BULLETS[support.macro], 'unsupported loadout')
    assert(ship ~= 0 and C.IsComponentOperational(ship), 'ship unavailable')
    local n = tonumber(C.GetNumUpgradeSlots(ship, '', 'weapon'))
    assert(n and n >= 1 and n <= 32 and n % 1 == 0, 'slot bounds')
    local out = {}
    for slot = 1, n do
        local id = C.GetUpgradeSlotCurrentComponent(ship, 'weapon', slot)
        if id ~= 0 then
            assert(C.IsComponentOperational(id), 'weapon unavailable')
            assert(
                GetComponentData(API.id(id), 'macro') == support.macro,
                'installed loadout changed'
            )
            assert(not C.GetInstalledWeaponMod(id, ffi.new('UIWeaponMod', {})), 'modified weapon')
            local macro = ffi.string(C.GetWeaponLaunchedMacro(id))
            assert(macro == pair[1] or macro == pair[2], 'foreign launched macro')
            out[#out + 1] = { slot = slot, id = id, macro = macro }
        end
    end
    assert(#out > 0, 'no installed weapons')
    return out, pair
end
local function verify(entity, row)
    local list, pair = guns(entity, row.ship)
    assert(#list == #row.guns, 'weapon count changed')
    for i, w in ipairs(list) do
        local previous = row.guns[i]
        assert(
            w.slot == previous.slot and w.id == previous.id and w.macro == pair[row.mode],
            'weapon proof changed'
        )
    end
end
local function proof(entity, row)
    verify(entity, row)
    local seq = serial()
    row.pendingProof = seq
    row.nextProof = shotVisual.now + 0.1
    event(
        'Proof',
        {
            entity = entity,
            gen = row.gen,
            ticket = row.ticket,
            mode = row.mode,
            sequence = seq,
            revision = shotAdapter.stateRevision,
        }
    )
end
function shotVisual.prepared(payload)
    local entity, generation, ticket, mode, id =
        tostring(payload):match('^(%d)|(%d+)|(%d+)|(%d)|([^|]+)$')
    entity, generation, ticket, mode =
        tonumber(entity), tonumber(generation), tonumber(ticket), tonumber(mode)
    local row = entity and shotVisual.rows[entity]
    if
        not current(entity, row)
        or row.phase ~= 'preparing'
        or ticket ~= row.ticket
        or generation ~= row.gen
        or mode ~= row.mode
        or not fresh()
        or shotVisual.now - row.started > 0.3
        or ConvertIDTo64Bit(id) ~= row.ship
    then
        return
    end
    local list, pair = guns(entity, row.ship)
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
    row.guns = list
    row.phase = 'proving'
    proof(entity, row)
    log(
        'switched entity='
            .. entity
            .. ' gen='
            .. generation
            .. ' mode='
            .. mode
            .. ' guns='
            .. #list
    )
end
function shotVisual.proven(payload)
    local entity, generation, ticket, mode, seq =
        tostring(payload):match('^(%d)|(%d+)|(%d+)|(%d)|(%d+)$')
    entity, generation, ticket, mode, seq =
        tonumber(entity), tonumber(generation), tonumber(ticket), tonumber(mode), tonumber(seq)
    local row = entity and shotVisual.rows[entity]
    if
        current(entity, row)
        and fresh()
        and generation == row.gen
        and mode == row.mode
        and ticket == row.ticket
        and seq == row.pendingProof
    then
        row.phase = 'ready'
        row.ackAt = shotVisual.now
    end
end
function shotVisual.fail(err)
    if shotVisual.failed then
        return
    end
    shotVisual.failed = true
    log('STOPPED ' .. tostring(err))
    pcall(AddUITriggeredEvent, 'X4LAN_ShotVisual', 'Stop', {})
    shotAdapter.fail('visual proof stopped')
end
function shotVisual.frame(now)
    shotVisual.now = now
    if shotAdapter.failed or healthAdapter.failed then
        shotVisual.fail('authority stopped')
        return
    end
    if not fresh() then
        return
    end
    for entity = 1, 2 do
        local generation, mode, ship = wanted(entity)
        local row = shotVisual.rows[entity]
        if not generation or not shotAdapter.support[entity] then
            shotVisual.rows[entity] = nil
        elseif not current(entity, row) or row.phase == 'preparing' and now - row.started > 0.3 then
            local ticket = serial()
            row = {
                gen = generation,
                mode = mode,
                ship = ship,
                ticket = ticket,
                started = now,
                phase = 'preparing',
            }
            shotVisual.rows[entity] = row
            event(
                'Prepare',
                {
                    entity = entity,
                    gen = generation,
                    ticket = ticket,
                    mode = mode,
                    revision = shotAdapter.stateRevision,
                }
            )
        elseif row.phase ~= 'preparing' and now >= (row.nextProof or 0) then
            proof(entity, row)
        end
    end
end
local frame = coopAdapter.frame
function coopAdapter.frame(now)
    frame(now)
    shotVisual.now = now
    if shotAdapter.ready and not shotVisual.setup then
        shotVisual.setup = true
        AddUITriggeredEvent(
            'X4LAN_ShotVisual',
            'Setup',
            { observed = cfg.projectile_visuals == true }
        )
    end
    if shotVisual.ready and not shotVisual.failed then
        local ok, err = pcall(shotVisual.frame, now)
        if not ok then
            shotVisual.fail(err)
        end
    end
end
local function guard(fn)
    return function(_, payload)
        local ok, err = pcall(fn, payload)
        if not ok then
            shotVisual.fail(err)
        end
    end
end
RegisterEvent('X4LAN_ShotVisual.Ready', function()
    shotVisual.ready = true
    log('READY')
end)
RegisterEvent('X4LAN_ShotVisual.Prepared', guard(shotVisual.prepared))
RegisterEvent('X4LAN_ShotVisual.Proven', guard(shotVisual.proven))
