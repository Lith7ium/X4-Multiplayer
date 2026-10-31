/* find_* candidate lists on saved IDs, DEFAULT OFF
 * (native_gate_find_order = 'observe' | 'sort').
 *
 * Census big-BW (tick 63) and W600a (tick 3): FactionSubgoal_Recon.Evaluate
 * runs under the same script key on both clients but makes different numbers
 * of draws (48/54; 34/32 and 30/31); in big-BW the next tick sends a different
 * ship on recon (6f93b vs 6f8fb) and motion diverges from tick 120. Evaluate
 * calls find_ship_by_true_owner (multiple, per space of $FindShipSpaces) and
 * shuffle_list on the result (ShuffleListAction 0xbe63d0: Fisher-Yates on the
 * TLS PCG, N-1 draws for N entries), then commandeers the first entries. Same
 * seed, other draw count: some candidate list had another length; another
 * pick with the same lengths would need another order. W600a's size changes
 * go both ways and leave every pick equal; big-BW's changes the pick.
 *
 * Native path (X4 9.00): FindComponentAction (vtable 0x2c0f3c8; the ctor
 * 0xb87510 maps find_ship 7/8, find_ship_by_true_owner 9, find_station 10,
 * find_station_by_true_owner 11, ...) Execute 0xb87a30 builds a vector
 * {begin,end,cap} of Component* with 0xc5c3c0: per space of space= it walks
 * the sub-space index (space+0xa8, 0x5c20c0/0x480ab0) and per zone the owner
 * bucket of zone+0xb8 (0x3b9590 -> 0x48c970 -> 0x4a77d0/0x49acc0: pointer
 * vectors with holes, chained per key). 0xc5f680 filters (distance), then the
 * call at 0xb87ae3 to 0xd2ab50 stores the whole list (multiple) or one element
 * picked with the TLS PCG. Nothing orders candidates by a portable key: the
 * order is container insertion history, and nothing removes a component seen
 * twice. Which of order or membership differs between the clients is not yet
 * established; the rows below decide it.
 *
 * Hook: that E8 rel32 (4-byte CAS inside one cache line) goes through
 * THUNK_JUMP 4080 to find_order_assign. 'sort' rewrites the vector in place:
 * a repeated pointer keeps its first position only; components with a saved
 * UniverseID (world identity map) come first in saved-ID order, components
 * created after the load follow in their native relative order (counted
 * unmapped: not yet portable). With native_gate_portable_created a component
 * with a created identity sorts between them, by created ID (counted created;
 * it enters set/native hashes and ids like a saved ID), so only components
 * without any portable identity keep their native relative order: order
 * (class, ID) of lockstep_world_identity.h. 'observe' computes the same without writing.
 * The wrapper then tail-jumps (musttail) to 0xd2ab50, so the native return
 * address stays 0xb87ae8, which the world values observer checks.
 * Rows (FIND_ORDER_ROW): tick, the enclosing script RNG owner key and name
 * (portable; 0 outside a keyed scope), a per-(tick, scope) ordinal, raw and
 * duplicate counts, unmapped count, an order-free hash of the saved IDs, the
 * hash of the native saved-ID sequence and up to 32 saved IDs in sorted order.
 * Failures (foreign return address, unreadable vector or component, oversize
 * list, two components with one saved ID) stop the gate with reason 92; stop
 * restores the call. */
