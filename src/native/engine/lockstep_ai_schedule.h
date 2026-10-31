/* Census fix, DEFAULT OFF: the Gravidar sector round-robin.
 *
 * S03/A01: hundreds of AI owners ran the same script sequence with identical
 * draws one gated tick apart. The script schedulers are not the cause:
 * AIDirector::Move 0x106d250 runs the contexts of its double-buffered wake
 * vectors (dir+0x200/+0x218) and moves timed wakes (multimap dir+0x1c0) whose
 * time is <= the sim thread's TLS game time; MDScheduler_Update 0x1121f80 pops
 * its heap while time <= that TLS time. Neither reads a wall clock, caps work
 * per frame or keeps a cursor. What moved was their input: the shifted chains
 * start with event_gravidar_has_scanned interrupts (move.seekenemies,
 * order.fight.escort, fight.attack.object.*). ActiveGravidarUnit slot 7
 * 0x10acd70 posts that event while the Gravidar thread scans a sector
 * (0x10b34c0 -> 0x10b5410), into the worker's private command list +0x69b0;
 * the next publish 0x10b12c0 splices it into the event queue.
 *
 * Gravidar::Run 0x10ae0e0 scans the player's entry (+0x68d0) every pass, but
 * the other entries of its sector list (+0x68b8, 32 bytes, [0] = sector) are
 * visited round-robin: max(1,count/10) consecutive entries per pass from a
 * cursor in a stack local of the thread loop ([rbp-0x70]: zeroed once at
 * thread start 0x10ae18e, read 0x10aeffa, stored 0x10af119, wrapped at count).
 * Gravidar Init 0x10aff30 (from GameClass::Load) builds the list by binary
 * insertion on the sector object's ADDRESS (0x10b0260..0x10b030b) and then
 * starts a load pass at once (0x10b0fed). Two clients loading the same save
 * therefore scan different sector windows whenever their allocators placed
 * sectors in a different address order: A01 client 2 had six late sectors
 * among its first fifteen entries, so its load pass and every later window
 * differed (Gravidar units per pass 1341/536/983... versus 1048/986/725...).
 * The cursor itself was 0 on both clients (boot_writes=0).
 *
 * Mask (gate.ai_schedule(mask), default off; failures stop with reason 84):
 *  1 cursor: zero the idle cursor before the load pass (phases 1-2, or phase 0
 *    while the Gravidar is disabled), check at admission, and require the
 *    native advance before every gated publish, hashed with tick and count.
 *  2 hold: while the Gravidar is still disabled (+0x44 == 0, so no publish or
 *    reset can wait on it) in phases 1-2, mark the idle worker busy (ready
 *    byte +0x6988 = 0) under its ready lock +0x6960. Init then skips its kick,
 *    so the loaded world's first pass is the one the first gated frame starts,
 *    with synced TLS time and a fully loaded world. The byte is set back to 1
 *    at phase 3 entry (and on stop); admission then requires cursor 0.
 *  4 order: the gated publish runs with the ready lock held (recursive), so
 *    the pass it kicks waits at its own lock barrier (0x10ae24c) while the
 *    entries are permuted, under the list lock +0x6890, into saved-sector-ID
 *    order and the player's index (+0x68d0) is remapped. The pass only reaches
 *    entries through that vector (base read only by Run and 0x10b34c0; no
 *    entry address or index is kept). After the pass, before anything else
 *    can use the vector, the address order and index are restored, so the
 *    publish keeps its own consistent layout (+0x68e0 index map, unit index
 *    lists). Windows are then identical on every client. An entry whose
 *    sector has no saved ID (B01: 153 entries, 152 sectors imported)
 *    gets a fallback key from its U::Macro name (+0x60, name getter slot 18
 *    = +0x20) mixed with its parent's saved ID (or parent macro); fallback
 *    keys carry bit 63 and sort after saved IDs. Only an entry without any
 *    portable key, or two equal keys, refuses.
 * The cursor slot is found once per bind by its frame: the thread trampoline
 * 0x14b7740 calls Runnable slot 1 (Run), so [E] is its return address
 * 0x14b779a and [E+8] Run's home of `this` (written at entry, only read
 * later); the slot is E-0x288 (8 pushes, rbp=rsp-0x1d8, rbp-0x70). The frame
 * is re-checked before every access. Only an idle worker is touched. No code
 * byte is patched; stop restores the ready byte and the address order. */
