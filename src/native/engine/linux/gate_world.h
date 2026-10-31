/* World identities and the world hash on the Linux X4 9.00 executable: the same
 * WORLD_IMPORTED_1/2 summaries, WORLD_OBJECT pages, WORLD_DETAIL rows and WORLD_BANKS
 * line as the Windows gate (lockstep_world_identity.h, lockstep_world_hash.h,
 * lockstep_world_nav.h, lockstep_dock.h, lockstep_state.h), so census_world.py compares
 * a Linux client with a Windows client unchanged. Included by gate_linux.c only.
 *
 * Every hashed byte is the Windows byte: the same fields, read at the Linux offsets
 * (Windows offset in each comment), the Windows vtable RVA for each object and movement
 * controller class (gate_world_classes.h, generated from both executables' RTTI) and the
 * Windows class flags. Sources of each Linux offset: reports/LOCKSTEP-LINUX-WORLD-HASH-
 * 20261029.txt; every function they were read from is pinned in gate_sites.h.
 *
 * An object's controller pointer is a MovementControllerInterface*. MSVC keeps that interface
 * at offset 0 of every controller; on Linux most controllers start with their value source
 * and hold the interface at +0x58, so the pointer is not the complete object. The class table
 * maps the vtable of that subobject and holds its offset-to-top; the controller offsets below
 * are offsets of the complete object.
 *
 * Identities. Windows hooks its import function 0x79f720 and records (object runtime ID,
 * parsed saved UniverseID) for each successful import during the snapshot load (boot
 * phases 1 and 2). Linux has no such function: its callers inline it, and each inline copy
 * inserts into the same import map through the shared map insert 0x10c8140, which the
 * Linux gate hooks instead (default off, boot option 2^46 like Windows). It keeps the
 * inserts into the UniverseID import map only (the insert also serves the AI order map).
 * The runtime ID is stored by the caller after the insert returns, and the load function
 * clears the map (the map clear 0x11e0b30, Windows 0x942540, which returns the node blocks
 * to the allocator), so a read at the first live frame sees freed memory. The gate hooks
 * that clear too and reads every recorded node at its entry, while the map is intact; the
 * table is sealed at the first live frame as on Windows. Read only; no engine writes. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "lockstep_world_nav.h"   /* snapshot, resolve and encode are shared; the reads are Linux's */
#pragma GCC diagnostic pop
#include "gate_world_classes.h"

/* ---- Linux layouts (Windows offset in comments) ---- */
#define LXW_OBJ_RUNTIME 0x8u       /* 0x8 */
#define LXW_OBJ_MODE 0x68u         /* 0x68 */
#define LXW_OBJ_PARENT 0x70u       /* 0x70 */
#define LXW_OBJ_STATE 0xd3u        /* 0xd3 */
#define LXW_OBJ_ATTENTION 0x1ecu   /* 0x1ec */
#define LXW_OBJ_CONTROLLER 0x200u  /* 0x200 */
#define LXW_OBJ_HULL 0x278u        /* 0x280 */
#define LXW_OBJ_SHIELD 0x288u      /* 0x290 */
#define LXW_OBJ_SPAN 0x208u        /* object bytes read in one copy (vtable .. controller, like Windows' reads) */
/* Shield group (ShieldGroupData) and its recharge source. */
#define LXW_SG_BEGIN 0x10u         /* 0x10 */
#define LXW_SG_END 0x18u           /* 0x18 */
#define LXW_SG_RATE 0x28u          /* 0x28 */
#define LXW_SG_MAXIMUM 0x40u       /* 0x40 */
#define LXW_SG_DELAY 0x48u         /* 0x48 */
#define LXW_SG_FACTOR 0x50u        /* 0x50 */
#define LXW_SG_VALUE 0x60u         /* 0x68 */
#define LXW_SG_CHANGED 0x68u       /* 0x70 */
#define LXW_SG_SOURCE_VTABLE 0x78u /* 0x90 */
#define LXW_SG_SOURCE_GROUP 0x80u  /* 0x98 */
#define LXW_SG_MODIFIERS 0x88u     /* 0xa0 */
#define LXW_SG_SPAN 0x90u
#define LXW_MODIFIERS 0x18u        /* 0x18 */
/* Movement controllers: transform (4 vectors) of the static and movement families. */
#define LXW_TRANSFORM 0x10u        /* static 0x10, movement 0x20; of the complete object */
/* Dock/undock controllers (whole class 0x10 below Windows; of the complete object). */
#define LXW_DOCK_POSES 0x10u       /* 0x20 */
#define LXW_DOCK_TIME 0x50u        /* 0x60 */
#define LXW_DOCK_OWNER 0x60u       /* 0x70 */
#define LXW_DOCK_MORE 0x70u        /* 0x80 */
#define LXW_DOCK_LAST 0xf0u        /* 0x100 */
#define LXW_DOCK_ANIMATION 0x110u  /* 0x120 */
#define LXW_DOCK_FLAGS 0x114u      /* 0x124 */
#define LXW_UNDOCK_ANIMATION 0xb0u /* 0xc0 */
#define LXW_UNDOCK_FLAG 0xb4u      /* 0xc4 */
/* NavigationController (its EventListener base sits 8 bytes lower than on Windows). */
#define LXW_NAV_OWNER 0x20u        /* 0x28 */
#define LXW_NAV_SLOTS 0x28u        /* 0x30 */
#define LXW_NAV_PARAMS 0x30u       /* 0x38 */
#define LXW_NAV_AGENT 0x38u        /* 0x40 */
#define LXW_NAV_COMMANDS 0x40u     /* 0x48 */
#define LXW_NAV_PENDING 0x48u      /* 0x50 */
#define LXW_NAV_TIME 0xc0u         /* 0xc0 (time f64, cursor u32) */
#define LXW_NAV_DEADLINE 0xd0u     /* 0xd0 (deadline f64, gate u8) */
#define LXW_NAV_SPAN 0xd9u
/* Pathing agent: 0x20 above Windows from the commands on. Banks are laid out alike. */
#define LXW_AGENT_COMMANDS 0x700u  /* 0x6e0 */
#define LXW_AGENT_SLOTS 0xaf0u     /* 0xad0 */
#define LXW_AGENT_PARAMS 0xe30u    /* 0xe10 */
#define LXW_AGENT_PENDING 0xef0u   /* 0xed0 */
#define LXW_AGENT_OWNER 0xf10u     /* 0xef0 */
#define LXW_AGENT_SPAN 0xf18u      /* 0xef8 */
enum {LXW_FAMILY_NONE,LXW_FAMILY_NAV,LXW_FAMILY_DOCK,LXW_FAMILY_UNDOCK,LXW_FAMILY_STATIC,LXW_FAMILY_MOVEMENT,LXW_FAMILY_OTHER};

static int (*world_reader)(const void *,void *,size_t)=gate_read_local;
#define WREAD(source,dest,length) world_reader((const void*)(source),(dest),(length))

