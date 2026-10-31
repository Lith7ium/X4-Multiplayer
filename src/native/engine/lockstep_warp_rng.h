/* Determinism fix, DEFAULT OFF: warps on a keyed word (native_gate_warp_rng =
 * 'split' or true; 'observe' keeps the native word and only counts and logs;
 * needs native_gate_world_identities).
 * U::WarpEvent::Dispatch 0x9aa290 (WarpEvent vtable 0x2baa8c0 slot 2, entry
 * 0x2baa8d0, the image's only reference) unpacks the event (+0x18 target
 * space ID, +0x20 transform, +0x28 warp record, +0x30/+0x31 flags) and calls
 * the listener's slot 23, U::Object::OnWarpEvent 0x6cf760 (ships through
 * 0x7ce400), which re-parents the object into the space (Space::AddChild
 * 0x83a8e0). With a warp record whose first byte is set it first asks the
 * safe-position search 0x835b80 for the entry point (radius record+4, else the
 * object's size * 1.1; other objects of the space at the TLS game time): the
 * requested position is pushed out along record+0x30, and without one along a
 * random direction from two Random::FloatState draws on TLS+0x318 (0x8370bc
 * angle 2*pi, 0x8370d0 height 2 - 1), taken whether or not a push is needed.
 * Event delivery runs on the frame's thread word, which other consumers of the
 * same pass leave per client. long-L19: add39 entered a space at 1780 on both
 * clients 13.1/-1.42/-6.71 apart (constant afterwards) with every pathing,
 * flight-parameter and agent field equal through 1779; long-L18: 90dc6/932c6
 * entered at 1782/1783 in swapped order (the no-parent group of the 1800
 * roots).
 * The entry is pointed at a wrapper (data only, no thunk; a CAS from the
 * original). In admitted frames on the MoveWorker, for a listener with a
 * portable key, 'split' runs the dispatch on a word seeded by (tick, the key,
 * the object's warp ordinal in the tick) and restores the caller's word
 * afterwards, also on unwind; the frame's word no longer moves for a warp.
 * Everything else passes through natively, counted (unmapped, foreign, not
 * admitted). One WARP_RNG_ROW per admitted warp: tick, object and target space
 * keys, the record's flag and whether it gives a direction, the native word
 * at entry (per client when the word is the input), the word used, the
 * draws taken, the event's time (when it was posted) and the delivery time. Pins: Dispatch, OnWarpEvent's safe-position block and the
 * search's direction block. Every gate tick re-checks entry and pins (stop
 * reason 143); stop restores by CAS and leaves a foreign value in place.
 * Poster attribution (long-L22): a warp is delivered a pass or more after it
 * was posted, by the MoveWorker, so the dispatch cannot name its poster. The
 * out-of-line constructor 0x9a9d00 (Agent_UpdatePathing's four warps 0xe90baf,
 * 0xe9116b, 0xe912ee, 0xe914fd, and 0x36ad22, 0x5cf1cb) is entered through
 * thunk 2976 (prologue relocated, whole body pinned at arming, tail every
 * tick) and notes event -> (return address, thread class, tick) in a small
 * table that the dispatch takes the entry from. Rows gain site (constructor
 * return RVA; 0 for the inline constructions, e.g. gates and transitions),
 * thread (moveworker, main, pass = any other thread while a joined pathing pass
 * runs, other; none when not seen), posted (the tick) and zone (the target's
 * runtime ID, also when it has no portable key). */
