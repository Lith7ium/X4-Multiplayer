/* Portable identities for components created after the snapshot load, on the Linux X4 9.00 executable: the
 * Linux twin of lockstep_world_created.h, whose table, assignment, retirement, rows and status are shared.
 * Included by gate_linux.c only, after gate_world.h. The scheme and the evidence are in that header.
 *
 * Linux differences:
 * - The hook on AssignNewID 0xeed870 lives in gate_runtime_ids.h, shared with the table of who allocates.
 * - A created ID's key holds the class as Windows' vtable RVA, through the world hash's class table, so the same
 *   object has the same ID on both platforms. A class the table does not know gets 0xffffffff, as on Windows, and
 *   the census shows it.
 * - The class bitmap comes from the IsRealClass stub at vtable+0x11e8 (Windows +0x11e0), whose form the class
 *   table's generator verifies for every pair. The class numbers are the same.
 * - U::Macro vtable 0x3ae6860, slot 19 0xa38880 (Windows slot 18) returns the libstdc++ string at +0x20.
 * - An object's macro +0x60, parent +0x70 and runtime ID +8 are as on Windows.
 * - The MoveWorker is told by its view selector, as everywhere in this gate (lockstep_tls.h). */
#define CREATED_OWN_PLATFORM
#define CREATED_TYPE_OF(vtable) lxc_type_of(vtable)
#define WORLD_KEY_SAVED_DEFINED
typedef uint64_t (*created_assign_fn)(void *);
static volatile long world_portable_created;    /* armed once before the first gated frame */
static volatile int64_t world_created_generation; /* bumped by every created table change */
static int created_read(const void *source,void *dest,size_t bytes) {return gate_read_local(source,dest,bytes);}
static uint32_t lxc_type_of(uintptr_t vtable) {
    const lxw_class *cls=lxw_class_of(vtable);
    return cls?cls->windows_rva:0xffffffffu;
}
/* The class bitmap of a vtable: 0 when the stub is not the verified form. Decoded, never called. */
static int world_class_words(uintptr_t vtable,uint32_t words[8]) {
    static const unsigned char head[6]={0x48,0x63,0xc6,0x48,0x8d,0x15};
    static const unsigned char tail[22]={0x83,0xe6,0x0f,0x48,0xc1,0xe8,0x04,0x48,0x8d,0x0c,0x36,0x8b,0x04,0x82,0xd3,0xe8,0xa8,0x03,0x0f,0x95,0xc0,0xc3};
    uintptr_t function=0;unsigned char code[32];int32_t relative;
    if(vtable<(uintptr_t)image || !created_read((void*)(vtable+0x11e8),&function,8) || function<(uintptr_t)image ||
       !created_read((void*)function,code,sizeof(code)) || memcmp(code,head,sizeof(head)) || memcmp(code+10,tail,sizeof(tail))) return 0;
    memcpy(&relative,code+6,4);
    return created_read((void*)(function+10+(intptr_t)relative),words,8*sizeof(*words));
}
static int world_class_has(const uint32_t words[8],unsigned klass) {
    return klass<128 && ((words[klass/16]>>(2*(klass%16)))&3);
}
static unsigned char *lxc_find(void *map,uint64_t runtime,unsigned mode) {
    lxw_find_fn find=world_find?world_find:(lxw_find_fn)(void*)LX(LX_FIND_VA);
    return find(map,runtime,mode,0);
}
#define __try
#define __finally
#define GetLastError() 0u
#define SetLastError(error) ((void)(error))
#define MemoryBarrier() __sync_synchronize()
#define LONG long /* the status prints the armed words with %ld */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "lockstep_world_created.h"
#pragma GCC diagnostic pop
#undef __try
#undef __finally
#undef GetLastError
#undef SetLastError
#undef MemoryBarrier
#undef LONG
static int world_key_portable(void) {return *(volatile long*)&world_portable_created!=0;}
static unsigned created_thread_class(void) {
    if(GetCurrentThreadId()==main_thread) return CREATED_MAIN;
    return gate_tls_worker(gate_tls_self())?CREATED_MOVEWORKER:CREATED_OTHER;
}
/* 1 named (hash of the name), 0 no macro (hash 0), -1 unreadable (hash 1). */
static int created_macro(const unsigned char *object,char name[96],uint64_t *hash) {
    unsigned char *macro=NULL;void *vtable=NULL,*getter=NULL;uint64_t length=0;const unsigned char *data=NULL;
    name[0]=0;*hash=1;
    if(!created_read(object+0x60,&macro,8)) return -1;
    if(!macro) {*hash=0;return 0;}
    if(!created_read(macro,&vtable,8) || vtable!=(void*)LX(LX_MACRO_VTABLE) || !created_read((unsigned char*)vtable+19*8,&getter,8) ||
       getter!=(void*)LX(LX_MACRO_NAME_GETTER_VA) || !created_read(macro+0x20,&data,8) || !data ||
       !created_read(macro+0x28,&length,8) || !length || length>=96 || !created_read(data,name,(size_t)length)) {name[0]=0;return -1;}
    name[length]=0;
    for(uint64_t i=0;i<length;i++) {char c=name[i];if(!((c>='a'&&c<='z') || (c>='0'&&c<='9') || c=='_')) {name[0]=0;return -1;}}
    *hash=created_text_hash(name);return 1;
}
static void *lxc_map(void) {return *(world_component_map?world_component_map:(void**)LX(LX_COMPONENT_MAP_VA));}
static int created_frame(uint64_t tick) {return created_frame_with(tick,lxc_find,lxc_map());}
static int created_begin(void) {return created_begin_with(lxc_find,created_load_armed?lxc_map():NULL);}
static void *created_alloc(size_t bytes) {return calloc(1,bytes);}
/* The hook lives in gate_runtime_ids.h, with the table of who allocates. */
static int rid_assign_install(void);
static void rid_callers_restore(void);
static int rid_assign_installed;
static int created_arm(int rows) {
    /* A loading-UI reload reruns configuration: accept the installed owner. */
    if(created_armed) return stats.active && created_installed && (!rows || created_rows_armed);
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread ||
       !world_identity_installed || !world_identities || !thunk || !created_allocate(rows) || !rid_assign_install()) return 0;
    created_installed=1;created_stage="armed";
    if(rows) InterlockedExchange(&created_rows_armed,1);
    InterlockedExchange(&created_armed,1);return 1;
}
static void created_restore(void) {
    created_stop();
    if(!created_installed) return;
    rid_callers_restore();
    created_restored=!rid_assign_installed;
    if(created_restored) created_installed=0;
}
static int lua_created_identities(lua_State *L) {
    double rows=p_lua_tonumber(L,1);
    p_lua_pushnumber(L,(rows==0 || rows==1) && created_arm(rows==1));
    return 1;
}
static int lua_portable_created(lua_State *L) {p_lua_pushnumber(L,portable_created_arm());return 1;}
static int lua_created_load(lua_State *L) {p_lua_pushnumber(L,created_load_arm());return 1;}
static int lua_created_identities_status(lua_State *L) {
    char out[1024];int n=accept_ui()?created_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
static int lua_created_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[256];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index)
        n=created_row_text((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
/* Runtime ID (decimal) -> created ID (16 hex, zeros for none). */
static int lua_created_identity(lua_State *L) {
    size_t size=0;const char *text=p_lua_tolstring(L,1,&size);uint64_t runtime=0;
    if(!text || !size || size>20 || !accept_ui()) {p_lua_pushnil(L);return 1;}
    for(size_t i=0;i<size;i++) {
        unsigned digit=(unsigned)(text[i]-'0');
        if(digit>9 || runtime>(UINT64_MAX-digit)/10) {p_lua_pushnil(L);return 1;}
        runtime=runtime*10+digit;
    }
    char out[17];snprintf(out,sizeof(out),"%016llx",(unsigned long long)created_lookup(runtime));
    p_lua_pushlstring(L,out,16);
    return 1;
}
/* Created ID (16 hex) -> live runtime ID (decimal), or nil. */
static int lua_created_runtime(lua_State *L) {
    size_t size=0;const char *text=p_lua_tolstring(L,1,&size);uint64_t id=0;
    if(!text || size!=16 || !accept_ui()) {p_lua_pushnil(L);return 1;}
    for(size_t i=0;i<16;i++) {
        unsigned digit=text[i]>='0' && text[i]<='9'?(unsigned)(text[i]-'0'):text[i]>='a' && text[i]<='f'?(unsigned)(text[i]-'a'+10):16;
        if(digit>15) {p_lua_pushnil(L);return 1;}
        id=(id<<4)|digit;
    }
    uint64_t runtime=created_runtime_of(id);
    if(!runtime) {p_lua_pushnil(L);return 1;}
    char out[21];int n=snprintf(out,sizeof(out),"%llu",(unsigned long long)runtime);
    p_lua_pushlstring(L,out,(size_t)n);
    return 1;
}
