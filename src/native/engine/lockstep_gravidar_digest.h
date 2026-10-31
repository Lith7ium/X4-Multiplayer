/* Read-only census diagnostic, DEFAULT OFF (native_gate_gravidar_digest).
 * long-L8/sc-control: at tick 2 station 0x29bfd's has_scanned handler counts an
 * enemy contact on one client only, with every tick-1 world capture, Gravidar
 * unit count and event identical; sc-missions3: 9fb3b gains 25 contacts on one
 * client at tick 643. Inputs to contact visibility that the world hash does not
 * cover, per Gravidar unit (table at *0x6cf1650: 256 buckets, lock
 * +0x88+b*0x28, tree +0x2890+b*0x40 with root at +8, children +8/+0x10, key
 * +0x20, unit +0x28; as AI::Gravidar lookup 0x10b2dc0 reads it):
 *   - the radar factor +0x44, set by SetObjectFactor 0x10b30c0 from the
 *     sectors' periodic factor pass (U::Sector::Move 0x784230: region
 *     interference at the object's position; next pass at sector+0x448 =
 *     now + 1 s at attention >= 5, else now + 5 s + an unkeyed draw, see
 *     lockstep_gravidar_deadlines.h) and from Object::SetParent 0x6cdab0;
 *   - the scan times +0xa8/+0xb0/+0xb8 (last scan, next due, last has_scanned
 *     event) of an AI::ActiveGravidarUnit (vtable 0x2c67610, 0xc8 bytes, built
 *     by 0x10b2820). A passive AI::GravidarUnit (vtable 0x2c67660) is only 0x68
 *     bytes (0x10b2aa0) and has no scan times: long-L9's times digest read 24
 *     bytes of the neighbouring heap block for each of them and parted at tick
 *     4 with every other part equal, so times now cover active units only;
 *   - the unit class (vtable: passive or active unit);
 * the Gravidar's active-source lists (+0x48 pending, +0x60 current: the
 * components GravidarListener::OnChangedAttention 0x10ad730 adds while their
 * attention is above 7), and per sector of the Gravidar's sector list
 * (+0x68b8, 32-byte entries, [0] = sector) that holds a Gravidar field
 * (+0x442): its factor-pass deadline +0x448 and its attention +0x1ec. Every
 * admitted tick (slot 6, the pass already joined) this sums one hash per unit,
 * active source and field sector keyed by its portable ID, so the result does
 * not depend on table order: digests that part between clients name the first
 * tick and the kind of input that differs. Units, sources and sectors without
 * a portable key are counted only. Nothing is written.
 * Rows (native_gate_gravidar_digest_rows = N, default none): the digest keeps
 * the last state of every keyed unit and field sector (bounded table) and
 * logs a GRAVIDAR_DIGEST_ROW with the raw values for
 *   - a unit whose factor, class or scan times changed, during the first N
 *     ticks; afterwards only when its factor changed. A unit seen for the first
 *     time in its constructor state (factor 1.0, times -1) gives no row;
 *   - a field sector seen for the first time, and every change of its deadline
 *     or attention (every tick).
 * Joined by (tick, key) across clients, the first differing row names the
 * unit or sector and its values on each client. */