/* ---- classes ---- */
static const lxw_class *lxw_class_of(uintptr_t vtable) {
    if(vtable<(uintptr_t)image) return NULL;
    uint64_t va=(uint64_t)(vtable-(uintptr_t)image)+LX_IMAGE_BASE;
    unsigned lo=0,hi=LXW_CLASS_COUNT;
    while(lo<hi) {
        unsigned mid=(lo+hi)/2;
        if(lxw_classes[mid].linux_vtable<va) lo=mid+1;else hi=mid;
    }
    return lo<LXW_CLASS_COUNT && lxw_classes[lo].linux_vtable==va?&lxw_classes[lo]:NULL;
}

/* ---- identities (lockstep_world_identity.h semantics) ---- */
#define WORLD_IDENTITY_CAPACITY 1048576u
#define WORLD_IDENTITY_PROBES 256u
#define LXW_INSERTS_MAX 1048576u
typedef struct {uint64_t runtime,saved;} world_identity_entry;
typedef struct {uint64_t saved;unsigned char *node;} lxw_insert;
typedef struct {unsigned char *node;uint64_t inserted;} lxw_insert_result;
typedef lxw_insert_result (*lxw_insert_fn)(void *,const uint64_t *);
typedef void (*lxw_clear_fn)(void *);
static world_identity_entry *world_identities;
static lxw_insert *lxw_inserts;
static uint64_t lxw_insert_count;
static pthread_mutex_t lxw_insert_lock=PTHREAD_MUTEX_INITIALIZER;
static volatile int64_t world_identity_calls,world_identity_count,world_identity_failures,world_identity_late;
static int world_identity_installed,world_identity_restored,world_identity_sealed;
static int lxw_insert_hooked,lxw_clear_hooked;
static uint64_t lxw_entry_patched,lxw_clear_patched;
#define LXW_THUNK_HOOK 0x40u              /* jmp [rip] -> lxw_import_insert */
#define LXW_THUNK_TRAMPOLINE 0x80u        /* the relocated prologue, then jmp [rip] -> insert+6 */
#define LXW_THUNK_CLEAR_HOOK 0xc0u        /* jmp [rip] -> lxw_import_clear */
#define LXW_THUNK_CLEAR_TRAMPOLINE 0x100u /* the relocated prologue, then jmp [rip] -> clear+5 */

static unsigned world_identity_bucket(uint64_t runtime) {
    runtime^=runtime>>33;runtime*=0xff51afd7ed558ccdULL;runtime^=runtime>>33;
    return (unsigned)runtime&(WORLD_IDENTITY_CAPACITY-1);
}
/* Windows world_identity_record: one saved ID per runtime ID, never two. */
static int world_identity_record(uint64_t runtime,uint64_t saved) {
    if(!world_identities || !runtime || !saved || world_identity_sealed) return 0;
    unsigned bucket=world_identity_bucket(runtime);
    for(unsigned i=0;i<WORLD_IDENTITY_PROBES;i++) {
        world_identity_entry *e=&world_identities[(bucket+i)&(WORLD_IDENTITY_CAPACITY-1)];
        if(e->runtime==runtime) return e->saved==saved;
        if(!e->runtime) {
            if(world_identity_count>=WORLD_IDENTITY_CAPACITY/2) return 0;
            e->runtime=runtime;e->saved=saved;++world_identity_count;return 1;
        }
    }
    return 0;
}
static uint64_t lxw_identity_probe(uint64_t runtime) {
    unsigned bucket=world_identity_bucket(runtime);
    for(unsigned i=0;i<WORLD_IDENTITY_PROBES;i++) {
        const world_identity_entry *e=&world_identities[(bucket+i)&(WORLD_IDENTITY_CAPACITY-1)];
        if(e->runtime==runtime) return e->saved;
        if(!e->runtime) break;
    }
    return 0;
}
static uint64_t world_identity_lookup(uint64_t runtime) {
    if(!world_identities || !runtime || !world_identity_sealed) return 0;
    return lxw_identity_probe(runtime);
}
static uint64_t created_lookup(uint64_t runtime); /* gate_world_created.h: 0 while it is off */
static uint64_t world_identity_portable(uint64_t runtime) {
    uint64_t saved=world_identity_lookup(runtime);
    return saved?saved:created_lookup(runtime);
}

static lxw_insert_result lxw_import_insert(void *map,const uint64_t *key) {
    lxw_insert_result result=((lxw_insert_fn)(void*)(thunk+LXW_THUNK_TRAMPOLINE))(map,key);
    if(map==(void*)LX(LX_IMPORT_MAP_VA)) {
        if(!(boot_mode && boot_phase>0 && boot_phase<3)) __atomic_add_fetch(&world_identity_late,1,__ATOMIC_SEQ_CST);
        else {
            pthread_mutex_lock(&lxw_insert_lock);
            __atomic_add_fetch(&world_identity_calls,1,__ATOMIC_SEQ_CST);
            if(!result.node || lxw_insert_count>=LXW_INSERTS_MAX) __atomic_add_fetch(&world_identity_failures,1,__ATOMIC_SEQ_CST);
            else lxw_inserts[lxw_insert_count++]=(lxw_insert){*key,result.node};
            pthread_mutex_unlock(&lxw_insert_lock);
        }
    }
    return result;
}
/* Each recorded node holds the runtime ID its inline import stored (Windows reads it from
 * object+8 after the call). Valid only while the map is alive: see the clear below. */
static void lxw_resolve(int loading) {
    pthread_mutex_lock(&lxw_insert_lock);
    uint64_t kept=0;
    for(uint64_t i=0;i<lxw_insert_count;i++) {
        uint64_t runtime=0;
        int readable=WREAD(lxw_inserts[i].node+0x28,&runtime,8);
        if(loading && readable && !runtime) {lxw_inserts[kept++]=lxw_inserts[i];continue;} /* inserted, not stored yet */
        if(!readable || !world_identity_record(runtime,lxw_inserts[i].saved))
            __atomic_add_fetch(&world_identity_failures,1,__ATOMIC_SEQ_CST);
    }
    lxw_insert_count=kept;
    pthread_mutex_unlock(&lxw_insert_lock);
}
static void lxw_resolve_inserts(void) {lxw_resolve(0);}
/* During the load, for a module keying what the load does: the stored nodes so far are read,
 * the table is not sealed. A node another thread is between its insert and its store on stays
 * for the next read, so the table ends with the same rows as without this call. */
static uint64_t world_identity_loading(uint64_t runtime) {
    if(!world_identities || !runtime) return 0;
    if(!world_identity_sealed) lxw_resolve(1);
    return lxw_identity_probe(runtime);
}
/* The load ends by clearing the import map, which frees its node blocks; read them first.
 * The clear also serves other maps, which pass through. */
