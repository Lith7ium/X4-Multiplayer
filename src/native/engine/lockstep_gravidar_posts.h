/* Census diagnostic, DEFAULT OFF, read-only: native_gate_gravidar_posts = true
 * (needs native_gate_gravidar_key_all: it rides that module's keyed unit
 * apply, one call in gravidar_key_unit, and adds no patch).
 * sc-missions59/63: fight.attack.object.station enters its 'Station Attacked'
 * handler on event_gravidar_has_scanned of its station (decoded from the
 * script RNG stack key: keyword 1286, the station's saved ID), and that event
 * fires on one client only: 59 at 981 (29bfd), 1039 (28956), 1108 (2251d),
 * 1215 (270d2); 63 at 1474 (23a8c), 1608 (223e6), 1612 (143b6). In 63 the
 * Gravidar digest parts first at 1474, on 23a8c's last-event time alone, with
 * its scan and due times equal on both clients: the scan result differed.
 * The apply, AI::ActiveGravidarUnit slot 7 0x10acd70, runs on the Gravidar
 * thread inside the sector scan 0x10b34c0 (0x10b5410 calls it per entry of a
 * group list with the entry as its result): it swaps the scan's primary set
 * into the unit (+0x68) and the old one into the entry (+0x10), both sorted by
 * runtime ID, and posts GravidarHasScannedEvent, setting +0xb8 to the scan
 * time +0xa8, when a contact was added or removed, or 10 s after its last post.
 * After every keyed apply that posted (+0xb8 == +0xa8) this module merges the
 * two sets and, for a change post, logs one GRAVIDAR_POST_ROW: tick, unit key,
 * set sizes, added and removed counts and the first GPOST_LIST of each as
 * portable keys (s:/c:) or, without one, u: runtime ID and class (vtable RVA,
 * found through the component lookup 0xced70). Timer posts are counted only.
 * The scan inputs come from the scan's own entries (0xc0 bytes, layout in
 * lockstep_gravidar_wide.h), alive until the sector's next scan: the scanning
 * unit's entry (the apply's result) gives its position (+0x70, in the
 * sector), range (+0xac), factor (+0xb0), size (+0xa8), visibility (+0xb4) and
 * flags (+0xb8..+0xbe as bits 0..6), and its group (z=index/groups in the
 * sector's address-ordered zone groups, n near or w wide list). Every listed
 * contact is looked up among the sector's entries: its position, distance d,
 * bound b = range * factor * (visibility + 1) + both sizes (the pair check
 * 0x10b4100 detects within b), visibility, size, flags and group, or z=- when
 * the scan did not gather it. Up to GPOST_LIST kept contacts with d >=
 * GPOST_EDGE * b follow as edge items. Floats are hex bits. Nothing is called
 * and nothing is written to the game.
 * sc-missions70a/b: the one-sided drops are stations losing ships that warped
 * into a new tempzone; a wide unit is applied before later groups are checked
 * against it (lockstep_gravidar_wide.h), so such a row shows the dropped ship
 * gathered, within its bound, in a near list of a group after the station's.
 * Joined by (tick, unit) across clients, a row on one client only names the
 * contacts that changed there. GRAVIDAR_POSTS per tick: posts, change and
 * timer posts, rows, dropped, unmapped contacts, scanners not found in their
 * sector (homeless), listed contacts not gathered (absent), and a cumulative
 * order-free digest of the change rows (portable parts only), so the first tick
 * where it differs is the first one-sided change. */