/* The Linux gate (linux/gate_find_order.h) defines these for its executable, and its own hook, pins and swap. */
#ifndef FIND_ORDER_OWN_PLATFORM
#define FIND_ORDER_EXECUTE 0xb87a30u /* FindComponentAction::Execute */
#define FIND_ORDER_SITE 0xb87ae3u    /* E8 -> FIND_ORDER_ASSIGN */
#define FIND_ORDER_ASSIGN 0xd2ab50u  /* result store: (declaration, vector*, exec, component) */
#define FIND_ORDER_THUNK 4080u
#endif
#define FIND_ORDER_MAX 16384u
#define FIND_ORDER_SCOPES 1024u
#define FIND_ORDER_RING 4096u
#define FIND_ORDER_IDS 32u
enum {FIND_ORDER_OFF=0,FIND_ORDER_OBSERVE=1,FIND_ORDER_SORT=2};
typedef uintptr_t (*find_assign_fn)(void *,void *,void *,void *);
typedef uint64_t (*find_order_lookup_fn)(uint64_t,unsigned *); /* runtime -> portable key and WORLD_KEY_* class */
typedef struct {uint64_t saved;uintptr_t pointer;uint32_t index,klass;} find_order_entry; /* saved: portable key, 0 none */
typedef struct {uint64_t tick,scope;uint32_t count,used;} find_order_scope_t;
typedef struct {
    uint64_t tick,scope,ordinal,set,native,ids[FIND_ORDER_IDS];
    uint32_t raw,duplicates,unmapped,count,moved,shown;uint8_t mode;char name[64];
} find_order_row_t;
static volatile LONG find_order_active;
static int find_order_armed,find_order_installed,find_order_restored,find_order_mode;
static int32_t find_order_original_rel,find_order_patched_rel;
static find_order_entry find_order_work[FIND_ORDER_MAX];
static uintptr_t find_order_raw[FIND_ORDER_MAX];
static struct {uintptr_t pointer;uint32_t stamp;} find_order_seen[2*FIND_ORDER_MAX];
static uint32_t find_order_stamp;
static find_order_scope_t find_order_scopes[FIND_ORDER_SCOPES];
static uint64_t find_order_scopes_tick=UINT64_MAX;
static find_order_row_t find_order_ring[FIND_ORDER_RING];
static volatile LONG64 find_order_calls,find_order_applied,find_order_observed,find_order_passed,find_order_threads,find_order_empty,
    find_order_candidates,find_order_duplicates,find_order_unmapped,find_order_reordered,find_order_collisions,find_order_oversize,
    find_order_unreadable,find_order_foreign,find_order_cursor,find_order_failures,find_order_created;

static int find_order_entry_less(const find_order_entry *a,const find_order_entry *b) {
    if(a->klass!=b->klass) return a->klass<b->klass; /* saved, then created, then none */
    if(a->klass!=WORLD_KEY_NONE && a->saved!=b->saved) return a->saved<b->saved;
    return a->index<b->index; /* unmapped: native relative order */
}
static int find_order_compare(const void *x,const void *y) {
    const find_order_entry *a=x,*b=y;return find_order_entry_less(a,b)?-1:find_order_entry_less(b,a)?1:0;
}
/* Per-(tick, scope) ordinal; 0 when the bounded table is exhausted. */
static uint64_t find_order_ordinal(uint64_t tick,uint64_t scope) {
    if(find_order_scopes_tick!=tick) {memset(find_order_scopes,0,sizeof(find_order_scopes));find_order_scopes_tick=tick;}
    unsigned first=(unsigned)(lockstep_mix(scope^lockstep_mix(tick))&(FIND_ORDER_SCOPES-1));
    for(unsigned n=0;n<64;n++) {
        find_order_scope_t *s=&find_order_scopes[(first+n)&(FIND_ORDER_SCOPES-1)];
        if(!s->used) {s->used=1;s->tick=tick;s->scope=scope;s->count=1;return 1;}
        if(s->scope==scope) return ++s->count;
    }
    return 0;
}
/* First occurrence of a pointer in this call; the table is cleared lazily by stamp. */
static int find_order_first(uintptr_t pointer,unsigned mask) {
    unsigned at=(unsigned)(lockstep_mix(pointer)&mask);
    for(unsigned n=0;n<=mask;n++) {
        unsigned i=(at+n)&mask;
        if(find_order_seen[i].stamp!=find_order_stamp) {find_order_seen[i].stamp=find_order_stamp;find_order_seen[i].pointer=pointer;return 1;}
        if(find_order_seen[i].pointer==pointer) return 0;
    }
    return 0;
}
static void find_order_record(uint64_t tick,uint64_t scope,const char *name,uint64_t ordinal,uint64_t set,uint64_t native,uint32_t raw,
                              uint32_t duplicates,uint32_t unmapped,uint32_t count,uint32_t moved,unsigned mode) {
    LONG64 index=InterlockedIncrement64(&find_order_cursor)-1;find_order_row_t *r=&find_order_ring[(uint64_t)index%FIND_ORDER_RING];
    memset(r,0,sizeof(*r));r->tick=tick;r->scope=scope;r->ordinal=ordinal;r->set=set;r->native=native;r->raw=raw;r->duplicates=duplicates;
    r->unmapped=unmapped;r->count=count;r->moved=moved;r->mode=(uint8_t)mode;
    r->shown=count<FIND_ORDER_IDS?count:FIND_ORDER_IDS;for(unsigned i=0;i<r->shown;i++) r->ids[i]=find_order_work[i].saved;
    if(name) {size_t n=strlen(name);if(n>=sizeof(r->name)) n=sizeof(r->name)-1;memcpy(r->name,name,n);}
}
/* Canonicalizes (sort) or measures (observe) one candidate vector. reader
 * reads foreign memory; lookup maps a runtime ID to its portable key and class
 * (world_identity_key: the saved ID, a created ID with portable keys on, else 0). */
