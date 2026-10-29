-- Original source-clock motion buffer. Units: metres, seconds and radians.
-- Rendering never advances the authoritative simulation or creates an object.
X4LAN_MOTION = {}
local M = X4LAN_MOTION
M.delay = 0.15
M.horizon = 0.35
M.stale = 0.75
-- Player motion modes (NMODE, see coop_game.lua). Outside 'normal' a remote ship is
-- driven by pose only. 'fast' is never sent: receivers infer it from a legacy/undeclared
-- stream whose sampled speed exceeds M.fastEnter (hysteresis M.fastExit, hold M.modeHold).
M.modes = { normal = true, boost = true, travel = true, highway = true }
M.profiles = {
    normal = { delay = 0.15, horizon = 0.35 },
    boost = { delay = 0.2, horizon = 0.35 },
    fast = { delay = 0.25, horizon = 0.5 },
    travel = { delay = 0.25, horizon = 0.5 },
    highway = { delay = 0.3, horizon = 0.6 },
}
M.fastEnter, M.fastExit, M.modeHold = 1200, 900, 1
-- Discontinuity speed allowance for declared non-normal modes (m/s). Normal mode keeps
-- the original rule, so in-sector teleports are still never interpolated there.
M.speedCap = { boost = 20000, travel = 20000, highway = 50000 }
-- Opt-in per track: the clock offset may rise this many seconds per arrival second, so a
-- faster observer clock (crystal drift) cannot slowly consume the interpolation delay.
M.leak = 0.002
-- Bounded render-clock slew (seconds per second) and the step that snaps instead.
M.slew, M.snap = 0.1, 1
-- Opt-in adaptive delay (track.adaptive): arrival latency above the minimum (the clock
-- offset) is queueing/jitter. Its decaying peak (half-life M.excessHalfLife s) widens the
-- interpolation delay up to M.maxDelay, so late samples interpolate instead of being
-- extrapolated past the horizon and corrected with a visible hop.
-- Widening slews at M.widen s/s (the replica runs briefly slower), narrowing at M.slew.
M.excessHalfLife, M.maxDelay, M.excessMargin, M.widen = 4, 0.8, 0.03, 0.5
function M.angle(v)
    return (v + math.pi) % (2 * math.pi) - math.pi
end
function M.valid(p, velocity)
    if type(p) ~= 'table' or #p ~= 6 or type(velocity) ~= 'table' or #velocity ~= 6 then
        return false
    end
    for i = 1, 6 do
        local a, b = p[i], velocity[i]
        if
            type(a) ~= 'number'
            or a ~= a
            or math.abs(a) > (i <= 3 and 50000000 or 2 * math.pi)
            or type(b) ~= 'number'
            or b ~= b
            or math.abs(b) > (i <= 3 and 100000 or 2 * math.pi)
        then
            return false
        end
    end
    return true
end
-- `stale` (optional) widens the sample gap over which a velocity is still estimated;
-- sparse NPC publication uses it. Absent, the original M.stale bound applies.
function M.velocity(previous, p, time, stale)
    local v = { 0, 0, 0, 0, 0, 0 }
    local dt = previous and time - previous.time or 0
    if dt > 0.001 and dt <= (stale or M.stale) then
        for i = 1, 6 do
            local delta = p[i] - previous.pose[i]
            if i > 3 then
                delta = M.angle(delta)
            end
            v[i] = delta / dt
            if math.abs(v[i]) > (i <= 3 and 100000 or 2 * math.pi) then
                return { 0, 0, 0, 0, 0, 0 }
            end
        end
    end
    return v
end
function M.speed(velocity)
    if type(velocity) ~= 'table' then
        return 0
    end
    return math.sqrt((velocity[1] or 0) ^ 2 + (velocity[2] or 0) ^ 2 + (velocity[3] or 0) ^ 2)
