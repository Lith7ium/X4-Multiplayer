/* Gravidar sector round-robin: native cursor arithmetic, the admission proof,
 * frame location in a real named thread's stack, boot zeroing under the ready
 * lock, per-frame advance checks and fail-closed paths. No native code runs. */
typedef struct {HANDLE ready,release;unsigned char *worker;volatile uint64_t *slot;} test_as_thread;
static DWORD WINAPI test_as_gravidar(LPVOID parameter) {
    test_as_thread *t=parameter;volatile uint64_t frame[0x100];
    for(unsigned i=0;i<0x100;i++) frame[i]=0x5a5a5a5a00000000ULL+i;
    /* Fake trampoline->Run frame: E=frame+0x60, cursor slot E-0x288. */
    frame[0x60]=(uint64_t)(uintptr_t)(image+AI_SCHED_RETURN);frame[0x61]=(uint64_t)(uintptr_t)t->worker;
    frame[0x60-AI_SCHED_CURSOR/8]=37;t->slot=&frame[0x60-AI_SCHED_CURSOR/8];
    SetEvent(t->ready);WaitForSingleObject(t->release,INFINITE);
    frame[0x61]=0;return 0;
}
static void test_as_reset(void) {
    ai_sched_armed=0;ai_sched_worker=NULL;ai_sched_entry=0;ai_sched_slot=NULL;
    ai_sched_zeroed=ai_sched_latched=ai_sched_admitted=ai_sched_pending=ai_sched_restored=0;
    ai_sched_binds=ai_sched_boot_checks=ai_sched_boot_busy=ai_sched_boot_writes=ai_sched_boot_prior=0;
    ai_sched_boot_cursor=ai_sched_boot_count=ai_sched_cursor=ai_sched_count=ai_sched_samples=ai_sched_irregular=0;
    ai_sched_failures=0;ai_sched_hash=0xcbf29ce484222325ULL;ai_sched_stage="idle";ai_sched_mask=0;
    ai_sched_held=ai_sched_released=ai_sched_permuted=0;ai_sched_order_count=0;
    ai_sched_hold_phase=ai_sched_hold_enabled=ai_sched_orders=ai_sched_order_restores=ai_sched_order_moved=0;
    ai_sched_order_hash=ai_sched_order_unmapped=ai_sched_priority_native=ai_sched_priority_portable=0;
    ai_sched_order_fallback=ai_sched_order_collisions=0;ai_sched_fallback_note[0]=0;
}
/* A U::Macro whose name is an MSVC string: inline below 16 bytes, else heap. */
static void test_as_macro(unsigned char *macro,const char *name) {
    size_t n=strlen(name);memset(macro,0,0x40);*(void**)macro=image+0x2ba5430;
    if(n<16) {memcpy(macro+0x20,name,n+1);*(uint64_t*)(macro+0x38)=15;}
    else {*(const char**)(macro+0x20)=name;*(uint64_t*)(macro+0x38)=n;}
    *(uint64_t*)(macro+0x30)=n;
}
static unsigned test_as_native_calls,test_as_native_unlocked;
static void test_as_native(void *worker,unsigned char reset) {
    CRITICAL_SECTION *ready=(CRITICAL_SECTION*)((unsigned char*)worker+0x6960);
    int owned=(DWORD)(uintptr_t)ready->OwningThread==GetCurrentThreadId();
    assert(!reset && (owned || test_as_native_unlocked));++test_as_native_calls;
}
static void test_ai_schedule(void) {
    /* 0x10aefd0..0x10af119: max(1,count/10) steps, wrap to 0 at count. */
    assert(ai_sched_step(0)==1 && ai_sched_step(9)==1 && ai_sched_step(25)==2 && ai_sched_step(300)==30);
    assert(ai_sched_advance(0,0)==0 && ai_sched_advance(0,1)==0 && ai_sched_advance(0,2)==1 && ai_sched_advance(1,2)==0);
    assert(ai_sched_advance(0,25)==2 && ai_sched_advance(23,25)==0 && ai_sched_advance(24,25)==1);
    assert(ai_sched_advance(30,25)==1); /* a start past the end skips one step, then wraps */
    /* Admission proof: one pass ends at advance(0,count) only from cursor 0. */
    for(uint64_t count=0;count<=300;count++) for(uint64_t start=0;start<=count+12;start++)
        assert((ai_sched_advance(start,count)==ai_sched_advance(0,count))==(start==0 || count<=1));
    /* Window scan: exactly one [return, worker] pair with room for the slot. */
    uint64_t words[0x80]={0};uintptr_t entry=0;const uintptr_t low=0x100000;
    words[0x70]=0x1111;words[0x71]=0x2222;
    assert(ai_sched_scan(words,0x80,low,low,0x1111,0x2222,&entry) && entry==low+0x70*8);
    assert(ai_sched_scan(words,0x80,low,low+0xf8,0x1111,0x2222,&entry) && !ai_sched_scan(words,0x80,low,low+0x100,0x1111,0x2222,&entry)); /* slot E-0x288 below StackLimit */
    words[0x20]=0x1111;words[0x21]=0x2222;assert(!ai_sched_scan(words,0x80,low,0,0x1111,0x2222,&entry));
    words[0x21]=0x3333;assert(ai_sched_scan(words,0x80,low,0,0x1111,0x2222,&entry) && entry==low+0x70*8);
    assert(!ai_sched_scan(words,0x80,low,0,0x1111,0x4444,&entry));

    /* Fake worker: vtable, ready lock/byte, enabled word, 25-entry list. */
    static unsigned char worker[0x7000];static unsigned char list[64*32];
    memset(worker,0,sizeof(worker));*(void**)worker=image+AI_SCHED_VTABLE;*(uint32_t*)(worker+0x40)=2;*(uint32_t*)(worker+0x44)=1;
    worker[0x6988]=1;InitializeCriticalSection((CRITICAL_SECTION*)(worker+0x6960));
    *(unsigned char**)(worker+0x68b8)=list;*(unsigned char**)(worker+0x68c0)=list+25*32;
    void *prior_worker=*(void**)(image+AI_SCHED_WORKER);*(void**)(image+AI_SCHED_WORKER)=worker;
    test_as_thread t={CreateEventW(NULL,TRUE,FALSE,NULL),CreateEventW(NULL,TRUE,FALSE,NULL),worker,NULL};
    typedef HRESULT (WINAPI *describe_fn)(HANDLE,PCWSTR);
    describe_fn describe=(describe_fn)(void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"SetThreadDescription");
    assert(t.ready && t.release && describe);
    HANDLE thread=CreateThread(NULL,0,test_as_gravidar,&t,0,NULL);assert(thread);
    assert(WaitForSingleObject(t.ready,5000)==WAIT_OBJECT_0 && t.slot && *t.slot==37);
    test_as_reset();ai_sched_armed=1;stats.issued=0;
    assert(!ai_sched_boot(1) && !strcmp(ai_sched_stage,"thread") && !ai_sched_binds); /* no thread named Gravidar yet */
    test_as_reset();ai_sched_armed=1;
    assert(SUCCEEDED(describe(thread,L"Gravidar")));

    /* Phase 0: an enabled (menu) Gravidar can still be kicked, so nothing is written. */
    assert(ai_sched_boot(0) && ai_sched_binds==1 && ai_sched_slot==t.slot && *t.slot==37 && !ai_sched_zeroed);
    assert(!strcmp(ai_sched_stage,"boot_menu") && !ai_sched_boot_writes);
    *(uint32_t*)(worker+0x44)=0;assert(ai_sched_boot(0) && *t.slot==0 && ai_sched_zeroed && ai_sched_boot_writes==1 && ai_sched_boot_prior==37);
    *(uint32_t*)(worker+0x44)=1;*t.slot=5;assert(ai_sched_boot(0) && *t.slot==5 && !ai_sched_zeroed); /* enabled again: unproven */

    /* Phases 1-2: publishes are held; the idle cursor is zeroed, busy checks change nothing. */
    assert(ai_sched_boot(1) && *t.slot==0 && ai_sched_zeroed && ai_sched_boot_writes==2 && ai_sched_boot_prior==5 && !ai_sched_latched);
    assert(ai_sched_boot(1) && ai_sched_boot_writes==2 && !ai_sched_latched && ai_sched_binds==1);
    worker[0x6988]=0;assert(ai_sched_boot(2) && ai_sched_boot_busy==1);
    *t.slot=2;assert(ai_sched_boot(2) && ai_sched_boot_busy==2 && !ai_sched_latched); /* load pass running */
    worker[0x6988]=1;assert(ai_sched_boot(2) && ai_sched_latched && ai_sched_boot_cursor==2 && ai_sched_boot_count==25);
    *t.slot=4;assert(ai_sched_boot(2) && *t.slot==4); /* latched: never written again */
    *t.slot=2;

    /* Admission: one pass from 0 over 25 entries. */
    worker[0x6988]=0;assert(!ai_sched_admit() && !strcmp(ai_sched_stage,"admit_busy"));worker[0x6988]=1;
    assert(ai_sched_admit() && ai_sched_admitted && ai_sched_cursor==2 && ai_sched_count==25 && !ai_sched_pending);
    assert(ai_sched_boot(2) && *t.slot==2); /* admitted: boot checks are inert */
    uint64_t admitted_hash=ai_sched_hash;

    /* Frames: unchanged before the first gated publish, then one native pass each. */
    stats.issued=1;assert(ai_sched_frame() && ai_sched_pending && ai_sched_samples==1 && ai_sched_sample_tick==1);
    stats.issued=2;*t.slot=4;assert(ai_sched_frame() && ai_sched_cursor==4);
    *(unsigned char**)(worker+0x68c0)=list+30*32; /* the publish appended five entries */
    stats.issued=3;*t.slot=ai_sched_advance(4,30);assert(ai_sched_frame() && ai_sched_cursor==7 && ai_sched_count==30);
    assert(ai_sched_hash!=admitted_hash && ai_sched_samples==3);
    stats.issued=4;assert(!ai_sched_frame() && ai_sched_irregular==1 && !strcmp(ai_sched_stage,"irregular")); /* skipped pass */
    *t.slot=ai_sched_advance(7,30);*(uint32_t*)(worker+0x44)=0;assert(!ai_sched_frame() && !strcmp(ai_sched_stage,"disabled"));
    *(uint32_t*)(worker+0x44)=1;assert(ai_sched_frame() && ai_sched_samples==4);
    *(unsigned char**)(worker+0x68c0)=list+30*32+8;stats.issued=5;assert(!ai_sched_frame() && !strcmp(ai_sched_stage,"frame_read"));
    *(unsigned char**)(worker+0x68c0)=list+25*32;
    char status[640];assert(ai_sched_status(status,sizeof(status)));
    assert(strstr(status,"armed=1 mask=0 binds=1 ") && strstr(status," boot_writes=2 boot_prior=5 zeroed=1 latched=1 admitted=1 boot_cursor=2 boot_count=25 "));
    assert(strstr(status," irregular=1 ") && strstr(status," restored=0 stage=frame_read failures=0"));

    /* Fail closed: a load pass from elsewhere, an unproven zero, another worker, a moved frame. */
    test_as_reset();ai_sched_armed=1;ai_sched_zeroed=1;*t.slot=3;assert(!ai_sched_admit() && !strcmp(ai_sched_stage,"admit_phase"));
    test_as_reset();ai_sched_armed=1;*t.slot=2;assert(!ai_sched_admit() && !strcmp(ai_sched_stage,"admit_unzeroed"));
    *(unsigned char**)(worker+0x68c0)=list+32;test_as_reset();ai_sched_armed=1;ai_sched_zeroed=1;*t.slot=0;
    assert(ai_sched_admit() && !ai_sched_latched); /* one entry: the pass leaves 0 */
    *(unsigned char**)(worker+0x68c0)=list+25*32;
    test_as_reset();ai_sched_armed=1;assert(!ai_sched_frame() && !strcmp(ai_sched_stage,"frame_unadmitted"));
    assert(ai_sched_boot(1) && ai_sched_binds==1);
    static unsigned char other[0x100];*(void**)other=image+AI_SCHED_VTABLE;*(void**)(image+AI_SCHED_WORKER)=other;
    assert(!ai_sched_boot(1) && !strcmp(ai_sched_stage,"frame_moved"));*(void**)(image+AI_SCHED_WORKER)=worker;
    *(void**)worker=image+0x2c68868;assert(!ai_sched_boot(1) && !strcmp(ai_sched_stage,"worker"));*(void**)worker=image+AI_SCHED_VTABLE;
    t.slot[AI_SCHED_CURSOR/8+1]=0;assert(!ai_sched_boot(1) && !strcmp(ai_sched_stage,"frame_moved"));
    t.slot[AI_SCHED_CURSOR/8+1]=(uint64_t)(uintptr_t)worker;assert(ai_sched_boot(1));
    *t.slot=AI_SCHED_ENTRIES+1;test_as_reset();ai_sched_armed=1;assert(!ai_sched_boot(1) && !strcmp(ai_sched_stage,"boot_read"));

    /* Hold (mask 2): phase 0 never holds; phases 1-2 hold an idle, disabled worker. */
    test_as_reset();ai_sched_armed=1;ai_sched_mask=AI_SCHED_GRAVIDAR|AI_SCHED_HOLD;*t.slot=9;*(uint32_t*)(worker+0x44)=0;
    assert(ai_sched_boot(0) && !ai_sched_held && worker[0x6988]==1 && *t.slot==0 && ai_sched_zeroed);
    *t.slot=4;assert(ai_sched_boot(1) && ai_sched_held && worker[0x6988]==0 && *t.slot==0 && ai_sched_boot_prior==4);
    assert(!strcmp(ai_sched_stage,"boot_hold") && ai_sched_hold_phase==1);
    uint64_t checks=ai_sched_boot_checks;assert(ai_sched_boot(2) && ai_sched_boot_checks==checks && !worker[0x6988]); /* held: inert */
    *(uint32_t*)(worker+0x44)=1; /* Init enabled the worker; its kick saw ready 0 */
    assert(ai_sched_release() && worker[0x6988]==1 && !ai_sched_held && ai_sched_released);
    *t.slot=2;assert(!ai_sched_admit() && !strcmp(ai_sched_stage,"admit_phase")); /* a held load pass leaves 0 */
    *t.slot=0;assert(ai_sched_admit() && ai_sched_admitted && !ai_sched_latched && ai_sched_cursor==0);
    status[0]=0;assert(ai_sched_status(status,sizeof(status)) && strstr(status," held=0 released=1 hold_phase=1 hold_enabled=0 "));
    test_as_reset();ai_sched_armed=1;ai_sched_mask=AI_SCHED_GRAVIDAR|AI_SCHED_HOLD;*(uint32_t*)(worker+0x44)=0;
    assert(ai_sched_boot(1) && ai_sched_held);*t.slot=2;*(uint32_t*)(worker+0x44)=1;
    assert(!ai_sched_release() && !strcmp(ai_sched_stage,"hold_pass") && worker[0x6988]==1 && !ai_sched_held);
    test_as_reset();ai_sched_armed=1;ai_sched_mask=AI_SCHED_GRAVIDAR|AI_SCHED_HOLD;*(uint32_t*)(worker+0x44)=0;*t.slot=0;
    assert(ai_sched_boot(1) && ai_sched_held);assert(!ai_sched_release() && !strcmp(ai_sched_stage,"hold_no_init") && worker[0x6988]==1);
    test_as_reset();ai_sched_armed=1;ai_sched_mask=AI_SCHED_GRAVIDAR|AI_SCHED_HOLD;*(uint32_t*)(worker+0x44)=1;
    assert(ai_sched_boot(1) && !ai_sched_held && ai_sched_hold_enabled==1 && worker[0x6988]==1);
    assert(!ai_sched_release() && !strcmp(ai_sched_stage,"hold_enabled"));
    test_as_reset();ai_sched_armed=1;ai_sched_mask=AI_SCHED_GRAVIDAR|AI_SCHED_HOLD;*(uint32_t*)(worker+0x44)=0;
    assert(ai_sched_boot(2) && ai_sched_held && !worker[0x6988]);ai_sched_restore();
    assert(worker[0x6988]==1 && !ai_sched_held && ai_sched_restored); /* stop gives the ready byte back */
    *(uint32_t*)(worker+0x44)=1;

    /* Order (mask 4): six entries, saved IDs unlike the address order, one empty entry. */
    static unsigned char sectors[5][0x80];static unsigned char order[6*32],before[6*32];
    world_identity_entry *prior_identities=world_identities;LONG64 prior_identity_count=world_identity_count;
    world_identities=VirtualAlloc(NULL,sizeof(world_identity_entry)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);world_identity_count=0;
    const uint64_t saved[5]={50,10,40,30,20};
    memset(order,0,sizeof(order));
    for(unsigned i=0;i<6;i++) {
        if(i<5) {*(uint64_t*)(sectors[i]+8)=101+i;assert(world_identity_record(101+i,saved[i]));*(unsigned char**)(order+i*32)=sectors[i];}
        for(unsigned b=8;b<32;b++) order[i*32+b]=(unsigned char)(0x10*i+b);
    }
    memcpy(before,order,sizeof(order));InitializeCriticalSection((CRITICAL_SECTION*)(worker+0x6890));
    *(unsigned char**)(worker+0x68b8)=order;*(unsigned char**)(worker+0x68c0)=order+6*32;*(uint64_t*)(worker+0x68d0)=2;
    test_as_reset();ai_sched_armed=1;ai_sched_mask=AI_SCHED_GRAVIDAR|AI_SCHED_ORDER;assert(ai_sched_bind());
    test_as_native_calls=0;assert(ai_sched_publish_with(test_as_native,worker,0) && test_as_native_calls==1 && ai_sched_permuted);
    const unsigned expected[6]={1,4,3,2,0,5}; /* saved 10,20,30,40,50, then the empty entry */
    for(unsigned i=0;i<6;i++) assert(!memcmp(order+i*32,before+expected[i]*32,32));
    assert(*(uint64_t*)(worker+0x68d0)==3 && ai_sched_priority_native==2 && ai_sched_priority_portable==3);
    assert(ai_sched_order_count==6 && ai_sched_order_moved==5 && ai_sched_orders==1 && ai_sched_order_hash);
    uint64_t portable_hash=ai_sched_order_hash;
    ai_sched_admitted=1;assert(!ai_sched_frame() && !strcmp(ai_sched_stage,"order_unrestored"));
    order[0*32+16]=0xee; /* the pass changed the first portable entry (address entry 1) */
    worker[0x6988]=0;assert(!ai_sched_settle() && !strcmp(ai_sched_stage,"order_busy") && ai_sched_permuted);worker[0x6988]=1;
    assert(ai_sched_settle() && !ai_sched_permuted && ai_sched_order_restores==1 && *(uint64_t*)(worker+0x68d0)==2);
    assert(order[1*32+16]==0xee);order[1*32+16]=before[1*32+16];assert(!memcmp(order,before,sizeof(order)));
    /* The same sectors at other addresses give the same portable order and hash. */
    unsigned char swap[32];memcpy(swap,order+0*32,32);memcpy(order+0*32,order+3*32,32);memcpy(order+3*32,swap,32);
    *(uint64_t*)(worker+0x68d0)=1; /* the player's sector (saved 40) now sits at address index 1... */
    memcpy(swap,order+1*32,32);memcpy(order+1*32,order+2*32,32);memcpy(order+2*32,swap,32);
    assert(ai_sched_publish_with(test_as_native,worker,0) && ai_sched_order_hash==portable_hash && ai_sched_priority_portable==3);
    for(unsigned i=0;i<6;i++) assert(!memcmp(order+i*32,before+expected[i]*32,32));
    assert(ai_sched_settle() && *(uint64_t*)(worker+0x68d0)==1);
    memcpy(order,before,sizeof(order));*(uint64_t*)(worker+0x68d0)=2;
    /* Fail closed: an unmapped or duplicate sector leaves the vector alone; a changed vector is not restored. */
    *(uint64_t*)(sectors[3]+8)=999;assert(!ai_sched_publish_with(test_as_native,worker,0) && !ai_sched_permuted);
    assert(!strcmp(ai_sched_stage,"order_unmapped") && !memcmp(order,before,sizeof(order)));
    assert(ai_sched_order_unmapped==1 && !ai_sched_order_fallback);
    char note[1200];assert(ai_sched_fallbacks(note,sizeof(note)) && strstr(note,"fallback=0 unmapped=1 ") && strstr(note,"runtime=3e7 ") && strstr(note,"macro=? "));
    assert(world_identity_record(999,10)); /* duplicates sector 1's saved ID */
    assert(!ai_sched_publish_with(test_as_native,worker,0) && !ai_sched_permuted && !memcmp(order,before,sizeof(order)));
    assert(!strcmp(ai_sched_stage,"order_collision") && ai_sched_order_collisions==1);
    /* Fallback: a sector without a saved ID keys on its macro name and its parent's saved ID. */
    static unsigned char cluster[0x80],macro_long[0x40],macro_short[0x40],macro_other[0x40],macro_parent[0x40];
    static const char long_name[]="cluster_99_sector001_macro",other_name[]="cluster_98_sector001_macro",parent_name[]="cluster_99_macro";
    void *prior_getter=*(void**)(image+0x2ba5430+18*8);*(void**)(image+0x2ba5430+18*8)=image+0xefb10;
    memset(cluster,0,sizeof(cluster));*(uint64_t*)(cluster+8)=200;assert(world_identity_record(200,7));
    test_as_macro(macro_long,long_name);test_as_macro(macro_short,"sec_x");test_as_macro(macro_other,other_name);test_as_macro(macro_parent,parent_name);
    *(uint64_t*)(sectors[3]+8)=998;*(unsigned char**)(sectors[3]+0x60)=macro_long;*(unsigned char**)(sectors[3]+0x70)=cluster;
    assert(ai_sched_publish_with(test_as_native,worker,0) && ai_sched_permuted && ai_sched_order_fallback==1 && !ai_sched_order_unmapped);
    const unsigned fallback_order[6]={1,4,2,0,3,5}; /* saved 10,20,40,50, the fallback, the empty entry */
    for(unsigned i=0;i<6;i++) assert(!memcmp(order+i*32,before+fallback_order[i]*32,32));
    assert(ai_sched_keys[3]>=AI_SCHED_FALLBACK_BIT && ai_sched_keys[3]!=UINT64_MAX && ai_sched_priority_portable==2);
    uint64_t fallback_key=ai_sched_keys[3],fallback_hash=ai_sched_order_hash;
    assert(ai_sched_fallbacks(note,sizeof(note)) && strstr(note,"fallback=1 unmapped=0 ") &&
           strstr(note,"runtime=3e6 ") && strstr(note,"macro=cluster_99_sector001_macro parent=c8/7 "));
    char wide_status[1024];assert(ai_sched_status(wide_status,sizeof(wide_status)) && strstr(wide_status," order_fallback=1 order_unmapped=0 "));
    assert(ai_sched_settle() && !memcmp(order,before,sizeof(order)));
    /* Another process: another runtime ID and address, same macro and parent: same key and order hash. */
    *(uint64_t*)(sectors[3]+8)=997;memcpy(swap,order+3*32,32);memcpy(order+3*32,order+4*32,32);memcpy(order+4*32,swap,32);
    assert(ai_sched_publish_with(test_as_native,worker,0) && ai_sched_keys[4]==fallback_key && ai_sched_order_hash==fallback_hash);
    for(unsigned i=0;i<6;i++) assert(!memcmp(order+i*32,before+fallback_order[i]*32,32));
    assert(ai_sched_settle());memcpy(order,before,sizeof(order));
    /* An inline (short) macro name works; an unmapped parent contributes its own macro. */
    *(unsigned char**)(sectors[3]+0x60)=macro_short;assert(ai_sched_publish_with(test_as_native,worker,0) && ai_sched_settle());
    assert(ai_sched_keys[3]>=AI_SCHED_FALLBACK_BIT && ai_sched_keys[3]!=fallback_key);
    *(unsigned char**)(sectors[3]+0x60)=macro_long;*(uint64_t*)(cluster+8)=201;*(unsigned char**)(cluster+0x60)=macro_parent;
    assert(ai_sched_publish_with(test_as_native,worker,0) && ai_sched_settle() && ai_sched_keys[3]!=fallback_key);
    assert(ai_sched_fallbacks(note,sizeof(note)) && strstr(note," parent=c9/"));
    *(unsigned char**)(cluster+0x60)=NULL; /* nothing portable about the parent: the macro alone keys */
    assert(ai_sched_publish_with(test_as_native,worker,0) && ai_sched_settle() && ai_sched_keys[3]>=AI_SCHED_FALLBACK_BIT && ai_sched_keys[3]!=fallback_key);
    *(uint64_t*)(cluster+8)=200;
    /* Two unmapped sectors with the same macro and parent collide and refuse; different macros do not. */
    *(uint64_t*)(sectors[0]+8)=996;*(unsigned char**)(sectors[0]+0x60)=macro_long;*(unsigned char**)(sectors[0]+0x70)=cluster;
    assert(!ai_sched_publish_with(test_as_native,worker,0) && !ai_sched_permuted && !strcmp(ai_sched_stage,"order_collision"));
    assert(ai_sched_order_collisions==2 && !memcmp(order,before,sizeof(order)));
    *(unsigned char**)(sectors[0]+0x60)=macro_other;
    assert(ai_sched_publish_with(test_as_native,worker,0) && ai_sched_order_fallback==2 && ai_sched_settle() && !memcmp(order,before,sizeof(order)));
    *(uint64_t*)(sectors[0]+8)=101;*(unsigned char**)(sectors[0]+0x60)=NULL;*(unsigned char**)(sectors[0]+0x70)=NULL;
    *(uint64_t*)(sectors[3]+8)=104;*(unsigned char**)(sectors[3]+0x60)=NULL;*(unsigned char**)(sectors[3]+0x70)=NULL;
    *(void**)(image+0x2ba5430+18*8)=prior_getter;
    assert(ai_sched_publish_with(test_as_native,worker,0) && !ai_sched_order_fallback && !ai_sched_fallbacks(note,sizeof(note)) && ai_sched_settle());
    assert(ai_sched_publish_with(test_as_native,worker,0) && ai_sched_permuted);
    *(unsigned char**)(order+5*32)=sectors[0];assert(!ai_sched_settle() && !strcmp(ai_sched_stage,"order_changed"));
    *(unsigned char**)(order+5*32)=NULL;assert(ai_sched_settle() && !memcmp(order,before,sizeof(order)));
    assert(ai_sched_publish_with(test_as_native,worker,0) && ai_sched_permuted);
    ai_sched_restore();assert(ai_sched_restored && !ai_sched_permuted && !memcmp(order,before,sizeof(order))); /* stop restores */
    test_as_native_calls=0;test_as_native_unlocked=1; /* after stop: plain native publish */
    assert(ai_sched_publish_with(test_as_native,worker,0) && test_as_native_calls==1 && !ai_sched_permuted);test_as_native_unlocked=0;
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=prior_identities;world_identity_count=prior_identity_count;
    DeleteCriticalSection((CRITICAL_SECTION*)(worker+0x6890));
    *(unsigned char**)(worker+0x68b8)=list;*(unsigned char**)(worker+0x68c0)=list+25*32;*(uint64_t*)(worker+0x68d0)=0;
    test_as_reset();ai_sched_armed=1;assert(ai_sched_bind());

    /* Worst-case status fits the 1024-byte Lua buffer. */
    ai_sched_armed=1;ai_sched_mask=7;ai_sched_failures=INT64_MIN;ai_sched_stage="order_unrestored";
    ai_sched_binds=ai_sched_boot_checks=ai_sched_boot_busy=ai_sched_boot_writes=ai_sched_boot_prior=UINT64_MAX;
    ai_sched_boot_cursor=ai_sched_boot_count=ai_sched_samples=ai_sched_sample_tick=ai_sched_cursor=ai_sched_count=UINT64_MAX;
    ai_sched_irregular=ai_sched_hold_phase=ai_sched_hold_enabled=ai_sched_orders=ai_sched_order_restores=ai_sched_order_moved=UINT64_MAX;
    ai_sched_priority_native=ai_sched_priority_portable=UINT64_MAX;ai_sched_order_count=UINT_MAX;
    ai_sched_order_fallback=ai_sched_order_unmapped=ai_sched_order_collisions=UINT64_MAX;
    char wide[1024];int width=ai_sched_status(wide,sizeof(wide));assert(width>0);
    printf("AI schedule worst-case status %d bytes\n",width);
    test_as_reset();ai_sched_armed=1;assert(ai_sched_bind());

    /* Restore ends writes; arming refuses without the boot/Gravidar prerequisites or pins. */
    *t.slot=0;assert(ai_sched_boot(1));ai_sched_restore();assert(ai_sched_restored && !ai_sched_slot && !ai_sched_entry);
    test_as_reset();assert(!ai_sched_arm(AI_SCHED_GRAVIDAR) && !ai_sched_arm(7) && !ai_sched_armed);
    SetEvent(t.release);assert(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0);CloseHandle(thread);
    CloseHandle(t.ready);CloseHandle(t.release);DeleteCriticalSection((CRITICAL_SECTION*)(worker+0x6960));
    *(void**)(image+AI_SCHED_WORKER)=prior_worker;test_as_reset();stats.issued=0;
    printf("native AI schedule Gravidar cursor/load-pass hold/sector order/fail-closed tests PASS\n");
}