#define WR_ENTRY 0x2baa8d0u    /* U::WarpEvent vtable 0x2baa8c0 slot 2 */
#define WR_DISPATCH 0x9aa290u  /* U::WarpEvent::Dispatch */
#define WR_DISPATCH_SIZE 0xc8u
#define WR_DISPATCH_SHA "521c91033742570080476acc3650f7b916bcc7726216f54d0fd152a52abb364a"
#define WR_PLACE 0x6cf960u     /* OnWarpEvent: record check, radius, safe-position call (to 0x6cfa64) */
#define WR_PLACE_SIZE 0x104u
#define WR_PLACE_SHA "dacde89f262dbd7a6bfd7152231a26f9c59b254cd4221b084cdb275886d959c2"
#define WR_DIRECTION 0x836ff3u /* safe-position search: the push direction, given or drawn (to 0x837131) */
#define WR_DIRECTION_SIZE 0x13eu
#define WR_DIRECTION_SHA "72acb12698119311549d768cef8ae340f9f38623b930c4d70e3c2c537d74aaff"
#define WR_CTOR 0x9a9d00u        /* U::WarpEvent constructor (out of line) */
#define WR_CTOR_SIZE 0x17au
#define WR_CTOR_SHA "795761efaf244531268cb102685a18a944ed6b0631bc302f9cd6a3de38e49473"
#define WR_CTOR_TAIL_SHA "3dddcfb71f432dcefb88382a28c6b5a96ecba740aa56af067279d7c0d4ce2805" /* +5.. */
#define WR_CTOR_THUNK 2976u
#define WR_POSTERS 4096u
#define WR_DOMAIN 0x58344c5741525052ULL /* "X4LWARPR" */
#define WR_ORDINALS 512u
#define WR_RING 4096u
#define WR_STOP_REASON 143u
enum {WR_OFF,WR_SPLIT,WR_OBSERVE};
typedef void (*wr_fn)(void *,void *,void *,double);
typedef struct {uint64_t tick,key,space,natural,word;uint32_t ordinal,draws;uint8_t klass,flag,direction,keyed;uint64_t time,now,zone,posted;uint32_t site;uint8_t thread;} wr_row_t;
typedef struct {uint64_t key;uint32_t count;} wr_slot_t;
typedef void *(*wr_ctor_fn)(void *,void *,void *,void *,unsigned char,unsigned char);
typedef struct {uintptr_t event;uint64_t tick;uint32_t site;uint8_t thread;} wr_poster_t;
static int wr_mode,wr_installed,wr_restored,wr_ctor_installed;
static volatile LONG wr_active;
static volatile LONG64 wr_calls,wr_failures,wr_rows,wr_constructed;
static uint64_t wr_attributed,wr_ctor_original_word,wr_ctor_patched_word;
static wr_poster_t wr_posters[WR_POSTERS];
static SRWLOCK wr_poster_lock=SRWLOCK_INIT;
static wr_ctor_fn wr_ctor_override; /* tests only; NULL runs the constructor through the thunk */
static uint64_t wr_admitted,wr_keyed,wr_unmapped,wr_foreign,wr_outside,wr_full,wr_drew,wr_draws,wr_unknown;
static uint64_t wr_tick=UINT64_MAX;
static wr_slot_t wr_slots[WR_ORDINALS];
static wr_row_t wr_ring[WR_RING];
static wr_fn wr_original_override; /* tests only; NULL runs WarpEvent::Dispatch */
static const char *wr_stage="idle";
static int wr_pins(void) {
    return hash_ok(image+WR_DISPATCH,WR_DISPATCH_SIZE,WR_DISPATCH_SHA) && hash_ok(image+WR_PLACE,WR_PLACE_SIZE,WR_PLACE_SHA) &&
        hash_ok(image+WR_DIRECTION,WR_DIRECTION_SIZE,WR_DIRECTION_SHA) && hash_ok(image+WR_CTOR+5,WR_CTOR_SIZE-5,WR_CTOR_TAIL_SHA);
}
static void wr_dispatch(void *event,void *listener,void *arg,double time);
static int wr_owned(void) {
    return (!wr_installed || *(void*volatile*)(image+WR_ENTRY)==(void*)wr_dispatch) &&
        (!wr_ctor_installed || *(volatile uint64_t*)(image+WR_CTOR)==wr_ctor_patched_word);
}
static unsigned wr_poster_slot(const void *event) {return (unsigned)(lockstep_mix((uint64_t)(uintptr_t)event)&(WR_POSTERS-1));}
/* Any thread: the constructor ran; ret is its caller's return address. */
static void *wr_construct_run(wr_ctor_fn original,uintptr_t ret,void *event,void *space,void *transform,void *record,unsigned char a,unsigned char b) {
    void *result=original(event,space,transform,record,a,b);
    if(event && InterlockedCompareExchange(&wr_active,0,0)) {
        wr_poster_t p={(uintptr_t)event,stats.issued,
            ret>=(uintptr_t)image && ret-(uintptr_t)image<0x6fa9000u?(uint32_t)(ret-(uintptr_t)image):0xffffffffu,(uint8_t)path_thread_class()};
        unsigned slot=wr_poster_slot(event);
        AcquireSRWLockExclusive(&wr_poster_lock);wr_posters[slot]=p;ReleaseSRWLockExclusive(&wr_poster_lock);
        InterlockedIncrement64(&wr_constructed);
    }
    return result;
}
/* noinline: the return address read is the native caller's (entered by jmp from the thunk). */
static __attribute__((noinline)) void *wr_construct(void *event,void *space,void *transform,void *record,unsigned char a,unsigned char b) {
    return wr_construct_run(wr_ctor_override?wr_ctor_override:(wr_ctor_fn)(thunk+WR_CTOR_THUNK),(uintptr_t)__builtin_return_address(0),
        event,space,transform,record,a,b);
}
/* The dispatch takes its event's note (an inline-constructed event finds none or another address's). */
static wr_poster_t wr_take_poster(const void *event) {
    wr_poster_t p;memset(&p,0,sizeof(p));if(!event) return p;
    unsigned slot=wr_poster_slot(event);
    AcquireSRWLockExclusive(&wr_poster_lock);
    if(wr_posters[slot].event==(uintptr_t)event) {p=wr_posters[slot];memset(&wr_posters[slot],0,sizeof(p));}
    ReleaseSRWLockExclusive(&wr_poster_lock);
    return p;
}
/* The object's warps so far in this tick (1 for the first), 0 when the table is full. */
static uint32_t wr_ordinal(uint64_t key) {
    if(wr_tick!=stats.issued) {wr_tick=stats.issued;memset(wr_slots,0,sizeof(wr_slots));}
    unsigned h=(unsigned)(lockstep_mix(key)&(WR_ORDINALS-1));
    for(unsigned n=0;n<WR_ORDINALS/2;n++,h=(h+1)&(WR_ORDINALS-1)) {
        if(wr_slots[h].key==key) return ++wr_slots[h].count;
        if(!wr_slots[h].key) {wr_slots[h].key=key;wr_slots[h].count=1;return 1;}
    }
    return 0;
}
/* One dispatch; rng is the calling thread's word (NULL when unreadable), tls its TLS block (game time +0x300). */
static void wr_run(wr_fn original,uint64_t *rng,const unsigned char *tls,void *event,void *listener,void *arg,double time) {
    InterlockedIncrement64(&wr_calls);
    wr_poster_t poster=wr_take_poster(event);
    if(!rng || !InterlockedCompareExchange(&wr_active,0,0) || !stats.active || boot_phase!=3 || !stats.issued) {
        ++wr_outside;original(event,listener,arg,time);return;
    }
    if(GetCurrentThreadId()!=ui_thread) {++wr_foreign;original(event,listener,arg,time);return;}
    if(poster.event) ++wr_attributed;
    ++wr_admitted;
    uint64_t runtime=0,key=0,space=0,target=0;unsigned klass=WORLD_KEY_NONE,space_klass;unsigned char *record=NULL,head[1]={0};void *direction=NULL;
    if(listener && read_local((unsigned char*)listener+8,&runtime,8) && runtime) key=world_identity_key(runtime,&klass);
    if(event && read_local((unsigned char*)event+0x18,&target,8) && target) space=world_identity_key(target,&space_klass);
    if(event && read_local((unsigned char*)event+0x28,&record,8) && record) {read_local(record,head,1);read_local(record+0x30,&direction,8);}
    uint32_t ordinal=key?wr_ordinal(key):0;
    if(!key) ++wr_unmapped;else if(!ordinal) ++wr_full;
    int split=wr_mode==WR_SPLIT && key && ordinal;
    uint64_t natural=*rng,seed=natural;
    if(split) {seed=lockstep_seed(WR_DOMAIN,stats.issued,key,ordinal,0,klass);*rng=seed;++wr_keyed;}
    __try {original(event,listener,arg,time);}
    __finally {
        uint64_t exit=*rng;if(split) *rng=natural;
        uint32_t draws=actor_all_steps(seed,exit);
        if(draws==UINT32_MAX) ++wr_unknown;else if(draws) {++wr_drew;wr_draws+=draws;}
        wr_row_t r={stats.issued,key,space,natural,seed,ordinal,draws,(uint8_t)klass,head[0],(uint8_t)(direction!=NULL),(uint8_t)split,0,0,
            target,poster.tick,poster.site,poster.thread};
        memcpy(&r.time,&time,8);if(tls) read_local(tls+0x300,&r.now,8);
        LONG64 index=InterlockedIncrement64(&wr_rows)-1;wr_ring[(uint64_t)index%WR_RING]=r;
        if(AbnormalTermination()) InterlockedIncrement64(&wr_failures);
    }
}
/* noinline keeps the TEB read out of the caller. */
static __attribute__((noinline)) void wr_dispatch(void *event,void *listener,void *arg,double time) {
    void **slots=*(void***)((unsigned char*)NtCurrentTeb()+0x58);unsigned char *tls=slots?(unsigned char*)slots[0]:NULL;
    wr_run(wr_original_override?wr_original_override:(wr_fn)(image+WR_DISPATCH),tls?(uint64_t*)(tls+0x318):NULL,tls,event,listener,arg,time);
}
static int wr_ctor_swap(uint64_t expected,uint64_t value) {
    DWORD old,ignored;if(!VirtualProtect(image+WR_CTOR,8,PAGE_EXECUTE_READWRITE,&old)) return 0;
    uint64_t actual=(uint64_t)InterlockedCompareExchange64((volatile LONG64*)(image+WR_CTOR),(LONG64)value,(LONG64)expected);
    VirtualProtect(image+WR_CTOR,8,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+WR_CTOR,8);return actual==expected;
}
/* The constructor's prologue to thunk 2976 (whole body pinned first). */
static int wr_ctor_install(void) {
    if(!hash_ok(image+WR_CTOR,WR_CTOR_SIZE,WR_CTOR_SHA)) return 0;
    intptr_t distance=thunk?(intptr_t)(thunk+WR_CTOR_THUNK+32)-(intptr_t)(image+WR_CTOR+5):0;
    if(!thunk || distance<INT32_MIN || distance>INT32_MAX || !thunk_slot_ready(WR_CTOR_THUNK)) return 0;
    memcpy(&wr_ctor_original_word,image+WR_CTOR,8);wr_ctor_patched_word=wr_ctor_original_word;
    unsigned char *patch=(unsigned char*)&wr_ctor_patched_word;patch[0]=0xe9;int32_t rel=(int32_t)distance;memcpy(patch+1,&rel,4);
    if(!wr_ctor_swap(wr_ctor_original_word,wr_ctor_patched_word)) return 0;
    wr_ctor_installed=1;return 1;
}
static void wr_ctor_restore(void) {
    if(wr_ctor_installed && wr_ctor_swap(wr_ctor_patched_word,wr_ctor_original_word)) wr_ctor_installed=0; /* a foreign value stays */
}
/* Before the first admitted frame, on the MoveWorker; needs the world identities. */
static int wr_arm(int mode) {
    if(wr_installed) return stats.active && mode==wr_mode; /* Identical rerun after a loading-UI reload. */
    if((mode!=WR_SPLIT && mode!=WR_OBSERVE) || !stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(!world_identity_installed) {wr_stage="needs";return 0;}
    if(!wr_pins()) {wr_stage="pins";return 0;}
    wr_calls=wr_failures=wr_rows=wr_constructed=0;wr_admitted=wr_keyed=wr_unmapped=wr_foreign=wr_outside=wr_full=wr_drew=wr_draws=wr_unknown=0;
    wr_attributed=0;wr_tick=UINT64_MAX;memset(wr_slots,0,sizeof(wr_slots));memset(wr_posters,0,sizeof(wr_posters));
    if(!wr_ctor_install()) {wr_stage="ctor";return 0;}
    wr_mode=mode;InterlockedExchange(&wr_active,1);
    if(!script_rng_slot_swap(WR_ENTRY,image+WR_DISPATCH,(void*)wr_dispatch)) {
        InterlockedExchange(&wr_active,0);wr_ctor_restore();wr_stage="entry";return 0;
    }
    wr_installed=1;wr_restored=0;wr_stage="armed";return 1;
}
/* Main-thread tick check; any failure stops the gate with reason 143. */
static int wr_check(void) {
    if(InterlockedCompareExchange64(&wr_failures,0,0)) {wr_stage="exception";return 0;}
    const char *why=!wr_owned()?"entry_changed":!wr_pins()?"block_changed":NULL;
    if(why) {wr_stage=why;InterlockedIncrement64(&wr_failures);return 0;}
    return 1;
}
static void wr_restore(void) {
    InterlockedExchange(&wr_active,0);
    wr_ctor_restore();
    if(!wr_installed) return;
    if(!script_rng_slot_swap(WR_ENTRY,(void*)wr_dispatch,image+WR_DISPATCH)) return; /* a foreign value stays */
    wr_installed=0;wr_restored=!wr_ctor_installed;
}
static int wr_row(uint64_t index,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&wr_rows,0,0);int n;
    if((LONG64)index>=count) return 0;
    if(count>(LONG64)WR_RING && index<(uint64_t)(count-(LONG64)WR_RING))
        return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    const wr_row_t *r=&wr_ring[index%WR_RING];
    n=snprintf(out,cap,"index=%llu tick=%llu key=%c:%016llx ordinal=%u space=%016llx flag=%u direction=%u keyed=%u natural=%016llx word=%016llx draws=%u "
        "time=%016llx now=%016llx site=%x thread=%s posted=%llu zone=%016llx",
        (unsigned long long)index,(unsigned long long)r->tick,r->klass==WORLD_KEY_CREATED?'c':r->klass==WORLD_KEY_SAVED?'s':'-',
        (unsigned long long)r->key,r->ordinal,(unsigned long long)r->space,r->flag,r->direction,r->keyed,(unsigned long long)r->natural,
        (unsigned long long)r->word,r->draws,(unsigned long long)r->time,(unsigned long long)r->now,r->site,path_thread_name(r->thread),
        (unsigned long long)r->posted,(unsigned long long)r->zone);
    return n>0 && (size_t)n<cap?n:0;
}
static int wr_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"mode=%s installed=%d calls=%lld admitted=%llu keyed=%llu unmapped=%llu foreign=%llu outside=%llu full=%llu "
        "drew=%llu draws=%llu unknown=%llu constructed=%lld attributed=%llu rows=%lld restored=%d stage=%s failures=%lld",
        wr_mode==WR_SPLIT?"split":wr_mode==WR_OBSERVE?"observe":"off",wr_installed,(long long)wr_calls,(unsigned long long)wr_admitted,
        (unsigned long long)wr_keyed,(unsigned long long)wr_unmapped,(unsigned long long)wr_foreign,(unsigned long long)wr_outside,
        (unsigned long long)wr_full,(unsigned long long)wr_drew,(unsigned long long)wr_draws,(unsigned long long)wr_unknown,
        (long long)wr_constructed,(unsigned long long)wr_attributed,(long long)wr_rows,wr_restored,wr_stage,(long long)wr_failures);
    return n>0 && (size_t)n<capacity?n:0;
}