static void lxw_import_clear(void *map) {
    if(map==(void*)LX(LX_IMPORT_MAP_VA)) lxw_resolve_inserts();
    ((lxw_clear_fn)(void*)(thunk+LXW_THUNK_CLEAR_TRAMPOLINE))(map);
}
static void lxw_jump_write(unsigned char *at,const void *target) {
    at[0]=0xff;at[1]=0x25;memset(at+2,0,4);memcpy(at+6,&target,8);
}
/* The thunk page (gate_linux.c make_thunk) holds the hook jumps and the trampolines. */
static void lxw_thunk_write(unsigned char *page) {
    lxw_jump_write(page+LXW_THUNK_HOOK,(void*)lxw_import_insert);
    unsigned char *p=page+LXW_THUNK_TRAMPOLINE;memcpy(p,"\x41\x56\x41\x55\x41\x54",LX_IMPORT_PROLOGUE); /* push r14; push r13; push r12 */
    lxw_jump_write(p+LX_IMPORT_PROLOGUE,LX(LX_IMPORT_INSERT_VA)+LX_IMPORT_PROLOGUE);
    lxw_jump_write(page+LXW_THUNK_CLEAR_HOOK,(void*)lxw_import_clear);
    p=page+LXW_THUNK_CLEAR_TRAMPOLINE;memcpy(p,"\x53\x48\x83\xec\x40",LX_IMPORT_CLEAR_PROLOGUE); /* push rbx; sub rsp,0x40 */
    lxw_jump_write(p+LX_IMPORT_CLEAR_PROLOGUE,LX(LX_IMPORT_CLEAR_VA)+LX_IMPORT_CLEAR_PROLOGUE);
}
/* One aligned 8-byte compare-exchange: the jump to the thunk, the last 3 bytes kept. */
static int lxw_patch(unsigned char *entry,uint64_t original,unsigned hook,uint64_t *patched) {
    intptr_t distance=(intptr_t)(thunk+hook)-(intptr_t)(entry+5);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    *patched=original;
    unsigned char *patch=(unsigned char*)patched;int32_t rel=(int32_t)distance;
    patch[0]=0xe9;memcpy(patch+1,&rel,4);
    return gate_code_swap64(entry,original,*patched);
}
static int world_identity_install(void) {
    unsigned char *entry=LX(LX_IMPORT_INSERT_VA),*clear=LX(LX_IMPORT_CLEAR_VA);
    if(world_identity_installed || !thunk || rd64(entry)!=LX_IMPORT_ENTRY_ORIGINAL || rd64(clear)!=LX_IMPORT_CLEAR_ORIGINAL) return 0;
    if(!world_identities) world_identities=calloc(WORLD_IDENTITY_CAPACITY,sizeof(*world_identities));
    if(!lxw_inserts) lxw_inserts=calloc(LXW_INSERTS_MAX,sizeof(*lxw_inserts));
    if(!world_identities || !lxw_inserts) return 0;
    /* The clear first: no insert is ever recorded without its clear hooked. */
    if(!lxw_patch(clear,LX_IMPORT_CLEAR_ORIGINAL,LXW_THUNK_CLEAR_HOOK,&lxw_clear_patched)) return 0;
    if(!lxw_patch(entry,LX_IMPORT_ENTRY_ORIGINAL,LXW_THUNK_HOOK,&lxw_entry_patched)) {
        gate_code_swap64(clear,lxw_clear_patched,LX_IMPORT_CLEAR_ORIGINAL);return 0;
    }
    lxw_insert_hooked=lxw_clear_hooked=1;world_identity_installed=1;return 1;
}
static void world_identity_restore(void) {
    if(!world_identity_installed) return;
    if(lxw_insert_hooked && gate_code_swap64(LX(LX_IMPORT_INSERT_VA),lxw_entry_patched,LX_IMPORT_ENTRY_ORIGINAL)) lxw_insert_hooked=0;
    if(lxw_clear_hooked && gate_code_swap64(LX(LX_IMPORT_CLEAR_VA),lxw_clear_patched,LX_IMPORT_CLEAR_ORIGINAL)) lxw_clear_hooked=0;
    world_identity_restored=!lxw_insert_hooked && !lxw_clear_hooked;
    if(world_identity_restored) world_identity_installed=0;
    /* The tables stay: Lua diagnostics may read them after the stop. */
}
static int world_identity_owned(void) {
    if(!world_identity_installed) return 1;
    return (!lxw_insert_hooked || __atomic_load_n((uint64_t*)LX(LX_IMPORT_INSERT_VA),__ATOMIC_SEQ_CST)==lxw_entry_patched) &&
           (!lxw_clear_hooked || __atomic_load_n((uint64_t*)LX(LX_IMPORT_CLEAR_VA),__ATOMIC_SEQ_CST)==lxw_clear_patched);
}
/* First live frame (boot phase 3): the import is over. Whatever the clear did not read
 * (a load that did not clear yet) is read now; the table is final from here on. */
static void world_identity_seal(void) {
    if(!world_identity_installed || world_identity_sealed) return;
    lxw_resolve_inserts();
    world_identity_sealed=1;
}

/* ---- dock and shield state (lockstep_dock.h, lockstep_state.h formats) ---- */
static int lxw_dock_state(unsigned char *controller,void *object,unsigned family,char out[512]) {
    void *owner;uint32_t vectors[14][4],animation;uint64_t time_bits;double time;
    unsigned char flags[2]={0};
    int undock=family==LXW_FAMILY_UNDOCK;
    if(!object || (family!=LXW_FAMILY_DOCK && !undock)) return 0;
    if(!WREAD(controller+LXW_DOCK_OWNER,&owner,8) || owner!=object ||
       !WREAD(controller+LXW_DOCK_POSES,vectors[0],64) ||
       !WREAD(controller+LXW_DOCK_MORE,vectors[4],undock?64:128) ||
       (!undock && !WREAD(controller+LXW_DOCK_LAST,vectors[12],32)) ||
       !WREAD(controller+LXW_DOCK_TIME,&time_bits,8) ||
       !WREAD(controller+(undock?LXW_UNDOCK_ANIMATION:LXW_DOCK_ANIMATION),&animation,4) ||
       !WREAD(controller+(undock?LXW_UNDOCK_FLAG:LXW_DOCK_FLAGS),flags,undock?1:2) || flags[0]>1 || flags[1]>1) return 0;
    memcpy(&time,&time_bits,8);if(!isfinite(time)) return 0;
    unsigned count=undock?8:14;
    for(unsigned i=0;i<count;i++) for(unsigned j=0;j<3;j++) {
        float value;memcpy(&value,&vectors[i][j],4);if(!isfinite(value)) return 0;
    }
    int n=undock?snprintf(out,512,"UNDOCKCTL_1:%016llx:%08x:%02x:",(unsigned long long)time_bits,animation,flags[0]):
                 snprintf(out,512,"DOCKCTL_1:%016llx:%08x:%02x%02x:",(unsigned long long)time_bits,animation,flags[0],flags[1]);
    for(unsigned i=0;i<count;i++) for(unsigned j=0;j<3;j++) n+=snprintf(out+n,512-(size_t)n,"%08x",vectors[i][j]);
    return n==(undock?233:377);
}
static int lxw_shield_state(unsigned char *object,char out[192]) {
    unsigned char *group,*modifiers,sg[LXW_SG_SPAN];
    uintptr_t begin,end,source_vtable;unsigned char *source_group;
    uint64_t value,changed,maximum,delay;uint32_t rate,factor,mods[3]={0x3f800000,0x3f800000,0x3f800000};
    if(!WREAD(object+LXW_OBJ_SHIELD,&group,8)) return 0;
    if(!group) {strcpy(out,"none");return 1;}
    if(!WREAD(group,sg,sizeof(sg))) return 0;
    memcpy(&source_vtable,sg+LXW_SG_SOURCE_VTABLE,8);memcpy(&source_group,sg+LXW_SG_SOURCE_GROUP,8);
    memcpy(&begin,sg+LXW_SG_BEGIN,8);memcpy(&end,sg+LXW_SG_END,8);
    memcpy(&value,sg+LXW_SG_VALUE,8);memcpy(&changed,sg+LXW_SG_CHANGED,8);
    memcpy(&maximum,sg+LXW_SG_MAXIMUM,8);memcpy(&delay,sg+LXW_SG_DELAY,8);
    memcpy(&rate,sg+LXW_SG_RATE,4);memcpy(&factor,sg+LXW_SG_FACTOR,4);memcpy(&modifiers,sg+LXW_SG_MODIFIERS,8);
    if(source_vtable!=(uintptr_t)LX(LX_SHIELD_SOURCE_VTABLE) || source_group!=group ||
       end<begin || (end-begin)%8 || (end-begin)/8>4096 || (modifiers && !WREAD(modifiers+LXW_MODIFIERS,mods,12))) return 0;
    snprintf(out,192,"%016llx%016llx%016llx%016llx%08x%08x%016llx%u%08x%08x%08x",
        (unsigned long long)value,(unsigned long long)changed,(unsigned long long)maximum,(unsigned long long)delay,
        rate,factor,(unsigned long long)((end-begin)/8),modifiers!=NULL,mods[0],mods[1],mods[2]);
    return 1;
}