#define GDIG_WORKER 0x6cf1650u
#define GDIG_VTABLE 0x2c68860u
#define GDIG_ACTIVE 0x2c67610u /* AI::ActiveGravidarUnit::vftable */
#define GDIG_SECTOR 0x2b1ea48u /* U::Sector::vftable */
#define GDIG_BUCKETS 256u
#define GDIG_NODE_CAP 200000u
#define GDIG_SECTOR_CAP 4096u
#define GDIG_TABLE 131072u
#define GDIG_RING 32768u
#define GDIG_ROW_TICKS_MAX 1000000u
enum {GDIG_ROW_ACTIVE=0,GDIG_ROW_PASSIVE=1,GDIG_ROW_SECTOR=2};
typedef struct {uint64_t key,times;uint32_t factor,extra;uint8_t kind;} gdig_last_t;
typedef struct {uint64_t tick,key,v[3];uint32_t factor,extra;uint8_t kind,klass,first;} gdig_row_t;
static int gdig_armed;
static unsigned gdig_row_ticks;
static uint64_t gdig_tick=UINT64_MAX,gdig_units,gdig_unmapped,gdig_factors,gdig_times,gdig_classes,gdig_active[2],gdig_active_digest[2],gdig_active_unmapped[2];
static uint64_t gdig_passive,gdig_sectors,gdig_sector_unmapped,gdig_deadlines,gdig_attention;
static uint64_t gdig_table_count,gdig_table_full,gdig_row_drop;
static gdig_last_t *gdig_table; /* allocated by the first arming with rows, kept */
static gdig_row_t *gdig_ring;
static volatile LONG64 gdig_rows;
static const char *gdig_stage="idle";
static const double gdig_unset=-1.0;
static uint64_t gdig_mix(uint64_t x) {x^=x>>33;x*=0xff51afd7ed558ccdULL;x^=x>>33;x*=0xc4ceb9fe1a85ec53ULL;x^=x>>33;return x;}
static uint64_t gdig_key(const void *component,unsigned *klass) {
    uint64_t runtime=0;*klass=WORLD_KEY_NONE;
    return component && read_local((const unsigned char*)component+8,&runtime,8) && runtime?world_identity_key(runtime,klass):0;
}
/* The key's slot in the last-state table, or NULL when the table is full. */
static gdig_last_t *gdig_slot(uint64_t key,int *fresh) {
    unsigned h=(unsigned)(gdig_mix(key)&(GDIG_TABLE-1));*fresh=0;
    for(unsigned n=0;n<GDIG_TABLE;n++,h=(h+1)&(GDIG_TABLE-1)) {
        if(gdig_table[h].key==key) return &gdig_table[h];
        if(!gdig_table[h].key) {
            if(gdig_table_count>=GDIG_TABLE/4*3) {++gdig_table_full;return NULL;}
            gdig_table[h].key=key;++gdig_table_count;*fresh=1;return &gdig_table[h];
        }
    }
    ++gdig_table_full;return NULL;
}
static void gdig_push(unsigned kind,unsigned klass,uint64_t key,uint32_t factor,uint32_t extra,const uint64_t v[3],int first) {
    LONG64 index=InterlockedIncrement64(&gdig_rows)-1;gdig_row_t *r=&gdig_ring[(uint64_t)index%GDIG_RING];
    r->tick=gdig_tick;r->key=key;memcpy(r->v,v,24);r->factor=factor;r->extra=extra;
    r->kind=(uint8_t)kind;r->klass=(uint8_t)klass;r->first=(uint8_t)first;
}
/* Rows of one keyed unit or field sector against its last state. */
static void gdig_track(unsigned kind,unsigned klass,uint64_t key,uint32_t factor,uint32_t extra,const uint64_t v[3]) {
    if(!gdig_row_ticks || !gdig_table || !gdig_ring) return;
    int fresh;gdig_last_t *last=gdig_slot(key,&fresh);
    uint64_t times=gdig_mix(v[0]^gdig_mix(v[1]^gdig_mix(v[2])));
    if(!last) {++gdig_row_drop;return;}
    if(fresh) {
        last->times=times;last->factor=factor;last->extra=extra;last->kind=(uint8_t)kind;
        uint64_t unset;memcpy(&unset,&gdig_unset,8);float one=1.0f;uint32_t one_bits;memcpy(&one_bits,&one,4);
        int pristine=kind!=GDIG_ROW_SECTOR && factor==one_bits && (kind==GDIG_ROW_PASSIVE || (v[0]==unset && v[1]==unset && v[2]==unset));
        if(!pristine) gdig_push(kind,klass,key,factor,extra,v,1);
        return;
    }
    int factor_changed=last->factor!=factor,other_changed=last->times!=times || last->extra!=extra || last->kind!=kind;
    last->times=times;last->factor=factor;last->extra=extra;last->kind=(uint8_t)kind;
    if(!factor_changed && !other_changed) return;
    if(kind==GDIG_ROW_SECTOR || factor_changed || gdig_tick<=gdig_row_ticks) gdig_push(kind,klass,key,factor,extra,v,0);
}
static int gdig_unit(unsigned char *unit) {
    unsigned klass;uint64_t key=gdig_key(unit,&klass);uint32_t factor=0;uint64_t times[3]={0,0,0};void *vtable=NULL;
    if(!key) {++gdig_unmapped;return 1;}
    read_local(unit+0x44,&factor,4);read_local(unit,&vtable,8);
    int active=vtable==(void*)(image+GDIG_ACTIVE);
    uint32_t rva=(unsigned char*)vtable>=image && (unsigned char*)vtable<image+0x6fa9000?(uint32_t)((unsigned char*)vtable-image):0xffffffffu;
    gdig_factors+=gdig_mix(key^gdig_mix(factor));
    if(active) {
        read_local(unit+0xa8,times,24);
        gdig_times+=gdig_mix(key^gdig_mix(times[0]^gdig_mix(times[1]^gdig_mix(times[2]))));
    } else ++gdig_passive;
    gdig_classes+=gdig_mix(key^gdig_mix(rva));++gdig_units;
    gdig_track(active?GDIG_ROW_ACTIVE:GDIG_ROW_PASSIVE,klass,key,factor,rva,times);
    return 1;
}
/* In-order walk of one bucket's tree; visit returning 0 stops it (also used by lockstep_gravidar_deadlines.h). */
static int gdig_tree(const unsigned char *head,int (*visit)(unsigned char *)) {
    const unsigned char *stack[64],*node=NULL;unsigned depth=0,count=0;
    if(!read_local(head+8,&node,8)) return 0;
    while(node || depth) {
        while(node) {
            if(depth==64 || ++count>GDIG_NODE_CAP) return 0;
            stack[depth++]=node;if(!read_local(node+8,&node,8)) return 0;
        }
        node=stack[--depth];
        const unsigned char *unit=NULL;if(!read_local(node+0x28,&unit,8)) return 0;
        if(unit && !visit((unsigned char*)unit)) return 0;
        if(!read_local(node+0x10,&node,8)) return 0;
    }
    return 1;
}
static int gdig_vector(const unsigned char *at,unsigned which) {
    uintptr_t bounds[2];
    if(!read_local(at,bounds,sizeof(bounds)) || bounds[1]<bounds[0] || ((bounds[1]-bounds[0])&7) || (bounds[1]-bounds[0])/8>GDIG_NODE_CAP) return 0;
    for(uintptr_t p=bounds[0];p<bounds[1];p+=8) {
        const void *component=NULL;if(!read_local((const void*)p,&component,8)) return 0;
        unsigned klass;uint64_t key=gdig_key(component,&klass);
        if(!key) {++gdig_active_unmapped[which];continue;}
        gdig_active_digest[which]+=gdig_mix(key);++gdig_active[which];
    }
    return 1;
}
/* Field sectors of the Gravidar's sector list: deadline and attention. The
 * list is read without its lock; the pass that uses it is joined. */
