/* Determinism fix, DEFAULT OFF: the parallel pathing pass's tempzone requests
 * granted in a portable order (native_gate_tempzone_order = 'order' or true;
 * 'observe' changes nothing and logs the native requests; needs
 * native_gate_world_identities and native_gate_serial_pathing, 'order' also
 * native_gate_travel_snapshot).
 * long-L21 and full-G1: 8bddc and ac854 warped into new tempzones at 1990/1991
 * on one client and 1991/1990 on the other; full-G1o: a7366/a738f (2245), a7351
 * (2256) and a737a (2292) warped at the same ticks on both clients, and from
 * 2400 only their motion differs. Their warps carry time 0 and space 0.
 * Mechanism (X4 9.00, static):
 * - Agent_UpdatePathing 0xe8f250 and the zone switch 0xe88160 run in the
 *   pathing batch jobs that RunPass 0xeae3c0 queues on the JobThreadManager.
 *   Those threads keep TLS game time 0, so everything they post (the warps
 *   through the batch command lists) carries time 0.
 * - An agent outside its zone (more than 50 km from the centre, or its path
 *   space and parent differ) asks FUN_140785db0(sector, &zone, position,
 *   create) for a zone at its position. The lookup 0x785b90 only sees zones
 *   active in the component view the thread reads (TLS+0x314 0: the view the
 *   main thread publishes, g_ComponentViewCacheIndex).
 * - One new tempzone may be pending process-wide: flag 0x3d72b88, zone
 *   0x3d72c40, critical section 0x3d72c48. The first job to take the lock while
 *   the flag is clear asks with create=1 (0xe910a1, 0xe890a1): 0x7877d0
 *   allocates the zone AT THAT AGENT'S POSITION (0x95f480, AutoIDMap) and posts
 *   an AddTempZoneEvent deferred to the sector; the job stores the zone in the
 *   slot and sets the flag. Every other agent of the pass asks with create=0
 *   (0xe9123c, 0xe89131), finds nothing and asks again in a later pass. RunPass
 *   clears the slot before queuing the jobs once the zone is active (0x69ed50).
 * So which agent gets the next zone, and where that zone is placed, is decided
 * by which job thread reaches the lock first. Two ships that need zones far
 * apart swap ticks (G1); ships flying together all enter one zone at the same
 * tick on both clients, but its origin is a different ship's position, so
 * their zone-relative motion differs (G1o). The zones are made off the
 * MoveWorker, so the created identities leave them unnamed (CREATED_ROW
 * kind=foreign macro=tempzone): the ships in them show parent 0 with flags bit
 * 0x10 and their warp rows space=0.
 * 'order': before each joined pass is released this module does RunPass's
 * clear on the MoveWorker (same published view), parks the slot and sets the
 * flag, so every request of the pass takes the create=0 lookup; RunPass then
 * finds nothing to clear. The entry of FUN_140785db0 (prologue relocated to
 * thunk 3024) records every lookup from the two create=0 request sites that
 * finds no zone: sector and position. After the join (late splices drained)
 * the slot is put back and, when it was free at the pass start, the smallest
 * request by (sector key, position bits, site) is granted as the winning job
 * would have: create=1 at its position under the slot lock, TLS game time 0 as
 * on a job, the TLS word keyed by (tick, sector key, position) and restored,
 * and the zone stored in the slot while it is not active. A pending zone still
 * defers every request, as natively. The zone is now made on the MoveWorker at
 * a fixed point of the frame, so it gets a created identity. A request that
 * finds a zone behaves as natively on either path: the lookup returns only
 * zones that pass the same active test in the reading view. Differences left:
 * the creator's lookup reads the MoveWorker's view, and a creating request
 * natively skipped the store when the agent was still inside its current
 * zone's shape (rare: it is past the 50 km test). 'observe' logs every
 * create=1 request of a pass (site, thread, sector, position, zone).
 * Counted, never arbitrated: request calls outside a joined pass (outside),
 * and create=1 calls from other code off the MoveWorker and the main thread
 * (other_creates). The image has one such caller: 0xee7290 (0xee7c7a), reached
 * only from U::Regions::RegionManager::Run 0xef30b0 through 0xeea8a0. It asks
 * for the zone at a region cell's centre ((cell index * size * 2000) in the
 * sector), so where a zone it makes sits does not depend on thread timing; the
 * region thread runs one pass at a time, cells in its own order, and
 * native_gate_region_pass 'sync' runs that pass between the end of a frame and
 * the next gate tick (MoveWorker parked, no pathing pass, slot untouched), so
 * when it asks is fixed too. Without 'sync' the region pass overlaps the next
 * frame and its timing is the region pass's class, not this slot's. long-L23:
 * other_creates=34 by tick 2 on both clients, no tempzone created then (the
 * cells found zones). other_made counts those calls that return a zone not yet
 * active in the published view (a new one; a found zone always passes that
 * test) and logs a TEMPZONE_ROW kind=other for each (site, thread, sector,
 * cell position, zone), other_in_pass those made while a joined pathing pass
 * runs (the only overlap that could race the grant's allocation). A zone made
 * there stays unnamed by the created identities (not the MoveWorker). Pins: FUN_140785db0 (tail after the relocated prologue), the
 * lookup, the creator, the active check, the reference release, both request
 * blocks and RunPass's clear. Every gate tick re-checks the entry and the pins;
 * a failure (also a slot changed during a parked pass) stops the gate with
 * reason 144, shared with the transition rows (both census-zone diagnostics of
 * this class; the status lines name the module). Stop puts a parked slot back
 * and restores the entry by CAS. */