/* ---- Nav capture (world_nav_capture with Linux offsets) ---- */
static int lxw_nav_links_read(uintptr_t object,uintptr_t controller,uint64_t runtime,world_nav_links *s,const char **why) {
    memset(s,0,sizeof(*s));s->object=object;s->controller=controller;
    *why="nav_links_incomplete";
    if(!world_nav_address(object,LXW_OBJ_SPAN) || !world_nav_address(controller,LXW_NAV_SPAN) ||
       !WREAD(object,&s->object_vtable,8) || !WREAD(object+LXW_OBJ_RUNTIME,&s->runtime,8) || !WREAD(object+LXW_OBJ_ATTENTION,&s->attention,4) ||
       !WREAD(object+LXW_OBJ_CONTROLLER,&s->controller,8) || !WREAD(controller,&s->vtable,8) ||
       !WREAD(controller+LXW_NAV_OWNER,&s->owner,8) || !WREAD(controller+LXW_NAV_SLOTS,&s->slots,8) ||
       !WREAD(controller+LXW_NAV_PARAMS,&s->params,8) || !WREAD(controller+LXW_NAV_AGENT,&s->agent,8) ||
       !WREAD(controller+LXW_NAV_COMMANDS,&s->commands,8) || !WREAD(controller+LXW_NAV_PENDING,&s->pending,8)) return 0;
    *why="nav_owner_invalid";
    const lxw_class *nav=lxw_class_of(s->vtable);
    if(!runtime || s->runtime!=runtime || s->controller!=controller || !nav || nav->family!=LXW_FAMILY_NAV ||
       s->owner!=object || !world_nav_address(s->agent,LXW_AGENT_SPAN) || s->slots!=s->agent+LXW_AGENT_SLOTS ||
       s->params!=s->agent+LXW_AGENT_PARAMS || s->commands!=s->agent+LXW_AGENT_COMMANDS || s->pending!=s->agent+LXW_AGENT_PENDING) return 0;
    *why="nav_links_incomplete";
    if(!WREAD(s->agent+LXW_AGENT_OWNER,&s->agent_owner,8) ||
       !WREAD(LX(LX_BANK_READ_VA),&s->read,4) || !WREAD(LX(LX_BANK_WRITE_VA),&s->write,4)) return 0;
    *why="nav_owner_invalid";if(s->agent_owner!=object) return 0;
    *why="nav_selector_invalid";
    return s->read<=1 && s->write<=1 && s->read+s->write==1;
}
static int lxw_nav_take(uintptr_t object,uintptr_t controller,uint64_t runtime,world_nav_snapshot *s,const char **why) {
    world_nav_links end;memset(s,0,sizeof(*s));
    if(!lxw_nav_links_read(object,controller,runtime,&s->links,why)) return 0;
    *why="nav_tuple_incomplete";
    if(!WREAD(controller+LXW_NAV_TIME,s->nav,12) || !WREAD(controller+LXW_NAV_DEADLINE,s->nav+12,9) ||
       !WREAD(s->links.slots,s->banks,sizeof(s->banks)) || !WREAD(s->links.pending,s->pending,sizeof(s->pending))) return 0;
    if(!lxw_nav_links_read(object,controller,runtime,&end,why)) return 0;
    *why="nav_links_changed";
    return !memcmp(&s->links,&end,sizeof(end));
}
static int lxw_nav_capture(uintptr_t object,uintptr_t controller,uint64_t runtime,world_nav_snapshot *s,const char **why) {
    world_nav_snapshot end;
    if(!lxw_nav_take(object,controller,runtime,s,why)) return 0;
    world_nav_resolve(s,world_identity_portable);
    if(!lxw_nav_take(object,controller,runtime,&end,why)) return 0;
    world_nav_resolve(&end,world_identity_portable);
    *why="nav_tuple_changed";if(memcmp(s,&end,sizeof(end))) return 0;
    uint32_t cursor;memcpy(&cursor,s->nav+8,4);
    *why="nav_cursor_invalid";if(cursor>2) return 0;
    *why="nav_pending_reference_unknown";
    if(s->states[0]==WORLD_NAV_UNKNOWN || s->states[1]==WORLD_NAV_UNKNOWN) return 0;
    *why="nav_complete";return 1;
}

/* ---- world hash (lockstep_world_hash.h) ---- */
#define WORLD_HASH_DOMAINS 4u
#define WORLD_HASH_PAGE 12u
#define WORLD_TIMING_BYTES 96u
#define WORLD_HASH_WATCH_MAX 64u
typedef struct {
    uint64_t runtime,saved,parent;
    unsigned type,flags;
    unsigned char hash[WORLD_HASH_DOMAINS][32];
    unsigned char timing[WORLD_TIMING_BYTES],timing_length;
    unsigned char nav_raw[WORLD_NAV_RAW_BYTES],nav_raw_length;
} world_hash_item;
typedef struct {unsigned char data[1024];unsigned length;} world_hash_data;
typedef struct {uint64_t saved,runtime;void *object;world_hash_data data[WORLD_HASH_DOMAINS];} world_hash_watch;
typedef struct {uint32_t read,write;} world_bank_phase;
typedef void *(*lxw_find_fn)(void *,uint64_t,unsigned,unsigned);
static world_hash_watch world_hash_watches[WORLD_HASH_WATCH_MAX];
static unsigned world_hash_watch_count;
static world_hash_item *world_hash_items;
static unsigned *world_hash_changed;
static unsigned world_hash_count,world_hash_changes,world_hash_alive,world_hash_class_unknown;
static unsigned world_hash_motion_unknown,world_hash_parent_unknown;
static uint64_t world_hash_tick,world_hash_scans,world_hash_failures,world_hash_failure_id;
static unsigned world_hash_schema=1;
static const char *world_hash_stage="not_started";
/* A capture reads every object even after a failure, so one run names everything that
 * does not hold: each distinct (stage, vtable) with its count and first saved ID. It still
 * fails closed: no hash is published when any object failed. */
