//! Rust port of `src/native/net.c`: a minimal Lua 5.1/LuaJIT UDP module for X4.
//!
//! Loaded by `src/extension/x4_lan_lab/ui/transport.lua` through
//! `package.loadlib(path, "luaopen_x4lan_net")`. Like the C original it links no Lua
//! library: the Lua 5.1 C API is resolved at runtime from the runtime X4 has already
//! loaded (`GetModuleHandleA("lua51_64.dll")` + `GetProcAddress` on Windows,
//! `dlsym(RTLD_DEFAULT, ..)` elsewhere). No game hooks or game memory offsets.
//!
//! Lua-facing API (identical to net.c):
//!   `luaopen_x4lan_net()` -> `{ open = function(host, port) }`, or nothing when the
//!   Lua ABI cannot be resolved.
//!   `open(host, port)` -> connection userdata (metatable `"x4lan.udp.v1"`), raising
//!   the same Lua error messages as net.c on invalid input or socket failure.
//!   `conn:send(payload)` -> boolean; `conn:receive()` -> string or nil;
//!   `conn:close()`; `__gc` closes.
//!
//! Error handling across the FFI boundary: `luaL_error` (and any API call that can run
//! out of memory) does not return. LuaJIT on x64 raises it as a native unwind
//! (SEH on Windows, DWARF elsewhere) and PUC Lua uses `longjmp`. Every Lua entry point
//! and every resolved API pointer therefore uses the `"C-unwind"` ABI, and no value
//! with a destructor is alive in a frame while such a call is made.
#![allow(non_camel_case_types)]

use std::ffi::{c_char, c_int, c_void, CStr};
use std::net::{Ipv4Addr, SocketAddrV4, UdpSocket};
use std::sync::OnceLock;

/// Maximum datagram, identical to net.c and the relay's `MAX_PACKET`.
pub const MAX_PACKET: usize = 768;
const METATABLE: &CStr = c"x4lan.udp.v1";
const REGISTRYINDEX: c_int = -10000;
/// `INET_ADDRSTRLEN` from the platform C headers; net.c rejects hosts of this length or
/// more. The constant differs: 22 in Windows' ws2tcpip.h, 16 in POSIX <netinet/in.h>.
#[cfg(windows)]
const INET_ADDRSTRLEN: usize = 22;
#[cfg(not(windows))]
const INET_ADDRSTRLEN: usize = 16;

#[repr(C)]
pub struct lua_State {
    _opaque: [u8; 0],
}

type lua_CFunction = unsafe extern "C-unwind" fn(*mut lua_State) -> c_int;

/// The sixteen Lua 5.1 C API entries net.c resolves, with their lua.h/lauxlib.h types.
struct Api {
    checklstring: unsafe extern "C-unwind" fn(*mut lua_State, c_int, *mut usize) -> *const c_char,
    checknumber: unsafe extern "C-unwind" fn(*mut lua_State, c_int) -> f64,
    checkudata: unsafe extern "C-unwind" fn(*mut lua_State, c_int, *const c_char) -> *mut c_void,
    newmetatable: unsafe extern "C-unwind" fn(*mut lua_State, *const c_char) -> c_int,
    error: unsafe extern "C-unwind" fn(*mut lua_State, *const c_char, ...) -> c_int,
    newuserdata: unsafe extern "C-unwind" fn(*mut lua_State, usize) -> *mut c_void,
    pushboolean: unsafe extern "C-unwind" fn(*mut lua_State, c_int),
    pushnil: unsafe extern "C-unwind" fn(*mut lua_State),
    pushlstring: unsafe extern "C-unwind" fn(*mut lua_State, *const c_char, usize),
    pushcclosure: unsafe extern "C-unwind" fn(*mut lua_State, lua_CFunction, c_int),
    pushvalue: unsafe extern "C-unwind" fn(*mut lua_State, c_int),
    setfield: unsafe extern "C-unwind" fn(*mut lua_State, c_int, *const c_char),
    getfield: unsafe extern "C-unwind" fn(*mut lua_State, c_int, *const c_char),
    setmetatable: unsafe extern "C-unwind" fn(*mut lua_State, c_int) -> c_int,
    createtable: unsafe extern "C-unwind" fn(*mut lua_State, c_int, c_int),
    settop: unsafe extern "C-unwind" fn(*mut lua_State, c_int),
}