#define TZ_FIND 0x785db0u             /* zone at a position; create=1 makes one at it */
#define TZ_FIND_SIZE 0xa8u
#define TZ_FIND_SHA "b9d725f375b615af29bd9141092116e1347b36a943f8d627800eecccf5638c2d"
#define TZ_FIND_TAIL_SHA "0878324228e636043a243681b32762cd149fbe915d63c018821c9e9a05eb41ed" /* +5.. */
#define TZ_LOOKUP 0x785b90u           /* the sector's zones in the thread's component view */
#define TZ_LOOKUP_SIZE 0x21du
#define TZ_LOOKUP_SHA "ed72468a41619d7e554cc8b4aa3635b4404792b2caa5edc8b9ff719f00d7bc1f"
#define TZ_CREATE 0x7877d0u           /* allocate the zone, post AddTempZoneEvent with TLS time */
#define TZ_CREATE_SIZE 0xdeu
#define TZ_CREATE_SHA "9831a1d486a8d043311abe4710da53db5af53f18eaac64de0a71b581d15c7f0f"
#define TZ_ACTIVE 0x69ed50u           /* zone active in the thread's view */
#define TZ_ACTIVE_SIZE 0x69u
#define TZ_ACTIVE_SHA "f5655dbf71cf8cd3e6934a4d256dcdc2736f4dce4cfa1fe775c14e0783affd90"
#define TZ_RELEASE 0x96940u           /* release a reference held in *ref */
#define TZ_RELEASE_SIZE 0x3bu
#define TZ_RELEASE_SHA "3feb054a27005972334deb6bf6ed8b88fb1f79be0753a317af894752550e82b7"
#define TZ_PATH_BLOCK 0xe91051u       /* Agent_UpdatePathing: slot test, create=1, store, create=0 */
#define TZ_PATH_BLOCK_SIZE 0x1f5u
#define TZ_PATH_BLOCK_SHA "96fe36dee49386666a4cf2f104b1d96eb4d0d9cb1c0d6a6718dd3d30e6e1c2a5"
#define TZ_SWITCH_BLOCK 0xe8905cu     /* the zone switch 0xe88160: the same request */
#define TZ_SWITCH_BLOCK_SIZE 0xdfu
#define TZ_SWITCH_BLOCK_SHA "22655f00a041afc1d2b51ded17cf86c5138e56e3f6c0d33a26cb8c60b4b7a8bb"
#define TZ_CLEAR_BLOCK 0xeae860u      /* RunPass: clear the slot once its zone is active */
#define TZ_CLEAR_BLOCK_SIZE 0x79u
#define TZ_CLEAR_BLOCK_SHA "ea55ccbcb3c5d4dd8220aa48ab07251f29c3646446179d981d39b29fb246aca3"
#define TZ_FLAG 0x3d72b88u
#define TZ_ZONE 0x3d72c40u
#define TZ_LOCK 0x3d72c48u
#define TZ_VIEW 0x38f4eb4u            /* g_ComponentViewCacheIndex: the published view the jobs read */
#define TZ_PATH_CREATE 0xe910a6u      /* return addresses of the four request calls */
#define TZ_PATH_FIND 0xe91241u
#define TZ_SWITCH_CREATE 0xe890a6u
#define TZ_SWITCH_FIND 0xe89136u
#define TZ_THUNK 3024u
#define TZ_CANDIDATES 256u
#define TZ_RING 4096u
#define TZ_DOMAIN 0x58344c545a4f4e45ULL /* "X4LTZONE" */
#define TZ_STOP_REASON 144u
enum {TZ_OFF,TZ_ORDER,TZ_OBSERVE};
enum {TZ_ROW_GRANT=1,TZ_ROW_WAIT,TZ_ROW_CREATE,TZ_ROW_OTHER};
typedef int64_t *(*tz_find_fn)(void *,int64_t *,const void *,unsigned char);
typedef void (*tz_release_fn)(int64_t *);
typedef struct {void *sector;uint64_t runtime;uint32_t position[4],site;} tz_candidate_t;
typedef struct {uint64_t tick,sector,zone,set;uint32_t position[3],site,n,draws;uint8_t kind,thread,klass,active;} tz_row_t;
static int tz_mode,tz_installed,tz_restored,tz_blocked,tz_open;
static volatile LONG tz_active;
static uint64_t tz_original_word,tz_patched_word,tz_saved_zone;
static LONG tz_saved_flag;
static volatile LONG64 tz_noted,tz_rows,tz_failures,tz_finds,tz_misses,tz_creates,tz_unblocked,tz_other_creates,tz_other_made,tz_other_in_pass,tz_outside;
static uint64_t tz_passes,tz_blocks,tz_opens,tz_cleared,tz_granted,tz_stored,tz_found,tz_waits,tz_overflow,tz_requests,tz_max,tz_drew,tz_unmapped,tz_gone;
static tz_candidate_t tz_candidates[TZ_CANDIDATES];
static tz_row_t tz_ring[TZ_RING];
static uintptr_t tz_last_ret; /* the latest active call's return address (ABI tests) */
static tz_find_fn tz_original_override; /* tests only; NULL runs FUN_140785db0 through the thunk */
static tz_release_fn tz_release_override; /* tests only; NULL runs 0x96940 */
static const char *tz_stage="idle";
static int tz_pins(int whole) {
    return (whole?hash_ok(image+TZ_FIND,TZ_FIND_SIZE,TZ_FIND_SHA):hash_ok(image+TZ_FIND+5,TZ_FIND_SIZE-5,TZ_FIND_TAIL_SHA)) &&
        hash_ok(image+TZ_LOOKUP,TZ_LOOKUP_SIZE,TZ_LOOKUP_SHA) && hash_ok(image+TZ_CREATE,TZ_CREATE_SIZE,TZ_CREATE_SHA) &&
        hash_ok(image+TZ_ACTIVE,TZ_ACTIVE_SIZE,TZ_ACTIVE_SHA) && hash_ok(image+TZ_RELEASE,TZ_RELEASE_SIZE,TZ_RELEASE_SHA) &&
        hash_ok(image+TZ_PATH_BLOCK,TZ_PATH_BLOCK_SIZE,TZ_PATH_BLOCK_SHA) && hash_ok(image+TZ_SWITCH_BLOCK,TZ_SWITCH_BLOCK_SIZE,TZ_SWITCH_BLOCK_SHA) &&
        hash_ok(image+TZ_CLEAR_BLOCK,TZ_CLEAR_BLOCK_SIZE,TZ_CLEAR_BLOCK_SHA);
}
static int tz_owned(void) {return !tz_installed || *(volatile uint64_t*)(image+TZ_FIND)==tz_patched_word;}
static tz_find_fn tz_original(void) {return tz_original_override?tz_original_override:(tz_find_fn)(thunk+TZ_THUNK);}
static void tz_release_ref(int64_t *ref) {
    if(tz_release_override) tz_release_override(ref);else ((tz_release_fn)(image+TZ_RELEASE))(ref);
}
/* 0x69ed50 with the published view (as the jobs and RunPass read it), whatever thread asks. */
static int tz_active_view(const unsigned char *zone) {
    uint32_t flags=0,view=0,index=0;
    if(!zone || !read_local(zone+0x1e4,&flags,4) || (flags&0x20000) || !read_local(image+TZ_VIEW,&index,4) || index>3 ||
       !read_local(zone+0x11c+index*0x80,&view,4)) return 0;
    return !(view&0x40000) && (view&0x10000);
}
/* The reference a slot store takes (0xe911a9..0xe911bd). */
static void tz_addref(unsigned char *zone) {
    InterlockedCompareExchange((volatile LONG*)(zone+0x44),2,1);InterlockedIncrement((volatile LONG*)(zone+0x40));
}
static uint32_t tz_site(uintptr_t ret) {
    return ret>=(uintptr_t)image && ret-(uintptr_t)image<0x6fa9000u?(uint32_t)(ret-(uintptr_t)image):0xffffffffu;
}
static void tz_append(const tz_row_t *r) {LONG64 index=InterlockedIncrement64(&tz_rows)-1;tz_ring[(uint64_t)index%TZ_RING]=*r;}
/* Job side: one request of the pass found no zone (lock-free append; the join orders them). */
static void tz_note(void *sector,const void *position,uint32_t site) {
    LONG64 index=InterlockedIncrement64(&tz_noted)-1;
    if(index>=(LONG64)TZ_CANDIDATES) return; /* tz_noted keeps counting: the pass is not granted */
    tz_candidate_t *c=&tz_candidates[index];memset(c,0,sizeof(*c));c->sector=sector;c->site=site;
    if(!sector || !read_local((unsigned char*)sector+8,&c->runtime,8)) c->runtime=0;
    if(!read_local(position,c->position,16)) memset(c->position,0xff,16);
}
static void tz_observe_row(uint8_t kind,void *sector,const void *position,uint32_t site,int64_t zone,unsigned thread) {
    tz_row_t r;memset(&r,0,sizeof(r));r.tick=stats.issued;r.kind=kind;r.site=site;r.thread=(uint8_t)thread;
    if(sector) read_local((unsigned char*)sector+8,&r.sector,8); /* runtime ID; keyed when printed */
    r.klass=WORLD_KEY_NONE+1; /* marks a runtime ID */
    if(!read_local(position,r.position,12)) memset(r.position,0xff,12);
    if(zone) {read_local((unsigned char*)zone+8,&r.zone,8);r.active=(uint8_t)tz_active_view((unsigned char*)zone);}
    tz_append(&r);
}
/* Every call of FUN_140785db0 while armed; ret is the caller's return address. */
static int64_t *tz_find_run(tz_find_fn original,uintptr_t ret,void *sector,int64_t *out,const void *position,unsigned char create) {
    int64_t *result=original(sector,out,position,create);
    if(!InterlockedCompareExchange(&tz_active,0,0)) return result;
    DWORD error=GetLastError();
    uint32_t site=tz_site(ret);int64_t zone=0;unsigned thread=path_thread_class();tz_last_ret=ret;
    if(out) read_local(out,&zone,8);
    int request=site==TZ_PATH_CREATE || site==TZ_PATH_FIND || site==TZ_SWITCH_CREATE || site==TZ_SWITCH_FIND;
    if(!request) {
        if(create && thread!=PATH_THREAD_MOVEWORKER && thread!=PATH_THREAD_MAIN) {
            InterlockedIncrement64(&tz_other_creates);
            if(zone && !tz_active_view((const unsigned char*)zone)) {
                InterlockedIncrement64(&tz_other_made);if(thread==PATH_THREAD_PASS) InterlockedIncrement64(&tz_other_in_pass);
                tz_observe_row(TZ_ROW_OTHER,sector,position,site,zone,thread);
            }
        }
    }
    else if(thread!=PATH_THREAD_PASS) InterlockedIncrement64(&tz_outside);
    else {
        InterlockedIncrement64(create?&tz_creates:&tz_finds);
        if(!create && !zone) InterlockedIncrement64(&tz_misses);
        if(tz_mode==TZ_ORDER) {
            if(create) InterlockedIncrement64(&tz_unblocked); /* a job saw the flag clear in a parked pass */
            else if(!zone) tz_note(sector,position,site);
        } else if(create) tz_observe_row(TZ_ROW_CREATE,sector,position,site,zone,thread);
    }
    SetLastError(error);return result;
}
/* noinline: the return address read is the native caller's (entered by jmp from the thunk). */
static __attribute__((noinline)) int64_t *tz_find(void *sector,int64_t *out,const void *position,unsigned char create) {
    return tz_find_run(tz_original(),(uintptr_t)__builtin_return_address(0),sector,out,position,create);
}
static int tz_swap(uint64_t expected,uint64_t value) {
    DWORD old,ignored;if(!VirtualProtect(image+TZ_FIND,8,PAGE_EXECUTE_READWRITE,&old)) return 0;
    uint64_t actual=(uint64_t)InterlockedCompareExchange64((volatile LONG64*)(image+TZ_FIND),(LONG64)value,(LONG64)expected);
    VirtualProtect(image+TZ_FIND,8,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+TZ_FIND,8);return actual==expected;
}
/* Before the first admitted frame, on the MoveWorker; needs the pathing join (options&1) and the world identities. */
static int tz_arm(int mode,unsigned options) {
    if(tz_installed) return stats.active && mode==tz_mode; /* Identical rerun after a loading-UI reload. */
    if((mode!=TZ_ORDER && mode!=TZ_OBSERVE) || !stats.active || (boot_mode && boot_phase) || stats.issued ||
       GetCurrentThreadId()!=ui_thread || !(options&1)) return 0;
    if(!world_identity_installed) {tz_stage="needs";return 0;}
    if(!tz_pins(1)) {tz_stage="pins";return 0;}
    intptr_t distance=thunk?(intptr_t)(thunk+TZ_THUNK+32)-(intptr_t)(image+TZ_FIND+5):0;
    if(!thunk || distance<INT32_MIN || distance>INT32_MAX || !thunk_slot_ready(TZ_THUNK)) {tz_stage="thunk";return 0;}
    tz_noted=tz_rows=tz_failures=tz_finds=tz_misses=tz_creates=tz_unblocked=tz_other_creates=tz_other_made=tz_other_in_pass=tz_outside=0;
    tz_passes=tz_blocks=tz_opens=tz_cleared=tz_granted=tz_stored=tz_found=tz_waits=tz_overflow=tz_requests=tz_max=tz_drew=tz_unmapped=tz_gone=0;
    tz_blocked=tz_open=0;
    memcpy(&tz_original_word,image+TZ_FIND,8);tz_patched_word=tz_original_word;
    unsigned char *patch=(unsigned char*)&tz_patched_word;patch[0]=0xe9;int32_t rel=(int32_t)distance;memcpy(patch+1,&rel,4);
    tz_mode=mode;InterlockedExchange(&tz_active,1);
    if(!tz_swap(tz_original_word,tz_patched_word)) {InterlockedExchange(&tz_active,0);tz_stage="entry";return 0;}
    tz_installed=1;tz_restored=0;tz_stage="armed";return 1;
}
/* MoveWorker, the pathing thread idle, right before the pass is released. */
static int tz_before_release(void) {
    if(!tz_installed || !InterlockedCompareExchange(&tz_active,0,0)) return 1;
    InterlockedExchange64(&tz_noted,0);++tz_passes;
    if(tz_mode!=TZ_ORDER) return 1;
    if(tz_blocked) {tz_stage="unjoined";return 0;}
    CRITICAL_SECTION *lock=(CRITICAL_SECTION*)(image+TZ_LOCK);volatile LONG *flag=(volatile LONG*)(image+TZ_FLAG);
    volatile int64_t *zone=(volatile int64_t*)(image+TZ_ZONE);
    EnterCriticalSection(lock);
    LONG f=*flag;
    /* RunPass's clear (0xeae860), here so that RunPass finds the slot parked. */
    if(f && *zone && tz_active_view((const unsigned char*)*zone)) {
        int64_t held=*zone;*zone=0;tz_release_ref(&held);InterlockedExchange(flag,0);f=0;++tz_cleared;
    }
    tz_open=!f;if(tz_open) ++tz_opens;
    tz_saved_flag=f;tz_saved_zone=(uint64_t)*zone;*zone=0;InterlockedExchange(flag,1);tz_blocked=1;++tz_blocks;
    LeaveCriticalSection(lock);
    return 1;
}
/* Puts a parked slot back (after the join, or at a stop); 0 when a job or other code changed it meanwhile. */
static int tz_unpark(void) {
    if(!tz_blocked) return 1;
    CRITICAL_SECTION *lock=(CRITICAL_SECTION*)(image+TZ_LOCK);volatile LONG *flag=(volatile LONG*)(image+TZ_FLAG);
    volatile int64_t *zone=(volatile int64_t*)(image+TZ_ZONE);
    EnterCriticalSection(lock);
    int intact=*flag==1 && !*zone;
    if(intact) {*zone=(int64_t)tz_saved_zone;InterlockedExchange(flag,tz_saved_flag);}
    tz_blocked=0;LeaveCriticalSection(lock);
    return intact;
}
static uint64_t tz_candidate_key(const tz_candidate_t *c,unsigned *klass) {
    uint64_t key=c->runtime?world_identity_key(c->runtime,klass):0;
    if(!key) {*klass=WORLD_KEY_NONE;++tz_unmapped;key=c->runtime;} /* sectors come from the save: the runtime ID is the same */
    return key;
}
static int tz_before(uint64_t ka,const tz_candidate_t *a,uint64_t kb,const tz_candidate_t *b) {
    if(ka!=kb) return ka<kb;
    for(unsigned i=0;i<3;i++) if(a->position[i]!=b->position[i]) return a->position[i]<b->position[i];
    return a->site<b->site;
}
typedef struct {unsigned char *tls;uint64_t time,word,seed,exit;} tz_context_t;
/* The job context for the call: TLS game time 0, the keyed word; leave restores both. */
static void tz_context_enter(tz_context_t *s,unsigned char *tls,uint64_t seed) {
    const uint64_t zero=0;s->tls=tls;s->seed=s->exit=seed;s->time=s->word=0;
    if(tls) {memcpy(&s->time,tls+0x300,8);memcpy(&s->word,tls+0x318,8);memcpy(tls+0x300,&zero,8);memcpy(tls+0x318,&seed,8);}
}
static void tz_context_leave(tz_context_t *s) {
    if(s->tls) {memcpy(&s->exit,s->tls+0x318,8);memcpy(s->tls+0x300,&s->time,8);memcpy(s->tls+0x318,&s->word,8);}
}
/* The winning job's create=1 request, on the MoveWorker under the slot lock (0xe9108a..0xe911d7);
 * tls is the MoveWorker's TLS block (game time +0x300, word +0x318). */
