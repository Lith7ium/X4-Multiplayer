/* Diagnostic, DEFAULT OFF: the code that writes a watched ship's RNG word inside
 * its actor_all ship scope (native_gate_draw_watch = {'<saved id>', ...}, up to
 * four; needs native_gate_actor_rng_all). long-L14/L15: the player's own ship
 * scope (0ae79c) takes one draw on one client only, at ticks that vary between
 * runs (669/786, then 777/1106); sound starts (slot 1143) are excluded. The
 * static candidates reached from that scope are PlayerController::vfunc33's
 * manual-flight branch (0xd98244 -> 0xd9a410 -> XSfx 0x14e94f0 -> 0x14eed60
 * -> Random::FloatState on the thread word) and Ship::Move -> 0x7dc650 ->
 * SpawnEffect 0xd73e00 (FloatState, U32, inlined PCG); inlined draws have no
 * call to hook, so the store itself is watched.
 * While the scope of a watched ship is open on the MoveWorker, debug register 0
 * holds a write breakpoint (8 bytes) on the thread's word TLS+0x318: a raised
 * exception lets this module's first vectored handler load Dr0/Dr7 into the
 * thread context (and clear them again before the scope restores the caller's
 * word). Every store to the word then raises a single-step exception after the
 * storing instruction; the handler records the instruction pointer, six
 * callers (RtlVirtualUnwind on a copy of the context) and the stored value, and
 * continues. DRAW_WATCH_ROW: tick, ship, ordinal of the store in the scope, rip
 * and chain as image RVAs ('g' + offset inside this module, 'x' + low 32 bits
 * elsewhere), the word.
 * Inert until the first admitted frame: arming only registers the handler (once,
 * never removed) and the scope observer; a scope sets the breakpoint only in an
 * admitted frame on the MoveWorker, and the first such scope first proves the
 * mechanism on a local word (exactly one store caught, none after the clear),
 * else the gate stops (reason 141). Fail-safe (an L16 attempt crashed with an
 * unhandled STATUS_SINGLE_STEP while loading): a single-step exception on the
 * thread that held the breakpoint, or one whose Dr6/Dr7 show breakpoint 0 in
 * this module's pattern, outside an open scope is a stray: the handler clears
 * breakpoint 0 for that thread and continues (counted, strays=). The gate tick
 * fails a breakpoint left set (reason 141). Stop clears it; the handler stays.
 * long-L16 counted about 23 strays per tick on both clients (30382/30409 over
 * 1300 scopes, one scope per tick) where the design expects none outside an
 * unwound scope; each costs one exception dispatch (microseconds, well under
 * 1 ms per tick). Likely source, seen with test_gate under load (six copies
 * at once, 6 of 18 failed): strays and false store rows with Dr6 B3, a
 * breakpoint 3 on a stack word of the raising path that this module never
 * set. The load and clear kept Dr1-Dr3 and the other Dr7 bits of the
 * exception's context, so debug state that arrived in one context (where from
 * is open: a standalone copy of the mechanism never saw any) was loaded back
 * and stayed. Every load now writes Dr0-Dr3, Dr6 and Dr7 whole (breakpoint 0
 * or none; 42 of 42 such copies passed), only a breakpoint 0 hit (Dr6 B0) is
 * recorded as a store, and foreign_dr= counts contexts on the watched thread
 * that arrive with debug state this module never loads (wiped by the next
 * load: a debugger's breakpoints 1-3 on the MoveWorker do not survive a
 * watched scope; foreign_first= gives the first one: path s = raised set/clear,
 * t = single step, 1 when its flags claim debug registers, Dr1-Dr3, Dr7).
 * long-L17 (with native_gate_player_timer): foreign_dr=4001 over 2000 scopes
 * (two per scope plus the self test), strays=0, 4650 rows equal on both
 * clients. Strays are classified: by
 * Dr6 (b0= our breakpoint hit, b123= breakpoints 1-3, bs= a trap-flag single
 * step, none= no status bit: Windows reports Dr6 = 0 for a thread without
 * debug state), armed= Dr7 held our pattern, other= not the thread that held
 * it, admitted= in an admitted frame; DRAW_WATCH_STRAY rows (the first 16, then
 * every 1024th; a ring of DW_STRAY_RING) give tick, flags, Dr6 and the
 * rip/chain like a store row.
 * Effect on the simulation: none on data or FP state, but not on timing. The
 * handler changes only the debug registers in the context it continues (and
 * keeps the thread's last error); MXCSR (FTZ/DAZ/rounding), the x87 control
 * word and the upper ymm halves come back unchanged across a breakpoint step
 * and a raised set/clear (checked with a standalone copy). Each watched store
 * and each scope's set and clear is a kernel exception round trip on the
 * MoveWorker (a few microseconds; about 4 per tick in L17), and the handler's
 * frames run on that thread's stack below its stack pointer. Code whose result
 * depends on thread timing (or on stale stack contents) can therefore come out
 * differently with the watch on: long-L17 (watch and player timer) had client
 * 1 alone compute other values for the second physics body's inverse inertia
 * of fixtures 5 and 6 (39704e01:39a5fbfe:3a2cadd8; 39704e20:39a5fc21:3a2cadd6
 * on client 2, as in all 452 other census client logs that show this body),
 * fixed before the first state row (tick 2), while L17nt (watch only) and
 * L17nw (timer only) passed. Diagnostic runs only; not in a pass/fail overlay.
 *
 * native_gate_draw_watch_frame (true; needs native_gate_seed_rng, not with the
 * list above): who draws from the frame's own word. sc-h103a/b: the word the probe
 * reads differs between clients on 5 and 46 of 1800 ticks, always by one draw or a
 * few, on either client, often the other client a tick later; the next frame's seed
 * hides it. Four ticks of run a part before the first script row, the rest after the
 * last one; the logs do not name the drawer, and most of the draws are inlined. The
 * watch opens where path_release writes the frame's seed and closes when the probe's
 * update starts (gate.draw_watch_frame_end, in slot 6, before the word is read). A
 * store is a row only if it is a draw at frame level: the word before it was the
 * frame's and its value is one PCG step further. A keyed scope's stores (its seed,
 * its own draws, the restore of the caller's word) are not rows, so a tick has as
 * many rows as the probe's word has steps from the seed, id=0 and store= the draw's
 * ordinal in the frame. Two clients' rows at a differing tick name the extra draw.
 * The same cost and caveats as above, about 2 stores per script frame. */
