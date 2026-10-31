/* Diagnostic, DEFAULT OFF: native_gate_build_rows = true. Rows of who gets built, in the order it is decided.
 *
 * play29-long parts at tick 16127 on a choice between waiting job ships: <jobs><job><waiting><ship>, ships
 * without a parent, and at 16200 client 1 has saved ship 1244b1 in docking bay 1c441 where client 2 has created
 * ship 800b79cd... After 270 activations that agreed, pairs of ships then swap docks at one capture. The way there:
 *   md/job_helper.xml: find_waiting_subordinate + find_waiting_job_ship -> $ShipsToBuild, shuffle_list
 *     (one action, 0xbbef00: the job's vector, [job+0x528]+0x30, in insertion order; find_order does not see it);
 *   aiscripts/build.shiptrader.xml: add_build_to_construct_ship, a task per ship (0x3a92c0), queued on
 *     [container+0xa68] (0x3ae820: a FIFO of task IDs, only the player's tasks go ahead of others);
 *   the same script walks builds.queued and starts what the station's resources allow: process_build (0x393990).
 * The list could differ, the adds could come in another order, or the start could skip other builds because a
 * shipyard's resources differ. One row per step tells which, on a run that parts:
 *   kind=find:  the script, the list as native built it (n, order-sensitive and order-free hashes, the first IDs),
 *               the RNG word at that point (the shuffle's input);
 *   kind=add:   the task, the ship it builds;
 *   kind=start: the task, the processor, whether it started.
 * IDs are portable; one without a portable ID is its runtime ID with bit 62 set.
 * census_runtime_callers.py's neighbour census_build_rows.py finds the first row that differs.
 *
 * It only watches. The three calls go through stubs and do what they did; a site that changes loses its rows and
 * the status says so, it never stops the gate. Restored at stop. */
