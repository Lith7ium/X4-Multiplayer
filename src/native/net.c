/* Minimal Lua 5.1/LuaJIT UDP module. No game hooks or game memory offsets.
 * Loaded via package.loadlib with X4's Protected UI mode disabled.
 * Lua ABI: https://www.lua.org/source/5.1/lua.h.html and lauxlib.h.
 * Only the already-loaded Lua runtime is used.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET net_socket;
#define INVALID_NET INVALID_SOCKET
#define EXPORT __declspec(dllexport)
#else
#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int net_socket;
#define INVALID_NET (-1)
#define EXPORT __attribute__((visibility("default")))
#endif

typedef struct lua_State lua_State;
typedef int (*lua_CFunction)(lua_State*);
#define API(ret, name, args) static ret (*p_##name) args
API(const char*, luaL_checklstring, (lua_State*, int, size_t*));
API(double, luaL_checknumber, (lua_State*, int));
API(void*, luaL_checkudata, (lua_State*, int, const char*));
API(int, luaL_newmetatable, (lua_State*, const char*));
API(int, luaL_error, (lua_State*, const char*, ...));
API(void*, lua_newuserdata, (lua_State*, size_t));
API(void, lua_pushboolean, (lua_State*, int));
API(void, lua_pushnil, (lua_State*));
API(void, lua_pushlstring, (lua_State*, const char*, size_t));
API(void, lua_pushcclosure, (lua_State*, lua_CFunction, int));
API(void, lua_pushvalue, (lua_State*, int));
API(void, lua_setfield, (lua_State*, int, const char*));
API(void, lua_getfield, (lua_State*, int, const char*));
API(int, lua_setmetatable, (lua_State*, int));
API(void, lua_createtable, (lua_State*, int, int));
API(void, lua_settop, (lua_State*, int));

static int resolve_lua(void) {
#ifdef _WIN32
    HMODULE runtime = GetModuleHandleA("lua51_64.dll");
    if (!runtime) {
        return 0;
    }
#define RESOLVE(name)                                          \
    do {                                                       \
        p_##name = (void*)GetProcAddress(runtime, #name);      \
        if (!p_##name) {                                       \
            return 0;                                          \
        }                                                      \
    } while (0)
#else
#define RESOLVE(name)                                          \
    do {                                                       \
        p_##name = dlsym(RTLD_DEFAULT, #name);                 \
        if (!p_##name) {                                       \
            return 0;                                          \
        }                                                      \
    } while (0)
#endif
    RESOLVE(luaL_checklstring);
    RESOLVE(luaL_checknumber);
    RESOLVE(luaL_checkudata);
    RESOLVE(luaL_newmetatable);
    RESOLVE(luaL_error);
    RESOLVE(lua_newuserdata);
    RESOLVE(lua_pushboolean);
    RESOLVE(lua_pushnil);
    RESOLVE(lua_pushlstring);
    RESOLVE(lua_pushcclosure);
    RESOLVE(lua_pushvalue);
    RESOLVE(lua_setfield);
    RESOLVE(lua_getfield);
    RESOLVE(lua_setmetatable);
    RESOLVE(lua_createtable);
    RESOLVE(lua_settop);
#undef RESOLVE
    return 1;
}

#define METATABLE "x4lan.udp.v1"
#define REGISTRYINDEX (-10000)
#define MAX_PACKET 768
typedef struct {
    net_socket fd;
    int winsock_started;
} connection;

static void close_connection(connection* socket_connection) {
    if (socket_connection->fd != INVALID_NET) {
#ifdef _WIN32
        closesocket(socket_connection->fd);
#else
        close(socket_connection->fd);
#endif
        socket_connection->fd = INVALID_NET;
    }
#ifdef _WIN32
    if (socket_connection->winsock_started) {
        WSACleanup();
        socket_connection->winsock_started = 0;
    }
#endif
}

static int net_close(lua_State* lua) {
    connection* socket_connection = p_luaL_checkudata(lua, 1, METATABLE);
    close_connection(socket_connection);
    return 0;
}

static int net_send(lua_State* lua) {
    connection* socket_connection = p_luaL_checkudata(lua, 1, METATABLE);
    size_t length = 0;
    const char* payload = p_luaL_checklstring(lua, 2, &length);
    if (socket_connection->fd == INVALID_NET || length > MAX_PACKET) {
        p_lua_pushboolean(lua, 0);
        return 1;
    }
    int sent = (int)send(socket_connection->fd, payload, (int)length, 0);
    p_lua_pushboolean(lua, sent == (int)length);
    return 1;
}

static int net_receive(lua_State* lua) {
    connection* socket_connection = p_luaL_checkudata(lua, 1, METATABLE);
    char buffer[MAX_PACKET + 1];
    if (socket_connection->fd == INVALID_NET) {
        p_lua_pushnil(lua);
        return 1;
    }
    int received = (int)recv(socket_connection->fd, buffer, sizeof(buffer), 0);
    if (received <= 0 || received > MAX_PACKET) {
        p_lua_pushnil(lua);
        return 1;
    }
    p_lua_pushlstring(lua, buffer, (size_t)received);
    return 1;
}

static int net_open(lua_State* lua) {
    size_t length = 0;
    const char* host = p_luaL_checklstring(lua, 1, &length);
    double port = p_luaL_checknumber(lua, 2);
    if (!(port >= 1 && port <= 65535) || port != (int)port) {
        return p_luaL_error(lua, "UDP port must be an integer from 1 to 65535");
    }
    if (length == 0 || length >= INET_ADDRSTRLEN || length != strlen(host)) {
        return p_luaL_error(lua, "host must be a numeric IPv4 address");
    }
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &address.sin_addr) != 1) {
        return p_luaL_error(lua, "invalid IPv4 address");
    }
    /* Attach cleanup before creating the OS socket so Lua errors cannot leak it. */
    connection* socket_connection = p_lua_newuserdata(lua, sizeof(connection));
    socket_connection->fd = INVALID_NET;
    socket_connection->winsock_started = 0;
    p_lua_getfield(lua, REGISTRYINDEX, METATABLE);
    p_lua_setmetatable(lua, -2);
