-- Bounded host-native mission records. No local objective/reward authority.
X4LAN_MISSION_PROTOCOL = {}
local M = X4LAN_MISSION_PROTOCOL
local P = X4LAN_UNIVERSE_PROTOCOL
M.limit = 256
M.partSize = 500
M.maxParts = 24
M.transitions = { O = { O = true, A = true, C = true, F = true, X = true, W = true }, A = {
    A = true,
    C = true,
    F = true,
    X = true,
} }
function M.encode(v)
    return (v:gsub('.', function(c)
        return string.format('%02x', c:byte())
    end))
end
local function utf8(s)
    local i = 1
    while i <= #s do
        local c = s:byte(i)
        local n = c < 128 and 1
            or c >= 194 and c <= 223 and 2
            or c >= 224 and c <= 239 and 3
            or c >= 240 and c <= 244 and 4
        if not n or i + n - 1 > #s then
            return false
        end
        for j = 1, n - 1 do
            local b = s:byte(i + j)
            if b < 128 or b > 191 then
                return false
            end
        end
        local b = s:byte(i + 1)
        if
            n == 3 and (c == 224 and b < 160 or c == 237 and b > 159)
            or n == 4 and (c == 240 and b < 144 or c == 244 and b > 143)
        then
            return false
        end
        i = i + n
    end
    return true
end
function M.text(v, max)
    if type(v) ~= 'string' or #v == 0 or #v > max * 2 or #v % 2 ~= 0 or v:find('[^0-9a-f]') then
        return nil
    end
    local s = v:gsub('..', function(c)
        return string.char(tonumber(c, 16))
    end)
    if not utf8(s) or s:find('[%z\1-\8\11-\31\127]') then
        return nil
    end
    return s
end
function M.parse(payload)
    local f = P.split(payload, ',')
    if
        #f ~= 9
        or f[1] ~= 'quota'
        or not ({ O = true, A = true, C = true, F = true, X = true, W = true })[f[2]]
    then
        return nil
    end
    local d, n, r = P.integer(f[3], 1000000), P.integer(f[4], 1000000), P.integer(f[5])
    if
        not d
        or not n
        or n < 1
        or d > n
        or not r
        or #f[6] > 64
        or not f[6]:match('^[a-z][a-z0-9_]*$')
    then
        return nil
    end
    if f[2] == 'O' and d ~= 0 or f[2] == 'C' and d ~= n then
        return nil
    end
    local title, description, objective = M.text(f[7], 256), M.text(f[8], 4096), M.text(f[9], 512)
    if not title or not description or not objective then
        return nil
    end
    return {
        kind = f[1],
        state = f[2],
        done = d,
        total = n,
        reward = r,
        faction = f[6],
        title = title,
        description = description,
        objective = objective,
    }
end
function M.advances(old, new)
    if not old then
        return true
    end
    if
        not M.transitions[old.state]
        or not M.transitions[old.state][new.state]
        or new.done < old.done
    then
        return false
    end
    for _, k in ipairs({ 'kind', 'total', 'reward', 'faction', 'title', 'description', 'objective' }) do
        if old[k] ~= new[k] then
            return false
        end
    end
    return true
end
function M.assembly()
    local a = { records = {}, pending = {} }
    function a:add(f, now)
        if #f ~= 10 or f[1] ~= 'MSTATE' or not P.hex(f[4]) or self.epoch and f[4] ~= self.epoch then
            return nil
        end
        local id, rev, part, total, digest =
            P.integer(f[5], M.limit),
            P.integer(f[6]),
            P.integer(f[7], M.maxParts),
            P.integer(f[8], M.maxParts),
            P.integer(f[9], 4294967295)
        if
            not id
            or id < 1
            or not rev
            or rev < 1
            or not part
            or not total
            or part < 1
            or part > total
            or not digest
            or #f[10] < 1
            or #f[10] > M.partSize
            or f[10]:find('[^a-zA-Z0-9_,]')
        then
            return nil
        end
        local old = self.records[id]
        if old and rev <= old.rev then
            return nil
        end
        local p = self.pending[id]
        if p and rev < p.rev then
            return nil
        end
        if not p or rev > p.rev or now - p.at >= 10 then
            p =
                { epoch = f[4], rev = rev, total = total, digest = digest, parts = {}, at = now, got = 0 }
            self.pending[id] = p
        end
        if
            p.epoch ~= f[4]
            or p.total ~= total
            or p.digest ~= digest
            or p.parts[part] and p.parts[part] ~= f[10]
        then
            return nil
        end
        if not p.parts[part] then
            p.got = p.got + 1
            p.parts[part] = f[10]
        end
        if p.got ~= total then
            return nil
        end
        local payload = table.concat(p.parts)
        if P.checksum({ payload }) ~= digest then
            return nil
        end
        local row = M.parse(payload)
        if not row or not M.advances(old and old.row, row) then
            return nil
        end
        local r =
            { id = id, rev = rev, row = row, payload = payload, digest = digest, epoch = f[4] }
        self.epoch = f[4]
        self.records[id] = r
        self.pending[id] = nil
        return r
    end
    return a
end
function M.packets(lease, epoch, r)
    local packets = {}
    local total = math.ceil(#r.payload / M.partSize)
    for i = 1, total do
        packets[i] = table.concat(
            {
                'MSTATE',
                1,
                lease,
                epoch,
                r.id,
                r.rev,
                i,
                total,
                string.format('%.0f', r.digest),
                r.payload:sub((i - 1) * M.partSize + 1, i * M.partSize),
            },
            '|'
        )
    end
    return packets
end