/* The Linux gate (linux/gate_gravidar.h) defines these for its executable, and its own thread access. */
#ifndef AI_SCHED_OWN_PLATFORM
#define AI_SCHED_WORKER 0x6cf1650u
#define AI_SCHED_VTABLE 0x2c68860u
#define AI_SCHED_RUN 0x10ae0e0u
#define AI_SCHED_TRAMPOLINE 0x14b7740u
#define AI_SCHED_RETURN 0x14b779au
#define AI_SCHED_CURSOR 0x288u
#define AI_SCHED_W_READY_LOCK 0x6960
#define AI_SCHED_W_READY 0x6988
#define AI_SCHED_W_LIST 0x68b8
#define AI_SCHED_W_PRIORITY 0x68d0
#define AI_SCHED_W_LIST_LOCK 0x6890
#define AI_SCHED_PUBLISH 0x10b12c0u
#endif
#define AI_SCHED_WINDOW 0x10000u
#define AI_SCHED_ENTRIES 0x100000u
#define AI_SCHED_ORDER_CAP 4096u
enum {AI_SCHED_GRAVIDAR=1,AI_SCHED_HOLD=2,AI_SCHED_ORDER=4};
static volatile LONG ai_sched_armed;
static unsigned ai_sched_mask;
static unsigned char *ai_sched_worker;
static uintptr_t ai_sched_entry;
static volatile uint64_t *ai_sched_slot;
static int ai_sched_zeroed,ai_sched_latched,ai_sched_admitted,ai_sched_pending,ai_sched_restored;
static uint64_t ai_sched_binds,ai_sched_boot_checks,ai_sched_boot_busy,ai_sched_boot_writes,ai_sched_boot_prior;
static uint64_t ai_sched_boot_cursor,ai_sched_boot_count,ai_sched_cursor,ai_sched_count,ai_sched_samples;
static uint64_t ai_sched_sample_tick,ai_sched_irregular,ai_sched_hash=0xcbf29ce484222325ULL;
static volatile LONG64 ai_sched_failures;
static const char *ai_sched_stage="idle";
static uint64_t ai_sched_window[AI_SCHED_WINDOW/8];
/* hold (mask 2) and order (mask 4) */
static int ai_sched_held,ai_sched_released,ai_sched_permuted;
static uint64_t ai_sched_hold_phase,ai_sched_hold_enabled,ai_sched_orders,ai_sched_order_restores,ai_sched_order_moved;
static uint64_t ai_sched_order_hash,ai_sched_order_unmapped,ai_sched_order_fallback,ai_sched_order_collisions;
#define AI_SCHED_FALLBACK_BIT 0x8000000000000000ULL
#define AI_SCHED_NOTES 8u
static char ai_sched_fallback_note[1024];
static unsigned ai_sched_order_count;
static uint64_t ai_sched_priority_native,ai_sched_priority_portable;
static uint32_t ai_sched_perm[AI_SCHED_ORDER_CAP];
static uint64_t ai_sched_keys[AI_SCHED_ORDER_CAP];
static uintptr_t ai_sched_order_bounds[2],ai_sched_order_sectors[AI_SCHED_ORDER_CAP];
static unsigned char ai_sched_order_copy[AI_SCHED_ORDER_CAP][32];

