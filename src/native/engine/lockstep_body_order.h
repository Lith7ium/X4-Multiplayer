/* Determinism fix, DEFAULT OFF: native_gate_body_order = 'sort' ('observe'
 * records the same rows and keeps the native order). Needs the world and
 * created identities (native_gate_world_identities,
 * native_gate_created_identities, native_gate_portable_created).
 *
 * Objects that need physics bodies (attention and parent changes:
 * U::Destructible slots 144/146/148 and U::Positional slot 700 call 0x51c790)
 * are queued in one global set (head 0x2f5dfd0, count 0x2f5dff8) ordered by
 * the raw object pointer: the insert 0x86a320 compares node+0x20 with the
 * pointer. UniverseFrameUpdate calls the pass 0x83e960 at 0x9fe427, before
 * both Scene::Step calls. Under TryEnterCriticalSection(0x3949b98) it walks
 * the set in ascending address order and per object drops its current bodies
 * (0x841110 at 0x83ed63, when map 0x2f5e340 holds a record) and creates its
 * bodies (0x840fd0 at 0x83ed85 -> U::Destructible::CreateBody 0x51b730 ->
 * XPhys::CreateBodyRecord 0xbe550 -> Jolt BodyManager::AddBody: the next
 * BodyID of the free list), then clears the set. Objects queued in one frame,
 * like the ships of the tick-15 spawn, so take their BodyIDs, broadphase and
 * active-list places in heap-address order, which is local to a process; the
 * physics step then solves their contacts in another order
 * (lockstep_physics_trace.h).
 *
 * Fix: in admitted frames on the MoveWorker the pass call goes through
 * border_pass, and the two calls inside the walk through border_remove and
 * border_create, which record the object (holding a reference, +0x40) instead
 * of acting. When the original pass has returned (its set cleared), the
 * recorded objects run in portable order, each its drop and then its
 * creation, under the pass's own recursive lock 0x3949b98, and the references
 * are released as the set releases its own (+0x40, state +0x44 2 -> 3, slot 0
 * of the +0x38 base). Portable order: saved ID, created ID, then objects
 * created in this frame by their MoveWorker creation order (the created
 * identities' pending list), then any other object in native order (counted
 * as unordered). A call outside the pass, on another thread or in a loading
 * frame runs natively. BODY_ORDER_ROW per recorded object (keys resolved when
 * printed, completed ticks only), BODY_ORDER status per tick. Pins: the pass,
 * its two callees, the enqueue, the set insert and the call site. Stop reason
 * 134: a site no longer ours, a pin change, a full record list or a fault. */
#define BORDER_PASS 0x83e960u
#define BORDER_LOCK 0x3949b98u
#define BORDER_SET_COUNT 0x2f5dff8u
#define BORDER_CALLS 3u
#define BORDER_STOP_REASON 134u
#define BORDER_MAX 8192u
#define BORDER_RING 65536u
#define BORDER_STUB_SIZE 0x1000u
enum {BORDER_OFF,BORDER_OBSERVE,BORDER_SORT};
enum {BORDER_KIND_SAVED,BORDER_KIND_CREATED,BORDER_KIND_FRAME,BORDER_KIND_UNORDERED};
/* e8 call sites and their native callees: the pass, drop, create. */
static const struct {unsigned rva,target;} border_calls[BORDER_CALLS]={{0x9fe427u,BORDER_PASS},{0x83ed63u,0x841110u},{0x83ed85u,0x840fd0u}};
static const struct {unsigned rva,size;const char *sha;} border_pins[]={
    {0x83e960u,0x554u,"e7ce0741dc8ed391a44a006c4c67434c77d49d078b17a37f85e2a034a79ee2e2"}, /* the pass */
    {0x840fd0u,0x13cu,"affffa1a78856a27c65a65a0badc0a2019ea82381ad07aa3e5be7b31bd12dd72"}, /* create */
    {0x841110u,0x302u,"a9cdad93a62138961da5828d5e416f788a3512d2614fca572d458a83ca7124a2"}, /* drop */
    {0x51c790u,0x11bu,"60e937ec21eb46448e1bf84ffb424bf78bda93741e28dc7c550cae56498294ae"}, /* enqueue, reference */
    {0x86a320u,0x142u,"82660d41515d9e91748024301d1e52630d59bf8c297011ea8f54145de7989c18"}, /* insert by pointer */
    {0x9fe418u,0x14u,"4cd1eeffc41824ae4e09bf0a582a05077833c40d4ba17491d5eff8dbb6c832df"}};  /* the pass call */