#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        return p_luaL_error(lua, "WSAStartup failed");
    }
    socket_connection->winsock_started = 1;
#endif
    socket_connection->fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_connection->fd == INVALID_NET) {
        close_connection(socket_connection);
        return p_luaL_error(lua, "UDP socket creation failed");
    }
#ifdef _WIN32
    u_long enabled = 1;
    int flags_ok = ioctlsocket(socket_connection->fd, FIONBIO, &enabled) == 0;
#else
    int flags = fcntl(socket_connection->fd, F_GETFL, 0);
    int flags_ok = flags >= 0 && fcntl(socket_connection->fd, F_SETFL, flags | O_NONBLOCK) == 0;
    flags_ok = flags_ok && fcntl(socket_connection->fd, F_SETFD, FD_CLOEXEC) == 0;
#endif
    if (!flags_ok || connect(socket_connection->fd, (struct sockaddr*)&address, sizeof(address)) != 0) {
        close_connection(socket_connection);
        return p_luaL_error(lua, "UDP nonblocking/connect failed");
    }
    return 1;
}

static void method(lua_State* lua, const char* name, lua_CFunction callback) {
    p_lua_pushcclosure(lua, callback, 0);
    p_lua_setfield(lua, -2, name);
}

EXPORT int luaopen_x4lan_net(lua_State* lua) {
    if (!resolve_lua()) {
        return 0;
    }
    p_luaL_newmetatable(lua, METATABLE);
    p_lua_pushvalue(lua, -1);
    p_lua_setfield(lua, -2, "__index");
    method(lua, "send", net_send);
    method(lua, "receive", net_receive);
    method(lua, "close", net_close);
    method(lua, "__gc", net_close);
    p_lua_settop(lua, -2);
    p_lua_createtable(lua, 0, 1);
    method(lua, "open", net_open);
    return 1;
}