typedef struct {const char *stage;uintptr_t vtable;uint64_t first_saved,count;} lxw_capture_problem;
#define LXW_CAPTURE_PROBLEMS_MAX 64u
#define LXW_IMAGE_END 0x7bb7000u     /* end of the last loaded segment */
static lxw_capture_problem lxw_capture_problems[LXW_CAPTURE_PROBLEMS_MAX];
static unsigned lxw_capture_problem_count;
static uint64_t lxw_capture_failed_objects,lxw_capture_problems_dropped;
static uintptr_t lxw_capture_vtable; /* of the class being read when a stage fails */
static char world_hash_summary[768],world_hash_state[384];
static world_bank_phase world_banks_before,world_banks_after;
static lxw_find_fn world_find;            /* the engine's find; tests supply their own */
static void **world_component_map;        /* LX_COMPONENT_MAP_VA; tests supply their own */

static int world_banks_read(world_bank_phase *phase) {
    return WREAD(LX(LX_BANK_READ_VA),&phase->read,4) && WREAD(LX(LX_BANK_WRITE_VA),&phase->write,4) &&
           phase->read<=1 && phase->write<=1 && phase->read+phase->write==1;
}
static int world_hash_watch_add(const char *text,size_t length) {
    if(!text || length!=16 || world_hash_scans || world_hash_watch_count==WORLD_HASH_WATCH_MAX) return 0;
    uint64_t saved=0;
    for(size_t i=0;i<length;i++) {
        unsigned digit=(unsigned)(text[i]>='0' && text[i]<='9'?text[i]-'0':text[i]>='a' && text[i]<='f'?text[i]-'a'+10:16);
        if(digit>15) return 0;saved=(saved<<4)|digit;
    }
    if(!saved) return 0;
    unsigned pos=0;while(pos<world_hash_watch_count && world_hash_watches[pos].saved<saved) ++pos;
    if(pos<world_hash_watch_count && world_hash_watches[pos].saved==saved) return 0;
    memmove(world_hash_watches+pos+1,world_hash_watches+pos,(world_hash_watch_count-pos)*sizeof(*world_hash_watches));
    memset(world_hash_watches+pos,0,sizeof(*world_hash_watches));world_hash_watches[pos].saved=saved;
    ++world_hash_watch_count;return 1;
}
static int world_hash_add(world_hash_data *b,const void *data,unsigned length) {
    if(length>sizeof(b->data)-b->length) return 0;
    memcpy(b->data+b->length,data,length);b->length+=length;return 1;
}
static int world_hash_read(world_hash_data *b,const void *source,unsigned length) {
    if(length>sizeof(b->data)-b->length || !WREAD(source,b->data+b->length,length)) return 0;
    b->length+=length;return 1;
}
static uint64_t world_hash_reference(uint64_t runtime,unsigned *flags) {
    uint64_t saved=world_identity_portable(runtime);
    if(runtime && !saved) *flags|=16;return saved;
}
static int world_hash_vectors(world_hash_data *b,const void *source,unsigned vectors) {
    uint32_t words[24];if(vectors>6 || !WREAD(source,words,vectors*16)) return 0;
    for(unsigned i=0;i<vectors;i++) if(!world_hash_add(b,words+i*4,12)) return 0;
    return 1;
}
static void lxw_capture_problem_note(uint64_t saved) {
    ++lxw_capture_failed_objects;
    for(unsigned i=0;i<lxw_capture_problem_count;i++) {
        lxw_capture_problem *p=&lxw_capture_problems[i];
        if(p->vtable==lxw_capture_vtable && !strcmp(p->stage,world_hash_stage)) {++p->count;return;}
    }
    if(lxw_capture_problem_count==LXW_CAPTURE_PROBLEMS_MAX) {++lxw_capture_problems_dropped;return;}
    lxw_capture_problems[lxw_capture_problem_count++]=(lxw_capture_problem){world_hash_stage,lxw_capture_vtable,saved,1};
}
/* The Itanium class name of a vtable (RTTI: vtable[-1] is the typeinfo, its second word the name). */
static void lxw_vtable_name(uintptr_t vtable,char *out,size_t capacity) {
    uintptr_t info=0,name=0;size_t n=0;
    if(vtable && WREAD(vtable-8,&info,8) && info && WREAD(info+8,&name,8) && name) {
        while(n+1<capacity) {
            char c=0;
            if(!WREAD(name+n,&c,1) || !((c>='0' && c<='9') || (c>='a' && c<='z') || (c>='A' && c<='Z') || c=='_')) break;
            out[n++]=c;
        }
    }
    if(!n) out[n++]='?';
    out[n]=0;
}
/* Appended to the capture error: one log line per problem, below the logger's 8191 bytes
 * per call (the probe logs the error in one call, after about 110 bytes of its own). */
