/* Offline tests for find_* candidate order (native_gate_find_order): saved-ID
 * order independent of the native order, duplicate removal, unmapped entries,
 * observe mode, failure paths, scope rows, the call-site CAS, arming refusals
 * and bounded status/rows. Synthetic components and vectors in owned memory. */
static unsigned char test_fo_components[160][16];
static uintptr_t test_fo_buffer[FIND_ORDER_MAX+8],test_fo_vector[3];
static uintptr_t test_fo_refuse;
static int test_fo_reader(const void *source,void *dest,size_t bytes) {
    if(test_fo_refuse && (uintptr_t)source==test_fo_refuse) return 0;
    return read_local(source,dest,bytes);
}
/* Runtime ID r maps to saved ID 0x1000+r for r<100; 100.. are created after
 * load without a portable identity (test_lockstep_portable_created.h covers
 * created IDs). */
static uint64_t test_fo_lookup(uint64_t runtime,unsigned *klass) {
    uint64_t saved=runtime && runtime<100?0x1000+runtime:0;*klass=saved?WORLD_KEY_SAVED:WORLD_KEY_NONE;return saved;
}
static void *test_fo_component(unsigned i,uint64_t runtime) {
    memset(test_fo_components[i],0,16);memcpy(test_fo_components[i]+8,&runtime,8);return test_fo_components[i];
}
static void test_fo_set(void *const *items,unsigned n) {
    for(unsigned i=0;i<n;i++) test_fo_buffer[i]=(uintptr_t)items[i];
    test_fo_vector[0]=(uintptr_t)test_fo_buffer;test_fo_vector[1]=(uintptr_t)(test_fo_buffer+n);test_fo_vector[2]=(uintptr_t)(test_fo_buffer+n+4);
}
static void test_fo_run(void) {
    find_order_process((uintptr_t)(image+FIND_ORDER_SITE+5),test_fo_vector,test_fo_reader,test_fo_lookup);
}
static unsigned test_fo_count(void) {return (unsigned)((test_fo_vector[1]-test_fo_vector[0])/8);}
static void test_fo_reset(void) {
    memset(find_order_ring,0,sizeof(find_order_ring));memset(find_order_scopes,0,sizeof(find_order_scopes));find_order_scopes_tick=UINT64_MAX;
    find_order_calls=find_order_applied=find_order_observed=find_order_passed=find_order_threads=find_order_empty=0;
    find_order_candidates=find_order_duplicates=find_order_unmapped=find_order_reordered=find_order_collisions=0;
    find_order_oversize=find_order_unreadable=find_order_foreign=find_order_cursor=find_order_failures=0;test_fo_refuse=0;
}
static void test_find_order(void) {
    stats.active=1;boot_mode=1;boot_phase=3;stats.issued=63;ui_thread=GetCurrentThreadId();
    find_order_armed=1;find_order_mode=FIND_ORDER_SORT;InterlockedExchange(&find_order_active,1);test_fo_reset();
    script_rng_frame *saved_current=script_rng_current;script_rng_current=NULL;
    void *c10=test_fo_component(0,10),*c20=test_fo_component(1,20),*c30=test_fo_component(2,30),
         *n1=test_fo_component(3,101),*n2=test_fo_component(4,102);

    /* Saved IDs first in saved-ID order; created-after-load entries keep their
     * native relative order behind them. */
    void *a[]={c30,n1,c10,c20,n2};test_fo_set(a,5);test_fo_run();
    assert(test_fo_count()==5 && test_fo_buffer[0]==(uintptr_t)c10 && test_fo_buffer[1]==(uintptr_t)c20 && test_fo_buffer[2]==(uintptr_t)c30 &&
           test_fo_buffer[3]==(uintptr_t)n1 && test_fo_buffer[4]==(uintptr_t)n2);
    find_order_row_t *r=&find_order_ring[0];
    assert(find_order_applied==1 && find_order_reordered==1 && find_order_unmapped==2 && !find_order_failures && find_order_cursor==1);
    assert(r->tick==63 && !r->scope && r->ordinal==1 && r->raw==5 && !r->duplicates && r->unmapped==2 && r->count==5 && r->moved==4 &&
           r->shown==5 && r->ids[0]==0x100a && r->ids[1]==0x1014 && r->ids[2]==0x101e && !r->ids[3] && !r->ids[4]);
    /* Another process, another native order: same list, same set, other native hash. */
    uint64_t set=r->set,native=r->native;
    void *b[]={c20,n1,c30,n2,c10};test_fo_set(b,5);test_fo_run();
    assert(test_fo_buffer[0]==(uintptr_t)c10 && test_fo_buffer[1]==(uintptr_t)c20 && test_fo_buffer[2]==(uintptr_t)c30 &&
           test_fo_buffer[3]==(uintptr_t)n1 && test_fo_buffer[4]==(uintptr_t)n2);
    assert(find_order_ring[1].set==set && find_order_ring[1].native!=native && find_order_ring[1].ordinal==2);
    /* Already canonical: nothing is written or counted as reordered. */
    test_fo_run();assert(find_order_ring[2].moved==0 && find_order_reordered==2 && find_order_ring[2].set==set);

    /* A repeated pointer keeps its first position; the list shrinks. */
    test_fo_reset();void *d[]={c20,c10,c20,n1,n1};test_fo_set(d,5);uintptr_t end=test_fo_vector[1];test_fo_run();
    assert(test_fo_count()==3 && test_fo_vector[1]==end-16 && test_fo_buffer[0]==(uintptr_t)c10 && test_fo_buffer[1]==(uintptr_t)c20 &&
           test_fo_buffer[2]==(uintptr_t)n1 && find_order_duplicates==2 && find_order_ring[0].duplicates==2 && find_order_ring[0].raw==5);
    /* A null entry is an unmapped entry and is never read. */
    void *z[]={NULL,c10};test_fo_set(z,2);test_fo_run();
    assert(test_fo_buffer[0]==(uintptr_t)c10 && !test_fo_buffer[1] && find_order_ring[1].unmapped==1 && !find_order_failures);
    /* Empty lists are counted and recorded. */
    test_fo_set(NULL,0);test_fo_run();assert(find_order_empty==1 && find_order_ring[2].raw==0 && !find_order_failures);

    /* Observe: same measurement, vector untouched. */
    test_fo_reset();find_order_mode=FIND_ORDER_OBSERVE;test_fo_set(a,5);test_fo_run();
    assert(test_fo_buffer[0]==(uintptr_t)c30 && test_fo_buffer[4]==(uintptr_t)n2 && find_order_observed==1 && !find_order_applied &&
           find_order_ring[0].set==set && find_order_ring[0].native==native && find_order_ring[0].ids[0]==0x100a);
    test_fo_set(d,5);end=test_fo_vector[1];test_fo_run();assert(test_fo_vector[1]==end && find_order_ring[1].duplicates==2);
    find_order_mode=FIND_ORDER_SORT;

    /* Large lists take the qsort path and end in saved-ID order. */
    test_fo_reset();void *many[90];
    for(unsigned i=0;i<90;i++) many[i]=test_fo_component(10+i,(uint64_t)(1+(i*37)%90));
    test_fo_set(many,90);test_fo_run();
    for(unsigned i=1;i<90;i++) {
        uint64_t x,y;memcpy(&x,(unsigned char*)test_fo_buffer[i-1]+8,8);memcpy(&y,(unsigned char*)test_fo_buffer[i]+8,8);
        assert(x<y);
    }
    assert(find_order_ring[0].shown==FIND_ORDER_IDS && find_order_ring[0].count==90 && !find_order_failures);

    /* The enclosing keyed script scope names the row; ranks are per scope. */
    test_fo_reset();script_rng_frame owner,inner;memset(&owner,0,sizeof(owner));memset(&inner,0,sizeof(inner));
    owner.key=0x420342c014014929ULL;owner.owner=&owner;strcpy(owner.id.name,"FactionSubgoal_Recon.Evaluate");inner.owner=&owner;
    script_rng_current=&inner;test_fo_set(a,5);test_fo_run();test_fo_run();script_rng_current=NULL;test_fo_run();
    assert(find_order_ring[0].scope==owner.key && find_order_ring[0].ordinal==1 && find_order_ring[1].ordinal==2 &&
           !strcmp(find_order_ring[0].name,"FactionSubgoal_Recon.Evaluate") && !find_order_ring[2].scope && find_order_ring[2].ordinal==1);
    char row[1024];assert(find_order_row(0,row,sizeof(row))>0 && strstr(row,"index=0 tick=63 scope=420342c014014929 ord=1 mode=2 raw=5 dups=0 unmapped=2 n=5 ") &&
        strstr(row," name=FactionSubgoal_Recon.Evaluate ids=100a,1014,101e,0,0"));
    printf("NATIVE_GATE FIND_ORDER_ROW %s\n",row);
    stats.issued=64;test_fo_run();assert(find_order_ring[3].ordinal==1 && find_order_ring[3].tick==64);stats.issued=63;

    /* Failure paths leave the vector untouched. */
    test_fo_reset();test_fo_set(a,5);
    c20=test_fo_component(1,10);test_fo_run(); /* two components with one saved ID */
    assert(find_order_collisions==1 && find_order_failures==1 && test_fo_buffer[0]==(uintptr_t)c30 && !find_order_cursor);
    c20=test_fo_component(1,20);test_fo_refuse=(uintptr_t)c10+8;test_fo_run();
    assert(find_order_unreadable==1 && find_order_failures==2 && test_fo_buffer[0]==(uintptr_t)c30);test_fo_refuse=0;
    test_fo_vector[1]=test_fo_vector[0]-8;test_fo_run();assert(find_order_unreadable==2 && find_order_failures==3);
    test_fo_set(a,5);test_fo_vector[1]+=4;test_fo_run();assert(find_order_unreadable==3);
    test_fo_set(a,5);test_fo_vector[2]=test_fo_vector[0];test_fo_run();assert(find_order_unreadable==4);
    find_order_process((uintptr_t)(image+FIND_ORDER_SITE+5),NULL,test_fo_reader,test_fo_lookup);assert(find_order_unreadable==5);
    test_fo_vector[0]=(uintptr_t)test_fo_buffer;test_fo_vector[1]=test_fo_vector[2]=(uintptr_t)(test_fo_buffer+FIND_ORDER_MAX+1);
    test_fo_run();assert(find_order_oversize==1 && find_order_failures==7);
    test_fo_set(a,5);find_order_process((uintptr_t)(image+FIND_ORDER_SITE+6),test_fo_vector,test_fo_reader,test_fo_lookup);
    assert(find_order_foreign==1 && find_order_failures==8 && test_fo_buffer[0]==(uintptr_t)c30 && !find_order_cursor);
    /* Pass-through: inactive, stopped gate, other thread. */
    InterlockedExchange(&find_order_active,0);test_fo_run();assert(find_order_passed==1);InterlockedExchange(&find_order_active,1);
    stats.active=0;test_fo_run();assert(find_order_passed==2);stats.active=1;
    ui_thread=GetCurrentThreadId()+1;test_fo_run();assert(find_order_threads==1 && test_fo_buffer[0]==(uintptr_t)c30);ui_thread=GetCurrentThreadId();

    /* Call-site CAS: only the rel32 of E8 at 0xb87ae3 changes; restore needs our value. */
    unsigned char *saved_thunk=thunk;thunk=image+0x6000000;
    unsigned char *site=image+FIND_ORDER_SITE;memset(site-16,0x90,48);site[0]=0xe8;
    int32_t rel=(int32_t)((intptr_t)(image+FIND_ORDER_ASSIGN)-(intptr_t)(site+5));memcpy(site+1,&rel,4);
    unsigned char before[48];memcpy(before,site-16,48);
    assert(find_order_prepare() && find_order_original_rel==rel);
    assert(find_order_swap(find_order_original_rel,find_order_patched_rel));find_order_installed=1;
    memcpy(&rel,site+1,4);assert(site[0]==0xe8 && site+5+rel==thunk+FIND_ORDER_THUNK);
    for(unsigned j=0;j<48;j++) if(j<17 || j>20) assert((site-16)[j]==before[j]);
    assert(find_order_owned() && !find_order_swap(find_order_original_rel,find_order_patched_rel) && !find_order_prepare());
    site[3]^=1;assert(!find_order_owned());
    find_order_restore();assert(!find_order_restored && find_order_installed && !find_order_active);
    site[3]^=1;find_order_restore();
    assert(find_order_restored && !find_order_installed && !memcmp(site-16,before,48) && find_order_owned());
    site[0]=0xe9;assert(!find_order_prepare());site[0]=0xe8;
    memcpy(&rel,site+1,4);rel+=1;memcpy(site+1,&rel,4);assert(!find_order_prepare());rel-=1;memcpy(site+1,&rel,4);

    /* Arming refusals precede any write. */
    find_order_armed=find_order_restored=0;find_order_mode=FIND_ORDER_OFF;boot_phase=0;stats.issued=0;
    int saved_identity=world_identity_installed;world_identity_installed=1;
    assert(!find_order_arm(0) && !find_order_arm(3) && !find_order_arm(FIND_ORDER_SORT)); /* pins fail on the placeholder */
    world_identity_installed=0;assert(!find_order_arm(FIND_ORDER_OBSERVE));world_identity_installed=saved_identity;
    assert(!find_order_armed && !find_order_installed && !find_order_active && !memcmp(site-16,before,48));
    memset(site-16,0,48);thunk=saved_thunk;

    /* Bounded status and rows. */
    char status[768];assert(find_order_status(status,sizeof(status))>0 && strstr(status,"armed=0 installed=0 active=0 mode=0 restored=0 "));
    printf("NATIVE_GATE FIND_ORDER tick=63 %s\n",status);
    find_order_armed=find_order_installed=find_order_restored=find_order_mode=INT32_MIN;InterlockedExchange(&find_order_active,(LONG)(-2147483647-1));
    find_order_calls=find_order_applied=find_order_observed=find_order_passed=find_order_threads=find_order_empty=INT64_MIN;
    find_order_candidates=find_order_duplicates=find_order_unmapped=find_order_reordered=find_order_collisions=INT64_MIN;
    find_order_oversize=find_order_unreadable=find_order_foreign=find_order_failures=INT64_MIN;find_order_cursor=INT64_MAX;
    int n=find_order_status(status,sizeof(status));
    assert(n>0 && (unsigned)n+strlen("[X4LAN] NATIVE_GATE FIND_ORDER tick=18446744073709551615 \n")<sizeof(status));
    for(unsigned i=0;i<FIND_ORDER_RING;i++) {
        find_order_row_t *e=&find_order_ring[i];e->tick=e->scope=e->ordinal=e->set=e->native=UINT64_MAX;
        e->raw=e->duplicates=e->unmapped=e->count=e->moved=UINT32_MAX;e->shown=FIND_ORDER_IDS;e->mode=255;
        for(unsigned j=0;j<FIND_ORDER_IDS;j++) e->ids[j]=UINT64_MAX;
        memset(e->name,'y',sizeof(e->name)-1);e->name[5]=',';e->name[sizeof(e->name)-1]=0;
    }
    n=find_order_row((uint64_t)INT64_MAX-1,row,sizeof(row));
    assert(n>0 && (unsigned)n+strlen("[X4LAN] NATIVE_GATE FIND_ORDER_ROW \n")<sizeof(row) && strstr(row," name=yyyyy_yyy"));
    assert(find_order_row(0,row,sizeof(row))>0 && !strcmp(row,"index=0 evicted=1") && !find_order_row((uint64_t)INT64_MAX,row,sizeof(row)));
    find_order_ring[0].shown=0;find_order_cursor=1;assert(find_order_row(0,row,sizeof(row))>0 && strstr(row," ids=-"));
    find_order_armed=find_order_installed=find_order_restored=find_order_mode=0;InterlockedExchange(&find_order_active,0);test_fo_reset();
    script_rng_current=saved_current;stats.active=0;boot_mode=0;boot_phase=0;stats.issued=0;
    puts("native find order saved-ID sort/dedup/observe/scope/failure/CAS tests PASS");
}
/* With the X4 9.00 executable: the Execute pin, lea rdx,[rsp+0x40] (the
 * candidate vector) and lea rcx,[rbp+0xf0] before the E8 to the store, and
 * tamper refusals. */
