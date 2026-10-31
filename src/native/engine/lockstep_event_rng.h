/* Determinism fix, DEFAULT OFF: every due event is delivered inside its own
 * keyed RNG scope (native_gate_event_rng = 'observe' | 'key'; needs
 * native_gate_event_order, whose delivery redirect it rides, and the world
 * identities).
 *
 * full-G3 and play15-soak, the 3600 transporter class: the roots (a1df9 and
 * acf94 at 3600; a1720, 962c1, 75278 at 3900) are all traders, and each one
 * parts at a trade search before its motion does. full-G3 client 2 against
 * client 1 (= full-G4/G5): a1df9's pilot a1dfe runs order.trade.middleman
 * under the same key at 3021 with 4 draws instead of 2 (shuffle_list of the
 * find_sell_offer result: N-1 draws for N offers), searches again at 3153
 * instead of 3069 and leaves its station at 3337 instead of 3500 (start_travel
 * at 3567 on client 1); acf94's pilot acf99 runs trade.find.free at 3305 with
 * 98 draws instead of 97 and docks at 3326 instead of 3362; the 3900 roots'
 * pilots follow with trade.find.free at 3630 (7527d), 3680 (962c6) and 3767
 * (a1725). No find_* candidate list (FIND_ORDER) differs before 3326: the
 * trade offers themselves do. The save's 2727 UpdateTradeOffersEvents
 * (U::UpdateTradeOffersEvent 0x2ac17b8, type 0x215) are due at 56..68 s,
 * ticks 2935..3655 (game time 7.083 + tick/60): the first wave of offer
 * updates lands exactly in the window where the class starts.
 * Their handler path draws from the delivering thread's word TLS slot0+0x318:
 * Container::UpdateTradeOffers 0x4079e0 -> economy update 0x431f90 (its next
 * run +0xa38 and +0xa28: inline PCG at 0x432b70, 0x14ddf70, Random::FloatRange
 * 0x14ddc90), the lazy production update 0x41a910 -> 0x761720 (production
 * timing: FloatRange on TLS+0x318) and the reschedule 0x407160 (inline PCG on
 * TLS+0x318: now + max(d, 5) - 4 + r % 10). Due events run on the MoveWorker
 * in GameClient::Move (VBI slot 3) after the per-frame seed of that word, so
 * those draws agree only while every earlier consumer of the frame drew
 * identically. full-G3's frame word parts at 1078 and in 37 ticks before 3600
 * (2927, 2932, 2933, 2937, 2997, 2998 inside the first wave); in full-G4, G5,
 * G7a and G7b it agrees until 3601. Same mechanism as the engine refresh
 * (lockstep_engine_rng.h), for every listener at once.
 *
 * 'key': the native DeliverEvent call runs with TLS+0x318 =
 *   lockstep_seed(EVENT_RNG_DOMAIN, tick, source key, event vtable RVA, due bits, class)
 * where the source key is the source's saved or created portable ID; a source
 * without one (runtime only, class none) uses its own vtable RVA as the key.
 * Both are equal on two clients that deliver the same event at the same tick
 * (event_order fixes the delivery order; the seed does not depend on it).
 * The caller's word is put back afterwards, also when an exception unwinds, so
 * a handler's draws neither depend on nor shift any other consumer of the
 * frame. Nested scopes (script RNG, actor scopes, a nested delivery pass) open
 * and close inside as before.
 * 'observe': the word is left native; the scope only counts the handler's
 * draws on it (rows carry the entry word, so two clients' rows show where the
 * natural word already differed).
 * Admitted frames only: boot and loading deliveries stay native (counted).
 * Rows (EVENT_RNG_ROW, deliveries that drew): tick, class, key, source and
 * event vtable RVAs, due time bits, entry word, draws (steps of the engine PCG
 * from the scope's start word to its exit word; -1 above the cap). Status:
 * cumulative deliveries, keyed, local, drawn, draws, unknown, trade-offer
 * deliveries and their draws, foreign, boot, and order-free keyset/drawset
 * sums that two clients compare tick by tick. With a row window (ticks
 * [from, to], at most 600 ticks) every delivery in it is a row, draws=0 too:
 * deliveries one client makes and the other does not, whatever their draws.
 * Fail closed (stop reason 151): a delivery in an admitted frame off the
 * MoveWorker or without a TLS word, an unwound scope, or the event order
 * redirect gone or failed while armed. Pins: Random::FloatRange 0x14ddc90 and
 * the reschedule 0x407160, which both step the word at TLS slot0+0x318 with
 * the PCG the step count assumes. Nothing in the image is patched. */