#define DW_SET_CODE 0xE0445753u /* raised to load or clear Dr0/Dr7 */
#define DW_IDS 4u
#define DW_CHAIN 6u /* FloatState <- 0x14eed60 <- 0x14e8ec0 <- 0x14e94f0 <- 0xd9a410 <- PlayerController::vfunc33 */
#define DW_RING 4096u
#define DW_STOP_REASON 141u
#define DW_STRAY_RING 64u
enum {DW_STRAY_MINE=1,DW_STRAY_ACTIVE=2,DW_STRAY_ARMED=4,DW_STRAY_ADMITTED=8};
typedef struct {uint64_t tick,id,word;uint32_t ordinal,rip,chain[DW_CHAIN];unsigned char kind[1+DW_CHAIN];} dw_row_t;
static dw_row_t dw_ring[DW_RING],dw_stray_ring[DW_STRAY_RING]; /* a stray row keeps flags in ordinal, Dr6 in word */
static volatile LONG64 dw_rows,dw_failures;
static uint64_t dw_ids[DW_IDS],dw_current;
static volatile uint64_t *dw_target; /* the watched word (also during the self test) */
static unsigned dw_id_count,dw_ordinal;
static PVOID dw_handle;
static int dw_armed,dw_proven;
static volatile LONG dw_active,dw_selftest;
static volatile DWORD dw_thread; /* the thread that last held the breakpoint */
static uint64_t dw_scopes,dw_stores,dw_selftest_hits;
static int dw_frame_armed;
static uint64_t dw_frame_base,dw_frame_prev,dw_frame_draws;
static volatile LONG64 dw_foreign_dr,dw_strays,dw_stray_b0,dw_stray_b123,dw_stray_bs,dw_stray_none,dw_stray_armed,dw_stray_other,dw_stray_admitted,dw_stray_rows;
static uint64_t dw_foreign_first[5]; /* the first foreign context: path ('s' set, 't' step) | 0x100 when it claims debug registers, Dr1-Dr3, Dr7 */
static uintptr_t dw_module_base,dw_module_size;
static const char *dw_stage="idle";
/* Image RVA ('i'), offset in this module ('g') or low 32 bits ('x'). */
static uint32_t dw_place(uint64_t address,unsigned char *kind) {
    if(address>=(uint64_t)(uintptr_t)image && address<(uint64_t)(uintptr_t)(image+0x6fa9000)) {*kind='i';return (uint32_t)(address-(uint64_t)(uintptr_t)image);}
    if(dw_module_base && address>=dw_module_base && address<dw_module_base+dw_module_size) {*kind='g';return (uint32_t)(address-dw_module_base);}
    *kind='x';return (uint32_t)address;
}
/* rip and six callers of the context. */
static void dw_unwind(const CONTEXT *context,dw_row_t *r) {
    r->rip=dw_place(context->Rip,&r->kind[0]);
    CONTEXT c=*context;
    for(unsigned i=0;i<DW_CHAIN;i++) {
        DWORD64 base=0;PRUNTIME_FUNCTION f=RtlLookupFunctionEntry(c.Rip,&base,NULL);
        if(f) {PVOID data=NULL;DWORD64 frame=0;RtlVirtualUnwind(UNW_FLAG_NHANDLER,base,c.Rip,f,&c,&data,&frame,NULL);}
        else {DWORD64 ret=0;if(!read_local((void*)(uintptr_t)c.Rsp,&ret,8)) break;c.Rip=ret;c.Rsp+=8;}
        if(!c.Rip) break;
        r->chain[i]=dw_place(c.Rip,&r->kind[1+i]);
    }
}
static void dw_record(const CONTEXT *context) {
    dw_row_t r;memset(&r,0,sizeof(r));r.tick=stats.issued;r.id=dw_current;r.ordinal=++dw_ordinal;
    r.word=dw_target?*dw_target:0; /* the open scope's own word: no system call in the handler */
    dw_unwind(context,&r);
    LONG64 index=InterlockedIncrement64(&dw_rows)-1;dw_ring[(uint64_t)index%DW_RING]=r;
}
/* Classify a stray before its breakpoint is cleared; sample a row. */
static void dw_stray(const CONTEXT *c,int mine) {
    LONG64 n=InterlockedIncrement64(&dw_strays);
    unsigned flags=(mine?DW_STRAY_MINE:0)|(InterlockedCompareExchange(&dw_active,0,0)?DW_STRAY_ACTIVE:0)|
        ((c->Dr7&1) && ((c->Dr7>>16)&0xfu)==9u?DW_STRAY_ARMED:0)|(stats.active && boot_phase==3 && stats.issued?DW_STRAY_ADMITTED:0);
    if(c->Dr6&1) InterlockedIncrement64(&dw_stray_b0);
    if(c->Dr6&0xe) InterlockedIncrement64(&dw_stray_b123);
    if(c->Dr6&0x4000) InterlockedIncrement64(&dw_stray_bs);
    if(!(c->Dr6&0x400f)) InterlockedIncrement64(&dw_stray_none);
    if(flags&DW_STRAY_ARMED) InterlockedIncrement64(&dw_stray_armed);
    if(!mine) InterlockedIncrement64(&dw_stray_other);
    if(flags&DW_STRAY_ADMITTED) InterlockedIncrement64(&dw_stray_admitted);
    if(n>16 && (n-16)%1024) return;
    dw_row_t r;memset(&r,0,sizeof(r));r.tick=stats.issued;r.id=(uint64_t)n;r.ordinal=flags;r.word=c->Dr6;
    dw_unwind(c,&r);
    LONG64 index=InterlockedIncrement64(&dw_stray_rows)-1;dw_stray_ring[(uint64_t)index%DW_STRAY_RING]=r;
}
/* Breakpoint 0 on address (write, 8 bytes), or no breakpoint for 0: every debug
 * register is written (see above: the record's own debug fields may be garbage). */