static void lxw_capture_problems_text(char *out,size_t capacity) {
    int n=snprintf(out,capacity," objects=%llu distinct=%u",(unsigned long long)lxw_capture_failed_objects,lxw_capture_problem_count);
    unsigned written=0;
    for(;written<lxw_capture_problem_count;written++) {
        const lxw_capture_problem *p=&lxw_capture_problems[written];
        char name[96],where[32]="outside";int64_t top=0;
        lxw_vtable_name(p->vtable,name,sizeof(name));
        if(p->vtable) WREAD(p->vtable-16,&top,8);
        if(p->vtable>=(uintptr_t)image && p->vtable<(uintptr_t)LX(LXW_IMAGE_END))
            snprintf(where,sizeof(where),"%llx",(unsigned long long)(p->vtable-(uintptr_t)image+LX_IMAGE_BASE));
        char row[320];
        int length=snprintf(row,sizeof(row),"\n[X4LAN] NATIVE_GATE WORLD_CAPTURE_PROBLEM stage=%s count=%llu first_saved=%016llx vtable=%s top=%lld class=%s",
            p->stage,(unsigned long long)p->count,(unsigned long long)p->first_saved,where,(long long)top,name);
        if(length<0 || (size_t)length>=sizeof(row) || (size_t)(n+length)+32>=capacity) break;
        memcpy(out+n,row,(size_t)length+1);n+=length;
    }
    if(written<lxw_capture_problem_count || lxw_capture_problems_dropped)
        snprintf(out+n,capacity-(size_t)n," more=%llu",(unsigned long long)(lxw_capture_problem_count-written+lxw_capture_problems_dropped));
}
static int world_hash_motion(void *object,unsigned char *controller,world_hash_data *motion,world_hash_data *timing,
                             unsigned *flags,world_hash_item *item,uintptr_t object_vtable) {
    world_hash_stage="controller";
    if(!controller) {unsigned zero=0;*flags|=8;return world_hash_add(motion,&zero,4);}
    uintptr_t vtable;unsigned kind;
    if(!WREAD(controller,&vtable,8)) return 0;
    lxw_capture_vtable=vtable;
    const lxw_class *cls=lxw_class_of(vtable);
    world_hash_stage="controller_class_unmapped";if(!cls) return 0;
    world_hash_stage="controller";
    unsigned char *complete=controller+cls->top; /* from the interface subobject */
    kind=cls->windows_rva;if(!world_hash_add(motion,&kind,4)) return 0;
    if(cls->family==LXW_FAMILY_NAV) {
        if(world_hash_schema==2) {
            world_nav_snapshot captured;unsigned char canonical[WORLD_NAV_TIMING_BYTES],movement[WORLD_NAV_MOTION_BYTES];
            world_hash_stage="nav_identity_missing";if(!item) return 0;
            if(!lxw_nav_capture((uintptr_t)object,(uintptr_t)controller,item->runtime,&captured,&world_hash_stage)) return 0;
            world_hash_stage="nav_census_phase_changed";
            if(captured.links.read!=world_banks_before.read || captured.links.write!=world_banks_before.write) return 0;
            world_hash_stage="nav_attention_changed";
            if(timing->length!=4 || memcmp(timing->data,&captured.links.attention,4)) return 0;
            world_hash_stage="nav_object_type_changed";
            if(captured.links.object_vtable!=object_vtable) return 0;
            world_nav_encode(&captured,canonical,movement,item->nav_raw,flags);item->nav_raw_length=WORLD_NAV_RAW_BYTES;
            return world_hash_add(timing,canonical+4,sizeof(canonical)-4) && world_hash_add(motion,movement+4,sizeof(movement)-4);
        }
        world_hash_stage="navigation";
        unsigned char *slots;unsigned index;
        if(!WREAD(controller+LXW_NAV_SLOTS,&slots,8) || !slots || !WREAD(LX(LX_BANK_READ_VA),&index,4) || index>1 ||
           !world_hash_read(timing,controller+LXW_NAV_TIME,12) || !world_hash_read(timing,controller+LXW_NAV_DEADLINE,9)) return 0;
        for(unsigned i=0;i<2;i++) {
            unsigned char *state=slots+(i?1-index:index)*WORLD_NAV_BANK_BYTES;uint64_t refs[2];
            if(!world_hash_vectors(motion,state,6) || !WREAD(state+0xb0,refs,16)) return 0;
            for(unsigned j=0;j<2;j++) refs[j]=world_hash_reference(refs[j],flags);
            if(!world_hash_add(motion,refs,16) || !world_hash_read(motion,state+0xcc,4) ||
               !world_hash_read(motion,state+0x15b,1) || !world_hash_read(timing,state+0x150,8) ||
               !world_hash_read(timing,state+0x159,2)) return 0;
        }
        return 1;
    }
    if(cls->family==LXW_FAMILY_DOCK || cls->family==LXW_FAMILY_UNDOCK) {
        world_hash_stage="dock_controller";
        char dock[512];return lxw_dock_state(complete,object,cls->family,dock) && world_hash_add(motion,dock,(unsigned)strlen(dock));
    }
    if(cls->family==LXW_FAMILY_STATIC || cls->family==LXW_FAMILY_MOVEMENT) return world_hash_vectors(motion,complete+LXW_TRANSFORM,4);
    /* Unknown controller identity is still visible; its state is not claimed. */
    *flags|=8;return 1;
}
static int world_hash_record(world_hash_item *item,world_hash_data data[WORLD_HASH_DOMAINS],unsigned char *object) {
    world_hash_stage="topology";
    item->type=item->flags=0;item->parent=0;item->nav_raw_length=0;
    if(object) {
        unsigned char copy[LXW_OBJ_SPAN];uint64_t id;uintptr_t vtable;unsigned char *parent;unsigned classes,mode;
        if(!WREAD(object,copy,sizeof(copy))) return 0;
        memcpy(&vtable,copy,8);memcpy(&id,copy+LXW_OBJ_RUNTIME,8);memcpy(&parent,copy+LXW_OBJ_PARENT,8);memcpy(&mode,copy+LXW_OBJ_MODE,4);
        lxw_capture_vtable=vtable;
        const lxw_class *cls=lxw_class_of(vtable);
        if(!cls || cls->family!=LXW_FAMILY_NONE) {world_hash_stage="object_class_unmapped";return 0;}
        if(id!=item->runtime) return 0;
        item->type=cls->windows_rva;item->flags=1;classes=cls->flags;
        if(classes&0x80000000u) {item->flags|=32;++world_hash_class_unknown;}
        if(parent) {
            uint64_t parent_id;if(!WREAD(parent+LXW_OBJ_RUNTIME,&parent_id,8)) return 0;
            item->parent=world_hash_reference(parent_id,&item->flags);
        }
        if(!world_hash_add(&data[0],&mode,4) || !world_hash_add(&data[0],copy+LXW_OBJ_STATE,1) ||
           !world_hash_add(&data[3],copy+LXW_OBJ_ATTENTION,4)) return 0;
        if(classes&1) {
            world_hash_stage="health";
            char shield[192];item->flags|=2;
            if(!world_hash_read(&data[1],object+LXW_OBJ_HULL,8) || !lxw_shield_state(object,shield) ||
               !world_hash_add(&data[1],shield,(unsigned)strlen(shield))) return 0;
        }
        if(classes&2) {
            unsigned char *controller;item->flags|=4;memcpy(&controller,copy+LXW_OBJ_CONTROLLER,8);
            if(!world_hash_motion(object,controller,&data[2],&data[3],&item->flags,item,vtable)) return 0;
        }
        world_hash_stage="identity_recheck";
        if(!WREAD(object+LXW_OBJ_RUNTIME,&id,8) || id!=item->runtime) return 0;
        ++world_hash_alive;
        if(item->flags&8) ++world_hash_motion_unknown;
        if(item->flags&16) ++world_hash_parent_unknown;
    }
    /* Absence, class and reference coverage are part of every domain. */
    for(unsigned domain=0;domain<WORLD_HASH_DOMAINS;domain++)
        if(!world_hash_add(&data[domain],&item->type,4) || !world_hash_add(&data[domain],&item->flags,4) ||
           !world_hash_add(&data[domain],&item->parent,8)) return 0;
    return 1;
}
/* Optional created identities (gate_world_created.h, default off): their rows follow the saved ones. */
#define WORLD_HASH_CREATED_MAX 524288u /* CREATED_LIVE */
static int created_enabled(void);
static world_hash_item *world_hash_created;
static unsigned world_hash_created_count;
typedef unsigned char *(*world_hash_find_fn)(void *,uint64_t,unsigned);
static unsigned char *lxc_find(void *map,uint64_t runtime,unsigned mode);
static int created_capture_rows(world_hash_find_fn,void *,world_hash_item *,unsigned *,unsigned);
static world_hash_item *world_hash_item_at(unsigned index) {
    return world_hash_created_count && index>=world_hash_count?&world_hash_created[index-world_hash_count]:&world_hash_items[index];
}
static int world_hash_compare(const void *a,const void *b) {
    uint64_t x=((const world_hash_item*)a)->saved,y=((const world_hash_item*)b)->saved;
    return x<y?-1:x>y;
}
static int world_hash_init(void) {
    if(!world_identities || !world_identity_sealed || world_identity_count<=0 || world_identity_count>WORLD_IDENTITY_CAPACITY/2) return 0;
    unsigned count=(unsigned)world_identity_count;
    world_hash_items=calloc(count,sizeof(*world_hash_items));
    world_hash_changed=calloc(count+WORLD_HASH_CREATED_MAX,sizeof(*world_hash_changed));
    if(!world_hash_items || !world_hash_changed) return 0;
    for(unsigned i=0;i<WORLD_IDENTITY_CAPACITY;i++) if(world_identities[i].runtime) {
        if(world_hash_count==count) return 0;
        world_hash_items[world_hash_count++]=(world_hash_item){.runtime=world_identities[i].runtime,.saved=world_identities[i].saved};
    }
    if(world_hash_count!=count) return 0;
    qsort(world_hash_items,count,sizeof(*world_hash_items),world_hash_compare);
    for(unsigned i=1;i<count;i++) if(world_hash_items[i-1].saved==world_hash_items[i].saved) return 0;
    if(created_enabled()) {
        /* Created rows sort after every saved row: the high bit is theirs. */
        world_hash_stage="created_saved_range";
        if(world_hash_items[count-1].saved>>63) return 0;
        world_hash_created=calloc(WORLD_HASH_CREATED_MAX,sizeof(*world_hash_created));
        if(!world_hash_created) return 0;
    }
    return 1;
}
static int world_hash_capture(uint64_t tick,unsigned schema) {
    world_hash_stage="schema";
    if((schema!=1 && schema!=2) || (world_hash_scans && schema!=world_hash_schema)) return 0;
    world_hash_schema=schema;
    world_hash_stage="initialization";
    if(!world_hash_items && !world_hash_init()) return 0;
    lxw_find_fn find=world_find?world_find:(lxw_find_fn)(void*)LX(LX_FIND_VA);
    void *map=*(world_component_map?world_component_map:(void**)LX(LX_COMPONENT_MAP_VA));
    if(!map || world_hash_failures || (world_hash_scans && tick<=world_hash_tick)) return 0;
    if(world_hash_created) {
        world_hash_stage="created_rows";
        if(!created_capture_rows(lxc_find,map,world_hash_created,&world_hash_created_count,WORLD_HASH_CREATED_MAX)) return 0;
    }
    const unsigned total=world_hash_count+world_hash_created_count;
    world_hash_stage="bank_phase_before";
    if(!world_banks_read(&world_banks_before)) return 0;
    world_hash_changes=world_hash_alive=world_hash_class_unknown=world_hash_motion_unknown=world_hash_parent_unknown=0;
    lxw_capture_problem_count=0;lxw_capture_failed_objects=lxw_capture_problems_dropped=0;
    /* Aggregate buffers contain only savedID + per-domain SHA, little endian. */
    static unsigned char aggregate[WORLD_HASH_DOMAINS][40960];unsigned used=0,watch=0;
    lockstep_sha256_ctx roots[WORLD_HASH_DOMAINS];
    for(unsigned d=0;d<WORLD_HASH_DOMAINS;d++) lockstep_sha256_init(&roots[d]);
    for(unsigned i=0;i<total;i++) {
        world_hash_item *item=world_hash_item_at(i);unsigned char previous[WORLD_HASH_DOMAINS][32];
        unsigned char previous_raw[WORLD_NAV_RAW_BYTES],previous_raw_length=item->nav_raw_length;
        memcpy(previous_raw,item->nav_raw,sizeof(previous_raw));
        world_hash_failure_id=item->saved;
        memcpy(previous,item->hash,sizeof(previous));world_hash_data data[WORLD_HASH_DOMAINS]={0};
        unsigned char *object=find(map,item->runtime,4,0);
        lxw_capture_vtable=0;
        if(!world_hash_record(item,data,object)) {lxw_capture_problem_note(item->saved);continue;}
        if(data[3].length<16 || data[3].length-16>WORLD_TIMING_BYTES) {world_hash_stage="timing_schema";lxw_capture_problem_note(item->saved);continue;}
        item->timing_length=(unsigned char)(data[3].length-16);
        memcpy(item->timing,data[3].data,item->timing_length);
        if(watch<world_hash_watch_count && world_hash_watches[watch].saved==item->saved) {
            world_hash_watches[watch].runtime=item->runtime;world_hash_watches[watch].object=object;
            memcpy(world_hash_watches[watch].data,data,sizeof(data));++watch;
        }
        for(unsigned d=0;d<WORLD_HASH_DOMAINS;d++) {
            lockstep_sha256(data[d].data,data[d].length,item->hash[d]);
            memcpy(aggregate[d]+used,&item->saved,8);memcpy(aggregate[d]+used+8,item->hash[d],32);
        }
        used+=40;
        if(!world_hash_scans || memcmp(previous,item->hash,sizeof(previous)) || previous_raw_length!=item->nav_raw_length ||
           memcmp(previous_raw,item->nav_raw,item->nav_raw_length)) world_hash_changed[world_hash_changes++]=i;
        if(used==sizeof(aggregate[0]) || i+1==total) {
            for(unsigned d=0;d<WORLD_HASH_DOMAINS;d++) lockstep_sha256_update(&roots[d],aggregate[d],used);
            used=0;
        }
    }
    if(lxw_capture_problem_count) { /* the first one names the failure; lxw_capture_problems_text lists them all */
        world_hash_failure_id=lxw_capture_problems[0].first_saved;world_hash_stage=lxw_capture_problems[0].stage;return 0;
    }
    if(watch!=world_hash_watch_count) {world_hash_failure_id=world_hash_watches[watch].saved;world_hash_stage="watch_identity_missing";return 0;}
    world_hash_stage="bank_phase_after";
    if(!world_banks_read(&world_banks_after) || memcmp(&world_banks_before,&world_banks_after,sizeof(world_banks_before))) return 0;
    char hexroots[WORLD_HASH_DOMAINS][65];unsigned char raw[32];
    for(unsigned d=0;d<WORLD_HASH_DOMAINS;d++) {lockstep_sha256_final(&roots[d],raw);hex32(raw,hexroots[d]);}
    world_hash_tick=tick;++world_hash_scans;world_hash_failure_id=0;world_hash_stage="complete";
    snprintf(world_hash_state,sizeof(world_hash_state),"WORLD_IMPORTED_%u:%llu:%u:%s:%s:%s:%s",schema,(unsigned long long)tick,total,hexroots[0],hexroots[1],hexroots[2],hexroots[3]);
    /* created= appears only when created identities are enabled, so default captures are unchanged. */
    char created[32]="";if(world_hash_created) snprintf(created,sizeof(created)," created=%u",world_hash_created_count);
    snprintf(world_hash_summary,sizeof(world_hash_summary),"tick=%llu objects=%u present=%u changed=%u unknown_class=%u unknown_motion=%u unknown_references=%u%s state=%s",
        (unsigned long long)tick,total,world_hash_alive,world_hash_changes,world_hash_class_unknown,world_hash_motion_unknown,world_hash_parent_unknown,created,world_hash_state);
    return 1;
}