static int gdig_sector_list(const unsigned char *worker) {
    uintptr_t bounds[2];
    if(!read_local(worker+0x68b8,bounds,sizeof(bounds)) || bounds[1]<bounds[0] || ((bounds[1]-bounds[0])&31) ||
       (!bounds[0] && bounds[1]) || (bounds[1]-bounds[0])/32>GDIG_SECTOR_CAP) return 0;
    for(uintptr_t p=bounds[0];p<bounds[1];p+=32) {
        const unsigned char *sector=NULL;void *vtable=NULL;unsigned char field=0;int32_t attention=0;uint64_t v[3]={0,0,0};
        if(!read_local((const void*)p,&sector,8)) return 0;
        if(!sector || !read_local(sector,&vtable,8) || vtable!=(void*)(image+GDIG_SECTOR)) continue;
        if(!read_local(sector+0x442,&field,1) || !read_local(sector+0x448,&v[0],8) || !read_local(sector+0x1ec,&attention,4)) return 0;
        if(!field) continue;
        unsigned klass;uint64_t key=gdig_key(sector,&klass);
        if(!key) {++gdig_sector_unmapped;continue;}
        gdig_deadlines+=gdig_mix(key^gdig_mix(v[0]));gdig_attention+=gdig_mix(key^gdig_mix((uint32_t)attention));++gdig_sectors;
        gdig_track(GDIG_ROW_SECTOR,klass,key,(uint32_t)attention,1,v);
    }
    return 1;
}
/* Slot 6 of an admitted frame: the Gravidar pass of this frame is joined. */
static int gdig_frame(void) {
    gdig_units=gdig_unmapped=gdig_factors=gdig_times=gdig_classes=gdig_passive=0;
    gdig_sectors=gdig_sector_unmapped=gdig_deadlines=gdig_attention=0;
    memset(gdig_active,0,sizeof(gdig_active));memset(gdig_active_digest,0,sizeof(gdig_active_digest));memset(gdig_active_unmapped,0,sizeof(gdig_active_unmapped));
    gdig_tick=stats.issued;
    unsigned char *worker=*(unsigned char*volatile*)(image+GDIG_WORKER);void *vtable=NULL;
    if(!worker || !read_local(worker,&vtable,8) || vtable!=image+GDIG_VTABLE) {gdig_stage="worker";return 0;}
    for(unsigned b=0;b<GDIG_BUCKETS;b++) {
        CRITICAL_SECTION *lock=(CRITICAL_SECTION*)(worker+0x88+b*0x28);int ok;
        EnterCriticalSection(lock);ok=gdig_tree(worker+0x2890+b*0x40,gdig_unit);LeaveCriticalSection(lock);
        if(!ok) {gdig_stage="tree";return 0;}
    }
    if(!gdig_vector(worker+0x48,0) || !gdig_vector(worker+0x60,1)) {gdig_stage="active";return 0;}
    if(!gdig_sector_list(worker)) {gdig_stage="sectors";return 0;}
    gdig_stage="ok";return 1;
}
/* rows: 0 none, else the number of ticks with every unit change. Repeating the
 * same arming (the loading-UI reload) is accepted, another value refused. */
