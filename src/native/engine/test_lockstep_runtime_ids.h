/* native_gate_runtime_ids: a stand-in AutoIDMap with the native list operations
 * (EnsureFreeSlots, InsertAtSlot, Remove as read from 0xcc670, 0xcc500, 0xcbe70),
 * two "processes" with different pre-load histories, the same load after the set.
 * The native functions themselves are pinned on the image test. */
typedef struct {unsigned char *map;unsigned blocks;} test_rid_process;
static uint64_t *test_rid_entry(unsigned char *map,uint32_t slot) {return (uint64_t*)rid_slot(map,slot);}
static void test_rid_append_block(unsigned char *map,unsigned block) {
    /* EnsureFreeSlots: a new block's slots, in order, at the tail. */
    unsigned char *base=VirtualAlloc(NULL,0xaaaac0,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(base);
    memcpy(map+0x58+block*8u,&base,8);
    const uint32_t first=block*0x100000u+1u,last=block*0x100000u+0x100000u;
    uint32_t tail=*(uint32_t*)(map+0x54);
    if(tail) {uint64_t *t=test_rid_entry(map,tail);*t=(*t&0xffffffffULL)|((uint64_t)first<<32);}
    else *(uint32_t*)(map+0x50)=first;
    for(uint32_t slot=first;slot<=last;slot++) *test_rid_entry(map,slot)=(slot==first?(uint64_t)tail:(uint64_t)(slot-1u))|(slot<last?(uint64_t)(slot+1u)<<32:0u);
    *(uint32_t*)(map+0x54)=last;*(uint32_t*)(map+0x44)+=0x100000u;
}
static test_rid_process test_rid_new(void) {
    test_rid_process p;p.map=VirtualAlloc(NULL,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(p.map);p.blocks=1;
    InitializeCriticalSection((CRITICAL_SECTION*)(p.map+0x10));test_rid_append_block(p.map,0);return p;
}
static void test_rid_free_process(test_rid_process *p) {
    for(unsigned b=0;b<32u;b++) {void *base;memcpy(&base,p->map+0x58+b*8u,8);if(base) assert(VirtualFree(base,0,MEM_RELEASE));}
    DeleteCriticalSection((CRITICAL_SECTION*)(p->map+0x10));assert(VirtualFree(p->map,0,MEM_RELEASE));
}
/* AssignNewID: EnsureFreeSlots, then the head through InsertAtSlot. */
static uint64_t test_rid_insert(test_rid_process *p) {
    unsigned char *map=p->map;
    if(*(uint32_t*)(map+0x44)<RID_FREE_MIN) test_rid_append_block(map,p->blocks++);
    const uint32_t slot=*(uint32_t*)(map+0x50);assert(slot);
    uint64_t *e=test_rid_entry(map,slot);const uint32_t prev=(uint32_t)*e,next=(uint32_t)(*e>>32);
    if(!prev) *(uint32_t*)(map+0x50)=next;
    else {uint64_t *q=test_rid_entry(map,prev);*q=(*q&0xffffffffULL)|((uint64_t)next<<32);}
    if(!next) *(uint32_t*)(map+0x54)=prev;
    else {uint64_t *q=test_rid_entry(map,next);*q=(*q&0xffffffff00000000ULL)|prev;}
    unsigned entry;unsigned char *b=rid_bucket(map,slot,&entry);
    *e=(uint64_t)(uintptr_t)map;*(uint32_t*)(b+0x1c)|=1u<<(entry*4u+16u);
    *(uint32_t*)(map+0x40)+=1u;*(uint32_t*)(map+0x44)-=1u;*(uint64_t*)(map+0x38)+=1u;
    return ((uint64_t)rid_slot_generation(map,slot)<<25)|slot;
}
/* Remove as native: generation, unused, appended at the tail. */
static uint64_t test_rid_remove_native(void *mapv,uint64_t id) {
    unsigned char *map=mapv;const uint32_t slot=(uint32_t)id&0x1ffffffu;
    CRITICAL_SECTION *lock=(CRITICAL_SECTION*)(map+0x10);EnterCriticalSection(lock);
    unsigned entry;unsigned char *b=rid_bucket(map,slot,&entry);uint64_t ok=0;
    if(b && ((*(uint32_t*)(b+0x1c)>>(entry*4u+16u))&1u) && rid_slot_generation(map,slot)==((uint32_t)(id>>25)&0xffffu)) {
        *(uint32_t*)(b+0x1c)&=~(0xfu<<(entry*4u+16u));
        uint32_t *g=(uint32_t*)(b+0x18+(entry>>1)*4u);const unsigned shift=(entry&1u)*16u;
        const uint32_t next=(((*g>>shift)&0xffffu)+1u)&0xffffu;*g=(*g&~(0xffffu<<shift))|(next<<shift);
        const uint32_t tail=*(uint32_t*)(map+0x54);*test_rid_entry(map,slot)=tail;
        if(!tail) *(uint32_t*)(map+0x50)=slot;
        else {uint64_t *q=test_rid_entry(map,tail);*q=(*q&0xffffffffULL)|((uint64_t)slot<<32);}
        *(uint32_t*)(map+0x54)=slot;*(uint32_t*)(map+0x40)-=1u;*(uint32_t*)(map+0x44)+=1u;ok=1;
    }
    LeaveCriticalSection(lock);return ok;
}
static uint64_t test_rid_stub_result;
static uint64_t test_rid_remove_stub(void *map,uint64_t id) {(void)map;(void)id;return test_rid_stub_result;}
/* The pre-load history: live objects stay (their IDs returned), the rest are freed. */
static unsigned test_rid_preload(test_rid_process *p,uint64_t *live,unsigned stay,unsigned transient) {
    for(unsigned i=0;i<transient;i++) assert(test_rid_remove_native(p->map,test_rid_insert(p)));
    for(unsigned i=0;i<stay;i++) live[i]=test_rid_insert(p);
    return stay;
}
/* The same load on both: long enough to run through block 0 into block 1; objects freed along the way, none handed out again;
 * the pre-load objects die early. Returns a digest of every ID handed out. */
static uint64_t test_rid_load(test_rid_process *p,const uint64_t *old,unsigned olds,uint32_t count,uint64_t *keep,unsigned keeps) {
    uint64_t digest=0;static uint64_t ring[64];memset(ring,0,sizeof(ring));
    for(uint32_t i=0;i<count;i++) {
        if(i==100) for(unsigned k=0;k<olds;k++) rid_remove(p->map,old[k]);
        uint64_t id=test_rid_insert(p);digest=lockstep_mix(digest^id);
        if(i<keeps) keep[i]=id;
        if(i%3==0) { /* a transient: freed a little later */
            uint64_t *r=&ring[i%64];if(*r) assert(rid_remove(p->map,*r)&1u);*r=id;
        }
    }
    return digest;
}
static void test_runtime_ids(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    int saved_world=world_identity_installed;world_identity_entry *saved_ids=world_identities;
    void *saved_map=*(void**)(image+RID_MAP);
    /* Slot layout: 3 slots per 32-byte bucket, IDs generation<<25 | slot. */
    test_rid_process a=test_rid_new(),b=test_rid_new();
    {
        unsigned entry;unsigned char *bucket=rid_bucket(a.map,4,&entry);void *base;memcpy(&base,a.map+0x58,8);
        assert(bucket==(unsigned char*)base+0x20 && entry==0);bucket=rid_bucket(a.map,3,&entry);assert(bucket==(unsigned char*)base && entry==2);
        assert(!rid_bucket(a.map,0,&entry) && !rid_bucket(a.map,0x100001,&entry) && !rid_bucket(a.map,0x2000001,&entry));
    }
    /* Different pre-load histories: 300 and 1500 transients, the same 40 live objects at different slots. */
    uint64_t old_a[40],old_b[40];test_rid_preload(&a,old_a,40,300);test_rid_preload(&b,old_b,40,1500);
    rid_base=0x10000;
    assert(old_a[0]!=old_b[0] && *(uint32_t*)(a.map+0x50)==341 && *(uint32_t*)(b.map+0x50)==1541);
    /* Refusals leave the map as it was. */
    {
        const rid_counts_t before=rid_counts(b.map);
        assert(!rid_apply(b.map,1000) && !strcmp(rid_stage,"head_past_base"));
        void *second=(void*)1;memcpy(b.map+0x60,&second,8);assert(!rid_apply(b.map,0x10000) && !strcmp(rid_stage,"block_layout"));second=NULL;memcpy(b.map+0x60,&second,8);
        uint32_t saved=*(uint32_t*)(b.map+0x44);*(uint32_t*)(b.map+0x44)=5;assert(!rid_apply(b.map,0x10000) && !strcmp(rid_stage,"free_count"));*(uint32_t*)(b.map+0x44)=saved;
        uint64_t *hole=test_rid_entry(b.map,0x5000),keep=*hole;*hole=0;assert(!rid_apply(b.map,0x10000) && !strcmp(rid_stage,"fresh_run"));*hole=keep;
        *(uint32_t*)(b.map+0x44)=saved+1u;assert(!rid_apply(b.map,0x10000) && !strcmp(rid_stage,"freed_run"));*(uint32_t*)(b.map+0x44)=saved;
        const rid_counts_t after=rid_counts(b.map);assert(!memcmp(&before,&after,sizeof(before)));
    }
    /* The set: both lists start at the agreed slot and end after block 0; everything freed before is off the list. */
    assert(rid_apply(a.map,0x10000) && rid_head_before==341 && rid_cut==300 && rid_skipped==0x10000-341);
    assert(rid_apply(b.map,0x10000) && rid_head_before==1541 && rid_cut==1500);
    {
        const rid_counts_t x=rid_counts(a.map),y=rid_counts(b.map);
        assert(x.head==0x10000 && x.tail==RID_BLOCK_LAST && x.free==RID_BLOCK_LAST-0x10000u+1u && y.head==x.head && y.tail==x.tail && y.free==x.free);
        assert(!(uint32_t)*test_rid_entry(a.map,0x10000) && !(*test_rid_entry(a.map,RID_BLOCK_LAST)>>32));
        assert(rid_walk(a.map) && !rid_below_base);
    }
    /* The Remove wrapper: on, every freed slot leaves the tail again; the same load then hands out the same IDs, a plain counter
     * through block 0 into block 1. Without the wrapper the old and the freed slots come back, in the order they died. */
    rid_remove_native=test_rid_remove_native;InterlockedExchange(&rid_active,1);rid_dropped=0;
    static uint64_t keep_a[512],keep_b[512];
    *(void**)(image+RID_MAP)=a.map;
    uint64_t digest_a=test_rid_load(&a,old_a,39,1100000,keep_a,512);
    const LONG64 freed=39+(366667-64); /* the old objects, every third ID less the ring's first 64 */
    assert(rid_dropped==freed && !rid_failures);
    assert(rid_walk(a.map) && !rid_below_base);
    *(void**)(image+RID_MAP)=b.map;
    uint64_t digest_b=test_rid_load(&b,old_b,39,1100000,keep_b,512);
    assert(rid_dropped==2*freed && !rid_failures && digest_a==digest_b);
    assert(!memcmp(keep_a,keep_b,sizeof(keep_a)) && keep_a[0]==0x10000 && keep_a[511]==0x10000+511);
    {
        const rid_counts_t x=rid_counts(a.map),y=rid_counts(b.map);
        assert(x.head==y.head && x.tail==y.tail && x.free==y.free && x.live==y.live);
        assert(x.head==0x10000u+1100000u && x.tail==2u*RID_BLOCK_LAST && x.free==x.tail-x.head+1u && x.generation==y.generation);
    }
    /* Past block 0: the next slot in order, never a recycled one; the order objects died in does not matter. */
    {
        const uint64_t x=test_rid_insert(&a),y=test_rid_insert(&b);assert(x==y && x==0x10000u+1100000u);
    }
    /* The limit: the head entering block 29 stops the gate, one block before the lists at the top of the table. */
    {
        rid_mode=RID_SET;*(void**)(image+RID_MAP)=a.map;
        const uint32_t saved_head=*(uint32_t*)(a.map+0x50);
        *(uint32_t*)(a.map+0x50)=RID_BLOCK_LIMIT*RID_BLOCK_LAST;assert(rid_frame(1) && !rid_failures);
        *(uint32_t*)(a.map+0x50)=RID_BLOCK_LIMIT*RID_BLOCK_LAST+1u;assert(!rid_frame(1) && !strcmp(rid_stage,"ids_exhausted"));
        *(uint32_t*)(a.map+0x50)=saved_head;rid_failures=0;rid_stage="set";rid_mode=RID_OFF;rid_digest=0;rid_next_id=0;rid_frames_done=0;
    }
    /* What the digest and the word mean: the next ID only. The pre-load history (+0x38 counts, here 300 and 1500 transients) and frees
     * landing on other ticks (tail, free, live) do not move it; a creation on one client only does, and the rolling digest keeps it. */
    {
        rid_mode=RID_SET;uint64_t digest_one=7,digest_two=7,next_one,next_two;
        assert(*(uint64_t*)(a.map+0x38)!=*(uint64_t*)(b.map+0x38)); /* the histories differ */
        *(void**)(image+RID_MAP)=a.map;rid_digest=digest_one;assert(rid_frame(100));digest_one=rid_digest;next_one=rid_next_id;
        *(void**)(image+RID_MAP)=b.map;rid_digest=digest_two;assert(rid_frame(100));digest_two=rid_digest;next_two=rid_next_id;
        assert(digest_one==digest_two && next_one==next_two && (next_one&0x1ffffffu)==*(uint32_t*)(a.map+0x50) && (next_one>>25)==rid_slot_generation(a.map,*(uint32_t*)(a.map+0x50)));
        /* A free on one client now, on the other later: the head stays, so nothing differs. */
        const uint32_t saved_tail=*(uint32_t*)(b.map+0x54),saved_free=*(uint32_t*)(b.map+0x44),saved_live=*(uint32_t*)(b.map+0x40);
        *(uint32_t*)(b.map+0x54)=saved_tail-1u;*(uint32_t*)(b.map+0x44)=saved_free+391u;*(uint32_t*)(b.map+0x40)=saved_live-391u;
        *(void**)(image+RID_MAP)=a.map;rid_digest=digest_one;assert(rid_frame(101));digest_one=rid_digest;
        *(void**)(image+RID_MAP)=b.map;rid_digest=digest_two;assert(rid_frame(101));digest_two=rid_digest;
        assert(digest_one==digest_two);
        *(uint32_t*)(b.map+0x54)=saved_tail;*(uint32_t*)(b.map+0x44)=saved_free;*(uint32_t*)(b.map+0x40)=saved_live;
        const uint64_t extra=test_rid_insert(&b);assert(extra);
        *(void**)(image+RID_MAP)=a.map;rid_digest=digest_one;assert(rid_frame(102));digest_one=rid_digest;next_one=rid_next_id;
        *(void**)(image+RID_MAP)=b.map;rid_digest=digest_two;assert(rid_frame(102));digest_two=rid_digest;next_two=rid_next_id;
        assert(digest_one!=digest_two && next_one!=next_two);
        /* The other client catches up: the word agrees again, the digest stays different. */
        const uint64_t late=test_rid_insert(&a);assert(late);
        *(void**)(image+RID_MAP)=a.map;rid_digest=digest_one;assert(rid_frame(103));digest_one=rid_digest;next_one=rid_next_id;
        *(void**)(image+RID_MAP)=b.map;rid_digest=digest_two;assert(rid_frame(103));digest_two=rid_digest;next_two=rid_next_id;
        assert(digest_one!=digest_two && next_one==next_two);
        rid_mode=RID_OFF;rid_digest=0;rid_next_id=0;rid_frames_done=0;
    }
    /* Failing loud: an object from before the set freed behind the wrapper's back is found by the walk; a removed slot that is
     * not at the tail stops the gate; another map passes through untouched. */
    {
        *(void**)(image+RID_MAP)=a.map;rid_failures=0;
        assert(test_rid_remove_native(a.map,old_a[39]));
        assert(!rid_walk(a.map) && !strcmp(rid_stage,"below_base") && rid_below_base==1 && rid_failures==1);rid_failures=0;
        assert(rid_remove_call(test_rid_remove_stub,(unsigned char*)b.map,5)==0 && !rid_failures); /* another map */
        test_rid_stub_result=1;
        assert(rid_remove_call(test_rid_remove_stub,a.map,5)==1 && rid_failures==1 && !strcmp(rid_stage,"remove_not_tail"));rid_failures=0;
        test_rid_stub_result=0;
    }
    InterlockedExchange(&rid_active,0);rid_remove_native=NULL;
    test_rid_free_process(&a);test_rid_free_process(&b);
    /* Saved map digest: independent of the table's order, sensitive to one ID. */
    {
        world_identity_entry *table=VirtualAlloc(NULL,sizeof(*table)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(table);
        world_identities=table;
        table[5]=(world_identity_entry){.runtime=0x10000,.saved=0x2d};table[900]=(world_identity_entry){.runtime=0x12345,.saved=0xa214c};
        table[901]=(world_identity_entry){.runtime=(1ULL<<25)|7,.saved=0x176cef};
        rid_map_fold();const uint64_t sum=rid_map_sum;uint32_t buckets[RID_BUCKETS];memcpy(buckets,rid_buckets,sizeof(buckets));
        assert(rid_map_count==3 && rid_map_min==0x10000 && rid_map_max==((1ULL<<25)|7) && rid_map_generations==1);
        world_identity_entry t=table[5];table[5]=table[900];table[900]=t;
        rid_map_fold();assert(rid_map_sum==sum && !memcmp(buckets,rid_buckets,sizeof(buckets)));
        table[5].runtime+=1;rid_map_fold();assert(rid_map_sum!=sum && memcmp(buckets,rid_buckets,sizeof(buckets)));
        char row[640];rid_sealed=1;assert(rid_map_row(row,sizeof(row))>0 && strstr(row,"count=3 sum=") && strstr(row," buckets="));
        unsigned commas=0;for(const char *c=row;*c;c++) commas+=*c==',';assert(commas==RID_BUCKETS-1u);
        VirtualFree(table,0,MEM_RELEASE);world_identities=saved_ids;
    }
    /* Who allocates: load phase by direct caller, play phase by X4 return addresses; one row per distinct stack. */
    {
        memset(rid_callers,0,sizeof(rid_callers));rid_caller_count=0;rid_caller_overflow=0;rid_sealed=0;
        uint32_t frames[RID_STACK];unsigned n=rid_frames_of(image+0x3d2431,frames);assert(n==1 && frames[0]==0x3d2431);
        rid_note_caller(frames,n,RID_ROUTE_SHARED,CREATED_MOVEWORKER);rid_note_caller(frames,n,RID_ROUTE_SHARED,CREATED_MOVEWORKER);
        n=rid_frames_of(image+0xc0039,frames);assert(n==1);rid_note_caller(frames,n,RID_ROUTE_SHARED,CREATED_OTHER);
        n=rid_frames_of((void*)1,frames);assert(!n);rid_note_caller(frames,n,RID_ROUTE_SHARED,CREATED_MAIN); /* outside the image: no frames */
        assert(rid_caller_count==3);
        char row[256];unsigned found=0;
        for(unsigned i=0;rid_caller_row(i,row,sizeof(row));i++) {
            if(strstr(row,"phase=load thread=moveworker route=shared count=2 frames=3d2431,0,0,0,0,0,0,0")) ++found;
            if(strstr(row,"phase=load thread=other route=shared count=1 frames=c0039,0,0,0,0,0,0,0")) ++found;
            if(strstr(row,"phase=load thread=main route=shared count=1 frames=0,0,0,0,0,0,0,0")) ++found;
        }
        assert(found==3 && !rid_caller_row(3,row,sizeof(row)));
        /* An MD cue instance: the same stack with another cue is another row, the name at the row's end. */
        {   const uint32_t cue_frames[RID_FRAMES]={RID_CUE_HANDLER,RID_CUE_CREATE,0x111c477,0x111e50a,0,0,0,0};
            assert(rid_is_cue(cue_frames,8) && !rid_is_cue(cue_frames,1) && !rid_is_cue(cue_frames+1,7));
            static __attribute__((aligned(16))) unsigned char inline_cue[0x100],heap_cue[0x100],bad_cue[0x100];static char long_name[40];
            char name[RID_CUE];uint64_t size=21,capacity=15;void *p=long_name;
            memset(inline_cue,0,sizeof(inline_cue));memcpy(inline_cue+RID_CUE_NAME,"Hull",4);size=4;
            memcpy(inline_cue+RID_CUE_NAME+0x10,&size,8);memcpy(inline_cue+RID_CUE_NAME+0x18,&capacity,8);
            rid_cue_of_static(inline_cue,name);assert(!strcmp(name,"Hull"));
            strcpy(long_name,"PlayerShipHullDamaged");size=21;capacity=31;memset(heap_cue,0,sizeof(heap_cue));
            memcpy(heap_cue+RID_CUE_NAME,&p,8);memcpy(heap_cue+RID_CUE_NAME+0x10,&size,8);memcpy(heap_cue+RID_CUE_NAME+0x18,&capacity,8);
            rid_cue_of_static(heap_cue,name);assert(!strcmp(name,"PlayerShipHullDamaged"));
            memcpy(bad_cue,inline_cue,sizeof(bad_cue));bad_cue[RID_CUE_NAME+1]=' ';rid_cue_of_static(bad_cue,name);assert(!name[0]);
            size=RID_CUE;memcpy(bad_cue+RID_CUE_NAME+0x10,&size,8);rid_cue_of_static(bad_cue,name);assert(!name[0]);
            rid_cue_of_static(NULL,name);assert(!name[0]);
            unsigned before=rid_caller_count;rid_sealed=1;
            rid_note_caller_cue(cue_frames,8,RID_ROUTE_SHARED,CREATED_MOVEWORKER,"PlayerShipHullDamaged");
            rid_note_caller_cue(cue_frames,8,RID_ROUTE_SHARED,CREATED_MOVEWORKER,"PlayerShipHullDamaged");
            rid_note_caller_cue(cue_frames,8,RID_ROUTE_SHARED,CREATED_MOVEWORKER,"Hull");
            rid_note_caller(cue_frames,8,RID_ROUTE_SHARED,CREATED_MOVEWORKER);
            assert(rid_caller_count==before+3);
            unsigned named=0;
            for(unsigned i=0;rid_caller_row(i,row,sizeof(row));i++) {
                if(strstr(row,"count=2 frames=caab18,1111aaa,111c477,111e50a,0,0,0,0 cue=PlayerShipHullDamaged")) ++named;
                if(strstr(row,"count=1 frames=caab18,1111aaa,111c477,111e50a,0,0,0,0 cue=Hull")) ++named;
                if(strstr(row,"count=1 frames=caab18,1111aaa,111c477,111e50a,0,0,0,0") && !strstr(row,"cue=")) ++named;
            }
            assert(named==3);
            /* From a stack that is not X4's cue path: no name, no fault. */
            rid_cue_name(name);assert(!name[0]);
            memset(rid_callers,0,sizeof(rid_callers));rid_caller_count=0;rid_sealed=0;
            rid_note_caller(frames,1,RID_ROUTE_SHARED,CREATED_MOVEWORKER);rid_note_caller(frames,1,RID_ROUTE_SHARED,CREATED_MOVEWORKER);
            n=rid_frames_of(image+0xc0039,frames);rid_note_caller(frames,n,RID_ROUTE_SHARED,CREATED_OTHER);
            n=rid_frames_of((void*)1,frames);rid_note_caller(frames,n,RID_ROUTE_SHARED,CREATED_MAIN);
            n=rid_frames_of(image+0x3d2431,frames);}
        rid_sealed=1;n=rid_frames_of(image+0x3d2431,frames);assert(!n); /* the test's own stack is outside the image */
        rid_note_caller(frames,n,RID_ROUTE_SHARED,CREATED_MOVEWORKER);assert(rid_caller_count==4);
        /* The same stack on another route is another row; only the first frames show. */
        rid_note_caller(frames,n,RID_ROUTE_UI,CREATED_MOVEWORKER);assert(rid_caller_count==5);
        memset(rid_callers,0,sizeof(rid_callers));rid_caller_count=0;
        {
            const uint32_t deep[]={1,2,3,4,5,6,7,8,9,10};rid_note_caller(deep,10,RID_ROUTE_VIEW,CREATED_MOVEWORKER);
            assert(rid_caller_row(0,row,sizeof(row))>0 && strstr(row,"phase=play thread=moveworker route=view count=1 frames=1,2,3,4,5,6,7,8") && !strstr(row,",9"));
            /* The widest row fits the Lua buffer. */
            memset(rid_callers,0,sizeof(rid_callers));rid_caller_count=0;
            uint32_t wide[RID_FRAMES];memset(wide,0xff,sizeof(wide));
            char longest[RID_CUE];memset(longest,'x',RID_CUE-1);longest[RID_CUE-1]=0;
            rid_caller_add_cue(wide,1,CREATED_MOVEWORKER,RID_ROUTE_SHARED,longest);
            for(unsigned i=0;i<RID_CALLERS;i++) if(rid_callers[i].count) rid_callers[i].count=UINT64_MAX;
            assert(rid_caller_row(0,row,sizeof(row))>0 && strstr(row,"count=18446744073709551615") && strstr(row,longest));
        }
        memset(rid_callers,0,sizeof(rid_callers));rid_caller_count=0;
        for(unsigned i=0;i<RID_CALLERS;i++) {uint32_t f[RID_FRAMES]={i+1000u,0,0,0,0,0};rid_caller_add(f,1,0,RID_ROUTE_SHARED);}
        assert(rid_caller_count==RID_CALLERS/2u && rid_caller_overflow>0);
        memset(rid_callers,0,sizeof(rid_callers));rid_caller_count=0;rid_caller_overflow=0;rid_sealed=0;
    }
    /* Arming: only before the load, on the UI thread, with the identities; rerun accepted only with the same base. */
    {
        stats.active=1;stats.issued=0;ui_thread=GetCurrentThreadId();boot_mode=1;boot_phase=0;world_identity_installed=1;
        rid_mode=RID_OFF;
        boot_phase=1;assert(!rid_arm(0) && !strcmp(rid_stage,"phase"));boot_phase=0;
        world_identities=NULL;assert(!rid_arm(0) && !strcmp(rid_stage,"identities"));world_identities=saved_ids?saved_ids:(world_identity_entry*)image;
        assert(!rid_arm(RID_BASE_MAX+1u) && !strcmp(rid_stage,"base_range"));
        assert(!rid_arm(0) && !strcmp(rid_stage,"pins")); /* the plain image has no pinned code */
        world_identities=saved_ids;
    }
    /* Status at the widest values fits the Lua buffer. */
    {
        rid_mode=RID_SET;rid_base=RID_BASE_MAX;rid_installed=rid_restored=1;rid_stage="remove_not_tail";rid_failures=INT64_MAX;rid_frames_done=UINT64_MAX;
        rid_head_before=rid_cut=rid_skipped=rid_below_base=UINT32_MAX;memset(&rid_now,0xff,sizeof(rid_now));rid_dropped=rid_caller_overflow=INT64_MAX;rid_digest=UINT64_MAX;
        rid_map_count=rid_map_generations=rid_load_count=rid_caller_count=UINT32_MAX;rid_map_sum=rid_load_sum=rid_map_min=rid_map_max=UINT64_MAX;
        rid_next_id=UINT64_MAX;memset(&rid_before,0,sizeof(rid_before));memset(rid_local_now,0xff,sizeof(rid_local_now));
        char text[900];assert(rid_status(text,sizeof(text))>0 && strstr(text,"mode=set base=524288 installed=1 restored=1 stage=remove_not_tail ") && strstr(text," next=ffffffffffffffff allocs="));
        memset(rid_local_now,0,sizeof(rid_local_now));
        rid_mode=RID_OFF;rid_base=0;rid_installed=rid_restored=0;rid_stage="idle";rid_failures=0;rid_frames_done=0;rid_head_before=rid_cut=rid_skipped=rid_below_base=0;
        memset(&rid_now,0,sizeof(rid_now));rid_next_id=0;rid_dropped=rid_caller_overflow=0;rid_digest=0;rid_map_count=rid_map_generations=rid_load_count=rid_caller_count=0;
        rid_map_sum=rid_load_sum=rid_map_min=rid_map_max=0;
        assert(rid_status(text,sizeof(text))>0 && strstr(text,"mode=off base=0 "));
    }
    *(void**)(image+RID_MAP)=saved_map;world_identities=saved_ids;world_identity_installed=saved_world;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    printf("runtime IDs list/set/remove/digest/callers/arming/status tests PASS\n");
}
/* Local allocations: two clients whose cameras differ. */
static test_rid_process *test_rid_current;
static unsigned test_rid_assign_calls;
static uint64_t test_rid_assign_stub(void *object) {
    const uint64_t id=test_rid_insert(test_rid_current);memcpy((unsigned char*)object+8,&id,8);++test_rid_assign_calls;return id;
}
static void *test_rid_allocate(size_t size) {return VirtualAlloc(NULL,size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);}
static void *test_rid_allocate_null(size_t size) {(void)size;return NULL;}
static void test_rid_use(test_rid_process *p,rid_local_t *from,rid_local_t *to) {
    if(from) memcpy(from,rid_local,sizeof(rid_local));
    memcpy(rid_local,to,sizeof(rid_local));*(void**)(image+RID_MAP)=p->map;test_rid_current=p;
}
#define TEST_RID_ROUTE(expected,...) do {const uint32_t f[]={__VA_ARGS__};assert(rid_route_of(f,sizeof(f)/sizeof(*f))==(expected));} while(0)
static void test_runtime_ids_local(void) {
    uint32_t saved_active=stats.active;DWORD saved_ui=ui_thread;void *saved_map=*(void**)(image+RID_MAP);int saved_created=created_installed;
    test_rid_process a=test_rid_new(),b=test_rid_new();uint64_t old[8];unsigned char object[16];
    static rid_local_t lists_a[RID_ROUTES],lists_b[RID_ROUTES];
    test_rid_preload(&a,old,8,300);test_rid_preload(&b,old,8,1500);
    assert(rid_apply(a.map,0x10000) && rid_apply(b.map,0x10000));
    rid_base=0x10000;rid_failures=0;rid_dropped=0;
    rid_remove_native=test_rid_remove_native;rid_assign_native=test_rid_assign_stub;
    ui_thread=GetCurrentThreadId();stats.active=1;
    /* The refusals: not set mode, after the seal, no hook, the wrong build, a taken block, no memory. */
    {
        *(void**)(image+RID_MAP)=a.map;rid_mode=RID_OBSERVE;assert(!rid_local_arm() && !strcmp(rid_stage,"local_phase"));
        rid_mode=RID_SET;rid_sealed=1;assert(!rid_local_arm() && !strcmp(rid_stage,"local_phase"));rid_sealed=0;
        created_installed=0;assert(!rid_local_arm() && !strcmp(rid_stage,"local_hook"));
        created_installed=1;assert(!rid_local_arm() && !strcmp(rid_stage,"local_pins")); /* the plain image has no pinned code */
        rid_allocate_native=test_rid_allocate_null;assert(!rid_local_build(a.map) && !strcmp(rid_stage,"local_allocate") && !rid_local_built);
        void *taken=(void*)1;memcpy(a.map+0x58+RID_LOCAL_VIEW_BLOCK*8u,&taken,8);assert(!rid_local_build(a.map) && !strcmp(rid_stage,"local_blocks"));
        taken=NULL;memcpy(a.map+0x58+RID_LOCAL_VIEW_BLOCK*8u,&taken,8);
        rid_allocate_native=test_rid_allocate;rid_stage="set";
    }
    /* The lists: the map's own two last blocks, every slot free, in order. */
    assert(rid_local_build(a.map));memcpy(lists_a,rid_local,sizeof(rid_local));
    *(void**)(image+RID_MAP)=b.map;assert(rid_local_build(b.map));memcpy(lists_b,rid_local,sizeof(rid_local));
    {
        const rid_local_t *u=&rid_local[RID_ROUTE_UI],*v=&rid_local[RID_ROUTE_VIEW];
        assert(u->head==0x1f00001u && u->tail==0x1ffffffu && u->free==0xfffffu && v->head==0x1e00001u && v->tail==0x1f00000u && v->free==0x100000u);
        assert(!(uint32_t)*test_rid_entry(b.map,u->head) && (*test_rid_entry(b.map,u->head)>>32)==u->head+1u && !(*test_rid_entry(b.map,u->tail)>>32) && (uint32_t)*test_rid_entry(b.map,u->tail)==u->tail-1u);
        assert(!(*test_rid_entry(b.map,v->tail)>>32) && !rid_slot_used(b.map,v->head) && !rid_slot_generation(b.map,v->head));
        assert(rid_local_of(0x1f00001u)==u && rid_local_of(0x1ffffffu)==u && rid_local_of(0x1f00000u)==v && rid_local_of(0x1e00001u)==v && !rid_local_of(0x1e00000u) && !rid_local_of(0x10000u) && !rid_local_of(0));
        /* The shared list did not see them: the walk and the set still hold. */
        rid_counts_t c=rid_counts(b.map);assert(c.head==0x10000u && c.tail==RID_BLOCK_LAST && rid_walk(b.map));
    }
    /* Routing. */
    InterlockedExchange(&rid_active,1);InterlockedExchange(&rid_local_on,1);rid_sealed=1;rid_mode=RID_SET;
    {
        TEST_RID_ROUTE(RID_ROUTE_SHARED,0x3d2431,0x7429dc,0x5152f2,0x812932,0x8e2716);             /* U::Destructible */
        TEST_RID_ROUTE(RID_ROUTE_SHARED,0xcaab18,0x106321a,0x106f549);                             /* ScriptEventHandler */
        TEST_RID_ROUTE(RID_ROUTE_UI,0x3d2431,0x7429dc,0x80d42e,0x8e26ca,0x86936c,0xaf0fc0);        /* the play23 stack */
        TEST_RID_ROUTE(RID_ROUTE_UI,0x3d1dd1,0x7426f8,0x80d1c7);                                   /* the other constructor */
        TEST_RID_ROUTE(RID_ROUTE_UI,0x997000,0xaf0fc0);                                            /* whatever the instance factory makes */
        TEST_RID_ROUTE(RID_ROUTE_VIEW,0xaddb73,0xada912,0xadab6e,0x9bd5f9);                        /* ICom::Message, from the simulation's events */
        TEST_RID_ROUTE(RID_ROUTE_VIEW,0x350ecb,0x7c7f9e,0x7da1c1,0xada8d5,0xadaa42);               /* the action a message start makes */
        TEST_RID_ROUTE(RID_ROUTE_UI,0xaddb73,0xada912,0xb3bb00);                                   /* the same from the UI's own slot */
        TEST_RID_ROUTE(RID_ROUTE_UI,0x116e538,0x75529d);TEST_RID_ROUTE(RID_ROUTE_UI,0xb1feab);TEST_RID_ROUTE(RID_ROUTE_UI,0xac472d);TEST_RID_ROUTE(RID_ROUTE_UI,0x779302,0xe186d2);
        TEST_RID_ROUTE(RID_ROUTE_VIEW,0x3d2431,0x7429dc,0x8e1b44,0x95bb77,0xd72456);               /* EffectObject */
        TEST_RID_ROUTE(RID_ROUTE_VIEW,0xd7313c,0xd74acb,0x818cb9,0x818193,0x81345c);               /* EffectObjectCreator, from the simulation */
        TEST_RID_ROUTE(RID_ROUTE_UI,0xd7313c,0xd74acb,0x89a7d0,0x121c225);                         /* the same in the render walk: the camera's */
        /* Body ends are exact. */
        TEST_RID_ROUTE(RID_ROUTE_SHARED,0x121c1ef);TEST_RID_ROUTE(RID_ROUTE_UI,0x121c1f0);TEST_RID_ROUTE(RID_ROUTE_UI,0x121c259);TEST_RID_ROUTE(RID_ROUTE_SHARED,0x121c25a);
        TEST_RID_ROUTE(RID_ROUTE_SHARED,0x80d42d);TEST_RID_ROUTE(RID_ROUTE_SHARED,0x80d42f);
        /* Slot 6, and the probe's own frame in it: stays shared, except what the table knows. */
        TEST_RID_ROUTE(RID_ROUTE_UI,0x997000,0xb3bb00);
        InterlockedExchange(&rid_probe_frame,1);
        TEST_RID_ROUTE(RID_ROUTE_SHARED,0x997000,0xb3bb00);TEST_RID_ROUTE(RID_ROUTE_UI,0x80d42e,0xb3bb00);TEST_RID_ROUTE(RID_ROUTE_UI,0x997000,0x121c225);
        InterlockedExchange(&rid_probe_frame,0);
        /* A presentation scope is all local, on its own thread only. */
        InterlockedExchange(&pres_open,1);
        TEST_RID_ROUTE(RID_ROUTE_UI,0x5c0017,0x5c7a99);
        ui_thread=GetCurrentThreadId()+1u;TEST_RID_ROUTE(RID_ROUTE_SHARED,0x5c0017,0x5c7a99);ui_thread=GetCurrentThreadId();
        InterlockedExchange(&pres_open,0);
        TEST_RID_ROUTE(RID_ROUTE_SHARED,0x5c0017,0x5c7a99);
        /* A view call of this client's alone, on its own thread only. */
        rid_scope(1);TEST_RID_ROUTE(RID_ROUTE_UI,0x5c0017,0x5c7a99);
        const DWORD me=rid_scope_thread;rid_scope_thread=me+1u;TEST_RID_ROUTE(RID_ROUTE_SHARED,0x5c0017,0x5c7a99);rid_scope_thread=me;
        rid_scope(0);TEST_RID_ROUTE(RID_ROUTE_SHARED,0x5c0017,0x5c7a99);assert(!rid_scope_thread);
        /* Off, and before the seal: everything shared. */
        InterlockedExchange(&rid_local_on,0);TEST_RID_ROUTE(RID_ROUTE_SHARED,0x80d42e);InterlockedExchange(&rid_local_on,1);
        rid_sealed=0;TEST_RID_ROUTE(RID_ROUTE_SHARED,0x80d42e);rid_sealed=1;
    }
    /* The case: client a's camera makes 25 more UI elements, on both the same simulation allocations and effects. */
    const rid_counts_t before_a=rid_counts(a.map),before_b=rid_counts(b.map);
    static uint64_t shared_a[100],shared_b[100],view_a[10],view_b[10],ui_a[25];
    test_rid_use(&a,NULL,lists_a);
    for(unsigned i=0;i<100;i++) {
        shared_a[i]=test_rid_insert(&a);
        if(!(i%4)) {ui_a[i/4]=rid_assign_ui(object);assert((ui_a[i/4]&0x1ffffffu)==0x1f00001u+i/4);}
        if(!(i%10)) view_a[i/10]=rid_assign_view(object);
    }
    rid_before=before_a;assert(rid_frame(5));
    const uint64_t next_a=rid_next_id,digest_a=rid_digest;
    char text[900];assert(rid_status(text,sizeof(text))>0 && strstr(text," allocs=100 ") && strstr(text," local=1,ui:25/0/1048550,view:10/0/1048566"));
    assert(rid_now.inserts-before_a.inserts==135);
    test_rid_use(&b,lists_a,lists_b);rid_digest=0;
    for(unsigned i=0;i<100;i++) {
        shared_b[i]=test_rid_insert(&b);
        if(!(i%10)) view_b[i/10]=rid_assign_view(object);
    }
    rid_before=before_b;assert(rid_frame(5));
    assert(rid_next_id==next_a && rid_digest==digest_a && !memcmp(shared_a,shared_b,sizeof(shared_a)) && !memcmp(view_a,view_b,sizeof(view_a)));
    assert(shared_a[0]==0x10000u && shared_a[99]==0x10000u+99u && view_a[0]==0x1e00001u && view_a[9]==0x1e00001u+9u);
    assert(rid_status(text,sizeof(text))>0 && strstr(text," allocs=100 ") && strstr(text," local=1,ui:0/0/1048575,view:10/0/1048566"));
    assert(a.blocks==1u && b.blocks==1u);
    /* A simulation allocation the table does not know on one client only still shows: the barrier's word moves. */
    {
        const uint64_t extra=test_rid_insert(&b);assert(extra);rid_digest=digest_a;
        assert(rid_frame(6));const uint64_t moved=rid_next_id,digest_moved=rid_digest;
        test_rid_use(&a,lists_b,lists_a);rid_digest=digest_a;assert(rid_frame(6));
        assert(rid_next_id!=moved && rid_digest!=digest_moved);
    }
    /* Local frees go back on their own list, generation up, FIFO; the shared fields do not move. */
    {
        rid_counts_t c=rid_counts(a.map);const LONG64 dropped=rid_dropped;const rid_local_t before=rid_local[RID_ROUTE_UI];
        assert(rid_remove(a.map,ui_a[3])&1u);
        rid_counts_t d=rid_counts(a.map);const rid_local_t *u=&rid_local[RID_ROUTE_UI];const uint32_t slot=(uint32_t)ui_a[3];
        assert(d.head==c.head && d.tail==c.tail && d.free==c.free && rid_dropped==dropped);
        assert(u->tail==slot && u->free==before.free+1u && u->frees==1u && u->head==before.head && !rid_slot_used(a.map,slot) && rid_slot_generation(a.map,slot)==1u);
        assert((*test_rid_entry(a.map,before.tail)>>32)==slot && (uint32_t)*test_rid_entry(a.map,slot)==before.tail);
        assert(!rid_remove(a.map,ui_a[3]));                /* a stale ID changes nothing */
        assert(rid_local[RID_ROUTE_UI].frees==1u);
        /* An emptied list takes a freed slot natively. */
        const rid_local_t held=rid_local[RID_ROUTE_VIEW];
        rid_local[RID_ROUTE_VIEW].head=rid_local[RID_ROUTE_VIEW].tail=rid_local[RID_ROUTE_VIEW].free=0;
        assert(rid_remove(a.map,view_a[1])&1u);
        assert(rid_local[RID_ROUTE_VIEW].head==(uint32_t)view_a[1] && rid_local[RID_ROUTE_VIEW].tail==(uint32_t)view_a[1] && rid_local[RID_ROUTE_VIEW].free==1u);
        rid_local[RID_ROUTE_VIEW]=held;
    }
    /* Shared frees: off the tail, nothing recycled, the walk still counts every slot. */
    {
        rid_counts_t c=rid_counts(a.map);const LONG64 dropped=rid_dropped;
        assert(rid_remove(a.map,shared_a[3])&1u);
        rid_counts_t d=rid_counts(a.map);
        assert(d.head==c.head && d.tail==c.tail && d.free==c.free && d.live==c.live-1u && rid_dropped==dropped+1 && rid_walk(a.map));
    }
    /* A list running out: no block is due while it lasts, then the shared list and a stop at the boundary. */
    {
        const uint32_t held=rid_local[RID_ROUTE_UI].free;rid_counts_t c=rid_counts(a.map);
        rid_local[RID_ROUTE_UI].free=RID_LOCAL_MIN;const uint32_t blocks=a.blocks;
        const uint64_t last_local=rid_assign_ui(object);assert((last_local&0x1ffffffu)>=0x1f00001u && a.blocks==blocks && rid_local[RID_ROUTE_UI].free==RID_LOCAL_MIN-1u);
        rid_counts_t d=rid_counts(a.map);assert(d.head==c.head && d.tail==c.tail && d.free==c.free);
        assert(!rid_failures);
        const uint64_t fallback=rid_assign_ui(object);assert(fallback==c.head && rid_failures==1 && !strcmp(rid_stage,"local_exhausted"));
        assert(!rid_frame(7));
        rid_local[RID_ROUTE_UI].free=held-1u;rid_failures=0;rid_stage="set";
    }
    /* The hook end to end: the original once, the row names the route; before the seal the direct caller only. */
    {
        memset(rid_callers,0,sizeof(rid_callers));rid_caller_count=0;rid_caller_overflow=0;
        char row[192];const uint64_t calls=created_calls;test_rid_assign_calls=0;
        InterlockedExchange(&pres_open,1);
        const uint64_t local=rid_assign(object,image+0x3d2431);
        InterlockedExchange(&pres_open,0);
        assert((local&0x1ffffffu)>=0x1f00001u && test_rid_assign_calls==1 && (uint64_t)created_calls==calls+1u);
        assert(rid_caller_count==1 && rid_caller_row(0,row,sizeof(row))>0 && strstr(row,"phase=play ") && strstr(row," route=ui count=1 frames=0,0,0,0,0,0,0,0"));
        const uint32_t head=*(uint32_t*)(a.map+0x50);
        const uint64_t shared=rid_assign(object,image+0x3d2431);
        assert(shared==head && test_rid_assign_calls==2 && rid_caller_count==2);
        rid_sealed=0;const uint32_t next_head=*(uint32_t*)(a.map+0x50);
        const uint64_t load=rid_assign(object,image+0x3d2431);assert(load==next_head && rid_caller_count==3);
        unsigned loads=0;for(unsigned i=0;rid_caller_row(i,row,sizeof(row));i++) loads+=strstr(row,"phase=load")!=NULL && strstr(row," route=shared count=1 frames=3d2431,0,0,0,0,0,0,0")!=NULL;
        assert(loads==1);
        memset(rid_callers,0,sizeof(rid_callers));rid_caller_count=0;
    }
    /* The status shows a set, armed run; a rerun of the arming is accepted only in set mode. */
    rid_sealed=0;assert(rid_local_arm());rid_mode=RID_OBSERVE;assert(!rid_local_arm());rid_mode=RID_SET;
    InterlockedExchange(&rid_active,0);InterlockedExchange(&rid_local_on,0);InterlockedExchange(&rid_probe_frame,0);
    rid_remove_native=NULL;rid_assign_native=NULL;rid_allocate_native=NULL;rid_local_built=0;memset(rid_local,0,sizeof(rid_local));memset(rid_local_now,0,sizeof(rid_local_now));
    rid_mode=RID_OFF;rid_base=0;rid_sealed=0;rid_failures=0;rid_dropped=0;rid_stage="idle";rid_digest=0;rid_next_id=0;rid_frames_done=0;rid_cut=rid_skipped=rid_head_before=rid_below_base=0;
    memset(&rid_now,0,sizeof(rid_now));memset(&rid_before,0,sizeof(rid_before));
    test_rid_free_process(&a);test_rid_free_process(&b);
    *(void**)(image+RID_MAP)=saved_map;stats.active=saved_active;ui_thread=saved_ui;created_installed=saved_created;
    printf("runtime IDs local lists/routing/no-recycle tests PASS\n");
}
static void test_runtime_ids_native_image(const char *path) {
    FILE *file=fopen(path,"rb");assert(file);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    assert(fread(&dos,1,sizeof(dos),file)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>0 && dos.e_lfanew<4096);
    assert(!fseek(file,dos.e_lfanew,SEEK_SET) && fread(&nt,1,sizeof(nt),file)==sizeof(nt) && nt.OptionalHeader.SizeOfImage==0x6fa9000);
    unsigned char *prior=image;image=VirtualAlloc(NULL,nt.OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(image);
    for(unsigned i=0;i<nt.FileHeader.NumberOfSections;i++) {
        IMAGE_SECTION_HEADER section;assert(fread(&section,1,sizeof(section),file)==sizeof(section));long next=ftell(file);
        if(section.SizeOfRawData) {
            assert(!fseek(file,section.PointerToRawData,SEEK_SET));
            assert(fread(image+section.VirtualAddress,1,section.SizeOfRawData,file)==section.SizeOfRawData);
        }
        assert(!fseek(file,next,SEEK_SET));
    }
    fclose(file);
    assert(rid_pins() && hash_ok(image+RID_REMOVE_ENTRY,414,"2ace935f514d696f4be0acbcb538baa81c82ebd95c2839d0f3906a73b340a921"));
    /* The route table: every call goes where the table says, every body is this build's; each one tampered refuses. */
    assert(rid_local_pins());
    for(unsigned i=0;i<RID_SITES;i++) if(rid_sites[i].target) {image[rid_sites[i].lo-1u]^=1;assert(!rid_local_pins());image[rid_sites[i].lo-1u]^=1;}
    for(unsigned i=0;i<RID_LOCAL_BODIES;i++) {
        unsigned char *first=image+rid_local_bodies[i].rva,*last=first+rid_local_bodies[i].size-1u;
        *first^=1;assert(!rid_local_pins());*first^=1;*last^=1;assert(!rid_local_pins());*last^=1;
    }
    assert(rid_local_pins());
    /* The block EnsureFreeSlots allocates, exactly as rid_allocate does; the play23 stack through the table. */
    assert(!memcmp(image+0xcc6ea,"\xc7\x44\x24\x20\x10\x00\x00\x00\xb9\xc0\xaa\xaa\x00\xe8",14) && 0xcc6fcu+*(int32_t*)(image+0xcc6f8)==RID_ALLOCATE && *(uint32_t*)(image+0xcc6f3)==RID_BLOCK_BYTES);
    assert(image[0xaf0fc0-5]==0xe8 && 0xaf0fc0u+*(int32_t*)(image+0xaf0fc0-4)==0x868e00u);
    assert(image[0x86936c-5]==0xe8 && 0x86936cu+*(int32_t*)(image+0x86936c-4)==0x8e0250u && image[0x8e26ca-5]==0xe8 && 0x8e26cau+*(int32_t*)(image+0x8e26ca-4)==0x80d420u);
    /* Block 31's last slot and block size as rid_local_link has them: EnsureFreeSlots' count, 0xfffff + (block != 31). */
    assert(!memcmp(image+0xcc751,"\x83\xfb\x1f\x41\x0f\x95\xc0\x41\x81\xc0\xff\xff\x0f\x00",14));
    const unsigned tamper[]={0xcc228,0xcc32b,0xcc500,0xcc660,0xcc670,0xcc843,0xced70,0xcee27};
    for(unsigned i=0;i<sizeof(tamper)/sizeof(*tamper);i++) {image[tamper[i]]^=1;assert(!rid_pins());image[tamper[i]]^=1;}
    /* The prefix the thunk relocates, and the entry word rid_install expects. */
    assert(!memcmp(image+RID_REMOVE_ENTRY,"\x48\x89\x5c\x24\x08\x48\x89\x6c",8));
    /* AssignNewID: g_UniverseIDMap, its lock +0x10, EnsureFreeSlots, the head +0x50; InsertAtSlot's return (generation<<25 | slot). */
    assert(0xcc239u+*(int32_t*)(image+0xcc235)==RID_MAP && !memcmp(image+0xcc239,"\x48\x8d\x5f\x10",4) && image[0xcc24f]==0xe8 && 0xcc254u+*(int32_t*)(image+0xcc250)==0xcc670u &&
           !memcmp(image+0xcc254,"\x44\x8b\x47\x50",4));
    /* Slot arithmetic as rid_bucket does it: block = (slot-1)>>20 against 0x20, +0x58, low 20 bits / 3 * 0x20. */
    assert(!memcmp(image+0xcc27d,"\x8b\xc1\xc1\xe8\x14\x83\xf8\x20",8) && !memcmp(image+0xcc287,"\x4c\x8b\x54\xc7\x58",5) && !memcmp(image+0xcc291,"\x81\xe1\xff\xff\x0f\x00",6) &&
           !memcmp(image+0xcc2a2,"\x48\xc1\xe0\x05",4));
    /* EnsureFreeSlots' threshold and block size; Find's slot and generation fields. */
    {
        unsigned threshold=0,size=0;
        for(unsigned at=0xcc670;at<0xcc670+468-4;at++) {
            if(*(uint32_t*)(image+at)==RID_FREE_MIN) threshold=at;
            if(*(uint32_t*)(image+at)==0xaaaac0u) size=at;
        }
        assert(threshold && size);
    }
    assert(!memcmp(image+0xced87,"\x25\xff\xff\xff\x01",5) && !memcmp(image+0xcede9,"\x49\xc1\xe9\x19",4));
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("runtime IDs native pins, map layout and Remove entry verified");
}
