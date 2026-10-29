-- Original source-clock motion buffer. Units: metres, seconds and radians.
-- Rendering never advances the authoritative simulation or creates an object.
X4LAN_MOTION = {}
local motionBuffer = X4LAN_MOTION
motionBuffer.delay = 0.15
motionBuffer.horizon = 0.35
motionBuffer.stale = 0.75
-- Player motion modes (NMODE, see coop_game.lua). Outside 'normal' a remote ship is
-- driven by pose only. 'fast' is never sent: receivers infer it from a legacy/undeclared
-- stream whose sampled speed exceeds motionBuffer.fastEnter (hysteresis motionBuffer.fastExit, hold motionBuffer.modeHold).
motionBuffer.modes = { normal = true, boost = true, travel = true, highway = true }
motionBuffer.profiles = {
    normal = { delay = 0.15, horizon = 0.35 },
    boost = { delay = 0.2, horizon = 0.35 },
    fast = { delay = 0.25, horizon = 0.5 },
    travel = { delay = 0.25, horizon = 0.5 },
    highway = { delay = 0.3, horizon = 0.6 },
}
motionBuffer.fastEnter, motionBuffer.fastExit, motionBuffer.modeHold = 1200, 900, 1
-- Discontinuity speed allowance for declared non-normal modes (m/s). Normal mode keeps
-- the original rule, so in-sector teleports are still never interpolated there.
motionBuffer.speedCap = { boost = 20000, travel = 20000, highway = 50000 }
-- Opt-in per track: the clock offset may rise this many seconds per arrival second, so a
-- faster observer clock (crystal drift) cannot slowly consume the interpolation delay.
motionBuffer.leak = 0.002
-- Bounded render-clock slew (seconds per second) and the step that snaps instead.
motionBuffer.slew, motionBuffer.snap = 0.1, 1
-- Opt-in adaptive delay (track.adaptive): arrival latency above the minimum (the clock
-- offset) is queueing/jitter. Its decaying peak (half-life motionBuffer.excessHalfLife s) widens the
-- interpolation delay up to motionBuffer.maxDelay, so late samples interpolate instead of being
-- extrapolated past the horizon and corrected with a visible hop.
-- Widening slews at motionBuffer.widen s/s (the replica runs briefly slower), narrowing at motionBuffer.slew.
motionBuffer.excessHalfLife, motionBuffer.maxDelay, motionBuffer.excessMargin, motionBuffer.widen =
    4, 0.8, 0.03, 0.5
function motionBuffer.angle(angle)
    return (angle + math.pi) % (2 * math.pi) - math.pi
end
function motionBuffer.valid(pose, velocity)
    if type(pose) ~= 'table' or #pose ~= 6 or type(velocity) ~= 'table' or #velocity ~= 6 then
        return false
    end
    for i = 1, 6 do
        local coordinate, rate = pose[i], velocity[i]
        if
            type(coordinate) ~= 'number'
            or coordinate ~= coordinate
            or math.abs(coordinate) > (i <= 3 and 50000000 or 2 * math.pi)
            or type(rate) ~= 'number'
            or rate ~= rate
            or math.abs(rate) > (i <= 3 and 100000 or 2 * math.pi)
        then
            return false
        end
    end
    return true
end
-- `stale` (optional) widens the sample gap over which a velocity is still estimated;
-- sparse NPC publication uses it. Absent, the original motionBuffer.stale bound applies.
function motionBuffer.velocity(previous, pose, time, stale)
    local velocity = { 0, 0, 0, 0, 0, 0 }
    local elapsed = previous and time - previous.time or 0
    if elapsed > 0.001 and elapsed <= (stale or motionBuffer.stale) then
        for i = 1, 6 do
            local delta = pose[i] - previous.pose[i]
            if i > 3 then
                delta = motionBuffer.angle(delta)
            end
            velocity[i] = delta / elapsed
            if math.abs(velocity[i]) > (i <= 3 and 100000 or 2 * math.pi) then
                return { 0, 0, 0, 0, 0, 0 }
            end
        end
    end
    return velocity