typedef void (*border_fn)(void *);
typedef void (*border_pass_fn)(void);
typedef struct {unsigned char *object;uint64_t runtime,key,rank;uint32_t pos,order;uint8_t klass,kind,remove,create,held;} border_entry;
typedef struct {uint64_t tick,runtime,key;uint32_t pos,order,count;uint8_t klass,kind,remove,create,mode;} border_row;
static int border_mode,border_installed,border_restored,border_in_pass;
static unsigned border_done; /* call sites patched, one bit each */
static unsigned char *border_stubs;
static border_entry border_list[BORDER_MAX],border_sorted[BORDER_MAX];
static unsigned border_count;
static border_row *border_ring;
static volatile LONG64 border_rows,border_failures;
static uint64_t border_tick=UINT64_MAX,border_passes,border_native,border_entries,border_moved,border_unordered,border_overflow,border_reenqueued;
static uint64_t border_frame_passes,border_frame_entries,border_frame_moved,border_frame_kinds[4];
static const char *border_stage="idle";
static int border_fail(const char *why) {border_stage=why;InterlockedIncrement64(&border_failures);return 0;}
static unsigned char *border_stub(unsigned i) {return border_stubs+0x20u*i;}
static int32_t border_rel_to(unsigned i,const void *target) {return (int32_t)((intptr_t)target-(intptr_t)(image+border_calls[i].rva+5));}
/* 0 original, 1 ours (its stub), -1 anything else. */
static int border_call_state(unsigned i) {
    unsigned char now[5];int32_t d;if(!read_local(image+border_calls[i].rva,now,5) || now[0]!=0xe8) return -1;
    memcpy(&d,now+1,4);
    if(d==border_rel_to(i,image+border_calls[i].target)) return 0;
    return border_stubs && d==border_rel_to(i,border_stub(i))?1:-1;
}
static int border_pins_ok(void) {
    static unsigned char copy[0x600];
    for(unsigned p=0;p<sizeof(border_pins)/sizeof(*border_pins);p++) {
        unsigned rva=border_pins[p].rva,size=border_pins[p].size;
        if(size>sizeof(copy) || !read_local(image+rva,copy,size)) return 0;
        for(unsigned i=0;i<BORDER_CALLS;i++) {
            unsigned at=border_calls[i].rva;if(at<rva || at+5>rva+size) continue;
            if(border_call_state(i)<0) return 0;
            int32_t d=border_rel_to(i,image+border_calls[i].target);memcpy(copy+(at-rva)+1,&d,4);
        }
        if(!hash_ok(copy,size,border_pins[p].sha)) return 0;
    }
    return 1;
}
/* The set's reference: +0x40 count, +0x44 state, +0x38 base whose slot 0 the
 * last release calls (0x51c872, 0x83ee22). */