static API: OnceLock<Api> = OnceLock::new();

#[cfg(windows)]
mod platform {
    use std::ffi::{c_char, c_void};

    #[link(name = "kernel32")]
    extern "system" {
        fn GetModuleHandleA(name: *const c_char) -> *mut c_void;
        fn GetProcAddress(module: *mut c_void, name: *const c_char) -> *mut c_void;
    }

    /// The module handle of the game's already-loaded Lua runtime, never loading one.
    pub fn runtime() -> Option<*mut c_void> {
        // SAFETY: a NUL-terminated module name; GetModuleHandleA does not change refcounts.
        let handle = unsafe { GetModuleHandleA(c"lua51_64.dll".as_ptr()) };
        (!handle.is_null()).then_some(handle)
    }

    pub fn symbol(runtime: *mut c_void, name: &std::ffi::CStr) -> *mut c_void {
        // SAFETY: valid module handle from `runtime` and a NUL-terminated name.
        unsafe { GetProcAddress(runtime, name.as_ptr()) }
    }
}

#[cfg(not(windows))]
mod platform {
    use std::ffi::{c_char, c_void};

    #[cfg_attr(any(target_os = "linux", target_os = "android"), link(name = "dl"))]
    extern "C" {
        fn dlsym(handle: *mut c_void, name: *const c_char) -> *mut c_void;
    }

    /// RTLD_DEFAULT: search the global scope (the game's runtime), as net.c does.
    pub fn runtime() -> Option<*mut c_void> {
        Some(std::ptr::null_mut())
    }

    pub fn symbol(runtime: *mut c_void, name: &std::ffi::CStr) -> *mut c_void {
        // SAFETY: RTLD_DEFAULT (null on glibc/musl) and a NUL-terminated name.
        unsafe { dlsym(runtime, name.as_ptr()) }
    }
}

/// Resolve every symbol or none, like net.c's `resolve_lua`.
// Each transmute's target type is the `Api` field it initialises.
#[allow(clippy::missing_transmute_annotations)]
fn resolve() -> Option<&'static Api> {
    if let Some(api) = API.get() {
        return Some(api);
    }
    let runtime = platform::runtime()?;
    macro_rules! get {
        ($name:literal) => {{
            let address = platform::symbol(runtime, $name);
            if address.is_null() {
                return None;
            }
            // SAFETY: the symbol is the named Lua 5.1 C API function; the field type
            // states its lua.h/lauxlib.h signature. Data and function pointers have the
            // same size and representation on every platform X4 supports.
            unsafe { std::mem::transmute::<*mut c_void, _>(address) }
        }};
    }
    let api = Api {
        checklstring: get!(c"luaL_checklstring"),
        checknumber: get!(c"luaL_checknumber"),
        checkudata: get!(c"luaL_checkudata"),
        newmetatable: get!(c"luaL_newmetatable"),
        error: get!(c"luaL_error"),
        newuserdata: get!(c"lua_newuserdata"),
        pushboolean: get!(c"lua_pushboolean"),
        pushnil: get!(c"lua_pushnil"),
        pushlstring: get!(c"lua_pushlstring"),
        pushcclosure: get!(c"lua_pushcclosure"),
        pushvalue: get!(c"lua_pushvalue"),
        setfield: get!(c"lua_setfield"),
        getfield: get!(c"lua_getfield"),
        setmetatable: get!(c"lua_setmetatable"),
        createtable: get!(c"lua_createtable"),
        settop: get!(c"lua_settop"),
    };
    Some(API.get_or_init(|| api))
}