static void find_order_process(uintptr_t back,void *vector,int (*reader)(const void *,void *,size_t),find_order_lookup_fn lookup) {
    InterlockedIncrement64(&find_order_calls);
    if(back!=(uintptr_t)(image+FIND_ORDER_SITE+5)) {InterlockedIncrement64(&find_order_foreign);InterlockedIncrement64(&find_order_failures);return;}
    if(!InterlockedCompareExchange(&find_order_active,0,0) || !stats.active || find_order_mode==FIND_ORDER_OFF) {InterlockedIncrement64(&find_order_passed);return;}
    if(GetCurrentThreadId()!=ui_thread) {InterlockedIncrement64(&find_order_threads);return;}
    uintptr_t head[3];
    if(!vector || !reader(vector,head,sizeof(head)) || head[1]<head[0] || head[2]<head[1] || ((head[1]-head[0])&7) || (head[0]&7)) {
        InterlockedIncrement64(&find_order_unreadable);InterlockedIncrement64(&find_order_failures);return;
    }
    uint64_t raw=(head[1]-head[0])/8;
    if(raw>FIND_ORDER_MAX) {InterlockedIncrement64(&find_order_oversize);InterlockedIncrement64(&find_order_failures);return;}
    if(raw && !reader((void*)head[0],find_order_raw,(size_t)raw*8)) {InterlockedIncrement64(&find_order_unreadable);InterlockedIncrement64(&find_order_failures);return;}
    unsigned mask=1;while(mask<2*raw) mask<<=1;mask-=1;
    if(!++find_order_stamp) {memset(find_order_seen,0,sizeof(find_order_seen));find_order_stamp=1;}
    uint32_t count=0,duplicates=0,unmapped=0,created=0;uint64_t set=0,native=0x58344c46494e444fULL; /* "X4LFINDO" */
    for(uint32_t i=0;i<(uint32_t)raw;i++) {
        uintptr_t p=find_order_raw[i];
        if(!find_order_first(p,mask)) {++duplicates;continue;}
        uint64_t runtime=0,saved=0;unsigned klass=WORLD_KEY_NONE;
        if(p && (!reader((unsigned char*)p+8,&runtime,8))) {InterlockedIncrement64(&find_order_unreadable);InterlockedIncrement64(&find_order_failures);return;}
        if(p && runtime) saved=lookup(runtime,&klass);
        if(!saved || klass>WORLD_KEY_NONE) klass=WORLD_KEY_NONE;
        find_order_entry *e=&find_order_work[count];e->saved=saved;e->pointer=p;e->index=count;e->klass=klass;
        if(!saved) ++unmapped;else {set+=lockstep_mix(saved);created+=klass==WORLD_KEY_CREATED;}
        native=lockstep_mix(native^saved);++count;
    }
    set+=(uint64_t)unmapped*0x9e3779b97f4a7c15ULL;
    if(count>1) {
        if(count<=32) {
            for(uint32_t i=1;i<count;i++) {
                find_order_entry v=find_order_work[i];uint32_t j=i;
                while(j && find_order_entry_less(&v,&find_order_work[j-1])) {find_order_work[j]=find_order_work[j-1];--j;}
                find_order_work[j]=v;
            }
        } else qsort(find_order_work,count,sizeof(*find_order_work),find_order_compare);
    }
    uint32_t moved=0;
    for(uint32_t i=0;i<count;i++) {
        if(find_order_work[i].index!=i) ++moved;
        if(i && find_order_work[i].klass!=WORLD_KEY_NONE && find_order_work[i].klass==find_order_work[i-1].klass &&
           find_order_work[i].saved==find_order_work[i-1].saved) {
            InterlockedIncrement64(&find_order_collisions);InterlockedIncrement64(&find_order_failures);return;
        }
    }
    if(!raw) InterlockedIncrement64(&find_order_empty);
    find_order_candidates+=raw;find_order_duplicates+=duplicates;find_order_unmapped+=unmapped;find_order_created+=created;
    if(moved || duplicates) ++find_order_reordered;
    if(find_order_mode==FIND_ORDER_SORT && (moved || duplicates)) {
        int ok=0;
        __try {
            uintptr_t *out=(uintptr_t*)head[0];for(uint32_t i=0;i<count;i++) out[i]=find_order_work[i].pointer;
            ((uintptr_t*)vector)[1]=head[0]+(uintptr_t)count*8;ok=1;
        } __except(EXCEPTION_EXECUTE_HANDLER) {ok=0;}
        if(!ok) {InterlockedIncrement64(&find_order_unreadable);InterlockedIncrement64(&find_order_failures);return;}
    }
    if(find_order_mode==FIND_ORDER_SORT) InterlockedIncrement64(&find_order_applied);else InterlockedIncrement64(&find_order_observed);
    const script_rng_frame *f=script_rng_current,*o=f?f->owner:NULL;
    uint64_t scope=o?o->key:0,tick=stats.issued,ordinal=find_order_ordinal(tick,scope);
    find_order_record(tick,scope,o?o->id.name:(f?f->id.name:NULL),ordinal,set,native,(uint32_t)raw,duplicates,unmapped,count,moved,(unsigned)find_order_mode);
}
#ifndef FIND_ORDER_OWN_PLATFORM
/* noinline: the return address is the native caller's (the thunk jumps); the
 * tail jump keeps it for the native store and any observer behind it. */