static void dw_load(CONTEXT *c,DWORD64 address) {
    c->Dr0=address;c->Dr1=c->Dr2=c->Dr3=0;c->Dr6=0;
    c->Dr7=address?1u|(1u<<16)|(2u<<18):0; /* L0, RW0 = write, LEN0 = 8 bytes */
    c->ContextFlags|=CONTEXT_DEBUG_REGISTERS;
}
/* Breakpoint 0 in this module's pattern: hit (Dr6 B0) or enabled as L0, write, 8 bytes. */
static int dw_ours(const CONTEXT *c) {return (c->Dr6&1) || ((c->Dr7&1) && ((c->Dr7>>16)&0xfu)==9u);}
/* Debug state this module never loads: Dr1-Dr3 or Dr7 bits beside breakpoint 0's (0x400 reads as set). */
static int dw_foreign(const CONTEXT *c) {return c->Dr1 || c->Dr2 || c->Dr3 || (c->Dr7&~(DWORD64)(3u|(0xfu<<16)|0x400u));}
static void dw_note_foreign(const CONTEXT *c,unsigned path) {
    if(InterlockedIncrement64(&dw_foreign_dr)!=1) return;
    dw_foreign_first[0]=path|((c->ContextFlags&CONTEXT_DEBUG_REGISTERS)==CONTEXT_DEBUG_REGISTERS?0x100u:0);
    dw_foreign_first[1]=c->Dr1;dw_foreign_first[2]=c->Dr2;dw_foreign_first[3]=c->Dr3;dw_foreign_first[4]=c->Dr7;
}
/* A store in frame mode: a draw at frame level, or a keyed scope's. */
static int dw_frame_keep(uint64_t now) {
    uint64_t next=dw_frame_base*0x5851f42d4c957f2dULL+0x14057b7ef767814fULL;next=(next>>30)|(next<<34);
    const int draw=dw_frame_prev==dw_frame_base && now==next;
    dw_frame_prev=now;
    if(draw) {dw_frame_base=now;++dw_frame_draws;}
    return draw;
}
static LONG dw_exception(EXCEPTION_POINTERS *ep) {
    EXCEPTION_RECORD *e=ep->ExceptionRecord;CONTEXT *c=ep->ContextRecord;
    if(e->ExceptionCode==DW_SET_CODE && e->NumberParameters==1) {
        if(dw_foreign(c)) dw_note_foreign(c,'s');
        dw_load(c,(DWORD64)e->ExceptionInformation[0]);return EXCEPTION_CONTINUE_EXECUTION;
    }
    if(e->ExceptionCode!=EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
    int mine=GetCurrentThreadId()==dw_thread;
    if(mine && dw_foreign(c)) dw_note_foreign(c,'t');
    if(mine && dw_target && (InterlockedCompareExchange(&dw_active,0,0) || InterlockedCompareExchange(&dw_selftest,0,0))) {
        /* Only a breakpoint 0 hit is a store; any other single step is a stray and the watch goes on. */
        if(!(c->Dr6&1)) {dw_stray(c,mine);dw_load(c,(DWORD64)(uintptr_t)dw_target);return EXCEPTION_CONTINUE_EXECUTION;}
        if(dw_selftest) ++dw_selftest_hits;
        else {++dw_stores;if(!dw_frame_armed || dw_frame_keep(*dw_target)) dw_record(c);}
        dw_load(c,(DWORD64)(uintptr_t)dw_target); /* re-armed explicitly: the record need not carry the debug registers */
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if(mine || dw_ours(c)) {dw_stray(c,mine);dw_load(c,0);return EXCEPTION_CONTINUE_EXECUTION;} /* stray: clear, go on */
    return EXCEPTION_CONTINUE_SEARCH;
}
/* The interrupted code sees its own last-error value again. */
static LONG CALLBACK dw_handler(EXCEPTION_POINTERS *ep) {
    DWORD error=GetLastError();LONG result=dw_exception(ep);SetLastError(error);return result;
}
static void dw_set(volatile void *address) {ULONG_PTR p=(ULONG_PTR)address;RaiseException(DW_SET_CODE,0,1,&p);}
static __attribute__((noinline)) void dw_store(volatile uint64_t *word,uint64_t value) {*word=value;}
/* Prove the mechanism on this thread: one store caught, none after the clear. */
static int dw_self_test(void) {
    volatile uint64_t probe=0;dw_thread=GetCurrentThreadId();dw_selftest_hits=0;
    dw_target=&probe;InterlockedExchange(&dw_selftest,1);dw_set(&probe);dw_store(&probe,1);dw_set(NULL);dw_store(&probe,2);
    InterlockedExchange(&dw_selftest,0);dw_target=NULL;
    return dw_selftest_hits==1 && probe==2;
}
/* The scope observer: set on open, clear before the caller's word is restored.
 * Only in admitted frames on the MoveWorker. */
static void dw_scope(int open,uint64_t id,uint64_t *rng) {
    if(open>0) {
        if(!dw_armed || !stats.active || (boot_mode && boot_phase!=3) || !stats.issued || GetCurrentThreadId()!=ui_thread) return;
        unsigned i=0;while(i<dw_id_count && dw_ids[i]!=id) ++i;
        if(i==dw_id_count || !rng || InterlockedCompareExchange(&dw_active,0,0) || InterlockedCompareExchange64(&dw_failures,0,0)) return;
        if(!dw_proven) {
            if(!dw_self_test()) {dw_stage="self_test";InterlockedIncrement64(&dw_failures);return;}
            dw_proven=1;
        }
        dw_thread=GetCurrentThreadId();dw_current=id;dw_ordinal=0;++dw_scopes;dw_target=rng;InterlockedExchange(&dw_active,1);dw_set(rng);
    } else if(InterlockedCompareExchange(&dw_active,0,0) && id==dw_current && GetCurrentThreadId()==dw_thread) {
        /* While an exception unwinds no exception is raised: the next store is a stray and clears it. */
        if(!open) dw_set(NULL);
        InterlockedExchange(&dw_active,0);dw_target=NULL;
    }
}
static int dw_add(uint64_t saved) {
    if(!saved || GetCurrentThreadId()!=ui_thread) return 0;
    /* The probe arms again after the loading UI reload: an ID already watched is accepted. */
    for(unsigned i=0;i<dw_id_count;i++) if(dw_ids[i]==saved) return 1;
    if(dw_armed || dw_id_count>=DW_IDS) return 0;
    dw_ids[dw_id_count++]=saved;return 1;
}
static int dw_install(void) {
    HMODULE self=NULL;
    if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)(void*)dw_handler,&self) && self) {
        const IMAGE_DOS_HEADER *dos=(const IMAGE_DOS_HEADER*)self;
        const IMAGE_NT_HEADERS64 *nt=(const IMAGE_NT_HEADERS64*)((const unsigned char*)self+dos->e_lfanew);
        dw_module_base=(uintptr_t)self;dw_module_size=nt->OptionalHeader.SizeOfImage;
    }
    if(!dw_handle) dw_handle=AddVectoredExceptionHandler(1,dw_handler);
    if(!dw_handle) {dw_stage="handler";return 0;}
    dw_rows=dw_failures=dw_strays=dw_foreign_dr=0;dw_scopes=dw_stores=0;memset(dw_foreign_first,0,sizeof(dw_foreign_first));
    dw_stray_b0=dw_stray_b123=dw_stray_bs=dw_stray_none=dw_stray_armed=dw_stray_other=dw_stray_admitted=dw_stray_rows=0;dw_active=0;dw_proven=0;dw_thread=0;
    return 1;
}
/* After native_gate_actor_rng_all, before the first admitted frame: no debug
 * register is touched here; the first admitted watched scope proves the mechanism. */