/* Native round-robin arithmetic of 0x10aefd0..0x10af119. */
static uint64_t ai_sched_step(uint64_t count) {uint64_t n=count/10;return n?n:1;}
static uint64_t ai_sched_advance(uint64_t cursor,uint64_t count) {
    for(uint64_t i=ai_sched_step(count);i;i--) cursor=cursor+1<count?cursor+1:0;
    return cursor;
}
#ifdef AI_SCHED_OWN_PLATFORM
static int ai_sched_frame_ok(void);
static int ai_sched_bind(void);
#else
/* One trampoline->Run frame in a stack window: [E]=return, [E+8]=worker. */
static int ai_sched_scan(const uint64_t *words,size_t count,uintptr_t low,uintptr_t limit,
                         uint64_t ret,uint64_t worker,uintptr_t *entry) {
    unsigned matches=0;uintptr_t found=0;
    for(size_t i=0;i+1<count;i++) if(words[i]==ret && words[i+1]==worker) {found=low+i*8;++matches;}
    if(matches!=1 || found<limit+AI_SCHED_CURSOR || found-AI_SCHED_CURSOR<limit) return 0;
    *entry=found;return 1;
}
typedef struct {LONG status;void *teb;uintptr_t process,thread,affinity;LONG priority,base;} ai_sched_thread_info;
_Static_assert(sizeof(ai_sched_thread_info)==48,"THREAD_BASIC_INFORMATION layout");
static unsigned char *ai_sched_find_teb(void) {
    typedef LONG (WINAPI *query_fn)(HANDLE,int,void *,ULONG,ULONG *);typedef LONG (WINAPI *name_fn)(HANDLE,PWSTR *);
    query_fn query=(query_fn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQueryInformationThread");
    name_fn describe=(name_fn)(void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"GetThreadDescription");
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
    if(!query || !describe || snap==INVALID_HANDLE_VALUE) {if(snap!=INVALID_HANDLE_VALUE) CloseHandle(snap);return NULL;}
    unsigned char *found=NULL;unsigned matches=0;THREADENTRY32 entry;entry.dwSize=sizeof(entry);
    for(BOOL ok=Thread32First(snap,&entry);ok;ok=Thread32Next(snap,&entry)) {
        if(entry.th32OwnerProcessID!=GetCurrentProcessId()) continue;
        HANDLE thread=OpenThread(THREAD_QUERY_INFORMATION,FALSE,entry.th32ThreadID);if(!thread) continue;
        PWSTR name=NULL;ai_sched_thread_info basic;
        if(describe(thread,&name)>=0 && name && !wcscmp(name,L"Gravidar") && query(thread,0,&basic,sizeof(basic),NULL)>=0 && basic.teb) {
            found=basic.teb;++matches;
        }
        if(name) LocalFree(name);
        CloseHandle(thread);
    }
    CloseHandle(snap);return matches==1?found:NULL;
}
static int ai_sched_frame_ok(void) {
    uint64_t frame[2];
    return ai_sched_entry && read_local((void*)ai_sched_entry,frame,sizeof(frame)) &&
        frame[0]==(uint64_t)(uintptr_t)(image+AI_SCHED_RETURN) && frame[1]==(uint64_t)(uintptr_t)ai_sched_worker;
}
/* The worker must be the one bound first; the frame is found once and then
 * re-checked, so a recreated thread or worker fails closed. */
static int ai_sched_bind(void) {
    unsigned char *worker=*(unsigned char**)(image+AI_SCHED_WORKER);void *vtable=NULL;
    if(!worker || !read_local(worker,&vtable,8) || vtable!=image+AI_SCHED_VTABLE) {ai_sched_stage="worker";return 0;}
    if(ai_sched_worker) {
        if(worker!=ai_sched_worker || !ai_sched_frame_ok()) {ai_sched_stage="frame_moved";return 0;}
        return 1;
    }
    unsigned char *teb=ai_sched_find_teb();uintptr_t base=0,limit=0,entry=0;
    if(!teb || !read_local(teb+8,&base,8) || !read_local(teb+16,&limit,8) || !base || base<=limit || (base&7) || (limit&7)) {
        ai_sched_stage="thread";return 0;
    }
    uintptr_t low=base-limit>AI_SCHED_WINDOW?base-AI_SCHED_WINDOW:limit;
    if(!read_local((void*)low,ai_sched_window,base-low) ||
       !ai_sched_scan(ai_sched_window,(base-low)/8,low,limit,(uint64_t)(uintptr_t)(image+AI_SCHED_RETURN),(uint64_t)(uintptr_t)worker,&entry)) {
        ai_sched_stage="frame";return 0;
    }
    ai_sched_worker=worker;ai_sched_entry=entry;ai_sched_slot=(volatile uint64_t*)(entry-AI_SCHED_CURSOR);++ai_sched_binds;
    if(!ai_sched_frame_ok()) {ai_sched_stage="frame";return 0;}
    return 1;
}
#endif
static int ai_sched_list(unsigned char *w,uint64_t *count) {
    uintptr_t bounds[2];
    if(!read_local(w+AI_SCHED_W_LIST,bounds,sizeof(bounds)) || bounds[1]<bounds[0] || ((bounds[1]-bounds[0])&31) ||
       (!bounds[0] && bounds[1]) || (bounds[1]-bounds[0])/32>AI_SCHED_ENTRIES) return 0;
    *count=(bounds[1]-bounds[0])/32;return 1;
}
/* Under the worker's ready lock: 1 idle (values read, optional zeroing done
 * while the slot cannot be in use), 0 busy, -1 unreadable. */
enum {AI_SCHED_READ=0,AI_SCHED_ZERO=1,AI_SCHED_ZERO_DISABLED=2};
static int ai_sched_peek(int mode,uint64_t *cursor,uint64_t *count,uint32_t *enabled,uint64_t *prior) {
    unsigned char *w=ai_sched_worker;int result=0;*prior=0;
    EnterCriticalSection((CRITICAL_SECTION*)(w+AI_SCHED_W_READY_LOCK));
    if(*(volatile unsigned char*)(w+AI_SCHED_W_READY)==1) {
        result=-1;
        if(ai_sched_frame_ok() && read_local((const void*)ai_sched_slot,cursor,8) && ai_sched_list(w,count) &&
           read_local(w+0x44,enabled,4) && *cursor<=AI_SCHED_ENTRIES) {
            result=1;
            if(*cursor && (mode==AI_SCHED_ZERO || (mode==AI_SCHED_ZERO_DISABLED && !*enabled))) {*prior=*cursor;*ai_sched_slot=0;*cursor=0;}
        }
    }
    LeaveCriticalSection((CRITICAL_SECTION*)(w+AI_SCHED_W_READY_LOCK));
    return result;
}
static uint64_t ai_sched_fnv(uint64_t hash,uint64_t value) {
    for(unsigned i=0;i<8;i++) {hash^=(unsigned char)(value>>(i*8));hash*=0x100000001b3ULL;}
    return hash;
}
/* Hold (mask 2): an idle, still disabled worker is marked busy, under the
 * ready lock, together with a zero cursor. 1 held, 2 enabled (cannot hold),
 * 0 busy, -1 unreadable. */
static int ai_sched_try_hold(uint64_t *prior) {
    unsigned char *w=ai_sched_worker;int result=0;uint64_t cursor=0,count=0;uint32_t enabled=0;*prior=0;
    EnterCriticalSection((CRITICAL_SECTION*)(w+AI_SCHED_W_READY_LOCK));
    if(*(volatile unsigned char*)(w+AI_SCHED_W_READY)==1) {
        result=-1;
        if(ai_sched_frame_ok() && read_local((const void*)ai_sched_slot,&cursor,8) && ai_sched_list(w,&count) &&
           read_local(w+0x44,&enabled,4) && cursor<=AI_SCHED_ENTRIES) {
            result=enabled?2:1;
            if(result==1) {
                if(cursor) {*prior=cursor;*ai_sched_slot=0;}
                *(volatile unsigned char*)(w+AI_SCHED_W_READY)=0;
            }
        }
    }
    LeaveCriticalSection((CRITICAL_SECTION*)(w+AI_SCHED_W_READY_LOCK));
    return result;
}
/* Main thread, boot phases 0-2, before FrameTick. */
static int ai_sched_boot(int phase) {
    if(!ai_sched_armed || ai_sched_latched || ai_sched_admitted || ai_sched_held) return 1;
    uint64_t cursor=0,count=0,prior=0;uint32_t enabled=0;
    if(!ai_sched_bind()) return 0;
    ++ai_sched_boot_checks;
    if(phase && (ai_sched_mask&AI_SCHED_HOLD)) {
        int held=ai_sched_try_hold(&prior);
        if(held<0) {ai_sched_stage="hold_read";return 0;}
        if(!held) {++ai_sched_boot_busy;return 1;}
        if(held==1) {
            if(prior) {ai_sched_boot_prior=prior;++ai_sched_boot_writes;}
            ai_sched_held=1;ai_sched_zeroed=1;ai_sched_hold_phase=(uint64_t)phase;ai_sched_stage="boot_hold";return 1;
        }
        ++ai_sched_hold_enabled; /* a world is already enabled: fall through, release refuses */
    }
    int mode=!phase?AI_SCHED_ZERO_DISABLED:ai_sched_zeroed?AI_SCHED_READ:AI_SCHED_ZERO;
    int state=ai_sched_peek(mode,&cursor,&count,&enabled,&prior);
    if(state<0) {ai_sched_stage="boot_read";return 0;}
    if(!state) {++ai_sched_boot_busy;return 1;}
    if(prior) {ai_sched_boot_prior=prior;++ai_sched_boot_writes;}
    if(!phase) {ai_sched_zeroed=!enabled;ai_sched_stage=enabled?"boot_menu":"boot_zero";return 1;}
    if(ai_sched_zeroed && cursor) {
        ai_sched_latched=1;ai_sched_boot_cursor=cursor;ai_sched_boot_count=count;ai_sched_stage="boot_pass";return 1;
    }
    /* Zero now, or still zero: no pass yet (or a list of one entry). */
    ai_sched_zeroed=1;ai_sched_stage="boot_zero";return 1;
}
/* Main thread, phase 3 entry, before the Gravidar boot drain: give the
 * worker its ready byte back. Init must have enabled it, and no pass may have
 * run (the cursor is still the zero written with the hold). */
static int ai_sched_release(void) {
    if(!ai_sched_armed || !(ai_sched_mask&AI_SCHED_HOLD)) return 1;
    if(!ai_sched_held) {ai_sched_stage=ai_sched_hold_enabled?"hold_enabled":"hold_missed";return 0;}
    if(!ai_sched_bind()) return 0;
    unsigned char *w=ai_sched_worker;uint64_t cursor=1;uint32_t enabled=0;int ok=0;
    EnterCriticalSection((CRITICAL_SECTION*)(w+AI_SCHED_W_READY_LOCK));
    if(*(volatile unsigned char*)(w+AI_SCHED_W_READY)==0) {
        ok=read_local((const void*)ai_sched_slot,&cursor,8) && read_local(w+0x44,&enabled,4) && enabled==1 && !cursor;
        *(volatile unsigned char*)(w+AI_SCHED_W_READY)=1;ai_sched_held=0;ai_sched_released=1;
    }
    LeaveCriticalSection((CRITICAL_SECTION*)(w+AI_SCHED_W_READY_LOCK));
    if(!ai_sched_released) {ai_sched_stage="hold_lost";return 0;}
    if(!ok) {ai_sched_stage=enabled!=1?"hold_no_init":"hold_pass";return 0;}
    return 1;
}
/* Main thread, phase 3 entry, after the Gravidar boot drain. */
static int ai_sched_admit(void) {
    if(!ai_sched_armed) return 1;
    uint64_t cursor=0,count=0,prior=0;uint32_t enabled=0;
    if(!ai_sched_bind()) return 0;
    int state=ai_sched_peek(AI_SCHED_READ,&cursor,&count,&enabled,&prior);
    if(state<=0) {ai_sched_stage=state?"admit_read":"admit_busy";return 0;}
    ai_sched_boot_cursor=cursor;ai_sched_boot_count=count;
    if(!ai_sched_zeroed) {ai_sched_stage="admit_unzeroed";return 0;}
    /* After one pass from 0 the cursor is max(1,count/10) (count >= 2); any
     * other start, or a start >= count, ends elsewhere. A held load pass never
     * ran, so the cursor is still 0. */
    if(cursor!=((ai_sched_mask&AI_SCHED_HOLD)?0:ai_sched_advance(0,count))) {ai_sched_stage="admit_phase";return 0;}
    ai_sched_latched=cursor!=0;ai_sched_cursor=cursor;ai_sched_count=count;ai_sched_pending=0;
    ai_sched_hash=ai_sched_fnv(ai_sched_fnv(ai_sched_hash,cursor),count);
    ai_sched_admitted=1;ai_sched_stage="admitted";return 1;
}
/* MoveWorker, every gated frame, before the gated publish kicks a pass. */
static int ai_sched_frame(void) {
    if(!ai_sched_armed) return 1;
    uint64_t cursor=0,count=0,prior=0;uint32_t enabled=0;
    if(!ai_sched_admitted || !ai_sched_slot) {ai_sched_stage="frame_unadmitted";return 0;}
    if(ai_sched_permuted) {ai_sched_stage="order_unrestored";return 0;}
    if(!ai_sched_bind()) return 0;
    int state=ai_sched_peek(AI_SCHED_READ,&cursor,&count,&enabled,&prior);
    if(state<=0) {ai_sched_stage=state?"frame_read":"frame_busy";return 0;}
    if(enabled!=1) {ai_sched_stage="disabled";return 0;}
    /* The list only changes in a publish, which precedes the pass it kicks. */
    uint64_t expected=ai_sched_pending?ai_sched_advance(ai_sched_cursor,count):ai_sched_cursor;
    if(cursor!=expected) {++ai_sched_irregular;ai_sched_stage="irregular";return 0;}
    ai_sched_hash=ai_sched_fnv(ai_sched_fnv(ai_sched_fnv(ai_sched_fnv(ai_sched_hash,stats.issued),cursor),count),ai_sched_order_hash);
    ai_sched_cursor=cursor;ai_sched_count=count;ai_sched_pending=1;ai_sched_sample_tick=stats.issued;++ai_sched_samples;
    ai_sched_stage="frame";return 1;
}
#ifdef AI_SCHED_OWN_PLATFORM
static int ai_sched_macro(const unsigned char *component,char out[96]);
#else
/* Macro name through U::Macro (vtable 0x2ba5430, slot 18 0xefb10 returns
 * +0x20, an MSVC string): lower-case identifier characters only. */
static int ai_sched_macro(const unsigned char *component,char out[96]) {
    unsigned char *macro=NULL;void *vtable=NULL,*getter=NULL;uint64_t length=0,capacity=0;const unsigned char *data;
    if(!read_local(component+0x60,&macro,8) || !macro || !read_local(macro,&vtable,8) || vtable!=image+0x2ba5430 ||
       !read_local((unsigned char*)vtable+18*8,&getter,8) || getter!=image+0xefb10 ||
       !read_local(macro+0x30,&length,8) || !length || length>=96 || !read_local(macro+0x38,&capacity,8) || capacity<length) return 0;
    data=macro+0x20;
    if(capacity>=16 && (!read_local(data,&data,8) || !data)) return 0;
    if(!read_local(data,out,(size_t)length+1) || out[length]) return 0;
    for(uint64_t i=0;i<length;i++) {char c=out[i];if(!((c>='a'&&c<='z') || (c>='0'&&c<='9') || c=='_')) return 0;}
    return 1;
}
#endif
static uint64_t ai_sched_text_hash(const char *text) {
    uint64_t hash=0xcbf29ce484222325ULL;
    for(;*text;text++) {hash^=(unsigned char)*text;hash*=0x100000001b3ULL;}
    return hash;
}
/* Portable key of an entry whose sector has no saved ID. Notes (bounded)
 * carry process-local runtime IDs for diagnosis only. */
static int ai_sched_fallback_key(const unsigned char *sector,uint64_t runtime,uint64_t *key,size_t *note) {
    char macro[96]="?",parent_macro[96];unsigned char *parent=NULL;uintptr_t vtable=0;
    uint64_t parent_runtime=0,parent_key=0;int ok=ai_sched_macro(sector,macro);
    read_local(sector,&vtable,8);
    if(ok && read_local(sector+0x70,&parent,8) && parent && read_local(parent+8,&parent_runtime,8) && parent_runtime) {
        parent_key=world_identity_lookup(parent_runtime);
        if(parent_key>=AI_SCHED_FALLBACK_BIT) ok=0;
        else if(!parent_key && ai_sched_macro(parent,parent_macro)) parent_key=AI_SCHED_FALLBACK_BIT|ai_sched_text_hash(parent_macro);
    }
    if(ok) *key=AI_SCHED_FALLBACK_BIT|(ai_sched_fnv(ai_sched_text_hash(macro),parent_key)&0x3fffffffffffffffULL);
    if(ai_sched_order_fallback+ai_sched_order_unmapped<AI_SCHED_NOTES && *note<sizeof(ai_sched_fallback_note)) {
        int n=snprintf(ai_sched_fallback_note+*note,sizeof(ai_sched_fallback_note)-*note,
            " [runtime=%llx vt=%llx macro=%s parent=%llx/%llx key=%016llx]",(unsigned long long)runtime,
            (unsigned long long)(vtable>=(uintptr_t)image?vtable-(uintptr_t)image:vtable),macro,(unsigned long long)parent_runtime,
            (unsigned long long)parent_key,(unsigned long long)(ok?*key:0));
        if(n>0) *note=*note+(size_t)n<sizeof(ai_sched_fallback_note)?*note+(size_t)n:sizeof(ai_sched_fallback_note)-1;
    }
    return ok;
}
/* Order (mask 4). Keys: saved ID of the entry's sector (runtime ID at +8),
 * else the fallback key; empty entries sort last, in address order.
 * perm[new]=old. 1 planned, 0 no portable key, -1 equal keys. */
static int ai_sched_order_plan(const unsigned char *entries,unsigned count,uint64_t *keys,uint32_t *perm) {
    size_t note=0;ai_sched_order_unmapped=ai_sched_order_fallback=0;ai_sched_fallback_note[0]=0;
    for(unsigned i=0;i<count;i++) {
        const unsigned char *sector=*(unsigned char *const*)(entries+(size_t)i*32);uint64_t runtime=0;
        perm[i]=i;
        if(!sector) {keys[i]=UINT64_MAX;continue;}
        if(!read_local(sector+8,&runtime,8) || !runtime) {++ai_sched_order_unmapped;return 0;}
        keys[i]=world_identity_lookup(runtime);
        if(keys[i]>=AI_SCHED_FALLBACK_BIT) {++ai_sched_order_unmapped;return 0;}
        if(!keys[i]) {
            if(!ai_sched_fallback_key(sector,runtime,&keys[i],&note)) {++ai_sched_order_unmapped;return 0;}
            ++ai_sched_order_fallback;
        }
    }
    for(unsigned i=1;i<count;i++) { /* stable insertion sort */
        uint32_t v=perm[i];unsigned j=i;
        while(j && keys[perm[j-1]]>keys[v]) {perm[j]=perm[j-1];--j;}
        perm[j]=v;
    }
    for(unsigned i=1;i<count;i++) if(keys[perm[i]]==keys[perm[i-1]] && keys[perm[i]]!=UINT64_MAX) {++ai_sched_order_collisions;return -1;}
    return 1;
}
/* MoveWorker, inside the gated publish, ready lock held: the kicked pass waits. */
static int ai_sched_order_apply(unsigned char *w) {
    uintptr_t bounds[2];uint64_t priority=0;int ok=0,plan=0;
    EnterCriticalSection((CRITICAL_SECTION*)(w+AI_SCHED_W_LIST_LOCK));
    if(read_local(w+AI_SCHED_W_LIST,bounds,sizeof(bounds)) && bounds[1]>=bounds[0] && !((bounds[1]-bounds[0])&31) &&
       (bounds[1]-bounds[0])/32<=AI_SCHED_ORDER_CAP && (bounds[0] || !bounds[1]) && read_local(w+AI_SCHED_W_PRIORITY,&priority,8)) {
        unsigned count=(unsigned)((bounds[1]-bounds[0])/32);unsigned char *v=(unsigned char*)bounds[0];
        plan=ai_sched_order_plan(v,count,ai_sched_keys,ai_sched_perm);
        if(plan==1) {
            uint64_t moved=0,hash=0xcbf29ce484222325ULL,portable=priority;
            if(count) memcpy(ai_sched_order_copy,v,(size_t)count*32);
            for(unsigned i=0;i<count;i++) {
                memcpy(v+(size_t)i*32,ai_sched_order_copy[ai_sched_perm[i]],32);
                ai_sched_order_sectors[i]=*(uintptr_t*)(v+(size_t)i*32);
                if(ai_sched_perm[i]!=i) ++moved;
                if(priority<count && ai_sched_perm[i]==priority) portable=i;
                hash=ai_sched_fnv(hash,ai_sched_keys[ai_sched_perm[i]]);
            }
            *(volatile uint64_t*)(w+AI_SCHED_W_PRIORITY)=portable;
            ai_sched_order_bounds[0]=bounds[0];ai_sched_order_bounds[1]=bounds[1];ai_sched_order_count=count;
            ai_sched_priority_native=priority;ai_sched_priority_portable=portable;ai_sched_order_moved+=moved;
            ai_sched_order_hash=ai_sched_fnv(hash,portable<count?ai_sched_keys[ai_sched_perm[portable]]:UINT64_MAX);
            ai_sched_permuted=1;++ai_sched_orders;ok=1;
        }
    }
    LeaveCriticalSection((CRITICAL_SECTION*)(w+AI_SCHED_W_LIST_LOCK));
    if(!ok) ai_sched_stage=plan<0?"order_collision":ai_sched_order_unmapped?"order_unmapped":"order_read";
    return ok;
}
typedef void (*ai_sched_publish_fn)(void *,unsigned char);
static int ai_sched_publish_with(ai_sched_publish_fn native,void *worker,unsigned char reset) {
    CRITICAL_SECTION *ready=(CRITICAL_SECTION*)((unsigned char*)worker+AI_SCHED_W_READY_LOCK);int ok=1;
    if(!(ai_sched_mask&AI_SCHED_ORDER) || !ai_sched_armed || ai_sched_restored || ai_sched_permuted) {
        native(worker,reset);return !ai_sched_permuted;
    }
    EnterCriticalSection(ready);
    __try {
        native(worker,reset);
        ok=ai_sched_order_apply((unsigned char*)worker);
    } __finally {
        LeaveCriticalSection(ready);
    }
    return ok;
}
static void ai_sched_publish(void *worker,unsigned char reset) {
    if(!ai_sched_publish_with((ai_sched_publish_fn)(image+AI_SCHED_PUBLISH),worker,reset)) InterlockedIncrement64(&ai_sched_failures);
}
/* Idle worker only: the address order and the player's index come back
 * before any publish, Init or reset can see the vector. 1 restored (or
 * nothing to do), 0 busy, -1 the vector changed while permuted. */
static int ai_sched_order_restore(unsigned char *w) {
    if(!ai_sched_permuted) return 1;
    int result=0;
    EnterCriticalSection((CRITICAL_SECTION*)(w+AI_SCHED_W_READY_LOCK));
    if(*(volatile unsigned char*)(w+AI_SCHED_W_READY)==1) {
        uintptr_t bounds[2];uint64_t priority=0;result=-1;
        EnterCriticalSection((CRITICAL_SECTION*)(w+AI_SCHED_W_LIST_LOCK));
        if(read_local(w+AI_SCHED_W_LIST,bounds,sizeof(bounds)) && bounds[0]==ai_sched_order_bounds[0] && bounds[1]==ai_sched_order_bounds[1] &&
           read_local(w+AI_SCHED_W_PRIORITY,&priority,8) && priority==ai_sched_priority_portable) {
            unsigned char *v=(unsigned char*)bounds[0];unsigned count=ai_sched_order_count,i=0;
            while(i<count && *(uintptr_t*)(v+(size_t)i*32)==ai_sched_order_sectors[i]) ++i;
            if(i==count) {
                if(count) memcpy(ai_sched_order_copy,v,(size_t)count*32);
                for(i=0;i<count;i++) memcpy(v+(size_t)ai_sched_perm[i]*32,ai_sched_order_copy[i],32);
                *(volatile uint64_t*)(w+AI_SCHED_W_PRIORITY)=ai_sched_priority_native;
                ai_sched_permuted=0;++ai_sched_order_restores;result=1;
            }
        }
        LeaveCriticalSection((CRITICAL_SECTION*)(w+AI_SCHED_W_LIST_LOCK));
    }
    LeaveCriticalSection((CRITICAL_SECTION*)(w+AI_SCHED_W_READY_LOCK));
    if(result<0) ai_sched_stage="order_changed";
    return result;
}
/* MoveWorker, after the gated Gravidar frame joined its pass. */
static int ai_sched_settle(void) {
    if(!ai_sched_armed || !ai_sched_permuted) return 1;
    int state=ai_sched_order_restore(ai_sched_worker);
    if(state==1) return 1;
    if(!state) ai_sched_stage="order_busy";
    return 0;
}
static void ai_sched_restore(void) {
    if(!ai_sched_armed || ai_sched_restored) return;
    unsigned char *w=ai_sched_worker;
    if(w && ai_sched_held) {
        EnterCriticalSection((CRITICAL_SECTION*)(w+AI_SCHED_W_READY_LOCK));
        if(*(volatile unsigned char*)(w+AI_SCHED_W_READY)==0) *(volatile unsigned char*)(w+AI_SCHED_W_READY)=1;
        ai_sched_held=0;
        LeaveCriticalSection((CRITICAL_SECTION*)(w+AI_SCHED_W_READY_LOCK));
    }
    /* A pass may still run on the permuted vector: wait for it (bounded). */
    for(ULONGLONG started=GetTickCount64();w && ai_sched_permuted;Sleep(1)) {
        if(ai_sched_order_restore(w)<0 || GetTickCount64()-started>=5000) break;
    }
    ai_sched_slot=NULL;ai_sched_entry=0;ai_sched_restored=!ai_sched_permuted;
}
#ifdef AI_SCHED_OWN_PLATFORM
static int ai_sched_pins(void) {return 1;} /* the gate's site pins, checked at start */
#else
/* Run (frame layout, cursor init/read/store, ready barrier, round-robin,
 * player index), the trampoline's slot-1 call, the scan's list bound, Init's
 * address-ordered insertion and its enable-and-kick tail (no kick while the
 * ready byte is 0), and the publish (ready lock, ready test, kick). */
static int ai_sched_pins(void) {
    return hash_ok(image+AI_SCHED_RUN,4688,"d7c4d4cafbec8c8de0fd0cf68f2331452aadb35870f1817a57b05bd59421c29c") &&
        hash_ok(image+0x10b12c0,317,"89362a2325ff0149e2e41e08628ad92eb2405e592c3dfc6f3e3e148667399daa") &&
        hash_ok(image+0x10b0233,221,"428a43fc4b9f8340dda82ecc8350f8de5bf5a9342c24b3807f887312ce254e9e") &&
        hash_ok(image+0xefb10,5,"a8adead0a89e8bcc0f6079669194e35b31c483df478a1515e46c55a9cf74fb51") &&
        hash_ok(image+AI_SCHED_TRAMPOLINE,125,"cc0621b1d70386cded5da0181270d6ac97940f66e3c6f068b12fe62fc7093ba9") &&
        hash_ok(image+0x10b34c0,96,"061fd637db836b4c611e1a3d5c91c4994d739cdcdf4076864a09585341b3754a") &&
        hash_ok(image+0x10b0fd0,84,"def46a64a906241ae9d43a461a99eaed6a13a6ab8aead15c9b9341f775d12cc9");
}
#endif
static int ai_sched_arm(unsigned mask) {
    /* A loading-UI reload reruns configuration: accept only the same mask. */
    if(ai_sched_armed) return stats.active && ai_sched_mask==mask;
    if(!(mask&AI_SCHED_GRAVIDAR) || mask>7 || !stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread ||
       !path_installed || !(path_options&1) || !gravidar_installed || !gravidar_time_sync ||
       ((mask&AI_SCHED_ORDER) && !world_identity_installed) || !ai_sched_pins()) return 0;
    ai_sched_mask=mask;InterlockedExchange(&ai_sched_armed,1);return 1;
}
static int ai_sched_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"armed=%ld mask=%u binds=%llu boot_checks=%llu boot_busy=%llu boot_writes=%llu boot_prior=%llu zeroed=%d "
        "latched=%d admitted=%d boot_cursor=%llu boot_count=%llu samples=%llu at=%llu cursor=%llu count=%llu irregular=%llu "
        "hash=%016llx held=%d released=%d hold_phase=%llu hold_enabled=%llu orders=%llu order_restores=%llu permuted=%d "
        "order_count=%u order_moved=%llu order_fallback=%llu order_unmapped=%llu order_collisions=%llu order_hash=%016llx "
        "priority=%llu/%llu restored=%d stage=%s failures=%lld",
        ai_sched_armed,ai_sched_mask,(unsigned long long)ai_sched_binds,
        (unsigned long long)ai_sched_boot_checks,(unsigned long long)ai_sched_boot_busy,(unsigned long long)ai_sched_boot_writes,
        (unsigned long long)ai_sched_boot_prior,ai_sched_zeroed,ai_sched_latched,ai_sched_admitted,(unsigned long long)ai_sched_boot_cursor,
        (unsigned long long)ai_sched_boot_count,(unsigned long long)ai_sched_samples,(unsigned long long)ai_sched_sample_tick,
        (unsigned long long)ai_sched_cursor,(unsigned long long)ai_sched_count,(unsigned long long)ai_sched_irregular,
        (unsigned long long)ai_sched_hash,ai_sched_held,ai_sched_released,(unsigned long long)ai_sched_hold_phase,
        (unsigned long long)ai_sched_hold_enabled,(unsigned long long)ai_sched_orders,(unsigned long long)ai_sched_order_restores,
        ai_sched_permuted,ai_sched_order_count,(unsigned long long)ai_sched_order_moved,(unsigned long long)ai_sched_order_fallback,
        (unsigned long long)ai_sched_order_unmapped,(unsigned long long)ai_sched_order_collisions,(unsigned long long)ai_sched_order_hash,
        (unsigned long long)ai_sched_priority_native,(unsigned long long)ai_sched_priority_portable,
        ai_sched_restored,ai_sched_stage,(long long)ai_sched_failures);
    return n>0 && (size_t)n<capacity?n:0;
}
/* Bounded diagnostic of the entries without a saved ID in the last plan. */
static int ai_sched_fallbacks(char *out,size_t capacity) {
    if(!ai_sched_order_fallback && !ai_sched_order_unmapped) return 0;
    int n=snprintf(out,capacity,"fallback=%llu unmapped=%llu collisions=%llu entries=%s",(unsigned long long)ai_sched_order_fallback,
        (unsigned long long)ai_sched_order_unmapped,(unsigned long long)ai_sched_order_collisions,ai_sched_fallback_note[0]?ai_sched_fallback_note+1:"-");
    return n>0 && (size_t)n<capacity?n:0;
}