#define BR_SITES 3u
#define BR_RING 8192u
#define BR_IDS 64u
#define BR_STUB 0x20u
#define BR_LOCAL_BIT 0x4000000000000000ULL
enum {BR_SITE_FIND=0,BR_SITE_ADD=1,BR_SITE_START=2};
enum {BR_KIND_FIND=0,BR_KIND_ADD=1,BR_KIND_START=2};
static const struct {uint32_t site,target;const char *sha,*name;} br_sites[BR_SITES]={
    {0xbbf00du,0xd2ab50u,"ef8c5d7a79d77626971b49c970d4409d27c4ed803b07a1a40e96b24f599aebb9","find"},
    {0xb947fcu,0x3a92c0u,"3190ed527fa07fe2177364e648912253a7373ffe617a8af725566c3e45e66475","add"},
    {0xb9b471u,0x393990u,"36ad6475c042f3939c00869b1cb95f51c3a4c9eadeaee499a5b568f00742170d","start"},
};
typedef struct {
    volatile LONG64 ready;uint64_t tick,a,b,rng,set,order;
    uint32_t kind,n,shown,ok;char scope[64];uint64_t ids[BR_IDS];
} br_row_t;
typedef uintptr_t (*br_find_fn)(void *,void *,void *,void *);
typedef void *(*br_add_fn)(void *,void *,void *,void *,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef unsigned char (*br_start_fn)(void *,uint64_t,unsigned char);
typedef uint64_t (*br_portable_fn)(uint64_t);
static int br_installed,br_restored;
static unsigned br_done;
static unsigned char *br_page;
static br_row_t *br_ring;
static volatile LONG64 br_rows;
static uint64_t br_counts[3],br_unreadable;
static const char *br_stage="idle";
static br_portable_fn br_portable; /* tests substitute */
static uint64_t br_id(uint64_t runtime) {
    if(!runtime) return 0;
    uint64_t id=br_portable?br_portable(runtime):world_identity_portable(runtime);
    return id?id:(runtime|BR_LOCAL_BIT);
}
static br_row_t *br_begin(unsigned kind) {
    if(!br_ring) return NULL;
    LONG64 index=InterlockedIncrement64(&br_rows)-1;
    br_row_t *r=&br_ring[(uint64_t)index%BR_RING];
    InterlockedExchange64(&r->ready,-1);
    r->tick=stats.issued;r->kind=kind;r->a=r->b=r->rng=r->set=r->order=0;r->n=r->shown=r->ok=0;r->scope[0]=0;
    if(kind<3) ++br_counts[kind];
    r->set=(uint64_t)index; /* until br_end */
    return r;
}
static void br_end(br_row_t *r,LONG64 index) {InterlockedExchange64(&r->ready,index);}
static int br_watching(void) {return stats.active && boot_phase==3;}
static uint64_t br_rng_word(void) {uint64_t *rng=interrupt_rng();return rng?*rng:0;}
/* The list as native built it, before the store that hands it to the script. */
static void br_note_find(const void *vector) {
    if(!br_watching()) return;
    uintptr_t head[3];
    if(!vector || !read_local(vector,head,sizeof(head)) || head[1]<head[0] || ((head[1]-head[0])&7)) {++br_unreadable;return;}
    br_row_t *r=br_begin(BR_KIND_FIND);if(!r) return;
    LONG64 index=(LONG64)r->set;r->set=0;
    uint64_t count=(head[1]-head[0])/8,order=0x58344c4255494c44ULL; /* "X4LBUILD" */
    r->n=(uint32_t)count;r->rng=br_rng_word();
    for(uint64_t i=0;i<count;i++) {
        uintptr_t p=0;uint64_t runtime=0,id=0;
        if(read_local((const void*)(head[0]+i*8),&p,8) && p && read_local((const void*)(p+8),&runtime,8)) id=br_id(runtime);
        else ++br_unreadable;
        r->set+=lockstep_mix(id);order=lockstep_mix(order^id);
        if(i<BR_IDS) r->ids[r->shown++]=id;
    }
    r->order=order;
    const script_rng_frame *f=script_rng_current,*o=f?f->owner:NULL;
    const char *name=o?o->id.name:(f?f->id.name:NULL);
    if(name) {size_t n=0;while(n<sizeof(r->scope)-1 && name[n]) {r->scope[n]=name[n];++n;}r->scope[n]=0;}
    br_end(r,index);
}
/* The constructed task: +8 its ID, +0x20 the runtime ID of what it builds. */
static void br_note_add(const unsigned char *task) {
    if(!br_watching()) return;
    uint64_t id=0,built=0;
    if(!task || !read_local(task+8,&id,8) || !read_local(task+0x20,&built,8)) {++br_unreadable;return;}
    br_row_t *r=br_begin(BR_KIND_ADD);if(!r) return;
    LONG64 index=(LONG64)r->set;r->set=0;
    r->a=id;r->b=br_id(built);r->rng=br_rng_word();
    br_end(r,index);
}
static void br_note_start(const unsigned char *processor,uint64_t task,unsigned char ok) {
    if(!br_watching()) return;
    uint64_t runtime=0;
    if(processor && !read_local(processor+8,&runtime,8)) ++br_unreadable;
    br_row_t *r=br_begin(BR_KIND_START);if(!r) return;
    LONG64 index=(LONG64)r->set;r->set=0;
    r->a=task;r->b=br_id(runtime);r->ok=ok;
    br_end(r,index);
}
static __attribute__((noinline)) uintptr_t br_find_entry(void *declaration,void *vector,void *exec,void *component) {
    br_note_find(vector);
    __attribute__((musttail)) return ((br_find_fn)(image+br_sites[BR_SITE_FIND].target))(declaration,vector,exec,component);
}
/* Ten stack slots pass through as they are: the constructor's own arguments, then what lies above them in the caller's frame. */
static void *br_add_entry(void *task,void *b,void *c,void *d,uint64_t e,uint64_t f,uint64_t g,uint64_t h,uint64_t i,uint64_t j,uint64_t k,uint64_t l,uint64_t m,uint64_t n) {
    void *result=((br_add_fn)(image+br_sites[BR_SITE_ADD].target))(task,b,c,d,e,f,g,h,i,j,k,l,m,n);
    br_note_add((const unsigned char*)result);
    return result;
}
static unsigned char br_start_entry(void *processor,uint64_t task,unsigned char flag) {
    unsigned char ok=((br_start_fn)(image+br_sites[BR_SITE_START].target))(processor,task,flag);
    br_note_start((const unsigned char*)processor,task,ok);
    return ok;
}
static void *const br_entries[BR_SITES]={(void*)br_find_entry,(void*)br_add_entry,(void*)br_start_entry};
static void br_write_stub(unsigned char *p,unsigned i) {
    static const unsigned char jump[6]={0xff,0x25,0,0,0,0};
    memset(p,0xcc,BR_STUB);memcpy(p,jump,sizeof(jump));memcpy(p+sizeof(jump),&br_entries[i],8);
}
static int32_t br_rel(unsigned i,const void *target) {return (int32_t)((intptr_t)target-(intptr_t)(image+br_sites[i].site+5));}
static int br_state(unsigned i) {
    unsigned char now[5];if(!read_local(image+br_sites[i].site,now,5) || now[0]!=0xe8) return -1;
    int32_t d;memcpy(&d,now+1,4);
    if(d==br_rel(i,image+br_sites[i].target)) return 0;
    if(br_page && d==br_rel(i,br_page+BR_STUB*i)) return 1;
    return -1;
}
static int br_pins(void) {
    for(unsigned i=0;i<BR_SITES;i++) {
        unsigned char copy[0x20];
        if(br_state(i)<0 || !read_local(image+br_sites[i].site-0x10,copy,sizeof(copy))) return 0;
        int32_t d=br_rel(i,image+br_sites[i].target);memcpy(copy+0x11,&d,4);
        if(!hash_ok(copy,sizeof(copy),br_sites[i].sha)) return 0;
    }
    return 1;
}
static unsigned char *br_alloc_page(void) {
    uintptr_t at=(uintptr_t)(image+0x900000u)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            int ok=1;
            for(unsigned i=0;i<BR_SITES && ok;i++) {intptr_t d=(intptr_t)p-(intptr_t)(image+br_sites[i].site+5);ok=d>INT32_MIN+0x1000 && d<INT32_MAX-0x1000;}
            if(ok) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static int br_cas(unsigned i,int32_t expected,int32_t value) {
    unsigned char e[4],v[4];memcpy(e,&expected,4);memcpy(v,&value,4);return view_cas_bytes(br_sites[i].site+1,e,v,4);
}
static void br_restore(void) {
    if(!br_installed) return;
    for(unsigned i=0;i<BR_SITES;i++) {
        if(!((br_done>>i)&1u) || br_state(i)!=1) continue;
        if(br_cas(i,br_rel(i,br_page+BR_STUB*i),br_rel(i,image+br_sites[i].target))) br_done&=~(1u<<i);
    }
    if(!br_done) {br_installed=0;br_restored=1;}
}
/* Before the load, on the MoveWorker; a rerun is accepted. */
static int br_arm(void) {
    if(br_installed) return stats.active!=0;
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    for(unsigned i=0;i<BR_SITES;i++) if(br_state(i)!=0) {br_stage="pins";return 0;}
    if(!br_pins()) {br_stage="pins";return 0;}
    if(!br_ring) br_ring=VirtualAlloc(NULL,sizeof(br_row_t)*BR_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!br_page) br_page=br_alloc_page();
    if(!br_ring || !br_page) {br_stage="page";return 0;}
    DWORD old,ignored;if(!VirtualProtect(br_page,0x1000,PAGE_READWRITE,&old)) {br_stage="page";return 0;}
    for(unsigned i=0;i<BR_SITES;i++) br_write_stub(br_page+BR_STUB*i,i);
    if(!VirtualProtect(br_page,0x1000,PAGE_EXECUTE_READ,&ignored)) {br_stage="page";return 0;}
    FlushInstructionCache(GetCurrentProcess(),br_page,0x1000);
    br_rows=0;br_restored=0;br_installed=1;
    for(unsigned i=0;i<BR_SITES;i++) {
        if(!br_cas(i,br_rel(i,image+br_sites[i].target),br_rel(i,br_page+BR_STUB*i))) {
            br_stage="cas";
            for(unsigned k=0;k<BR_SITES;k++) if((br_done>>k)&1u && br_cas(k,br_rel(k,br_page+BR_STUB*k),br_rel(k,image+br_sites[k].target))) br_done&=~(1u<<k);
            br_installed=0;return 0;
        }
        br_done|=1u<<i;
    }
    br_stage="armed";return 1;
}
static int br_owned(void) {
    if(!br_installed) return 1;
    for(unsigned i=0;i<BR_SITES;i++) if((br_done>>i)&1u) {
        unsigned char want[BR_STUB];br_write_stub(want,i);
        if(br_state(i)!=1 || memcmp(br_page+BR_STUB*i,want,BR_STUB)) return 0;
    }
    return 1;
}
static int br_row(uint64_t index,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&br_rows,0,0);int n=0;
    if(!br_ring || (LONG64)index>=count) return 0;
    if(count>(LONG64)BR_RING && index<(uint64_t)(count-(LONG64)BR_RING))
        return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    const br_row_t *r=&br_ring[index%BR_RING];
    if(InterlockedCompareExchange64((volatile LONG64*)&r->ready,0,0)!=(LONG64)index) return 0;
    if(r->kind==BR_KIND_FIND) {
        n=snprintf(out,cap,"index=%llu tick=%llu kind=find scope=%s n=%u set=%016llx order=%016llx rng=%016llx ids=",(unsigned long long)index,(unsigned long long)r->tick,
            r->scope[0]?r->scope:"-",r->n,(unsigned long long)r->set,(unsigned long long)r->order,(unsigned long long)r->rng);
        for(unsigned i=0;n>0 && (size_t)n<cap && i<r->shown;i++) n+=snprintf(out+n,cap-(size_t)n,"%s%llx",i?",":"",(unsigned long long)r->ids[i]);
        if(n>0 && (size_t)n<cap && !r->shown) n+=snprintf(out+n,cap-(size_t)n,"-");
    } else if(r->kind==BR_KIND_ADD)
        n=snprintf(out,cap,"index=%llu tick=%llu kind=add task=%llx ship=%llx rng=%016llx",(unsigned long long)index,(unsigned long long)r->tick,(unsigned long long)r->a,(unsigned long long)r->b,(unsigned long long)r->rng);
    else
        n=snprintf(out,cap,"index=%llu tick=%llu kind=start task=%llx processor=%llx ok=%u",(unsigned long long)index,(unsigned long long)r->tick,(unsigned long long)r->a,(unsigned long long)r->b,r->ok);
    return n>0 && (size_t)n<cap?n:0;
}
static int br_status(char *out,size_t cap) {
    int n=snprintf(out,cap,"installed=%d frame=%llu rows=%lld find=%llu add=%llu start=%llu unreadable=%llu owned=%d restored=%d stage=%s",
        br_installed,(unsigned long long)stats.issued,(long long)br_rows,(unsigned long long)br_counts[0],(unsigned long long)br_counts[1],(unsigned long long)br_counts[2],
        (unsigned long long)br_unreadable,br_owned(),br_restored,br_stage);
    return n>0 && (size_t)n<cap?n:0;
}