static void tz_grant(unsigned char *tls,const tz_candidate_t *c,uint64_t key,unsigned klass,tz_row_t *r) {
    tz_context_t context;int64_t made=0;
    tz_context_enter(&context,tls,lockstep_seed(TZ_DOMAIN,stats.issued,key,((uint64_t)c->position[0]<<32)|c->position[1],c->position[2],klass));
    __try {tz_original()(c->sector,&made,c->position,1);}
    __finally {tz_context_leave(&context);}
    r->draws=actor_all_steps(context.seed,context.exit);if(r->draws) ++tz_drew;
    ++tz_granted;
    if(!made) return;
    read_local((unsigned char*)made+8,&r->zone,8);r->active=(uint8_t)tz_active_view((unsigned char*)made);
    if(!r->active) {
        volatile int64_t *zone=(volatile int64_t*)(image+TZ_ZONE);int64_t old=*zone;
        tz_addref((unsigned char*)made);*zone=made;InterlockedExchange((volatile LONG*)(image+TZ_FLAG),1);++tz_stored;
        if(old) tz_release_ref(&old);
    } else ++tz_found;
    tz_release_ref(&made);
}
/* MoveWorker after the join and the drained late splices: the slot back, then at most one grant. */
static int tz_after_join_with(unsigned char *tls) {
    if(!tz_blocked) return 1;
    if(!tz_unpark()) {tz_stage="slot_changed";return 0;}
    LONG64 noted=InterlockedCompareExchange64(&tz_noted,0,0);
    if(noted<=0) return 1;
    unsigned n=noted>(LONG64)TZ_CANDIDATES?TZ_CANDIDATES:(unsigned)noted,best=UINT_MAX,best_klass=WORLD_KEY_NONE;
    uint64_t best_key=0,set=0;
    for(unsigned i=0;i<n;i++) {
        const tz_candidate_t *c=&tz_candidates[i];unsigned klass;uint64_t key=tz_candidate_key(c,&klass);
        set+=lockstep_mix(lockstep_seed(TZ_DOMAIN,key,((uint64_t)c->position[0]<<32)|c->position[1],c->position[2],c->site,0));
        if(best==UINT_MAX || tz_before(key,c,best_key,&tz_candidates[best])) {best=i;best_key=key;best_klass=klass;}
    }
    tz_requests+=(uint64_t)noted;if((uint64_t)noted>tz_max) tz_max=(uint64_t)noted;
    const tz_candidate_t *c=&tz_candidates[best];
    tz_row_t r;memset(&r,0,sizeof(r));r.tick=stats.issued;r.sector=best_key;r.klass=(uint8_t)best_klass;r.site=c->site;
    r.thread=PATH_THREAD_MOVEWORKER;r.n=(uint32_t)(noted>UINT32_MAX?UINT32_MAX:noted);r.set=set;memcpy(r.position,c->position,12);
    uint64_t runtime=0;
    if(!tz_open || noted>(LONG64)TZ_CANDIDATES || !c->sector || !read_local((unsigned char*)c->sector+8,&runtime,8) || runtime!=c->runtime) {
        if(noted>(LONG64)TZ_CANDIDATES) ++tz_overflow;else if(tz_open) ++tz_gone;
        ++tz_waits;r.kind=TZ_ROW_WAIT;tz_append(&r);return 1;
    }
    CRITICAL_SECTION *lock=(CRITICAL_SECTION*)(image+TZ_LOCK);int done=0;
    EnterCriticalSection(lock);
    __try {
        if(!*(volatile LONG*)(image+TZ_FLAG) && !*(volatile int64_t*)(image+TZ_ZONE)) {tz_grant(tls,c,best_key,best_klass,&r);r.kind=TZ_ROW_GRANT;}
        else {++tz_waits;r.kind=TZ_ROW_WAIT;}
        done=1;
    }
    __finally {LeaveCriticalSection(lock);if(!done) {tz_stage="grant_exception";InterlockedIncrement64(&tz_failures);}}
    tz_append(&r);return 1;
}
static int tz_after_join(void) {
    void **slots=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    return tz_after_join_with(slots?(unsigned char*)slots[0]:NULL);
}
/* Main-thread tick check; any failure stops the gate with reason 144. */
static int tz_check(void) {
    if(InterlockedCompareExchange64(&tz_failures,0,0)) return 0;
    const char *why=!tz_owned()?"entry_changed":!tz_pins(0)?"block_changed":NULL;
    if(why) {tz_stage=why;InterlockedIncrement64(&tz_failures);return 0;}
    return 1;
}
static void tz_restore(void) {
    InterlockedExchange(&tz_active,0);
    tz_unpark(); /* a slot changed meanwhile stays as it is */
    if(!tz_installed) return;
    if(!tz_swap(tz_patched_word,tz_original_word)) return; /* a foreign value stays */
    tz_installed=0;tz_restored=1;
}
static const char *tz_kind_name(uint8_t kind) {
    return kind==TZ_ROW_GRANT?"grant":kind==TZ_ROW_WAIT?"wait":kind==TZ_ROW_CREATE?"create":kind==TZ_ROW_OTHER?"other":"?";
}
static int tz_row(uint64_t index,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&tz_rows,0,0);int n;
    if((LONG64)index>=count) return 0;
    if(count>(LONG64)TZ_RING && index<(uint64_t)(count-(LONG64)TZ_RING))
        return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    const tz_row_t *r=&tz_ring[index%TZ_RING];uint64_t sector=r->sector;unsigned klass=r->klass;
    if(klass==WORLD_KEY_NONE+1) { /* create/other rows: a runtime ID, keyed now */uint64_t key=sector?world_identity_key(sector,&klass):0;if(key) sector=key;else klass=WORLD_KEY_NONE;}
    n=snprintf(out,cap,"index=%llu tick=%llu kind=%s site=%x thread=%s sector=%c:%016llx pos=%08x,%08x,%08x zone=%016llx active=%u n=%u set=%016llx draws=%u",
        (unsigned long long)index,(unsigned long long)r->tick,tz_kind_name(r->kind),r->site,path_thread_name(r->thread),
        klass==WORLD_KEY_CREATED?'c':klass==WORLD_KEY_SAVED?'s':'-',(unsigned long long)sector,r->position[0],r->position[1],r->position[2],
        (unsigned long long)r->zone,r->active,r->n,(unsigned long long)r->set,r->draws);
    return n>0 && (size_t)n<cap?n:0;
}
static int tz_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"mode=%s installed=%d passes=%llu blocks=%llu open=%llu cleared=%llu granted=%llu stored=%llu found=%llu "
        "waits=%llu overflow=%llu gone=%llu requests=%llu max=%llu finds=%lld misses=%lld creates=%lld unblocked=%lld other_creates=%lld "
        "other_made=%lld other_in_pass=%lld outside=%lld drew=%llu unmapped=%llu rows=%lld restored=%d stage=%s failures=%lld",
        tz_mode==TZ_ORDER?"order":tz_mode==TZ_OBSERVE?"observe":"off",tz_installed,(unsigned long long)tz_passes,(unsigned long long)tz_blocks,
        (unsigned long long)tz_opens,(unsigned long long)tz_cleared,(unsigned long long)tz_granted,(unsigned long long)tz_stored,
        (unsigned long long)tz_found,(unsigned long long)tz_waits,(unsigned long long)tz_overflow,(unsigned long long)tz_gone,
        (unsigned long long)tz_requests,(unsigned long long)tz_max,(long long)tz_finds,(long long)tz_misses,(long long)tz_creates,
        (long long)tz_unblocked,(long long)tz_other_creates,(long long)tz_other_made,(long long)tz_other_in_pass,(long long)tz_outside,(unsigned long long)tz_drew,(unsigned long long)tz_unmapped,
        (long long)tz_rows,tz_restored,tz_stage,(long long)tz_failures);
    return n>0 && (size_t)n<capacity?n:0;
}