static int dw_arm(void) {
    if(dw_armed) return 1;
    if(dw_frame_armed || !dw_id_count || !actor_all_on() || !stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(!dw_install()) return 0;
    actor_all_ship_watch=dw_scope;dw_armed=1;dw_stage="armed";return 1;
}
/* Frame mode: the same promises; the first frame proves the mechanism. */
static int dw_frame_arm(void) {
    if(dw_frame_armed) return 1;
    if(dw_armed || dw_id_count || !stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(!dw_install()) return 0;
    dw_frame_draws=0;dw_frame_armed=1;dw_stage="armed_frame";return 1;
}
static void dw_frame_end(void) {
    if(!dw_frame_armed || !InterlockedCompareExchange(&dw_active,0,0) || GetCurrentThreadId()!=dw_thread) return;
    dw_set(NULL);InterlockedExchange(&dw_active,0);dw_target=NULL;
}
/* Where path_release has just written the frame's seed: admitted frames on the MoveWorker. A frame without a probe update ends here. */
static void dw_frame_begin(uint64_t *word) {
    if(!dw_frame_armed || !stats.active || (boot_mode && boot_phase!=3) || !stats.issued || GetCurrentThreadId()!=ui_thread ||
       InterlockedCompareExchange64(&dw_failures,0,0)) return;
    dw_frame_end();
    if(!dw_proven) {
        if(!dw_self_test()) {dw_stage="self_test";InterlockedIncrement64(&dw_failures);return;}
        dw_proven=1;
    }
    dw_thread=GetCurrentThreadId();dw_current=0;dw_ordinal=0;++dw_scopes;dw_target=word;dw_frame_base=dw_frame_prev=*word;
    InterlockedExchange(&dw_active,1);dw_set(word);
}
/* Main-thread tick check: no breakpoint may outlive its scope (a frame's stays open across this check). */
static int dw_check(void) {
    if(!dw_frame_armed && InterlockedCompareExchange(&dw_active,0,0)) {dw_stage="left_set";InterlockedIncrement64(&dw_failures);}
    return !InterlockedCompareExchange64(&dw_failures,0,0);
}
static void dw_stop(void) {
    if(!dw_armed && !dw_frame_armed) return;
    if(dw_armed) actor_all_ship_watch=NULL;
    if(InterlockedCompareExchange(&dw_active,0,0) && GetCurrentThreadId()==dw_thread) {dw_set(NULL);InterlockedExchange(&dw_active,0);dw_target=NULL;}
    else if(dw_frame_armed) InterlockedExchange(&dw_active,0); /* nobody else closes a frame's */
    /* The handler stays registered: a breakpoint still set on another thread is cleared as a stray. */
    dw_armed=0;dw_frame_armed=0;dw_stage="stopped";
}
static int dw_row(uint64_t index,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&dw_rows,0,0);int n;
    if((LONG64)index>=count) return 0;
    if(count>(LONG64)DW_RING && index<(uint64_t)(count-(LONG64)DW_RING))
        return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    const dw_row_t *r=&dw_ring[index%DW_RING];
    n=snprintf(out,cap,"index=%llu tick=%llu id=%016llx store=%u rip=%c%08x chain=",(unsigned long long)index,(unsigned long long)r->tick,
        (unsigned long long)r->id,r->ordinal,r->kind[0]?r->kind[0]:'-',r->rip);
    for(unsigned i=0;n>0 && (size_t)n<cap && i<DW_CHAIN;i++)
        n+=snprintf(out+n,cap-(size_t)n,"%s%c%08x",i?",":"",r->kind[1+i]?r->kind[1+i]:'-',r->chain[i]);
    if(n>0 && (size_t)n<cap) n+=snprintf(out+n,cap-(size_t)n," word=%016llx",(unsigned long long)r->word);
    return n>0 && (size_t)n<cap?n:0;
}
/* index= the stray's number, flags (DW_STRAY_*), Dr6, rip and chain. */
static int dw_stray_row(uint64_t index,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&dw_stray_rows,0,0);int n;
    if((LONG64)index>=count) return 0;
    if(count>(LONG64)DW_STRAY_RING && index<(uint64_t)(count-(LONG64)DW_STRAY_RING))
        return (n=snprintf(out,cap,"row=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    const dw_row_t *r=&dw_stray_ring[index%DW_STRAY_RING];
    n=snprintf(out,cap,"row=%llu stray=%llu tick=%llu mine=%u active=%u armed=%u admitted=%u dr6=%llx rip=%c%08x chain=",(unsigned long long)index,
        (unsigned long long)r->id,(unsigned long long)r->tick,!!(r->ordinal&DW_STRAY_MINE),!!(r->ordinal&DW_STRAY_ACTIVE),!!(r->ordinal&DW_STRAY_ARMED),
        !!(r->ordinal&DW_STRAY_ADMITTED),(unsigned long long)r->word,r->kind[0]?r->kind[0]:'-',r->rip);
    for(unsigned i=0;n>0 && (size_t)n<cap && i<DW_CHAIN;i++)
        n+=snprintf(out+n,cap-(size_t)n,"%s%c%08x",i?",":"",r->kind[1+i]?r->kind[1+i]:'-',r->chain[i]);
    return n>0 && (size_t)n<cap?n:0;
}
static int dw_status(char *out,size_t cap) {
    int n=snprintf(out,cap,"armed=%d ids=%u proven=%d scopes=%llu stores=%llu rows=%lld active=%ld strays=%lld stage=%s failures=%lld "
        "stray_b0=%lld stray_b123=%lld stray_bs=%lld stray_none=%lld stray_armed=%lld stray_other=%lld stray_admitted=%lld stray_rows=%lld "
        "foreign_dr=%lld foreign_first=%c%u:%llx:%llx:%llx:%llx frame=%d frame_draws=%llu",dw_armed,
        dw_id_count,dw_proven,(unsigned long long)dw_scopes,(unsigned long long)dw_stores,(long long)dw_rows,dw_active,(long long)dw_strays,dw_stage,
        (long long)dw_failures,(long long)dw_stray_b0,(long long)dw_stray_b123,(long long)dw_stray_bs,(long long)dw_stray_none,(long long)dw_stray_armed,
        (long long)dw_stray_other,(long long)dw_stray_admitted,(long long)dw_stray_rows,(long long)dw_foreign_dr,
        dw_foreign_first[0]&0xff?(char)(dw_foreign_first[0]&0xff):'-',(unsigned)(dw_foreign_first[0]>>8&1),(unsigned long long)dw_foreign_first[1],
        (unsigned long long)dw_foreign_first[2],(unsigned long long)dw_foreign_first[3],(unsigned long long)dw_foreign_first[4],dw_frame_armed,(unsigned long long)dw_frame_draws);
    return n>0 && (size_t)n<cap?n:0;
}
