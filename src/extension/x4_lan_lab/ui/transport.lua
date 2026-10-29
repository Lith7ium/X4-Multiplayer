-- X4 restricts OS symbols in ffi.C and removes ffi.load.
-- Use a small Lua module via the supported unprotected package.loadlib path.
X4LAN_TRANSPORT = {}
local native
local function load_native()
    if not native then
        assert(
            package and package.loadlib,
            'native Lua modules unavailable; disable Protected UI mode'
        )
        if GetUISafeModeOption then
            assert(GetUISafeModeOption() == false, 'disable Protected UI mode')
        end
        local windows = package.config:sub(1, 1) == '\\'
        local path = X4LAN_NATIVE_PATH
            or (
                './extensions/x4_lan_lab/native/'
                .. (windows and 'x4lan_net_64.dll' or 'x4lan_net.so')
            )
        local initialise, message = package.loadlib(path, 'luaopen_x4lan_net')
        assert(initialise, message)
        native = initialise()
        assert(native and native.open, 'native module could not resolve the running Lua 5.1 ABI')
    end
    return native
end

function X4LAN_TRANSPORT.open(host, port)
    return load_native().open(host, port)
end

-- Dedicated authority control record: found, firstLine (see x4lan_net read_control).
function X4LAN_TRANSPORT.read_control(path)
    local module = load_native()
    assert(module.read_control, 'native module lacks read_control; rebuild the package')
    return module.read_control(path)
end