end
-- Optional track fields (all absent by default, preserving the original behaviour):
--  retainClock: a discontinuity resets samples but keeps the clock offset (both peers use
--   real time, so the mapping survives pauses and teleports of the same source process).
--  leak: see M.leak.  mode: declared mode of the incoming sample (see M.speedCap).
--  stale: per-track gap (s) before samples reset, instead of M.stale.
--  cap: speed allowance cap (m/s) for the incoming velocity, instead of M.speedCap[mode].
--  lastReset: 'gap' or 'jump', set when a push discards the earlier samples.
function M.push(track, time, p, velocity, arrival)
    if
        type(time) ~= 'number'
        or time ~= time
        or time < 0
        or time > 1e12
        or not M.valid(p, velocity)
    then
        return false
    end
    local samples = track.samples or {}
    local last = samples[#samples]
    if last and time <= last.time then
        return false
    end
    -- A pause or discontinuity must not interpolate a ship through intervening space.
    local reset = last and time - last.time > (track.stale or M.stale)
    if reset then
        track.lastReset = 'gap'
    end
    if last and not reset then
        local d2 = 0
        local speed = M.speed(last.velocity)
        for i = 1, 3 do
            d2 = d2 + (p[i] - last.pose[i]) ^ 2
        end
        local cap = track.cap or M.speedCap[track.mode or 'normal']
        if cap then
            speed = math.max(speed, math.min(cap, M.speed(velocity)))
        end
        reset = math.sqrt(d2) > 10000 + speed * (time - last.time)
        if reset then
            track.lastReset = 'jump'
        end
    end
    if reset then
        samples = {}
        track.resets = (track.resets or 0) + 1
        if not track.retainClock then
            track.offset = nil
        end
    end
    if track.offset and track.leak and track.arrival and arrival > track.arrival then
        track.offset = track.offset + track.leak * (arrival - track.arrival)
    end
    track.offset = math.min(track.offset or math.huge, arrival - time)
    if track.adaptive then
        -- A gap/jump reset (pause, teleport) is not network jitter; bound one sample's share.
        local excess = reset and 0 or math.min(
            M.maxDelay,
            math.max(0, arrival - time - track.offset)
        )
        local old = track.excess or 0
        if track.excessAt and arrival > track.excessAt then
            old = old * 0.5 ^ ((arrival - track.excessAt) / M.excessHalfLife)
        end
        track.excess, track.excessAt = math.max(excess, old), arrival
    end
    local pp, vv = {}, {}
    for i = 1, 6 do
        pp[i] = p[i]
        vv[i] = velocity[i]
    end
    samples[#samples + 1] = { time = time, pose = pp, velocity = vv }
    while #samples > 16 do
        table.remove(samples, 1)
    end
    track.samples = samples
    track.arrival = arrival
    return true
end
-- Source time to render at `now`: the observer-clock lag (offset + delay) moves toward
-- its wanted value at no more than `rate` seconds per second, so a delay or offset change
-- slows or speeds the replica slightly instead of making it hop by speed * change.
function M.clock(track, now, delay, rate)
    if not track.offset then
        return nil
    end
    local want = track.offset + (delay or M.delay)
    local lag, at = track.lag, track.lagAt
    if not lag or not at or now < at or math.abs(want - lag) > M.snap then
        lag = want
    else
        local step = ((track.adaptive and want > lag) and M.widen or rate or M.slew) * (now - at)
        lag = want > lag and math.min(want, lag + step) or math.max(want, lag - step)
    end
    track.lag, track.lagAt = lag, now
    return now - lag
end
-- Interpolation delay for a track: the profile delay, widened by the observed excess.
function M.delayFor(track, base)
    if not track.adaptive or not track.excess then
        return base
    end
    return math.max(base, math.min(M.maxDelay, track.excess + M.excessMargin))
end
function M.renderAt(track, target, horizon)
    local samples = track.samples
    if not samples or #samples == 0 or not target then
        return nil
    end
    horizon = horizon or M.horizon
    local first, last = samples[1], samples[#samples]
    local p = {}
    local velocity = { 0, 0, 0 }
    -- Optional second result: the derivative of rendered translation. Pose-only
    -- callers retain their API; Euler rates are not a native angular vector.
    if target <= first.time then
        for i = 1, 6 do
            p[i] = first.pose[i]
        end
        return p, velocity
    end
    for n = 2, #samples do
        local a, b = samples[n - 1], samples[n]
        if target <= b.time then
            local t = (target - a.time) / (b.time - a.time)
            for i = 1, 6 do
                local delta = b.pose[i] - a.pose[i]
                p[i] = a.pose[i] + (i > 3 and M.angle(delta) or delta) * t
                if i <= 3 then
                    velocity[i] = delta / (b.time - a.time)
                end
                if i > 3 then
                    p[i] = M.angle(p[i])
                end
            end
            return p, velocity
        end
    end
    -- After packet loss, freeze at the bounded prediction; never snap back to last pose.
    local dt = math.min(horizon, math.max(0, target - last.time))
    for i = 1, 6 do
        p[i] = last.pose[i] + last.velocity[i] * dt
        if i <= 3 and target - last.time < horizon then
            velocity[i] = last.velocity[i]
        end
        if i > 3 then
            p[i] = M.angle(p[i])
        end
    end
    return p, velocity
end
function M.render(track, now, delay)
    if not track.samples or #track.samples == 0 then
        return nil
    end
    return M.renderAt(track, now - track.offset - (delay or M.delay), M.horizon)
end
-- Dead reckoning for server-owned replicas (NPC UMOVE; see universe_game.lua). The rendered
-- pose is the latest authoritative sample (source time, pose, velocity) extrapolated to the
-- source clock every frame, plus a correction offset that decays with time constant tau:
-- a new sample never moves a replica in one frame unless its error exceeds the snap limit
-- (max(snapMin m, snapTime s of speed)), and a replica snaps at most once per snapEvery s.
-- Prediction runs `coast` source seconds past the last sample, then holds the pose.
M.dr = {
    tau = 0.3,
    snapMin = 1000,
    snapTime = 1,
    snapEvery = 1,
    coast = 8,
    back = 0.5,
    window = 3,
    bucket = 0.05,
    minSpan = 1,
    rateMin = 0.25,
    rateMax = 2,
    rateTau = 1.5,
    slew = 0.1,
    clockSnap = 1,
}
-- Source clock of one server stream: T = rate * now + base. The dedicated server steps a
-- fixed 0.05 s per frame at 12-20 fps, so its clock advances at about 0.6-1.1 x wall time.
-- A minimum-latency offset (M.push) can follow only a source clock at least as fast as the
-- observer's and leaks up at M.leak s/s; a slower server clock therefore ran the render clock
-- ahead of every sample. This mapping follows the least-delayed samples (the upper envelope
-- of source time over arrival) in a sliding window and estimates the rate from the best
-- sample of each half of it.
function M.drClock()
    return { points = {}, rate = 1 }
end
function M.drObserve(c, time, arrival)
    local d = M.dr
    local pts = c.points
    local last = pts[#pts]
    if last and arrival - last.a < d.bucket then
        if time - c.rate * arrival > last.t - c.rate * last.a then
            last.a, last.t = arrival, time
        end
    else
        pts[#pts + 1] = { a = arrival, t = time }
    end
    while #pts > 1 and arrival - pts[1].a > d.window do
        table.remove(pts, 1)
    end
    local mid = arrival - d.window / 2
    local b1, b2, s1, s2
    for _, p in ipairs(pts) do
        local s = p.t - c.rate * p.a
        if p.a < mid then
            if not s1 or s > s1 then
                s1, b1 = s, p
            end
        elseif not s2 or s > s2 then
            s2, b2 = s, p
        end
    end
    if b1 and b2 and b2.a - b1.a >= d.minSpan then
        local r = math.max(d.rateMin, math.min(d.rateMax, (b2.t - b1.t) / (b2.a - b1.a)))
        -- Time-based smoothing (time constant rateTau s), independent of the sample rate.
        if c.rateAt then
            c.rate = c.rate
                + (r - c.rate) * (1 - math.exp(-math.max(0, arrival - c.rateAt) / d.rateTau))
        else
            c.rate = r
        end
        c.rateAt = arrival
    end
    local base
    for _, p in ipairs(pts) do
        local s = p.t - c.rate * p.a
        if not base or s > base then
            base = s
        end
    end
    c.base = base
end
-- Source time rendered at local `now`: advances at the estimated rate and moves toward the
-- envelope at no more than `slew` source seconds per second (a step above clockSnap snaps).
function M.drTime(c, now)
    if not c.base then
        return nil
    end
    local want = c.rate * now + c.base
    local T, at = c.T, c.at
    if T and at and now < at then
        return T + c.rate * (now - at)
    end -- an earlier local time: no update
    if not T or not at then
        T = want
    else
        T = T + c.rate * (now - at)
        local delta, step = want - T, M.dr.slew * (now - at)
        if math.abs(delta) > M.dr.clockSnap then
            T = want
        else
            T = T + math.max(-step, math.min(step, delta))
        end
    end
    c.T, c.at = T, now
    return T
end
-- Arrival delay of a sample beyond the least-delayed one, in local seconds.
function M.drExcess(c, time, arrival)
    if not c.base then
        return 0
    end
    return math.max(0, (c.rate * arrival + c.base - time) / c.rate)
end
-- Shared prediction: the server evaluates exactly this function on the last sample it sent
-- to decide whether clients still show an NPC in the right place, and clients render it, so
-- without new packets both sides agree. Pure math on the sample (pose, velocity) and the
-- elapsed source time `dt`; no engine state, randomness or frame timing.
-- Constant turn rate: when the horizontal velocity heading matches the ship's yaw (under
-- either sign convention, within headingMatch rad), the velocity turns with the yaw rate and
-- the position follows the arc. Every angular change is capped at turnCap rad (ships end
-- their turns; the course then continues straight). Sideways drift or a mismatched heading
-- predicts a straight line.
M.dr.headingMatch, M.dr.turnCap = 0.5, math.pi / 2
function M.drPredict(pose, velocity, dt)
    local p = { pose[1], pose[2], pose[3], pose[4], pose[5], pose[6] }
    local v = { velocity[1], velocity[2], velocity[3] }
    local cap = M.dr.turnCap
    for i = 4, 6 do
        local w = velocity[i]
        local t = dt
        if w ~= 0 and math.abs(w * t) > cap then
            t = cap / math.abs(w) * (t < 0 and -1 or 1)
        end
        p[i] = M.angle(pose[i] + w * t)
    end
    local h = math.sqrt(v[1] ^ 2 + v[3] ^ 2)
    local w = velocity[4]
    local turn = 0
    if h >= 1 and w ~= 0 then
        -- The closer convention wins; near heading 0 or pi both match and the X4 convention
        -- (forward +z, positive yaw toward +x) is assumed.
        local heading = math.atan2(v[1], v[3])
        local a, b = math.abs(M.angle(heading - pose[4])), math.abs(M.angle(heading + pose[4]))
        if a < M.dr.headingMatch and a <= b + 0.05 then
            turn = w
        elseif b < M.dr.headingMatch then
            turn = -w
        end
    end
    if turn == 0 or math.abs(turn) < 1e-4 then
        for i = 1, 3 do
            p[i] = pose[i] + v[i] * dt
        end
        return p, v
    end
    local heading = math.atan2(v[1], v[3])
    local tc = math.min(math.abs(dt), cap / math.abs(turn)) * (dt < 0 and -1 or 1)
    local a = heading + turn * tc
    local x = pose[1] + h / turn * (math.cos(heading) - math.cos(a))
    local z = pose[3] + h / turn * (math.sin(a) - math.sin(heading))
    local rest = dt - tc
    v[1], v[3] = h * math.sin(a), h * math.cos(a)
    p[1], p[3] = x + v[1] * rest, z + v[3] * rest
    p[2] = pose[2] + v[2] * dt
    return p, v
end
local function project(track, T)
    local dt = math.max(-M.dr.back, math.min(M.dr.coast, T - track.time))
    local p, v = M.drPredict(track.pose, track.velocity, dt)
    if T - track.time >= M.dr.coast then
        v = { 0, 0, 0 }
    end
    return p, v
end
-- Rendered pose and translation velocity (m per local second; `rate` converts source
-- seconds) at source time T and local time now.
function M.drRender(track, T, now, rate)
    if not track.time or not T then
        return nil
    end
    local p, v = project(track, T)
    rate = rate or 1
    for i = 1, 3 do
        v[i] = v[i] * rate
    end
    local e = track.err
    if e then
        local k = math.exp(-math.max(0, now - track.errAt) / M.dr.tau)
        if k < 0.001 then
            track.err = nil
        else
            for i = 1, 6 do
                p[i] = p[i] + e[i] * k
                if i > 3 then
                    p[i] = M.angle(p[i])
                end
            end
            for i = 1, 3 do
                v[i] = v[i] - e[i] * k / M.dr.tau
            end
        end
    end
    return p, v
end
-- Accepts a sample: returns accepted, correction error (m) and whether the replica snapped.
-- `from` is the pose rendered now for a replica without a live track (a baseline pose), so
-- its first sample blends or snaps like any other. The clock observes every sample.
function M.drPush(track, clock, time, p, velocity, arrival, from)
    if
        type(time) ~= 'number'
        or time ~= time
        or time < 0
        or time > 1e12
        or not M.valid(p, velocity)
    then
        return false
    end
    if track.time and time <= track.time then
        return false
    end
    local T = clock.base and M.drTime(clock, arrival)
    local current = from
    if track.time and T then
        current = M.drRender(track, T, arrival)
    end
    M.drObserve(clock, time, arrival)
    T = M.drTime(clock, arrival)
    -- The snap limit uses the faster of the old and the new velocity: a hard stop (highway exit)
    -- is a large but continuous correction, not a teleport.
    local oldSpeed = track.velocity and M.speed(track.velocity) or 0
    local pp, vv = {}, {}
    for i = 1, 6 do
        pp[i] = p[i]
        vv[i] = velocity[i]
    end
    track.time, track.pose, track.velocity, track.err = time, pp, vv, nil
    if not current then
        return true, 0, false
    end
    local new = project(track, T)
    local e, d2 = {}, 0
    for i = 1, 6 do
        local delta = current[i] - new[i]
        if i > 3 then
            delta = M.angle(delta)
        else
            d2 = d2 + delta * delta
        end
        e[i] = delta
    end
    local err = math.sqrt(d2)
    if
        err > math.max(M.dr.snapMin, M.dr.snapTime * math.max(oldSpeed, M.speed(velocity)))
        and arrival >= (track.snapAt or -math.huge) + M.dr.snapEvery
    then
        track.snapAt = arrival
        return true, err, true
    end
    track.err, track.errAt = e, arrival
    return true, err, false
end
-- Effective render mode from the declared mode (nil for legacy senders) and sampled
-- speed. `state` keeps hysteresis: any non-normal indication holds for M.modeHold s.
function M.classify(state, declared, speed, now)
    local mode = declared and declared ~= 'normal' and M.profiles[declared] and declared or nil
    if not mode then
        if
            speed > M.fastEnter or (state.mode and state.mode ~= 'normal' and speed > M.fastExit)
        then
            mode = 'fast'
        end
    end
    if mode then
        state.mode, state.until_ = mode, now + M.modeHold
    elseif state.mode and state.mode ~= 'normal' and now < (state.until_ or 0) then
        mode = state.mode
    else
        mode = 'normal'
        state.mode = mode
    end
    return mode, M.profiles[mode]
end