static void test_fo_native_image(const char *path) {
    FILE *file=fopen(path,"rb");assert(file);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    assert(fread(&dos,1,sizeof(dos),file)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>0 && dos.e_lfanew<4096);
    assert(!fseek(file,dos.e_lfanew,SEEK_SET) && fread(&nt,1,sizeof(nt),file)==sizeof(nt) && nt.OptionalHeader.SizeOfImage==0x6fa9000);
    unsigned char *prior=image,*prior_thunk=thunk;image=VirtualAlloc(NULL,nt.OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(image);
    for(unsigned i=0;i<nt.FileHeader.NumberOfSections;i++) {
        IMAGE_SECTION_HEADER section;assert(fread(&section,1,sizeof(section),file)==sizeof(section));long next=ftell(file);
        assert(section.VirtualAddress<=nt.OptionalHeader.SizeOfImage && section.SizeOfRawData<=nt.OptionalHeader.SizeOfImage-section.VirtualAddress);
        if(section.SizeOfRawData) {
            assert(!fseek(file,section.PointerToRawData,SEEK_SET));
            assert(fread(image+section.VirtualAddress,1,section.SizeOfRawData,file)==section.SizeOfRawData);
        }
        assert(!fseek(file,next,SEEK_SET));
    }
    fclose(file);thunk=image+0x6f00000;
    assert(find_order_pinned() && find_order_prepare());
    int32_t rel;memcpy(&rel,image+FIND_ORDER_SITE+1,4);
    assert(image[FIND_ORDER_SITE]==0xe8 && FIND_ORDER_SITE+5+rel==FIND_ORDER_ASSIGN && rel==find_order_original_rel);
    assert(!memcmp(image+FIND_ORDER_SITE-12,"\x48\x8d\x54\x24\x40\x48\x8d\x8d\xf0\x00\x00\x00",12));
    assert(((FIND_ORDER_SITE+1)&63)<=60);
    void *vtable_slot=NULL;memcpy(&vtable_slot,image+0x2c0f3c8+0x18,8); /* file bytes: preferred-base VA of Execute */
    assert((uintptr_t)vtable_slot==0x140000000ULL+FIND_ORDER_EXECUTE);
    const unsigned tampers[]={FIND_ORDER_EXECUTE,FIND_ORDER_SITE,FIND_ORDER_SITE+4,0xb87a66,0xb87b42};
    for(unsigned i=0;i<sizeof(tampers)/sizeof(*tampers);i++) {image[tampers[i]]^=1;assert(!find_order_pinned());image[tampers[i]]^=1;}
    assert(find_order_pinned());find_order_original_rel=find_order_patched_rel=0;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;thunk=prior_thunk;
    puts("find order call site native pin, call target and tamper refusals PASS");
}