fn api() -> &'static Api {
    // Lua entry points other than luaopen are only reachable after a successful resolve.
    API.get().expect("x4lan_net: Lua API used before luaopen_x4lan_net")
}

/// Userdata payload. `None` is net.c's `INVALID_NET`; dropping the socket closes it.
struct Connection {
    socket: Option<UdpSocket>,
}

/// Strict dotted-quad parser with the inet_pton(AF_INET) rules of glibc/musl/BSD:
/// exactly four decimal octets, each 0..=255, no leading zeros, nothing else.
pub fn parse_ipv4(text: &[u8]) -> Option<Ipv4Addr> {
    let mut octets = [0u8; 4];
    let mut index = 0usize;
    let mut value: u32 = 0;
    let mut digits = 0usize;
    for &byte in text {
        match byte {
            b'0'..=b'9' => {
                if digits > 0 && value == 0 {
                    return None; // leading zero
                }
                value = value * 10 + u32::from(byte - b'0');
                digits += 1;
                if value > 255 {
                    return None;
                }
            }
            b'.' => {
                if digits == 0 || index == 3 {
                    return None;
                }
                octets[index] = value as u8;
                index += 1;
                value = 0;
                digits = 0;
            }
            _ => return None,
        }
    }
    if digits == 0 || index != 3 {
        return None;
    }
    octets[3] = value as u8;
    Some(Ipv4Addr::from(octets))
}

/// The host/port checks of net_open, in its order. `Err` carries the exact message.
pub fn validate_target(host: &[u8], port: f64) -> Result<SocketAddrV4, &'static CStr> {
    // NaN is outside the range, exactly as `!(port >= 1 && port <= 65535)` in C.
    if !(1.0..=65535.0).contains(&port) || port != f64::from(port as i32) {
        return Err(c"UDP port must be an integer from 1 to 65535");
    }
    // `length != strlen(host)`: an embedded NUL is rejected rather than truncated.
    if host.is_empty() || host.len() >= INET_ADDRSTRLEN || host.contains(&0) {
        return Err(c"host must be a numeric IPv4 address");
    }
    match parse_ipv4(host) {
        Some(address) => Ok(SocketAddrV4::new(address, port as u16)),
        None => Err(c"invalid IPv4 address"),
    }
}

fn connect(target: SocketAddrV4) -> Result<UdpSocket, &'static CStr> {
    let socket =
        UdpSocket::bind(SocketAddrV4::new(Ipv4Addr::UNSPECIFIED, 0)).map_err(|_| c"UDP socket creation failed")?;
    // std creates the socket close-on-exec / non-inheritable, as net.c's FD_CLOEXEC.
    if socket.set_nonblocking(true).is_err() || socket.connect(target).is_err() {
        return Err(c"UDP nonblocking/connect failed");
    }
    Ok(socket)
}

unsafe fn connection<'a>(lua: *mut lua_State) -> &'a mut Connection {
    let api = api();
    // luaL_checkudata raises a Lua error unless argument 1 is our userdata.
    &mut *((api.checkudata)(lua, 1, METATABLE.as_ptr()) as *mut Connection)
}

unsafe extern "C-unwind" fn net_close(lua: *mut lua_State) -> c_int {
    connection(lua).socket = None;
    0
}

unsafe extern "C-unwind" fn net_send(lua: *mut lua_State) -> c_int {
    let api = api();
    let conn: *mut Connection = connection(lua);
    let mut length = 0usize;
    let payload = (api.checklstring)(lua, 2, &mut length);
    let sent = match (*conn).socket.as_ref() {
        Some(socket) if length <= MAX_PACKET => {
            let bytes = std::slice::from_raw_parts(payload.cast::<u8>(), length);
            matches!(socket.send(bytes), Ok(count) if count == length)
        }
        _ => false,
    };
    (api.pushboolean)(lua, c_int::from(sent));
    1
}