end
function motionBuffer.speed(velocity)
    if type(velocity) ~= 'table' then
        return 0
    end
    return math.sqrt((velocity[1] or 0) ^ 2 + (velocity[2] or 0) ^ 2 + (velocity[3] or 0) ^ 2)
end
-- Optional track fields (all absent by default, preserving the original behaviour):
--  retainClock: a discontinuity resets samples but keeps the clock offset (both peers use
--   real time, so the mapping survives pauses and teleports of the same source process).
--  leak: see motionBuffer.leak.  mode: declared mode of the incoming sample (see motionBuffer.speedCap).
--  stale: per-track gap (s) before samples reset, instead of motionBuffer.stale.
--  cap: speed allowance cap (m/s) for the incoming velocity, instead of motionBuffer.speedCap[mode].
--  lastReset: 'gap' or 'jump', set when a push discards the earlier samples.
function motionBuffer.push(track, time, pose, velocity, arrival)
    if
        type(time) ~= 'number'
        or time ~= time
        or time < 0
        or time > 1e12
        or not motionBuffer.valid(pose, velocity)
    then
        return false
    end
    local samples = track.samples or {}
    local last = samples[#samples]
    if last and time <= last.time then
        return false
    end
    -- A pause or discontinuity must not interpolate a ship through intervening space.
    local reset = last and time - last.time > (track.stale or motionBuffer.stale)
    if reset then
        track.lastReset = 'gap'
    end
    if last and not reset then
        local distanceSquared = 0
        local speed = motionBuffer.speed(last.velocity)
        for i = 1, 3 do
            distanceSquared = distanceSquared + (pose[i] - last.pose[i]) ^ 2
        end
        local cap = track.cap or motionBuffer.speedCap[track.mode or 'normal']
        if cap then
            speed = math.max(speed, math.min(cap, motionBuffer.speed(velocity)))
        end
        reset = math.sqrt(distanceSquared) > 10000 + speed * (time - last.time)
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
        local excess = reset and 0
            or math.min(motionBuffer.maxDelay, math.max(0, arrival - time - track.offset))
        local old = track.excess or 0
        if track.excessAt and arrival > track.excessAt then
            old = old * 0.5 ^ ((arrival - track.excessAt) / motionBuffer.excessHalfLife)
        end
        track.excess, track.excessAt = math.max(excess, old), arrival
    end
    local poseCopy, velocityCopy = {}, {}
    for i = 1, 6 do
        poseCopy[i] = pose[i]
        velocityCopy[i] = velocity[i]
    end
    samples[#samples + 1] = { time = time, pose = poseCopy, velocity = velocityCopy }
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
function motionBuffer.clock(track, now, delay, rate)
    if not track.offset then
        return nil
    end
    local desiredLag = track.offset + (delay or motionBuffer.delay)
    local lag, lastAt = track.lag, track.lagAt
    if not lag or not lastAt or now < lastAt or math.abs(desiredLag - lag) > motionBuffer.snap then
        lag = desiredLag
    else
        local step = (
            (track.adaptive and desiredLag > lag) and motionBuffer.widen
            or rate
            or motionBuffer.slew
        ) * (now - lastAt)
        lag = desiredLag > lag and math.min(desiredLag, lag + step)
            or math.max(desiredLag, lag - step)
    end
    track.lag, track.lagAt = lag, now
    return now - lag
end
-- Interpolation delay for a track: the profile delay, widened by the observed excess.
function motionBuffer.delayFor(track, base)
    if not track.adaptive or not track.excess then
        return base
    end
    return math.max(base, math.min(motionBuffer.maxDelay, track.excess + motionBuffer.excessMargin))
end
function motionBuffer.renderAt(track, target, horizon)
    local samples = track.samples
    if not samples or #samples == 0 or not target then
        return nil
    end
    horizon = horizon or motionBuffer.horizon
    local first, last = samples[1], samples[#samples]
    local pose = {}
    local velocity = { 0, 0, 0 }
    -- Optional second result: the derivative of rendered translation. Pose-only
    -- callers retain their API; Euler rates are not a native angular vector.
    if target <= first.time then
        for i = 1, 6 do
            pose[i] = first.pose[i]
        end
        return pose, velocity
    end
    for sampleIndex = 2, #samples do
        local earlier, later = samples[sampleIndex - 1], samples[sampleIndex]
        if target <= later.time then
            local fraction = (target - earlier.time) / (later.time - earlier.time)
            for i = 1, 6 do
                local delta = later.pose[i] - earlier.pose[i]
                pose[i] = earlier.pose[i]
                    + (i > 3 and motionBuffer.angle(delta) or delta) * fraction
                if i <= 3 then
                    velocity[i] = delta / (later.time - earlier.time)
                end
                if i > 3 then
                    pose[i] = motionBuffer.angle(pose[i])
                end
            end
            return pose, velocity
        end
    end
    -- After packet loss, freeze at the bounded prediction; never snap back to last pose.
    local elapsed = math.min(horizon, math.max(0, target - last.time))
    for i = 1, 6 do
        pose[i] = last.pose[i] + last.velocity[i] * elapsed
        if i <= 3 and target - last.time < horizon then
            velocity[i] = last.velocity[i]
        end
        if i > 3 then
            pose[i] = motionBuffer.angle(pose[i])
        end
    end
    return pose, velocity
end
function motionBuffer.render(track, now, delay)
    if not track.samples or #track.samples == 0 then
        return nil
    end
    return motionBuffer.renderAt(
        track,
        now - track.offset - (delay or motionBuffer.delay),
        motionBuffer.horizon
    )
end
-- Dead reckoning for server-owned replicas (NPC UMOVE; see universe_game.lua). The rendered
-- pose is the latest authoritative sample (source time, pose, velocity) extrapolated to the
-- source clock every frame, plus a correction offset that decays with time constant tau:
-- a new sample never moves a replica in one frame unless its error exceeds the snap limit
-- (max(snapMin m, snapTime s of speed)), and a replica snaps at most once per snapEvery s.
-- Prediction runs `coast` source seconds past the last sample, then holds the pose.
motionBuffer.dr = {
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
-- A minimum-latency offset (motionBuffer.push) can follow only a source clock at least as fast as the
-- observer's and leaks up at motionBuffer.leak s/s; a slower server clock therefore ran the render clock
-- ahead of every sample. This mapping follows the least-delayed samples (the upper envelope
-- of source time over arrival) in a sliding window and estimates the rate from the best
-- sample of each half of it.
function motionBuffer.drClock()
    return { points = {}, rate = 1 }
end
function motionBuffer.drObserve(clock, time, arrival)
    local settings = motionBuffer.dr
    local points = clock.points
    local last = points[#points]
    if last and arrival - last.a < settings.bucket then
        if time - clock.rate * arrival > last.t - clock.rate * last.a then
            last.a, last.t = arrival, time
        end
    else
        points[#points + 1] = { a = arrival, t = time }
    end
    while #points > 1 and arrival - points[1].a > settings.window do
        table.remove(points, 1)
    end
    local midpoint = arrival - settings.window / 2
    local firstBest, secondBest, firstOffset, secondOffset
    for _, point in ipairs(points) do
        local offset = point.t - clock.rate * point.a
        if point.a < midpoint then
            if not firstOffset or offset > firstOffset then
                firstOffset, firstBest = offset, point
            end
        elseif not secondOffset or offset > secondOffset then
            secondOffset, secondBest = offset, point
        end
    end
    if firstBest and secondBest and secondBest.a - firstBest.a >= settings.minSpan then
        local estimatedRate = math.max(
            settings.rateMin,
            math.min(settings.rateMax, (secondBest.t - firstBest.t) / (secondBest.a - firstBest.a))
        )
        -- Time-based smoothing (time constant rateTau s), independent of the sample rate.
        if clock.rateAt then
            clock.rate = clock.rate
                + (estimatedRate - clock.rate)
                    * (1 - math.exp(-math.max(0, arrival - clock.rateAt) / settings.rateTau))
        else
            clock.rate = estimatedRate
        end
        clock.rateAt = arrival
    end
    local base
    for _, point in ipairs(points) do
        local offset = point.t - clock.rate * point.a
        if not base or offset > base then
            base = offset
        end
    end
    clock.base = base
end
-- Source time rendered at local `now`: advances at the estimated rate and moves toward the
-- envelope at no more than `slew` source seconds per second (a step above clockSnap snaps).
function motionBuffer.drTime(clock, now)
    if not clock.base then
        return nil
    end
    local desiredTime = clock.rate * now + clock.base
    local renderTime, lastAt = clock.T, clock.at
    if renderTime and lastAt and now < lastAt then
        return renderTime + clock.rate * (now - lastAt)
    end -- an earlier local time: no update
    if not renderTime or not lastAt then
        renderTime = desiredTime
    else
        renderTime = renderTime + clock.rate * (now - lastAt)
        local delta, step = desiredTime - renderTime, motionBuffer.dr.slew * (now - lastAt)
        if math.abs(delta) > motionBuffer.dr.clockSnap then
            renderTime = desiredTime
        else
            renderTime = renderTime + math.max(-step, math.min(step, delta))
        end
    end
    clock.T, clock.at = renderTime, now
    return renderTime
end
-- Arrival delay of a sample beyond the least-delayed one, in local seconds.
function motionBuffer.drExcess(clock, time, arrival)
    if not clock.base then
        return 0
    end
    return math.max(0, (clock.rate * arrival + clock.base - time) / clock.rate)
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
motionBuffer.dr.headingMatch, motionBuffer.dr.turnCap = 0.5, math.pi / 2
function motionBuffer.drPredict(pose, velocity, dt)
    local predictedPose = { pose[1], pose[2], pose[3], pose[4], pose[5], pose[6] }
    local predictedVelocity = { velocity[1], velocity[2], velocity[3] }
    local cap = motionBuffer.dr.turnCap
    for i = 4, 6 do
        local angularRate = velocity[i]
        local elapsed = dt
        if angularRate ~= 0 and math.abs(angularRate * elapsed) > cap then
            elapsed = cap / math.abs(angularRate) * (elapsed < 0 and -1 or 1)
        end
        predictedPose[i] = motionBuffer.angle(pose[i] + angularRate * elapsed)
    end
    local horizontalSpeed = math.sqrt(predictedVelocity[1] ^ 2 + predictedVelocity[3] ^ 2)
    local angularRate = velocity[4]
    local turn = 0
    if horizontalSpeed >= 1 and angularRate ~= 0 then
        -- The closer convention wins; near heading 0 or pi both match and the X4 convention
        -- (forward +z, positive yaw toward +x) is assumed.
        local heading = math.atan2(predictedVelocity[1], predictedVelocity[3])
        local angleValue, oppositeError =
            math.abs(motionBuffer.angle(heading - pose[4])),
            math.abs(motionBuffer.angle(heading + pose[4]))
        if angleValue < motionBuffer.dr.headingMatch and angleValue <= oppositeError + 0.05 then
            turn = angularRate
        elseif oppositeError < motionBuffer.dr.headingMatch then
            turn = -angularRate
        end
    end
    if turn == 0 or math.abs(turn) < 1e-4 then
        for i = 1, 3 do
            predictedPose[i] = pose[i] + predictedVelocity[i] * dt
        end
        return predictedPose, predictedVelocity
    end
    local heading = math.atan2(predictedVelocity[1], predictedVelocity[3])
    local turnTime = math.min(math.abs(dt), cap / math.abs(turn)) * (dt < 0 and -1 or 1)
    local angleValue = heading + turn * turnTime
    local x = pose[1] + horizontalSpeed / turn * (math.cos(heading) - math.cos(angleValue))
    local z = pose[3] + horizontalSpeed / turn * (math.sin(angleValue) - math.sin(heading))
    local rest = dt - turnTime
    predictedVelocity[1], predictedVelocity[3] =
        horizontalSpeed * math.sin(angleValue), horizontalSpeed * math.cos(angleValue)
    predictedPose[1], predictedPose[3] =
        x + predictedVelocity[1] * rest, z + predictedVelocity[3] * rest
    predictedPose[2] = pose[2] + predictedVelocity[2] * dt
    return predictedPose, predictedVelocity
end
local function project(track, renderTime)
    local elapsed =
        math.max(-motionBuffer.dr.back, math.min(motionBuffer.dr.coast, renderTime - track.time))
    local pose, velocity = motionBuffer.drPredict(track.pose, track.velocity, elapsed)
    if renderTime - track.time >= motionBuffer.dr.coast then
        velocity = { 0, 0, 0 }
    end
    return pose, velocity
end
-- Rendered pose and translation velocity (m per local second; `rate` converts source
-- seconds) at source time T and local time now.
function motionBuffer.drRender(track, renderTime, now, rate)
    if not track.time or not renderTime then
        return nil
    end
    local pose, velocity = project(track, renderTime)
    rate = rate or 1
    for i = 1, 3 do
        velocity[i] = velocity[i] * rate
    end
    local error = track.err
    if error then
        local decay = math.exp(-math.max(0, now - track.errAt) / motionBuffer.dr.tau)
        if decay < 0.001 then
            track.err = nil
        else
            for i = 1, 6 do
                pose[i] = pose[i] + error[i] * decay
                if i > 3 then
                    pose[i] = motionBuffer.angle(pose[i])
                end
            end
            for i = 1, 3 do
                velocity[i] = velocity[i] - error[i] * decay / motionBuffer.dr.tau
            end
        end
    end
    return pose, velocity
end
-- Accepts a sample: returns accepted, correction error (m) and whether the replica snapped.
-- `from` is the pose rendered now for a replica without a live track (a baseline pose), so
-- its first sample blends or snaps like any other. The clock observes every sample.
function motionBuffer.drPush(track, clock, time, pose, velocity, arrival, from)
    if
        type(time) ~= 'number'
        or time ~= time
        or time < 0
        or time > 1e12
        or not motionBuffer.valid(pose, velocity)
    then
        return false
    end
    if track.time and time <= track.time then
        return false
    end
    local renderTime = clock.base and motionBuffer.drTime(clock, arrival)
    local current = from
    if track.time and renderTime then
        current = motionBuffer.drRender(track, renderTime, arrival)
    end
    motionBuffer.drObserve(clock, time, arrival)
    renderTime = motionBuffer.drTime(clock, arrival)
    -- The snap limit uses the faster of the old and the new velocity: a hard stop (highway exit)
    -- is a large but continuous correction, not a teleport.
    local oldSpeed = track.velocity and motionBuffer.speed(track.velocity) or 0
    local poseCopy, velocityCopy = {}, {}
    for i = 1, 6 do
        poseCopy[i] = pose[i]
        velocityCopy[i] = velocity[i]
    end
    track.time, track.pose, track.velocity, track.err = time, poseCopy, velocityCopy, nil
    if not current then
        return true, 0, false
    end
    local predicted = project(track, renderTime)
    local error, distanceSquared = {}, 0
    for i = 1, 6 do
        local delta = current[i] - predicted[i]
        if i > 3 then
            delta = motionBuffer.angle(delta)
        else
            distanceSquared = distanceSquared + delta * delta
        end
        error[i] = delta
    end
    local err = math.sqrt(distanceSquared)
    if
        err
            > math.max(
                motionBuffer.dr.snapMin,
                motionBuffer.dr.snapTime * math.max(oldSpeed, motionBuffer.speed(velocity))
            )
        and arrival >= (track.snapAt or -math.huge) + motionBuffer.dr.snapEvery
    then
        track.snapAt = arrival
        return true, err, true
    end
    track.err, track.errAt = error, arrival
    return true, err, false
end
-- Effective render mode from the declared mode (nil for legacy senders) and sampled
-- speed. `state` keeps hysteresis: any non-normal indication holds for motionBuffer.modeHold s.
function motionBuffer.classify(state, declared, speed, now)
    local mode = declared and declared ~= 'normal' and motionBuffer.profiles[declared] and declared
        or nil
    if not mode then
        if
            speed > motionBuffer.fastEnter
            or (state.mode and state.mode ~= 'normal' and speed > motionBuffer.fastExit)
        then
            mode = 'fast'
        end
    end
    if mode then
        state.mode, state.until_ = mode, now + motionBuffer.modeHold
    elseif state.mode and state.mode ~= 'normal' and now < (state.until_ or 0) then
        mode = state.mode
    else
        mode = 'normal'
        state.mode = mode
    end
    return mode, motionBuffer.profiles[mode]
end