static void border_hold(unsigned char *object) {InterlockedIncrement((volatile LONG*)(object+0x40));}
static void border_release(unsigned char *object) {
    if(InterlockedExchangeAdd((volatile LONG*)(object+0x40),-1)!=1) return;
    if(InterlockedCompareExchange((volatile LONG*)(object+0x44),3,2)!=2) return;
    unsigned char *base=object+0x38;void (**slots)(void *,unsigned)=*(void (***)(void *,unsigned))base;slots[0](base,3);
}
static int border_live(void) {
    return border_installed && stats.active && boot_phase==3 && GetCurrentThreadId()==ui_thread &&
           !InterlockedCompareExchange64(&border_failures,0,0);
}
/* One drop or creation of the walk: recorded (sort: deferred, held). */
static void border_note(border_fn original,unsigned char *object,int create) {
    if(!border_in_pass || GetCurrentThreadId()!=ui_thread) {original(object);return;}
    border_entry *e=border_count?&border_list[border_count-1]:NULL;
    if(!e || e->object!=object) {
        if(border_count==BORDER_MAX) {++border_overflow;border_fail("full");original(object);return;}
        e=&border_list[border_count];memset(e,0,sizeof(*e));e->object=object;e->pos=border_count++;
        unsigned klass=WORLD_KEY_NONE;
        if(!created_read(object+8,&e->runtime,8)) border_fail("object");
        e->key=e->runtime?world_identity_key(e->runtime,&klass):0;e->klass=(uint8_t)klass;
        e->kind=klass==WORLD_KEY_SAVED?BORDER_KIND_SAVED:klass==WORLD_KEY_CREATED?BORDER_KIND_CREATED:BORDER_KIND_UNORDERED;
        if(border_mode==BORDER_SORT) {border_hold(object);e->held=1;}
    }
    if(create) e->create=1;else e->remove=1;
    if(border_mode!=BORDER_SORT) original(object);
}
static void border_remove(void *object) {border_note((border_fn)(void *)(image+border_calls[1].target),object,0);}
static void border_create(void *object) {border_note((border_fn)(void *)(image+border_calls[2].target),object,1);}
/* Objects without a portable key that this frame's MoveWorker created: their
 * place in the created identities' pending list (creation order). */