static __attribute__((noinline)) uintptr_t find_order_assign(void *declaration,void *vector,void *exec,void *component) {
    find_order_process((uintptr_t)__builtin_return_address(0),vector,read_local,world_identity_key);
    __attribute__((musttail)) return ((find_assign_fn)(image+FIND_ORDER_ASSIGN))(declaration,vector,exec,component);
}
/* Execute with the call site (the store's entry may carry another observer). */
static int find_order_pinned(void) {
    return hash_ok(image+FIND_ORDER_EXECUTE,275,"b0e683b728526d447edd027a564797983e766ca0ebba80981f857fa45f937a69");
}
/* The rel32 at site+1 sits inside one 64-byte line: one locked CAS. */
static int find_order_swap(int32_t expected,int32_t value) {
    unsigned char *rel=image+FIND_ORDER_SITE+1;DWORD old,ignored;
    if(((uintptr_t)rel&63)>60 || !VirtualProtect(rel,4,PAGE_EXECUTE_READWRITE,&old)) return 0;
    int32_t actual=(int32_t)InterlockedCompareExchange((volatile LONG*)rel,(LONG)value,(LONG)expected);
    VirtualProtect(rel,4,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+FIND_ORDER_SITE,5);
    return actual==expected;
}
#endif
static int find_order_prepare(void) {
    unsigned char call[5];int32_t rel;
    if(!read_local(image+FIND_ORDER_SITE,call,5) || call[0]!=0xe8) return 0;
    memcpy(&rel,call+1,4);if((uintptr_t)(image+FIND_ORDER_SITE+5)+(intptr_t)rel!=(uintptr_t)(image+FIND_ORDER_ASSIGN)) return 0;
    intptr_t distance=(intptr_t)(thunk+FIND_ORDER_THUNK)-(intptr_t)(image+FIND_ORDER_SITE+5);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    find_order_original_rel=rel;find_order_patched_rel=(int32_t)distance;return 1;
}
static int find_order_owned(void) {
    int32_t rel=0;if(!find_order_installed) return 1;
    memcpy(&rel,image+FIND_ORDER_SITE+1,4);return rel==find_order_patched_rel;
}
/* Armed once at boot, before load (idempotent on a loading-UI reload with the
 * same mode). Needs the world identity map: without it every entry is unmapped. */
