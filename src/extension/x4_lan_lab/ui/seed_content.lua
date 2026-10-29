-- Read-only active-content check. Never changes extension or Workshop settings.
X4LAN_SEED_CONTENT = function(expected)
    local rows, seen = {}, {}
    for _, extension in ipairs(GetExtensionList()) do
        if extension.enabled then
            assert(
                not extension.error,
                'enabled extension reports an error: ' .. tostring(extension.id)
            )
            local name = tostring(extension.id)
            assert(
                name:match('^[%w_.-]+$') and not seen[name],
                'invalid/duplicate enabled extension'
            )
            local version = tostring(extension.version):gsub('%.', ''):gsub('^0+', '')
            if version == '' then
                version = '0'
            end
            assert(version:match('^%d+$'), 'unresolved extension version: ' .. name)
            seen[name] = true
            rows[#rows + 1] = name .. ':' .. version
        end
    end
    table.sort(rows)
    local actual = table.concat(rows, ',')
    assert(
        actual == expected,
        'enabled DLC/mods differ from the shared world; no settings were changed. Actual: '
            .. actual
    )
    return actual
end