unsafe extern "C-unwind" fn net_receive(lua: *mut lua_State) -> c_int {
    let api = api();
    let conn: *mut Connection = connection(lua);
    let mut buffer = [0u8; MAX_PACKET + 1];
    let received = match (*conn).socket.as_ref() {
        Some(socket) => match socket.recv(&mut buffer) {
            Ok(count) if (1..=MAX_PACKET).contains(&count) => count,
            _ => 0,
        },
        None => 0,
    };
    if received == 0 {
        (api.pushnil)(lua);
    } else {
        (api.pushlstring)(lua, buffer.as_ptr().cast(), received);
    }
    1
}

unsafe extern "C-unwind" fn net_open(lua: *mut lua_State) -> c_int {
    let api = api();
    let mut length = 0usize;
    let host = (api.checklstring)(lua, 1, &mut length);
    let port = (api.checknumber)(lua, 2);
    let target = match validate_target(std::slice::from_raw_parts(host.cast::<u8>(), length), port) {
        Ok(target) => target,
        Err(message) => return (api.error)(lua, message.as_ptr()),
    };
    let conn = (api.newuserdata)(lua, std::mem::size_of::<Connection>()) as *mut Connection;
    conn.write(Connection { socket: None });
    (api.getfield)(lua, REGISTRYINDEX, METATABLE.as_ptr());
    (api.setmetatable)(lua, -2);
    // Resolve to a plain pointer first so no Rust value is live across luaL_error.
    let failure = match connect(target) {
        Ok(socket) => {
            (*conn).socket = Some(socket);
            None
        }
        Err(message) => Some(message.as_ptr()),
    };
    match failure {
        None => 1,
        Some(message) => (api.error)(lua, message),
    }
}

/// Largest dedicated control file the authority reads.
const MAX_CONTROL: u64 = 4096;

/// First line of the dedicated supervisor's control file, `None` when it is absent.
/// X4's UI Lua has no `io` and restricts libc through FFI, so the authority reads its
/// control record here. Only an absolute path to a small regular `control.txt` is read.
fn read_control(path: &[u8]) -> Result<Option<Vec<u8>>, &'static CStr> {
    let text = std::str::from_utf8(path).map_err(|_| c"control path must be UTF-8")?;
    let path = std::path::Path::new(text);
    if text.contains('\0') || !path.is_absolute() || path.file_name() != Some("control.txt".as_ref()) {
        return Err(c"control path must be an absolute path to control.txt");
    }
    let Ok(meta) = std::fs::metadata(path) else { return Ok(None) };
    if !meta.is_file() || meta.len() > MAX_CONTROL {
        return Err(c"control file must be a small regular file");
    }
    let Ok(data) = std::fs::read(path) else { return Ok(None) };
    let line = data.split(|&b| b == b'\n').next().unwrap_or_default();
    let line = line.strip_suffix(b"\r").unwrap_or(line);
    Ok(Some(line.iter().copied().take(257).collect()))
}

unsafe extern "C-unwind" fn net_read_control(lua: *mut lua_State) -> c_int {
    let api = api();
    let mut length = 0usize;
    let path = (api.checklstring)(lua, 1, &mut length);
    // Resolve to a plain pointer first so no Rust value is live across luaL_error.
    let failure = match read_control(std::slice::from_raw_parts(path.cast::<u8>(), length)) {
        Ok(None) => {
            (api.pushboolean)(lua, 0);
            return 1;
        }
        Ok(Some(line)) => {
            (api.pushboolean)(lua, 1);
            (api.pushlstring)(lua, line.as_ptr().cast(), line.len());
            return 2;
        }
        Err(message) => message.as_ptr(),
    };
    (api.error)(lua, failure)
}

unsafe fn method(lua: *mut lua_State, name: &CStr, function: lua_CFunction) {
    let api = api();
    (api.pushcclosure)(lua, function, 0);
    (api.setfield)(lua, -2, name.as_ptr());
}