static void border_rank_frame(border_entry *list,unsigned count) {
    unsigned left=0;for(unsigned i=0;i<count;i++) left+=list[i].kind==BORDER_KIND_UNORDERED && list[i].runtime;
    if(!left || !created_enabled()) return;
    AcquireSRWLockShared(&created_pending_lock);
    for(unsigned n=0;left && created_pending && n<created_pending_count;n++) {
        if(created_pending[n].thread!=CREATED_MOVEWORKER) continue;
        for(unsigned i=0;i<count;i++) if(list[i].kind==BORDER_KIND_UNORDERED && list[i].runtime==created_pending[n].runtime) {
            list[i].kind=BORDER_KIND_FRAME;list[i].rank=n;--left;break;
        }
    }
    ReleaseSRWLockShared(&created_pending_lock);
}
static int border_compare(const void *a,const void *b) {
    const border_entry *x=a,*y=b;
    if(x->kind!=y->kind) return x->kind<y->kind?-1:1;
    uint64_t u=x->kind==BORDER_KIND_FRAME?x->rank:x->kind==BORDER_KIND_UNORDERED?x->pos:x->key;
    uint64_t v=y->kind==BORDER_KIND_FRAME?y->rank:y->kind==BORDER_KIND_UNORDERED?y->pos:y->key;
    if(u!=v) return u<v?-1:1;
    return x->pos<y->pos?-1:x->pos>y->pos;
}
/* Sorts list into border_sorted and gives every entry its portable place. */
static void border_order(border_entry *list,unsigned count) {
    border_rank_frame(list,count);
    memcpy(border_sorted,list,sizeof(*list)*count);qsort(border_sorted,count,sizeof(*border_sorted),border_compare);
    for(unsigned i=0;i<count;i++) {border_sorted[i].order=i;list[border_sorted[i].pos].order=i;}
}
static void border_record(const border_entry *list,unsigned count) {
    for(unsigned i=0;i<count;i++) {
        const border_entry *e=&list[i];LONG64 index=InterlockedIncrement64(&border_rows)-1;border_row *r=&border_ring[(uint64_t)index%BORDER_RING];
        *r=(border_row){stats.issued,e->runtime,e->key,e->pos,e->order,count,e->klass,e->kind,e->remove,e->create,(uint8_t)border_mode};
        ++border_frame_kinds[e->kind];if(e->kind==BORDER_KIND_UNORDERED) ++border_unordered;
        if(e->order!=e->pos) {++border_moved;++border_frame_moved;}
    }
    border_entries+=count;border_frame_entries+=count;
}
/* After the pass: the recorded drops and creations in portable order. */
static void border_flush_with(border_fn drop,border_fn create) {
    border_order(border_list,border_count);border_record(border_list,border_count);
    if(border_mode!=BORDER_SORT) return;
    EnterCriticalSection((CRITICAL_SECTION *)(image+BORDER_LOCK));
    for(unsigned i=0;i<border_count;i++) {
        border_entry *e=&border_sorted[i];
        if(e->remove) drop(e->object);
        if(e->create) create(e->object);
    }
    LeaveCriticalSection((CRITICAL_SECTION *)(image+BORDER_LOCK));
    for(unsigned i=0;i<border_count;i++) if(border_sorted[i].held) border_release(border_sorted[i].object);
    uint64_t queued=0;if(read_local(image+BORDER_SET_COUNT,&queued,8) && queued) ++border_reenqueued;
}
static void border_pass(void) {
    border_pass_fn original=(border_pass_fn)(void *)(image+BORDER_PASS);
    if(!border_live()) {++border_native;original();return;}
    if(border_tick!=stats.issued) {border_tick=stats.issued;border_frame_passes=border_frame_entries=border_frame_moved=0;memset(border_frame_kinds,0,sizeof(border_frame_kinds));}
    border_count=0;border_in_pass=1;original();border_in_pass=0;
    ++border_passes;++border_frame_passes;
    if(border_count) border_flush_with((border_fn)(void *)(image+border_calls[1].target),(border_fn)(void *)(image+border_calls[2].target));
}
static unsigned char *border_alloc_near(unsigned size) {
    uintptr_t at=(uintptr_t)(image+border_calls[0].rva)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            int fits=1;
            for(unsigned i=0;fits && i<BORDER_CALLS;i++) {
                intptr_t lo=(intptr_t)p-(intptr_t)(image+border_calls[i].rva+5),hi=(intptr_t)(p+size)-(intptr_t)(image+border_calls[i].rva+5);
                fits=lo>=INT32_MIN && hi<=INT32_MAX;
            }
            if(fits) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static void *border_target(unsigned i) {return i==0?(void*)border_pass:i==1?(void*)border_remove:(void*)border_create;}
static int border_write_stubs(void) {
    DWORD old,ignored;if(!VirtualProtect(border_stubs,BORDER_STUB_SIZE,PAGE_READWRITE,&old)) return 0;
    for(unsigned i=0;i<BORDER_CALLS;i++) {
        static const unsigned char jump[6]={0xff,0x25,0,0,0,0};void *target=border_target(i);
        memcpy(border_stub(i),jump,6);memcpy(border_stub(i)+6,&target,8);
    }
    int ok=VirtualProtect(border_stubs,BORDER_STUB_SIZE,PAGE_EXECUTE_READ,&ignored)!=0;
    FlushInstructionCache(GetCurrentProcess(),border_stubs,BORDER_STUB_SIZE);return ok;
}
static int border_stubs_owned(void) {
    for(unsigned i=0;i<BORDER_CALLS;i++) {
        void *t=NULL;if(!read_local(border_stub(i)+6,&t,8) || t!=border_target(i) || border_stub(i)[0]!=0xff || border_stub(i)[1]!=0x25) return 0;
    }
    return 1;
}
static int border_cas(unsigned i,int32_t expected,int32_t value) {
    unsigned char e[4],v[4];memcpy(e,&expected,4);memcpy(v,&value,4);return view_cas_bytes(border_calls[i].rva+1,e,v,4);
}
/* Restore every call still ours; 1 when none is left patched. */
static int border_unpatch(void) {
    for(unsigned i=0;i<BORDER_CALLS;i++) if(border_done&(1u<<i)) {
        int state=border_call_state(i);
        if(state==1 && border_cas(i,border_rel_to(i,border_stub(i)),border_rel_to(i,image+border_calls[i].target))) border_done&=~(1u<<i);
        else if(state==0) border_done&=~(1u<<i);
    }
    return !border_done;
}
/* Before the first admitted frame, on the MoveWorker (which runs the pass). */
static int border_arm(int mode) {
    if(border_installed) return stats.active && border_mode==mode;
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread ||
       (mode!=BORDER_OBSERVE && mode!=BORDER_SORT)) return 0;
    if(!world_identity_installed || !created_enabled()) {border_stage="needs";return 0;}
    for(unsigned i=0;i<BORDER_CALLS;i++) if(border_call_state(i)!=0) {border_stage="site";return 0;}
    if(!border_pins_ok()) {border_stage="pins";return 0;}
    if(!border_ring) border_ring=VirtualAlloc(NULL,sizeof(*border_ring)*BORDER_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!border_stubs) border_stubs=border_alloc_near(BORDER_STUB_SIZE);
    if(!border_ring || !border_stubs) {border_stage="memory";return 0;}
    if(!border_write_stubs()) {border_stage="stubs";return 0;}
    border_rows=border_failures=0;border_passes=border_native=border_entries=border_moved=border_unordered=border_overflow=border_reenqueued=0;
    border_tick=UINT64_MAX;border_count=0;border_in_pass=0;border_restored=0;border_mode=mode;
    for(unsigned i=BORDER_CALLS;i-->0;) { /* the walk's calls first: they pass through until the pass call is ours */
        if(!border_cas(i,border_rel_to(i,image+border_calls[i].target),border_rel_to(i,border_stub(i)))) {border_unpatch();border_stage="cas";return 0;}
        border_done|=1u<<i;
    }
    border_installed=1;border_stage="armed";return 1;
}
static int border_owned(void) {
    if(!border_installed) return 1;
    for(unsigned i=0;i<BORDER_CALLS;i++) if((border_done&(1u<<i)) && border_call_state(i)!=1) return 0;
    return border_stubs_owned();
}
/* Main-thread tick check; any failure stops the gate with reason 134. */
static int border_check(void) {
    if(InterlockedCompareExchange64(&border_failures,0,0)) return 0;
    const char *why=!border_owned()?"site_changed":!border_pins_ok()?"block_changed":NULL;
    return why?border_fail(why):1;
}
static void border_restore(void) {
    if(!border_installed) return;
    if(border_unpatch()) {border_installed=0;border_restored=1;}
}
static int border_row_text(uint64_t index,int final,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&border_rows,0,0);int n;
    if(!border_ring || (LONG64)index>=count) return 0;
    if(count>(LONG64)BORDER_RING && index<(uint64_t)(count-(LONG64)BORDER_RING)) return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    border_row r=border_ring[index%BORDER_RING];
    if(!final && r.tick>=stats.issued) return 0;
    if(!r.key && r.runtime) {uint64_t created=created_lookup(r.runtime);if(created) {r.key=created;r.klass=WORLD_KEY_CREATED;}}
    static const char *const kinds[4]={"saved","created","frame","unordered"};
    n=snprintf(out,cap,"index=%llu tick=%llu mode=%s count=%u pos=%u order=%u kind=%s klass=%u key=%016llx drop=%u create=%u",
        (unsigned long long)index,(unsigned long long)r.tick,r.mode==BORDER_SORT?"sort":"observe",r.count,r.pos,r.order,kinds[r.kind&3],
        r.klass,(unsigned long long)r.key,r.remove,r.create);
    return n>0 && (size_t)n<cap?n:0;
}
static int border_status(char *out,size_t cap) {
    int n=snprintf(out,cap,"mode=%s installed=%d frame=%llu last_tick=%llu passes=%llu entries=%llu moved=%llu kinds=%llu/%llu/%llu/%llu total_passes=%llu "
        "total_entries=%llu total_moved=%llu unordered=%llu native=%llu overflow=%llu reenqueued=%llu rows=%lld restored=%d stage=%s failures=%lld",
        border_mode==BORDER_SORT?"sort":border_mode==BORDER_OBSERVE?"observe":"off",border_installed,(unsigned long long)stats.issued,
        (unsigned long long)(border_tick==UINT64_MAX?0:border_tick),(unsigned long long)border_frame_passes,(unsigned long long)border_frame_entries,
        (unsigned long long)border_frame_moved,(unsigned long long)border_frame_kinds[0],(unsigned long long)border_frame_kinds[1],
        (unsigned long long)border_frame_kinds[2],(unsigned long long)border_frame_kinds[3],(unsigned long long)border_passes,
        (unsigned long long)border_entries,(unsigned long long)border_moved,(unsigned long long)border_unordered,(unsigned long long)border_native,
        (unsigned long long)border_overflow,(unsigned long long)border_reenqueued,(long long)border_rows,border_restored,border_stage,(long long)border_failures);
    return n>0 && (size_t)n<cap?n:0;
}