#define EVENT_RNG_DOMAIN 0x58344c45564e5452ULL /* "X4LEVNTR" */
#define EVENT_RNG_STOP_REASON 151u /* 150 is the gravidar wide module's */
#define EVENT_RNG_STEP_CAP 4096u
#define EVENT_RNG_RING 16384u
#define EVENT_RNG_TRADE_EVENT 0x2ac17b8u /* U::UpdateTradeOffersEvent::vftable */
#define EVENT_RNG_RESCHEDULE 0x407160u   /* trade offer reschedule (inline PCG on TLS+0x318) */
#define EVENT_RNG_FLOAT_RANGE 0x14ddc90u /* Random::FloatRange(uint64_t *word, float range) */
enum {EVENT_RNG_OFF=0,EVENT_RNG_OBSERVE=1,EVENT_RNG_KEY=2};
typedef void (*event_rng_deliver_fn)(void *,void *,double);
typedef struct {volatile LONG64 ready;uint64_t tick,key,due,entry;uint32_t stype,etype,draws;uint8_t klass,keyed;} event_rng_row_t;
static int sample_read(const void *,void *,size_t);
static volatile LONG event_rng_armed;
static int event_rng_mode;
static const char *event_rng_stage="idle";
static uint64_t event_rng_calls,event_rng_keyed,event_rng_local,event_rng_drawn,event_rng_draws,event_rng_unknown,event_rng_boot;
static uint64_t event_rng_trade,event_rng_trade_drawn,event_rng_trade_draws,event_rng_keyset,event_rng_drawset,event_rng_unreadable;
static volatile LONG64 event_rng_foreign,event_rng_failures,event_rng_unwound;
static unsigned event_rng_depth,event_rng_max_depth;
static uint64_t event_rng_all_from,event_rng_all_to; /* row window, 0/0: drawn deliveries only */
static event_rng_row_t *event_rng_ring;
static volatile LONG64 event_rng_rows;
/* The calling thread's TLS RNG word (tests swap in a local word). */
static __attribute__((noinline)) uint64_t *event_rng_tls_word(void) {
    void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    return tls && tls[0]?(uint64_t*)((unsigned char*)tls[0]+0x318):NULL;
}
static uint64_t *(*event_rng_word)(void)=event_rng_tls_word;
static uint32_t event_rng_rva(uint64_t pointer) {
    uintptr_t base=(uintptr_t)image;
    return base && pointer>=base && pointer<base+0x6fa9000u?(uint32_t)(pointer-base):0xffffffffu;
}
/* Generator steps of the engine PCG (Random::FloatRange's step) from one word to another. */
static uint32_t event_rng_steps(uint64_t from,uint64_t to) {
    for(uint32_t n=0;n<=EVENT_RNG_STEP_CAP;n++) {
        if(from==to) return n;
        from=from*0x5851f42d4c957f2dULL+0x14057b7ef767814fULL;from=(from>>30)|(from<<34);
    }
    return UINT32_MAX;
}
/* The delivery's identity: portable key and class of its source (or the
 * source's vtable RVA, class none), both vtable RVAs; 0 when unreadable. */
static int event_rng_identify(void *source,void *event,uint64_t *key,unsigned *klass,uint32_t *stype,uint32_t *etype) {
    uint64_t runtime=0,svt=0,evt=0;
    if(!sample_read(source,&svt,8) || !sample_read((unsigned char*)source+8,&runtime,8) || !event || !sample_read(event,&evt,8)) return 0;
    *stype=event_rng_rva(svt);*etype=event_rng_rva(evt);*klass=WORLD_KEY_NONE;
    *key=runtime?world_identity_key(runtime,klass):0;
    if(!*key) {*klass=WORLD_KEY_NONE;*key=*stype;}
    return 1;
}
static uint64_t event_rng_seed(uint64_t tick,uint64_t key,unsigned klass,uint32_t etype,uint64_t due) {
    return lockstep_seed(EVENT_RNG_DOMAIN,tick,key,etype,due,klass);
}
static void event_rng_record(uint64_t key,unsigned klass,uint32_t stype,uint32_t etype,uint64_t due,uint64_t entry,uint32_t draws,int keyed) {
    if(!event_rng_ring) return;
    LONG64 index=InterlockedIncrement64(&event_rng_rows)-1;event_rng_row_t *r=&event_rng_ring[(uint64_t)index%EVENT_RNG_RING];
    InterlockedExchange64(&r->ready,-1);
    r->tick=stats.issued;r->key=key;r->due=due;r->entry=entry;r->stype=stype;r->etype=etype;r->draws=draws;r->klass=(uint8_t)klass;r->keyed=(uint8_t)keyed;
    InterlockedExchange64(&r->ready,index);
}
/* Rides event_order_deliver_with's delivery: the source is live (the native
 * loop just read its slot). */