static int find_order_arm(int mode) {
    if(mode!=FIND_ORDER_OBSERVE && mode!=FIND_ORDER_SORT) return 0;
    if(find_order_armed) return stats.active && find_order_installed && find_order_mode==mode;
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread || !thunk || !world_identity_installed ||
       !find_order_pinned() || !thunk_slot_ready(FIND_ORDER_THUNK) || !find_order_prepare() ||
       !find_order_swap(find_order_original_rel,find_order_patched_rel)) return 0;
    find_order_mode=mode;find_order_installed=1;find_order_armed=1;InterlockedExchange(&find_order_active,1);return 1;
}
static void find_order_restore(void) {
    if(!find_order_armed) return;
    InterlockedExchange(&find_order_active,0);
    if(find_order_installed && find_order_swap(find_order_patched_rel,find_order_original_rel)) find_order_installed=0;
    find_order_restored=!find_order_installed;
}
static int find_order_status(char *out,size_t cap) {
    LONG64 cursor=InterlockedCompareExchange64(&find_order_cursor,0,0);
    LONG64 dropped=cursor>(LONG64)FIND_ORDER_RING?cursor-(LONG64)FIND_ORDER_RING:0;
    int n=snprintf(out,cap,"armed=%d installed=%d active=%ld mode=%d restored=%d calls=%lld applied=%lld observed=%lld passed=%lld threads=%lld"
        " empty=%lld candidates=%lld duplicates=%lld unmapped=%lld reordered=%lld collisions=%lld oversize=%lld unreadable=%lld foreign=%lld"
        " records=%lld dropped=%lld ring=%u failures=%lld created=%lld",
        find_order_armed,find_order_installed,find_order_active,find_order_mode,find_order_restored,(long long)find_order_calls,
        (long long)find_order_applied,(long long)find_order_observed,(long long)find_order_passed,(long long)find_order_threads,
        (long long)find_order_empty,(long long)find_order_candidates,(long long)find_order_duplicates,(long long)find_order_unmapped,
        (long long)find_order_reordered,(long long)find_order_collisions,(long long)find_order_oversize,(long long)find_order_unreadable,
        (long long)find_order_foreign,(long long)cursor,(long long)dropped,FIND_ORDER_RING,(long long)find_order_failures,
        (long long)find_order_created);
    return n>0 && (size_t)n<cap?n:0;
}
/* One census row per call; evicted rows report evicted=1 so the probe keeps going. */
static int find_order_row(uint64_t index,char *out,size_t cap) {
    LONG64 cursor=InterlockedCompareExchange64(&find_order_cursor,0,0);
    if((LONG64)index>=cursor) return 0;
    int n;
    if(cursor>(LONG64)FIND_ORDER_RING && index<(uint64_t)(cursor-(LONG64)FIND_ORDER_RING))
        return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    const find_order_row_t *r=&find_order_ring[index%FIND_ORDER_RING];char name[sizeof(r->name)];
    memcpy(name,r->name,sizeof(name));name[sizeof(name)-1]=0;
    for(size_t i=0;name[i];i++) if((unsigned char)name[i]<=32 || (unsigned char)name[i]>126 || name[i]=='|' || name[i]==',') name[i]='_';
    n=snprintf(out,cap,"index=%llu tick=%llu scope=%016llx ord=%llu mode=%u raw=%u dups=%u unmapped=%u n=%u moved=%u set=%016llx native=%016llx name=%s ids=",
        (unsigned long long)index,(unsigned long long)r->tick,(unsigned long long)r->scope,(unsigned long long)r->ordinal,r->mode,r->raw,
        r->duplicates,r->unmapped,r->count,r->moved,(unsigned long long)r->set,(unsigned long long)r->native,name[0]?name:"-");
    if(n<=0 || (size_t)n>=cap) return 0;
    for(unsigned i=0;i<r->shown && i<FIND_ORDER_IDS;i++) {
        int k=snprintf(out+n,cap-(size_t)n,i?",%llx":"%llx",(unsigned long long)r->ids[i]);
        if(k<=0 || (size_t)k>=cap-(size_t)n) return 0;
        n+=k;
    }
    if(!r->shown) {if((size_t)n+1>=cap) return 0;out[n++]='-';out[n]=0;}
    return n;
}