/// Module entry point. Returns no value when the Lua ABI cannot be resolved, exactly
/// like net.c, so transport.lua reports "could not resolve the running Lua 5.1 ABI".
///
/// # Safety
/// Must be called by a Lua 5.1-compatible runtime with a valid `lua_State`.
#[no_mangle]
pub unsafe extern "C-unwind" fn luaopen_x4lan_net(lua: *mut lua_State) -> c_int {
    let Some(api) = resolve() else { return 0 };
    (api.newmetatable)(lua, METATABLE.as_ptr());
    (api.pushvalue)(lua, -1);
    (api.setfield)(lua, -2, c"__index".as_ptr());
    method(lua, c"send", net_send);
    method(lua, c"receive", net_receive);
    method(lua, c"close", net_close);
    method(lua, c"__gc", net_close);
    (api.settop)(lua, -2);
    (api.createtable)(lua, 0, 2);
    method(lua, c"open", net_open);
    method(lua, c"read_control", net_read_control);
    1
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn read_control_accepts_only_small_absolute_control_files() {
        let dir = std::env::temp_dir().join(format!("x4lan-control-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let file = dir.join("control.txt");
        let path = file.to_str().unwrap().as_bytes().to_vec();
        assert_eq!(read_control(&path).unwrap(), None);
        std::fs::write(&file, b"3|abc|pause\r\nignored\n").unwrap();
        assert_eq!(read_control(&path).unwrap(), Some(b"3|abc|pause".to_vec()));
        std::fs::write(&file, vec![b'x'; 5000]).unwrap();
        assert!(read_control(&path).is_err());
        assert!(read_control(b"control.txt").is_err());
        assert!(read_control(dir.join("other.txt").to_str().unwrap().as_bytes()).is_err());
        std::fs::remove_dir_all(dir).unwrap();
    }

    #[test]
    fn ipv4_follows_inet_pton_rules() {
        assert_eq!(parse_ipv4(b"127.0.0.1"), Some(Ipv4Addr::LOCALHOST));
        assert_eq!(parse_ipv4(b"0.0.0.0"), Some(Ipv4Addr::UNSPECIFIED));
        assert_eq!(parse_ipv4(b"255.255.255.255"), Some(Ipv4Addr::BROADCAST));
        for bad in [
            &b""[..],
            b"1",
            b"1.2.3",
            b"1.2.3.4.5",
            b"256.1.1.1",
            b"01.2.3.4",
            b"1..2.3",
            b".1.2.3",
            b"1.2.3.",
            b"1.2.3.4 ",
            b" 1.2.3.4",
            b"0x1.2.3.4",
            b"1.2.3.-4",
            b"localhost",
        ] {
            assert_eq!(parse_ipv4(bad), None, "{:?}", String::from_utf8_lossy(bad));
        }
    }

    #[test]
    fn target_checks_match_net_c_order_and_messages() {
        let err = |host: &[u8], port: f64| validate_target(host, port).unwrap_err().to_str().unwrap();
        assert_eq!(err(b"bad", 0.0), "UDP port must be an integer from 1 to 65535");
        assert_eq!(err(b"127.0.0.1", 65536.0), "UDP port must be an integer from 1 to 65535");
        assert_eq!(err(b"127.0.0.1", 1.5), "UDP port must be an integer from 1 to 65535");
        assert_eq!(err(b"127.0.0.1", f64::NAN), "UDP port must be an integer from 1 to 65535");
        assert_eq!(err(b"", 1.0), "host must be a numeric IPv4 address");
        let long: &[u8] = if cfg!(windows) { b"255.255.255.255.255.25" } else { b"255.255.255.2555" };
        assert_eq!(err(long, 1.0), "host must be a numeric IPv4 address");
        assert_eq!(err(b"127.0.0.1\0x", 1.0), "host must be a numeric IPv4 address");
        assert_eq!(err(b"example.org", 1.0), "invalid IPv4 address");
        assert_eq!(validate_target(b"10.0.0.2", 45841.0).unwrap().to_string(), "10.0.0.2:45841");
    }
}