#define GPOST_RING 4096u
#define GPOST_LIST 8u
#define GPOST_SET_MAX 4096u
#define GPOST_TICK_ROWS 2048u
#define GPOST_ENTRIES_MAX 65536u /* entries walked per row */
#define GPOST_CHUNK 64u
#define GPOST_EDGE 0.97f
#define GPOST_FIND 0xced70u
#define GPOST_MAP 0x6cf1500u
#define GPOST_WORKER GWIDE_WORKER
enum {GPOST_FOUND=1};
typedef void *(*gpost_find_fn)(void *,uint64_t,uint32_t);
/* klass WORLD_KEY_*; type: vtable RVA of an unmapped contact; floats as bits; group in the sector, wide: the list. */
typedef struct {uint64_t key,runtime;uint32_t klass,type,pos[3],d,bound,vis,size,group;uint8_t flags,state,wide;} gpost_item_t;
typedef struct {
    uint64_t tick,unit,time;uint32_t now,old,added,removed,edges,pos[3],range,factor,size,vis,group,groups;uint8_t unit_klass,flags,state,wide;
    gpost_item_t plus[GPOST_LIST],minus[GPOST_LIST],edge[GPOST_LIST];
} gpost_row_t;
/* The scanning unit's inputs as floats (the pair bound). */
typedef struct {float pos[3],range,factor,size;uint64_t runtime;} gpost_scanner_t;
static gpost_row_t gpost_ring[GPOST_RING];
static uint64_t gpost_new[GPOST_SET_MAX],gpost_old[GPOST_SET_MAX];
static unsigned char gpost_chunk[GPOST_CHUNK*GWIDE_ENTRY];
static volatile LONG gpost_active,gpost_busy;
static int gpost_armed;
static volatile LONG64 gpost_rows;
static uint64_t gpost_posts,gpost_changes,gpost_timers,gpost_dropped,gpost_unmapped,gpost_failures,gpost_oversize,gpost_digest,gpost_tick,gpost_tick_rows;
static uint64_t gpost_homeless,gpost_absent;
static gpost_find_fn gpost_find_override; /* tests only */
static uint32_t gpost_bits(float f) {uint32_t b;memcpy(&b,&f,4);return b;}
static float gpost_float(uint32_t bits) {float f;memcpy(&f,&bits,4);return f;}
static int gpost_read_set(const unsigned char *vector,uint64_t *out,unsigned *count) {
    uintptr_t bounds[2];*count=0;
    if(!read_local(vector,bounds,16) || bounds[1]<bounds[0] || ((bounds[1]-bounds[0])&7)) return 0;
    uint64_t n=(bounds[1]-bounds[0])/8;
    if(n>GPOST_SET_MAX) {++gpost_oversize;return 0;}
    if(n && !read_local((void*)bounds[0],out,(size_t)n*8)) return 0;
    *count=(unsigned)n;return 1;
}
static int gpost_cmp(const void *a,const void *b) {uint64_t x=*(const uint64_t*)a,y=*(const uint64_t*)b;return x<y?-1:x>y;}
static void gpost_sort(uint64_t *v,unsigned n) {
    for(unsigned i=1;i<n;i++) if(v[i-1]>v[i]) {qsort(v,n,8,gpost_cmp);return;}
}
static int gpost_has(const uint64_t *v,unsigned n,uint64_t id) {return n && bsearch(&id,v,n,8,gpost_cmp)!=NULL;}
static void *gpost_object(uint64_t runtime) {
    void *map=NULL;gpost_find_fn find=gpost_find_override?gpost_find_override:(gpost_find_fn)(void *)(image+GPOST_FIND);
    return runtime && read_local(image+GPOST_MAP,&map,8) && map?find(map,runtime,4):NULL;
}
static uint32_t gpost_class(const void *object) {
    uintptr_t vtable=0;
    return object && read_local(object,&vtable,8) && vtable>=(uintptr_t)image && vtable-(uintptr_t)image<0x6fa9000u?
        (uint32_t)(vtable-(uintptr_t)image):0;
}
/* A scan entry's flags +0xb8..+0xbe (live view, player view, +0x68 zero, due, active, slot 4, +0x60) as bits 0..6. */
static uint8_t gpost_entry_flags(const unsigned char *entry) {
    uint8_t flags=0;for(unsigned i=0;i<7;i++) if(entry[0xb8+i]) flags|=(uint8_t)(1u<<i);
    return flags;
}
/* The item's key (portable, or the runtime ID and class), as the digest uses it. */
static void gpost_key(uint64_t runtime,gpost_item_t *item) {
    unsigned klass=WORLD_KEY_NONE;uint64_t key=world_identity_key(runtime,&klass);
    memset(item,0,sizeof(*item));item->runtime=runtime;item->key=key?key:runtime;item->klass=key?klass:WORLD_KEY_NONE;
    if(!key) {++gpost_unmapped;item->type=gpost_class(gpost_object(runtime));}
}
/* The contact's scan entry against the scanner's: position, distance, the pair bound, inputs, group. */
static void gpost_geometry(const unsigned char *entry,const gpost_scanner_t *a,unsigned group,unsigned wide,gpost_item_t *item) {
    float p[3],vis,size;memcpy(p,entry+0x70,12);memcpy(&size,entry+0xa8,4);memcpy(&vis,entry+0xb4,4);
    float dx=p[0]-a->pos[0],dy=p[1]-a->pos[1],dz=p[2]-a->pos[2];
    for(unsigned i=0;i<3;i++) item->pos[i]=gpost_bits(p[i]);
    item->d=gpost_bits(sqrtf(dx*dx+dy*dy+dz*dz));item->bound=gpost_bits(a->range*a->factor*(vis+1.0f)+a->size+size);
    item->vis=gpost_bits(vis);item->size=gpost_bits(size);item->flags=gpost_entry_flags(entry);item->group=group;item->wide=(uint8_t)wide;
    item->state|=GPOST_FOUND;
}
/* Keeps the GPOST_LIST smallest runtime IDs of the edge candidates, sorted. */
static void gpost_edge_add(gpost_row_t *r,const gpost_item_t *item) {
    unsigned n=r->edges<GPOST_LIST?r->edges:GPOST_LIST,at=n;
    while(at && r->edge[at-1].runtime>item->runtime) --at;
    if(at==GPOST_LIST) return;
    memmove(&r->edge[at+1],&r->edge[at],(size_t)((n<GPOST_LIST?n:GPOST_LIST-1)-at)*sizeof(*item));
    r->edge[at]=*item;if(r->edges<GPOST_LIST) ++r->edges;
}
/* One walk over the sector's scan entries: the listed contacts' inputs and the edge candidates. */
static void gpost_walk(gpost_row_t *r,const uintptr_t groups[2],const gpost_scanner_t *a,unsigned now,unsigned old) {
    unsigned walked=0;
    for(uintptr_t at=groups[0];at<groups[1];at+=GWIDE_GROUP) for(unsigned wide=0;wide<2;wide++) {
        uintptr_t list[2];
        if(!gwide_vector((const void*)(at+(wide?GWIDE_WIDE:GWIDE_NEAR)),GWIDE_ENTRY,GPOST_ENTRIES_MAX,list)) {++gpost_failures;continue;}
        unsigned group=(unsigned)((at-groups[0])/GWIDE_GROUP);
        for(uintptr_t e=list[0];e<list[1];) {
            size_t n=(list[1]-e)/GWIDE_ENTRY;if(n>GPOST_CHUNK) n=GPOST_CHUNK;
            if(walked+n>GPOST_ENTRIES_MAX) {++gpost_oversize;return;}
            if(!read_local((const void*)e,gpost_chunk,n*GWIDE_ENTRY)) {++gpost_failures;break;}
            for(size_t k=0;k<n;k++) {
                const unsigned char *entry=gpost_chunk+k*GWIDE_ENTRY;uint64_t id;memcpy(&id,entry,8);
                if(id==a->runtime) continue;
                gpost_item_t *lists[2]={r->plus,r->minus};unsigned counts[2]={r->added,r->removed},hit=0;
                for(unsigned l=0;l<2 && !hit;l++) for(unsigned i=0;i<counts[l] && i<GPOST_LIST;i++)
                    if(lists[l][i].runtime==id && !(lists[l][i].state&GPOST_FOUND)) {gpost_geometry(entry,a,group,wide,&lists[l][i]);hit=1;break;}
                if(hit || !gpost_has(gpost_new,now,id) || !gpost_has(gpost_old,old,id)) continue;
                gpost_item_t item;memset(&item,0,sizeof(item));item.runtime=id;gpost_geometry(entry,a,group,wide,&item);
                if(gpost_float(item.d)>=GPOST_EDGE*gpost_float(item.bound)) gpost_edge_add(r,&item);
            }
            e+=n*GWIDE_ENTRY;walked+=(unsigned)n;
        }
    }
}
/* Gravidar thread, right after one keyed apply of the unit with portable key saved; result is its scan entry. */
static void gpost_note(void *unit,void *result,uint64_t saved,unsigned klass) {
    if(!InterlockedCompareExchange(&gpost_active,0,0) || !unit || !result) return;
    double scan=0,event=0;
    if(!read_local((unsigned char*)unit+0xa8,&scan,8) || !read_local((unsigned char*)unit+0xb8,&event,8)) {++gpost_failures;return;}
    if(scan!=event || !(scan>=0)) return; /* no post in this apply */
    if(InterlockedExchange(&gpost_busy,1)) {++gpost_failures;return;}
    DWORD error=GetLastError();
    ++gpost_posts;
    unsigned now=0,old=0;
    if(!gpost_read_set((unsigned char*)unit+0x68,gpost_new,&now) || !gpost_read_set((unsigned char*)result+0x10,gpost_old,&old)) {
        ++gpost_failures;InterlockedExchange(&gpost_busy,0);SetLastError(error);return;
    }
    gpost_sort(gpost_new,now);gpost_sort(gpost_old,old);
    unsigned added=0,removed=0;
    for(unsigned i=0,j=0;i<now || j<old;) {
        int add=j==old || (i<now && gpost_new[i]<gpost_old[j]),rem=i==now || (j<old && gpost_old[j]<gpost_new[i]);
        if(!add && !rem) {++i;++j;continue;}
        if(add) {++added;++i;} else {++removed;++j;}
    }
    if(!added && !removed) {++gpost_timers;InterlockedExchange(&gpost_busy,0);SetLastError(error);return;}
    static gpost_row_t r;memset(&r,0,sizeof(r));
    r.tick=stats.issued;r.unit=saved;r.unit_klass=(uint8_t)klass;r.now=now;r.old=old;memcpy(&r.time,&scan,8);
    /* The scanning unit: its own scan entry, and its group in the sector. */
    static unsigned char self[GWIDE_ENTRY];gpost_scanner_t a;memset(&a,0,sizeof(a));
    int entry=read_local(result,self,GWIDE_ENTRY);
    if(entry) {
        memcpy(a.pos,self+0x70,12);memcpy(&a.size,self+0xa8,4);memcpy(&a.range,self+0xac,4);memcpy(&a.factor,self+0xb0,4);memcpy(&a.runtime,self,8);
        for(unsigned i=0;i<3;i++) r.pos[i]=gpost_bits(a.pos[i]);
        r.range=gpost_bits(a.range);r.factor=gpost_bits(a.factor);r.size=gpost_bits(a.size);memcpy(&r.vis,self+0xb4,4);r.flags=gpost_entry_flags(self);
    } else ++gpost_failures;
    void *worker=NULL;uintptr_t groups[2]={0,0};unsigned group=0,wide=0;
    const unsigned char *home=entry && read_local(image+GPOST_WORKER,&worker,8)?
        gwide_entry_home((const unsigned char*)worker,(const unsigned char*)result,groups,&group,&wide):NULL;
    if(home) {r.state|=GPOST_FOUND;r.group=group;r.wide=(uint8_t)wide;r.groups=(uint32_t)((groups[1]-groups[0])/GWIDE_GROUP);} else ++gpost_homeless;
    uint64_t hash=0;
    for(unsigned i=0,j=0;i<now || j<old;) {
        int add=j==old || (i<now && gpost_new[i]<gpost_old[j]),rem=i==now || (j<old && gpost_old[j]<gpost_new[i]);
        if(!add && !rem) {++i;++j;continue;}
        gpost_item_t item;gpost_key(add?gpost_new[i]:gpost_old[j],&item);
        if(item.klass!=WORLD_KEY_NONE) hash+=lockstep_mix(item.key^((uint64_t)add<<63)^((uint64_t)item.klass<<62));
        if(add) {if(r.added<GPOST_LIST) r.plus[r.added]=item;++r.added;++i;}
        else {if(r.removed<GPOST_LIST) r.minus[r.removed]=item;++r.removed;++j;}
    }
    if(home) {
        gpost_walk(&r,groups,&a,now,old);
        for(unsigned i=0;i<r.edges;i++) {gpost_item_t keyed;gpost_key(r.edge[i].runtime,&keyed);r.edge[i].key=keyed.key;r.edge[i].klass=keyed.klass;r.edge[i].type=keyed.type;}
    }
    for(unsigned i=0;i<r.added && i<GPOST_LIST;i++) if(!(r.plus[i].state&GPOST_FOUND)) ++gpost_absent;
    for(unsigned i=0;i<r.removed && i<GPOST_LIST;i++) if(!(r.minus[i].state&GPOST_FOUND)) ++gpost_absent;
    ++gpost_changes;gpost_digest+=lockstep_mix(lockstep_seed(0x58344c4750535453ULL,r.tick,saved,hash,klass,0)); /* "X4LGPSTS" */
    if(gpost_tick!=r.tick) {gpost_tick=r.tick;gpost_tick_rows=0;}
    if(++gpost_tick_rows>GPOST_TICK_ROWS) ++gpost_dropped;
    else {LONG64 index=InterlockedCompareExchange64(&gpost_rows,0,0);gpost_ring[(uint64_t)index%GPOST_RING]=r;InterlockedExchange64(&gpost_rows,index+1);}
    InterlockedExchange(&gpost_busy,0);SetLastError(error);
}
/* Before the first admitted frame, on the MoveWorker, with the keyed unit apply armed. */
static int gpost_arm(int keyed) {
    if(gpost_armed) return stats.active!=0;
    if(!keyed || !stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    gpost_rows=0;gpost_posts=gpost_changes=gpost_timers=gpost_dropped=gpost_unmapped=gpost_failures=gpost_oversize=gpost_digest=gpost_tick_rows=0;
    gpost_homeless=gpost_absent=0;gpost_tick=UINT64_MAX;gpost_armed=1;InterlockedExchange(&gpost_active,1);return 1;
}
static void gpost_restore(void) {InterlockedExchange(&gpost_active,0);}
static char gpost_klass_char(uint32_t klass) {return klass==WORLD_KEY_SAVED?'s':klass==WORLD_KEY_CREATED?'c':'u';}
static int gpost_list(char *out,size_t cap,const gpost_item_t *items,unsigned count) {
    size_t used=0;unsigned shown=count<GPOST_LIST?count:GPOST_LIST;
    if(cap) out[0]=0;
    for(unsigned i=0;i<shown;i++) {
        const gpost_item_t *t=&items[i];int m;
        if(t->klass==WORLD_KEY_NONE) m=snprintf(out+used,cap-used,"%su:%llx/%08x",i?",":"",(unsigned long long)t->key,t->type);
        else m=snprintf(out+used,cap-used,"%s%c:%016llx",i?",":"",gpost_klass_char(t->klass),(unsigned long long)t->key);
        if(m<=0 || (size_t)m>=cap-used) return -1;
        used+=(size_t)m;
        if(t->state&GPOST_FOUND)
            m=snprintf(out+used,cap-used,"{p=%08x,%08x,%08x d=%08x b=%08x v=%08x s=%08x fl=%x z=%u%c}",t->pos[0],t->pos[1],t->pos[2],t->d,t->bound,
                       t->vis,t->size,t->flags,t->group,t->wide?'w':'n');
        else m=snprintf(out+used,cap-used,"{z=-}"); /* not gathered by this scan */
        if(m<=0 || (size_t)m>=cap-used) return -1;
        used+=(size_t)m;
    }
    return (int)used;
}
static int gpost_row(uint64_t index,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&gpost_rows,0,0);int n;
    if((LONG64)index>=count) return 0;
    if(count>(LONG64)GPOST_RING && index<(uint64_t)(count-(LONG64)GPOST_RING))
        return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    const gpost_row_t *r=&gpost_ring[index%GPOST_RING];static char plus[1536],minus[1536],edge[1536],zone[48];
    if(gpost_list(plus,sizeof(plus),r->plus,r->added)<0 || gpost_list(minus,sizeof(minus),r->minus,r->removed)<0 ||
       gpost_list(edge,sizeof(edge),r->edge,r->edges)<0) return 0;
    if(r->state&GPOST_FOUND) snprintf(zone,sizeof(zone),"%u/%u%c",r->group,r->groups,r->wide?'w':'n');else strcpy(zone,"-");
    n=snprintf(out,cap,"index=%llu tick=%llu unit=%c:%016llx sizes=%u/%u added=%u removed=%u time=%016llx "
        "a={p=%08x,%08x,%08x r=%08x f=%08x s=%08x v=%08x fl=%x z=%s} plus=%s minus=%s edge=%s",
        (unsigned long long)index,(unsigned long long)r->tick,gpost_klass_char(r->unit_klass),(unsigned long long)r->unit,r->old,r->now,
        r->added,r->removed,(unsigned long long)r->time,r->pos[0],r->pos[1],r->pos[2],r->range,r->factor,r->size,r->vis,r->flags,zone,plus,minus,edge);
    return n>0 && (size_t)n<cap?n:0;
}
static int gpost_status(char *out,size_t cap) {
    int n=snprintf(out,cap,"armed=%d active=%ld posts=%llu changes=%llu timers=%llu rows=%lld dropped=%llu unmapped=%llu oversize=%llu "
        "homeless=%llu absent=%llu failures=%llu digest=%016llx",gpost_armed,gpost_active,(unsigned long long)gpost_posts,(unsigned long long)gpost_changes,
        (unsigned long long)gpost_timers,(long long)gpost_rows,(unsigned long long)gpost_dropped,(unsigned long long)gpost_unmapped,
        (unsigned long long)gpost_oversize,(unsigned long long)gpost_homeless,(unsigned long long)gpost_absent,(unsigned long long)gpost_failures,
        (unsigned long long)gpost_digest);
    return n>0 && (size_t)n<cap?n:0;
}