static void event_rng_deliver(event_rng_deliver_fn native,void *source,void *event,double due) {
    if(!event_rng_armed || !source) {native(source,event,due);return;}
    if(!stats.active || boot_phase!=3) {++event_rng_boot;native(source,event,due);return;}
    if(GetCurrentThreadId()!=ui_thread) {InterlockedIncrement64(&event_rng_foreign);InterlockedIncrement64(&event_rng_failures);native(source,event,due);return;}
    uint64_t *word=event_rng_word();
    if(!word) {InterlockedIncrement64(&event_rng_failures);event_rng_stage="word";native(source,event,due);return;}
    uint64_t key=0,bits;unsigned klass=WORLD_KEY_NONE;uint32_t stype=0xffffffffu,etype=0xffffffffu;memcpy(&bits,&due,8);
    if(!event_rng_identify(source,event,&key,&klass,&stype,&etype)) {
        /* Not readable here: the native call reads it next; leave it native and count it. */
        ++event_rng_unreadable;native(source,event,due);return;
    }
    ++event_rng_calls;if(klass==WORLD_KEY_NONE) ++event_rng_local;
    int keyed=event_rng_mode==EVENT_RNG_KEY;
    uint64_t entry=*word,start=keyed?event_rng_seed(stats.issued,key,klass,etype,bits):entry;
    if(keyed) {*word=start;++event_rng_keyed;}
    ++event_rng_depth;if(event_rng_depth>event_rng_max_depth) event_rng_max_depth=event_rng_depth;
    volatile int finished=0;volatile uint64_t last=start;
    __try {native(source,event,due);finished=1;}
    __finally {
        --event_rng_depth;last=*word;
        if(keyed) *word=entry;
        if(!finished) {InterlockedIncrement64(&event_rng_unwound);InterlockedIncrement64(&event_rng_failures);}
    }
    uint32_t draws=event_rng_steps(start,last);
    uint64_t id=lockstep_mix(key^((uint64_t)klass<<62))^lockstep_mix(((uint64_t)etype<<32)^bits);
    event_rng_keyset+=id;event_rng_drawset+=lockstep_mix(id^(uint64_t)draws);
    if(draws==UINT32_MAX) ++event_rng_unknown;
    else if(draws) {++event_rng_drawn;event_rng_draws+=draws;}
    if(etype==EVENT_RNG_TRADE_EVENT) {++event_rng_trade;if(draws) {++event_rng_trade_drawn;if(draws!=UINT32_MAX) event_rng_trade_draws+=draws;}}
    if(draws || (event_rng_all_to && stats.issued>=event_rng_all_from && stats.issued<=event_rng_all_to))
        event_rng_record(key,klass,stype,etype,bits,entry,draws,keyed);
}
static int event_rng_pins(void) {
    return hash_ok(image+EVENT_RNG_FLOAT_RANGE,0x78,"584854a7bdbf956043c7756157888674152bd0ffc0a307c00f29537332e8780e") &&
        hash_ok(image+EVENT_RNG_RESCHEDULE,0xe0,"61628271288d5c65b23b62aa6c7ce5e023b577802f1a58d769257569e4401216");
}
static void event_rng_reset(void) {
    event_rng_calls=event_rng_keyed=event_rng_local=event_rng_drawn=event_rng_draws=event_rng_unknown=event_rng_boot=0;
    event_rng_trade=event_rng_trade_drawn=event_rng_trade_draws=event_rng_keyset=event_rng_drawset=event_rng_unreadable=0;
    event_rng_foreign=event_rng_failures=event_rng_unwound=0;event_rng_depth=event_rng_max_depth=0;event_rng_rows=0;
}
/* Before the first admitted frame, on the MoveWorker, after the event order arms. */
/* The row window: every delivery of ticks [from, to] is a row; to=0 turns it off. Before arming. */
static int event_rng_window(uint64_t from,uint64_t to) {
    if(event_rng_armed) return from==event_rng_all_from && to==event_rng_all_to; /* a configuration rerun: the same window */
    if(to && (from<1 || to<from || to-from>600)) return 0;
    event_rng_all_from=from;event_rng_all_to=to;return 1;
}
static int event_rng_arm(int mode,int order_installed) {
    if(event_rng_armed) return stats.active && event_rng_mode==mode;
    if(mode!=EVENT_RNG_OBSERVE && mode!=EVENT_RNG_KEY) {event_rng_stage="mode";return 0;}
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(!world_identity_installed || !order_installed) {event_rng_stage="needs";return 0;}
    if(!event_rng_pins()) {event_rng_stage="pins";return 0;}
    if(!event_rng_ring) event_rng_ring=VirtualAlloc(NULL,sizeof(event_rng_row_t)*EVENT_RNG_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!event_rng_ring) {event_rng_stage="ring";return 0;}
    event_rng_reset();event_rng_mode=mode;event_rng_stage="armed";InterlockedExchange(&event_rng_armed,1);return 1;
}
/* Main-thread tick check: any failure, or the redirect it rides gone, stops the gate (151). */
static int event_rng_check(int order_owned) {
    if(!event_rng_armed) return 1;
    if(!order_owned) {event_rng_stage="order";return 0;}
    if(event_rng_failures) {if(!strcmp(event_rng_stage,"armed")) event_rng_stage="failure";return 0;}
    return 1;
}
static void event_rng_stop(void) {InterlockedExchange(&event_rng_armed,0);}
static int event_rng_row(uint64_t index,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&event_rng_rows,0,0);int n;
    if(!event_rng_ring || (LONG64)index>=count) return 0;
    if(count>(LONG64)EVENT_RNG_RING && index<(uint64_t)(count-(LONG64)EVENT_RNG_RING))
        return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    const event_rng_row_t *r=&event_rng_ring[index%EVENT_RNG_RING];
    if(InterlockedCompareExchange64((volatile LONG64*)&r->ready,0,0)!=(LONG64)index) return 0;
    static const char *const classes[3]={"saved","created","none"};
    char draws[16];if(r->draws==UINT32_MAX) strcpy(draws,"-1");else snprintf(draws,sizeof(draws),"%u",r->draws);
    n=snprintf(out,cap,"index=%llu tick=%llu class=%s key=%016llx stype=%x etype=%x due=%016llx entry=%016llx draws=%s keyed=%u",
        (unsigned long long)index,(unsigned long long)r->tick,classes[r->klass<3?r->klass:2],(unsigned long long)r->key,r->stype,r->etype,
        (unsigned long long)r->due,(unsigned long long)r->entry,draws,(unsigned)r->keyed);
    return n>0 && (size_t)n<cap?n:0;
}
static int event_rng_status(char *out,size_t cap) {
    static const char *const modes[3]={"off","observe","key"};
    int n=snprintf(out,cap,"armed=%ld mode=%s frame=%llu calls=%llu keyed=%llu local=%llu drawn=%llu draws=%llu unknown=%llu trade=%llu "
        "trade_drawn=%llu trade_draws=%llu boot=%llu unreadable=%llu foreign=%lld unwound=%lld max_depth=%u rows=%lld keyset=%016llx "
        "drawset=%016llx window=%llu-%llu failures=%lld stage=%s",
        event_rng_armed,modes[event_rng_mode>=0 && event_rng_mode<3?event_rng_mode:0],(unsigned long long)stats.issued,
        (unsigned long long)event_rng_calls,(unsigned long long)event_rng_keyed,(unsigned long long)event_rng_local,
        (unsigned long long)event_rng_drawn,(unsigned long long)event_rng_draws,(unsigned long long)event_rng_unknown,
        (unsigned long long)event_rng_trade,(unsigned long long)event_rng_trade_drawn,(unsigned long long)event_rng_trade_draws,
        (unsigned long long)event_rng_boot,(unsigned long long)event_rng_unreadable,(long long)event_rng_foreign,(long long)event_rng_unwound,
        event_rng_max_depth,(long long)event_rng_rows,(unsigned long long)event_rng_keyset,(unsigned long long)event_rng_drawset,
        (unsigned long long)event_rng_all_from,(unsigned long long)event_rng_all_to,
        (long long)event_rng_failures,event_rng_stage);
    return n>0 && (size_t)n<cap?n:0;
}