static int gdig_arm(double rows) {
    if(!stats.active || GetCurrentThreadId()!=ui_thread || !isfinite(rows) || rows<0 || rows>GDIG_ROW_TICKS_MAX || floor(rows)!=rows) return 0;
    if(gdig_armed) return gdig_row_ticks==(unsigned)rows;
    if(stats.issued) return 0;
    if(rows && !gdig_table) gdig_table=VirtualAlloc(NULL,sizeof(*gdig_table)*GDIG_TABLE,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(rows && !gdig_ring) gdig_ring=VirtualAlloc(NULL,sizeof(*gdig_ring)*GDIG_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(rows && (!gdig_table || !gdig_ring)) return 0;
    if(gdig_table) memset(gdig_table,0,sizeof(*gdig_table)*GDIG_TABLE);
    gdig_table_count=gdig_table_full=gdig_row_drop=0;gdig_rows=0;
    gdig_row_ticks=(unsigned)rows;gdig_armed=1;return 1;
}
static int gdig_status(char *out,size_t capacity) {
    LONG64 rows=InterlockedCompareExchange64(&gdig_rows,0,0);
    int n=snprintf(out,capacity,"tick=%llu units=%llu unmapped=%llu factors=%016llx times=%016llx classes=%016llx active=%llu/%llu "
        "active_digest=%016llx/%016llx active_unmapped=%llu/%llu stage=%s passive=%llu sectors=%llu sector_unmapped=%llu "
        "deadlines=%016llx attention=%016llx rows=%lld row_ticks=%u table=%llu table_full=%llu row_drop=%llu",
        (unsigned long long)(gdig_tick==UINT64_MAX?0:gdig_tick),
        (unsigned long long)gdig_units,(unsigned long long)gdig_unmapped,(unsigned long long)gdig_factors,(unsigned long long)gdig_times,
        (unsigned long long)gdig_classes,(unsigned long long)gdig_active[0],(unsigned long long)gdig_active[1],
        (unsigned long long)gdig_active_digest[0],(unsigned long long)gdig_active_digest[1],
        (unsigned long long)gdig_active_unmapped[0],(unsigned long long)gdig_active_unmapped[1],gdig_stage,
        (unsigned long long)gdig_passive,(unsigned long long)gdig_sectors,(unsigned long long)gdig_sector_unmapped,
        (unsigned long long)gdig_deadlines,(unsigned long long)gdig_attention,(long long)rows,gdig_row_ticks,
        (unsigned long long)gdig_table_count,(unsigned long long)gdig_table_full,(unsigned long long)gdig_row_drop);
    return n>0 && (size_t)n<capacity?n:0;
}
/* One row: unit kind a (active: factor, class, the three scan times) or p
 * (passive: factor, class), or sector s (attention, deadline). Values are
 * exact bits; the key is s:saved or c:created; first=1 for a first sighting. */
static int gdig_row(uint64_t index,char *out,size_t capacity) {
    LONG64 cursor=InterlockedCompareExchange64(&gdig_rows,0,0);int n;
    if(!gdig_ring || (LONG64)index>=cursor) return 0;
    if(cursor>(LONG64)GDIG_RING && index<(uint64_t)(cursor-(LONG64)GDIG_RING))
        return (n=snprintf(out,capacity,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<capacity?n:0;
    const gdig_row_t *r=&gdig_ring[index%GDIG_RING];char klass=r->klass==WORLD_KEY_CREATED?'c':'s';
    if(r->kind==GDIG_ROW_SECTOR)
        n=snprintf(out,capacity,"index=%llu tick=%llu kind=s key=%c:%016llx first=%u attention=%d due=%016llx",
            (unsigned long long)index,(unsigned long long)r->tick,klass,(unsigned long long)r->key,r->first,(int32_t)r->factor,
            (unsigned long long)r->v[0]);
    else if(r->kind==GDIG_ROW_PASSIVE)
        n=snprintf(out,capacity,"index=%llu tick=%llu kind=p key=%c:%016llx first=%u factor=%08x class=%08x",
            (unsigned long long)index,(unsigned long long)r->tick,klass,(unsigned long long)r->key,r->first,r->factor,r->extra);
    else
        n=snprintf(out,capacity,"index=%llu tick=%llu kind=a key=%c:%016llx first=%u factor=%08x class=%08x scan=%016llx due=%016llx event=%016llx",
            (unsigned long long)index,(unsigned long long)r->tick,klass,(unsigned long long)r->key,r->first,r->factor,r->extra,
            (unsigned long long)r->v[0],(unsigned long long)r->v[1],(unsigned long long)r->v[2]);
    return n>0 && (size_t)n<capacity?n:0;
}