/* ---- Lua (lockstep_gate.c formats) ---- */
static int lua_world_watch(lua_State *L) {
    size_t size=0;const char *text=p_lua_tolstring(L,1,&size);
    p_lua_pushnumber(L,accept_ui() && world_hash_watch_add(text,size));return 1;
}
static int lua_world_detail(lua_State *L) {
    double number=p_lua_tonumber(L,1);
    if(!accept_ui() || !isfinite(number) || number<0 || floor(number)!=number || !world_hash_scans || world_hash_failures) {p_lua_pushnil(L);return 1;}
    unsigned count=world_hash_watch_count*WORLD_HASH_DOMAINS;
    if(number==0) {p_lua_pushnumber(L,count);return 1;}
    if(number>count) {p_lua_pushnil(L);return 1;}
    unsigned index=(unsigned)number-1,domain=index%WORLD_HASH_DOMAINS;
    const char *names[WORLD_HASH_DOMAINS]={"topology","health","motion","timing"};
    world_hash_watch *watch=&world_hash_watches[index/WORLD_HASH_DOMAINS];world_hash_data *data=&watch->data[domain];
    char out[2300];int n=snprintf(out,sizeof(out),"tick=%llu id=%016llx domain=%s bytes=",
        (unsigned long long)world_hash_tick,(unsigned long long)watch->saved,names[domain]);
    static const char digits[]="0123456789abcdef";
    if(n<0 || (size_t)n+data->length*2>=sizeof(out)) {p_lua_pushnil(L);return 1;}
    for(unsigned i=0;i<data->length;i++) {out[n++]=digits[data->data[i]>>4];out[n++]=digits[data->data[i]&15];}
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_world_banks(lua_State *L) {
    if(GetCurrentThreadId()!=ui_thread || !world_hash_scans || world_hash_failures) {p_lua_pushnil(L);return 1;}
    char out[160];int n=snprintf(out,sizeof(out),"tick=%llu read_before=%u write_before=%u read_after=%u write_after=%u",
        (unsigned long long)world_hash_tick,world_banks_before.read,world_banks_before.write,world_banks_after.read,world_banks_after.write);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_world_capture_schema(lua_State *L,unsigned schema) {
    double number=p_lua_tonumber(L,1);
    if(!accept_ui() || !stats.active || !boot_mode || boot_phase!=3 || !world_identity_installed ||
       !isfinite(number) || number<1 || number!=(double)stats.issued || !world_hash_capture((uint64_t)number,schema)) {
        static char error[8000];int n=snprintf(error,sizeof(error),"saved=%016llx stage=%s",(unsigned long long)world_hash_failure_id,world_hash_stage);
        if(lxw_capture_problem_count) lxw_capture_problems_text(error+n,sizeof(error)-(size_t)n);
        ++world_hash_failures;p_lua_pushnil(L);p_lua_pushlstring(L,error,strlen(error));return 2;
    }
    p_lua_pushlstring(L,world_hash_summary,strlen(world_hash_summary));return 1;
}
static int lua_world_capture(lua_State *L) {return lua_world_capture_schema(L,1);}
static int lua_world_capture_nav_v2(lua_State *L) {return lua_world_capture_schema(L,2);}
static int lua_world_page(lua_State *L) {
    double number=p_lua_tonumber(L,1);
    if(!accept_ui() || !isfinite(number) || number<0 || floor(number)!=number || !world_hash_scans || world_hash_failures) {p_lua_pushnil(L);return 1;}
    unsigned pages=(world_hash_changes+WORLD_HASH_PAGE-1)/WORLD_HASH_PAGE;
    if(number==0) {p_lua_pushnumber(L,pages);return 1;}
    if(number>pages) {p_lua_pushnil(L);return 1;}
    static char out[12288];int n=0;unsigned begin=((unsigned)number-1)*WORLD_HASH_PAGE,end=begin+WORLD_HASH_PAGE;
    if(end>world_hash_changes) end=world_hash_changes;
    for(unsigned i=begin;i<end;i++) {
        world_hash_item *item=world_hash_item_at(world_hash_changed[i]);char hashes[WORLD_HASH_DOMAINS][65],timing[WORLD_TIMING_BYTES*2+1];
        if(item->timing_length>WORLD_TIMING_BYTES || item->nav_raw_length>WORLD_NAV_RAW_BYTES) {++world_hash_failures;p_lua_pushnil(L);return 1;}
        for(unsigned d=0;d<WORLD_HASH_DOMAINS;d++) hex32(item->hash[d],hashes[d]);
        static const char digits[]="0123456789abcdef";
        for(unsigned j=0;j<item->timing_length;j++) {timing[2*j]=digits[item->timing[j]>>4];timing[2*j+1]=digits[item->timing[j]&15];}
        timing[2*item->timing_length]=0;
        char raw[WORLD_NAV_RAW_BYTES*2+1],suffix[WORLD_NAV_RAW_BYTES*2+16];suffix[0]=0;
        if(world_hash_schema==2) {
            for(unsigned j=0;j<item->nav_raw_length;j++) {raw[2*j]=digits[item->nav_raw[j]>>4];raw[2*j+1]=digits[item->nav_raw[j]&15];}
            raw[2*item->nav_raw_length]=0;snprintf(suffix,sizeof(suffix)," nav_raw=%s",item->nav_raw_length?raw:"-");
        }
        n+=snprintf(out+n,sizeof(out)-(size_t)n,"[X4LAN] NATIVE_GATE WORLD_OBJECT tick=%llu id=%016llx type=%x flags=%x parent=%016llx topology=%s health=%s motion=%s timing=%s timing_fields=%s%s\n",
            (unsigned long long)world_hash_tick,(unsigned long long)item->saved,item->type,item->flags,(unsigned long long)item->parent,hashes[0],hashes[1],hashes[2],hashes[3],item->timing_length?timing:"-",suffix);
        if(n<0 || (size_t)n>=sizeof(out)) {++world_hash_failures;p_lua_pushnil(L);return 1;}
    }
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_saved_identity(lua_State *L) {
    size_t size=0;const char *text=p_lua_tolstring(L,1,&size);uint64_t runtime=0;
    if(!text || !size || size>20 || !accept_ui()) {p_lua_pushnil(L);return 1;}
    for(size_t i=0;i<size;i++) {
        unsigned digit=(unsigned)(text[i]-'0');
        if(digit>9 || runtime>(UINT64_MAX-digit)/10) {p_lua_pushnil(L);return 1;}
        runtime=runtime*10+digit;
    }
    char out[17];snprintf(out,sizeof(out),"%016llx",(unsigned long long)world_identity_lookup(runtime));
    p_lua_pushlstring(L,out,16);return 1;
}
